#!/bin/bash
ulimit -c unlimited
eval "$(/opt/homebrew/bin/brew shellenv)"

LOG_DIR="$HOME/Library/Logs/TrunkRecorder"
LOG_FILE="$LOG_DIR/wmata.log"
MAX_BYTES=10485760  # 10MB
KEEP=5

# Watchdog: trunk-recorder can hang forever in "stopping flow graph" when an
# RTL source stops delivering samples (rtl_source_c::work never returns), so
# kill it if it writes no log output for STALL_SECS.
STALL_SECS=300
CHECK_SECS=30
KILL_GRACE=15

mkdir -p "$LOG_DIR"

rotate_log() {
    local sz
    sz=$(stat -f%z "$LOG_FILE" 2>/dev/null || echo 0)
    if [ "$sz" -ge "$MAX_BYTES" ]; then
        for i in $(seq $KEEP -1 1); do
            [ -f "${LOG_FILE}.${i}" ] && mv -f "${LOG_FILE}.${i}" "${LOG_FILE}.$((i + 1))"
        done
        mv -f "$LOG_FILE" "${LOG_FILE}.1"
        rm -f "${LOG_FILE}.$((KEEP + 1))" 2>/dev/null
    fi
}

# Bytes written so far on the process's stdout. Uses the fd offset rather than
# the log path because newsyslog rotates wmata.log out from under the process.
log_offset() {
    lsof -a -p "$1" -d 1 -o -Fo 2>/dev/null | sed -n 's/^o0t//p'
}

run_recorder() {
    local config=$1 pid off now last_off last_change rc

    rotate_log
    echo "Starting with $config" >> "$LOG_FILE"
    ./trunk-recorder --config="$config" >> "$LOG_FILE" 2>&1 &
    pid=$!
    caffeinate -us -w "$pid" &

    last_off=$(log_offset "$pid")
    last_change=$(date +%s)
    while kill -0 "$pid" 2>/dev/null; do
        /bin/sleep $CHECK_SECS
        off=$(log_offset "$pid")
        now=$(date +%s)
        [ -z "$off" ] && continue
        if [ "$off" != "$last_off" ]; then
            last_off=$off
            last_change=$now
        elif [ $((now - last_change)) -ge $STALL_SECS ]; then
            echo "[watchdog] No log output from pid $pid for $((now - last_change))s - killing" >> "$LOG_FILE"
            kill -TERM "$pid" 2>/dev/null
            for i in $(seq $KILL_GRACE); do
                kill -0 "$pid" 2>/dev/null || break
                /bin/sleep 1
            done
            kill -KILL "$pid" 2>/dev/null
            break
        fi
    done

    wait "$pid"
    rc=$?
    echo "Server $config exited with exit code $rc. Try $COUNTER.  Respawning.." >> "$LOG_FILE"
    /bin/sleep 5
}

COUNTER=0
while true; do
    run_recorder config-wmata-496.json
    run_recorder config-wmata-490.json
    let COUNTER+=1
done
