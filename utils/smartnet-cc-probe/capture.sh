#!/bin/bash
# Capture raw IQ from the wmata RTL-SDR dongles simultaneously so every
# SmartNet control channel can be analyzed side by side with analyze.py.
#
# The dongles are exclusively owned by trunk-recorder, so this pauses the
# launchd job for the duration of the capture and always restarts it.
#
# usage: capture.sh <seconds> <out_dir>
set -u

SECS=${1:-300}
OUT=${2:-.}
RATE=1800000
JOB="gui/$(id -u)/local.trunk-recorder-wmata"
PLIST="$HOME/Library/LaunchAgents/local.trunk-recorder-wmata.plist"

# serial:center:gain -- mirrors config-wmata-49x.json
DONGLES=("50:496337000:16.6" "51:490600000:25.4")

eval "$(/opt/homebrew/bin/brew shellenv)"
mkdir -p "$OUT"

restart_job() {
    echo "Restarting $JOB"
    launchctl bootstrap "gui/$(id -u)" "$PLIST" 2>/dev/null || launchctl kickstart "$JOB"
}
trap restart_job EXIT

echo "Stopping $JOB"
launchctl bootout "$JOB" 2>/dev/null
for i in $(seq 20); do
    pgrep -f "config-wmata" >/dev/null || break
    sleep 1
done
pkill -9 -f "trunk-recorder --config=config-wmata" 2>/dev/null
sleep 2

pids=()
for d in "${DONGLES[@]}"; do
    IFS=: read -r serial center gain <<<"$d"
    file="$OUT/wmata-sn${serial}-${center}-${RATE}.u8"
    echo "Capturing SN $serial @ $center Hz gain $gain -> $file"
    rtl_sdr -d "$serial" -f "$center" -s "$RATE" -g "$gain" -n $((SECS * RATE)) "$file" 2>"$file.log" &
    pids+=($!)
done
date +%s >"$OUT/capture-start.txt"
for p in "${pids[@]}"; do wait "$p"; done
echo "Capture done"
