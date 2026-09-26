#!/bin/bash
# Continuously watch all wmata SmartNet control channels with the spare
# RTL-SDR dongles, independent of trunk-recorder, logging one CSV row per
# channel per second (see analyze.py for the columns).
#
# usage: monitor.sh <out_dir> [python]
set -u

OUT=${1:-.}
PY=${2:-python3}
RATE=1800000
HERE="$(cd "$(dirname "$0")" && pwd)"

# serial:center:gain:channels -- spare dongles, not used by trunk-recorder
WATCH=(
    "91:496337000:25.4:496437500,496537500"
    "200:490600000:25.4:490787500,490862500"
)

eval "$(/opt/homebrew/bin/brew shellenv)"
mkdir -p "$OUT"
trap 'kill 0' EXIT

for w in "${WATCH[@]}"; do
    IFS=: read -r serial center gain chans <<<"$w"
    chan_args=()
    for c in ${chans//,/ }; do chan_args+=(--chan "$c"); done
    (
        while true; do
            rtl_sdr -d "$serial" -f "$center" -s "$RATE" -g "$gain" - 2>>"$OUT/rtl-sn$serial.log" |
                "$PY" "$HERE/analyze.py" - --center "$center" --rate "$RATE" "${chan_args[@]}" \
                    --csv "$OUT/cc-sn$serial-$(date +%Y%m%d-%H%M%S).csv" >/dev/null 2>>"$OUT/analyze-sn$serial.log"
            echo "$(date) pipeline for SN $serial exited, restarting" >>"$OUT/monitor.log"
            sleep 5
        done
    ) &
done
wait
