#!/usr/bin/env bash
# Does an opaque cross-TU accessor get inlined, and at what setting?
#
# For each (compiler, -O level, lto) we build the whole program, disassemble the consumer's hot
# loop, and count (a) call instructions targeting the accessors and (b) total instructions in the
# loop. 0 calls with a non-zero instruction count = the accessor was inlined into the caller.
#
# hot_opq / hot_pub are __attribute__((noinline)) so they survive LTO as their own symbols. Without
# that, LTO folds them into main and an empty disassembly reads as "0 calls" either way.
set -u
cd "$(dirname "$0")"

STD="-std=c11 -D_POSIX_C_SOURCE=200809L"
MINGW_SYSROOT="/c/Strawberry/c"

# Disassembly of one function, up to the blank line that ends it. Matches on a PREFIX: GCC's LTO
# privatizes local symbols and renames them (hot_opq.lto_priv.0), so an exact match silently finds
# nothing and reads as "0 calls".
body_of() {
    objdump -d "$1" 2>/dev/null | awk -v f="<$2" '
        index($0, f) && /:$/ { inside = 1; next }
        inside && /^$/ { exit }
        inside && /\t/ { print }
    '
}

run_one() {
    local label="$1" cc="$2" opt="$3" lto="$4"
    shift 4
    local flags="$STD $opt $*"
    [ "$lto" = flto ] && flags="$flags -flto"
    local exe="out_${label}_${opt#-}_${lto}.exe"
    if ! $cc $flags main.c opq.c use_opq.c use_pub.c -o "$exe" 2>/dev/null; then
        printf '%-7s %-4s %-5s | BUILD FAILED\n' "$label" "$opt" "$lto"
        return
    fi
    local ob pb oc pc oi pi
    ob=$(body_of "$exe" hot_opq)
    pb=$(body_of "$exe" hot_pub)
    oc=$(printf '%s\n' "$ob" | grep -cE 'call.*<(rt_available|rt_read_byte)' || true)
    pc=$(printf '%s\n' "$pb" | grep -cE 'call.*<(pub_available|pub_read_byte)' || true)
    oi=$(printf '%s\n' "$ob" | grep -c . || true)
    pi=$(printf '%s\n' "$pb" | grep -c . || true)
    printf '%-7s %-4s %-5s | %-13s %-11s | %-13s %-11s\n' \
        "$label" "$opt" "$lto" "$oc" "$oi" "$pc" "$pi"
}

printf '%-7s %-4s %-5s | %-13s %-11s | %-13s %-11s\n' \
    CC OPT LTO "opaque calls" "opaque insn" "inline calls" "inline insn"
printf '%.0s-' {1..81}
echo

for OPT in -O0 -O1 -O2 -O3 -Os; do
    for LTO in none flto; do
        run_one gcc13 gcc "$OPT" "$LTO"
    done
done
for OPT in -O0 -O1 -O2 -O3 -Os; do
    for LTO in none flto; do
        run_one gcc16 cc "$OPT" "$LTO"
    done
done
for OPT in -O0 -O1 -O2 -O3 -Os -Oz; do
    for LTO in none flto; do
        run_one clang clang "$OPT" "$LTO" --target=x86_64-w64-mingw32 --sysroot="$MINGW_SYSROOT"
    done
done

echo
echo "Sanity: opq and pub must compute the same checksum."
./out_gcc13_O2_none.exe >/dev/null && echo "  gcc13 -O2      ok"
./out_clang_O2_flto.exe >/dev/null && echo "  clang -O2 -flto ok"
