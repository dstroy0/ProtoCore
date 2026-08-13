#!/usr/bin/env bash
# What the opaque cross-TU accessor costs in time, across the flag matrix.
#
# sweep.sh says whether the call survives; this says what it is worth. Same ring accessors, reached
# the two ways, timed. Minimum over trials, so the number is the run least disturbed by scheduling.
#
# The absolute nanoseconds are x86-64 host figures and are NOT a device claim. The RATIO is the
# decision-relevant part: it is what the encapsulation costs where LTO is unavailable.
set -u
cd "$(dirname "$0")"

STD="-std=c11 -D_POSIX_C_SOURCE=200809L"

printf '%-6s %-4s %-5s | %-11s %-11s %-7s | %-11s %-11s %-7s\n' \
    CC OPT LTO "avail opq" "avail inl" "ratio" "drain opq" "drain inl" "ratio"
printf '%.0s-' {1..90}; echo

for CC in gcc cc; do
    command -v "$CC" >/dev/null 2>&1 || continue
    for OPT in -O1 -O2 -Os; do
        for LTO in none flto; do
            FLAGS="$STD $OPT"
            [ "$LTO" = flto ] && FLAGS="$FLAGS -flto"
            EXE="bench_${CC}_${OPT#-}_${LTO}.exe"
            if ! $CC $FLAGS bench.c opq.c use_pub.c -o "$EXE" 2>/dev/null; then
                printf '%-6s %-4s %-5s | BUILD FAILED\n' "$CC" "$OPT" "$LTO"
                continue
            fi
            read -r _ ao _ ai _ do_ _ di _ _ < <("./$EXE")
            printf '%-6s %-4s %-5s | %-11s %-11s %-7s | %-11s %-11s %-7s\n' \
                "$CC" "$OPT" "$LTO" "$ao" "$ai" \
                "$(awk -v a="$ao" -v b="$ai" 'BEGIN{printf "%.2fx", a/b}')" \
                "$do_" "$di" \
                "$(awk -v a="$do_" -v b="$di" 'BEGIN{printf "%.2fx", a/b}')"
            rm -f "$EXE"
        done
    done
done

echo
echo "avail = ns per available() call.  drain = ns per byte read out of the ring."
echo "Absolute values are x86-64 host, not a device measurement; the ratio is the portable part."
