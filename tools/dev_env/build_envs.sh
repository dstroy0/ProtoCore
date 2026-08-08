#!/usr/bin/env bash
# Build each named pio env in the WSL clone and print a one-line verdict per env.
set -u
ROOT="${PROTOCORE_ROOT:-$HOME/pc/ProtoCore}"
cd "$ROOT" || exit 1
PIO="$HOME/.pio-venv/bin/pio"
rc=0
for env in "$@"; do
    out=$("$PIO" test -e "$env" 2>&1)
    status=$?
    # pio's exit code is the verdict; the log scan only catches a case it reported without failing.
    if [ "$status" -eq 0 ] && ! printf '%s' "$out" | grep -qE '\[FAILED\]|\[ERRORED\]'; then
        echo "PASS $env"
    else
        echo "FAIL $env"
        printf '%s\n' "$out" | grep -E 'error:|Error|undefined reference|\[FAILED\]|\[ERRORED\]' | head -25
        rc=1
    fi
done
exit $rc
