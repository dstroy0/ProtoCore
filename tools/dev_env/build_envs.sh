#!/usr/bin/env bash
# Build each named pio env in the WSL clone and print a one-line verdict per env.
set -u
cd "$HOME/pc/ProtoCore" || exit 1
PIO="$HOME/.pio-venv/bin/pio"
rc=0
for env in "$@"; do
    out=$("$PIO" test -e "$env" 2>&1)
    if printf '%s' "$out" | grep -q '\[PASSED\]'; then
        echo "PASS $env"
    else
        echo "FAIL $env"
        printf '%s\n' "$out" | grep -E 'error:|Error|undefined reference|\[FAILED\]|warning: .*unused' | head -25
        rc=1
    fi
done
exit $rc
