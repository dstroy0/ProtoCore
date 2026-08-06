#!/usr/bin/env bash
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Compile every Arduino example against the real ESP32 core and report failures.
#
# This exists because the native suite cannot catch what it catches. Host tests
# compile src/ directly with explicit -I flags, so they never exercise Arduino's
# library resolution - 130 of 152 sketches were unbuildable while 5756 host tests
# passed. Only a real `arduino-cli compile` reaches that path.
#
# Two modes:
#   compile_examples.sh              build here (WSL/local)
#   compile_examples.sh --remote     rsync to the RPi and build there (faster, 4 cores)
#
# --remote runs the batch under setsid+nohup so it survives the ssh session. A run
# driven from an interactive shell died mid-batch once and left 0-byte logs that read
# as slow compiles rather than a dead pipeline; detaching removes that failure mode.
#
# Exit: 0 if every sketch builds, 1 otherwise (the failures are listed).
set -u

REMOTE_HOST="${PC_RPI_HOST:-192.168.1.223}"
REMOTE_USER="${PC_RPI_USER:-dstroy0}"
FQBN="${PC_FQBN:-esp32:esp32:esp32s3:PSRAM=opi,FlashMode=qio,FlashSize=16M,CDCOnBoot=cdc,USBMode=hwcdc}"
JOBS="${PC_JOBS:-4}"

# Test-network credentials, injected into COPIES under the work dir so the tree
# keeps YOUR_SSID and no rig credential is ever committed.
SSID="${PC_TEST_SSID:-q_6}"
PASS="${PC_TEST_PASS:-12345678!}"

here() { cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd; }
ROOT="$(here)"
WORK="${PC_WORK:-/tmp/pc_examples}"

run_local() {
    local acli="$HOME/bin/arduino-cli"
    command -v arduino-cli >/dev/null 2>&1 && acli=$(command -v arduino-cli)
    [ -x "$acli" ] || { echo "arduino-cli not found (expected ~/bin/arduino-cli)"; exit 2; }

    rm -rf "$WORK"; mkdir -p "$WORK/logs" "$HOME/Arduino/libraries"
    rm -f "$HOME/Arduino/libraries/ProtoCore"
    ln -s "$ROOT" "$HOME/Arduino/libraries/ProtoCore"
    # a pre-rename copy here would satisfy includes with the wrong headers
    for stale in "$HOME/Arduino/libraries/DeterministicESPAsyncWebServer"; do
        [ -e "$stale" ] && mv "$stale" "$stale.bak.$$" 2>/dev/null
    done

    mapfile -t INOS < <(find "$ROOT/examples" -name '*.ino' | sort)
    echo ">> ${#INOS[@]} sketches, $JOBS parallel, $(nproc) cores"

    compile_one() {
        local ino="$1" work="$2" fqbn="$3" acli="$4" ssid="$5" pass="$6"
        local name; name=$(basename "$(dirname "$ino")")
        local src; src=$(dirname "$ino")
        local dir="$work/$name"
        mkdir -p "$dir"
        cp "$ino" "$dir/$name.ino"
        for extra in "$src"/*.h "$src"/*.cpp; do [ -f "$extra" ] && cp "$extra" "$dir/" 2>/dev/null; done
        sed -i "s/\"YOUR_SSID\"/\"$ssid\"/; s/\"YOUR_PASSWORD\"/\"$pass\"/" "$dir/$name.ino"
        if "$acli" compile --fqbn "$fqbn" --libraries "$HOME/Arduino/libraries" \
                --build-path "$work/bp_$name" "$dir" >"$work/logs/$name.log" 2>&1; then
            echo "PASS $name"
        else
            echo "FAIL $name"
        fi
        rm -rf "$work/bp_$name"   # each build tree is ~100 MB; 152 of them is not survivable
    }
    export -f compile_one

    printf '%s\n' "${INOS[@]}" \
        | xargs -P "$JOBS" -I{} bash -c 'compile_one "$@"' _ {} "$WORK" "$FQBN" "$acli" "$SSID" "$PASS" \
        > "$WORK/results.txt" 2>&1

    local pass fail
    pass=$(grep -c '^PASS' "$WORK/results.txt" || true)
    fail=$(grep -c '^FAIL' "$WORK/results.txt" || true)
    echo ">> PASS $pass   FAIL $fail"
    if [ "$fail" -gt 0 ]; then
        grep '^FAIL' "$WORK/results.txt" | sort | while read -r _ n; do
            echo "  $n: $(grep -m1 -E 'error:' "$WORK/logs/$n.log" 2>/dev/null | sed 's/.*error: //' | cut -c1-90)"
        done
        return 1
    fi
    return 0
}

run_remote() {
    local rsh="ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=10"
    command -v sshpass >/dev/null 2>&1 && [ -n "${PC_RPI_PASS:-}" ] && \
        rsh="sshpass -p $PC_RPI_PASS $rsh"

    echo ">> syncing to $REMOTE_USER@$REMOTE_HOST"
    rsync -a --delete -e "$rsh" "$ROOT/" "$REMOTE_USER@$REMOTE_HOST:~/pc/" || return 2

    echo ">> launching detached (survives this ssh session)"
    $rsh "$REMOTE_USER@$REMOTE_HOST" \
        "setsid nohup bash ~/pc/ci_tooling/check/compile_examples.sh > ~/pc_examples_run.log 2>&1 < /dev/null & echo started"

    echo ">> poll with:  $rsh $REMOTE_USER@$REMOTE_HOST 'tail -20 ~/pc_examples_run.log'"
}

case "${1:-}" in
    --remote) run_remote ;;
    *)        run_local ;;
esac
