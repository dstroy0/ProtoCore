#!/usr/bin/env bash
# Price the namespace struct in C, on the xtensa target toolchain.
#
# Freestanding: -nostdlib with app_main as the entry, so nothing but this file lands in the image and
# the .text figure is the code under test rather than a framework. -ffunction-sections +
# --gc-sections is what the real builds use, so the strip behaviour measured here is theirs.
set -euo pipefail
cd "$(dirname "$0")"

SRC="$PWD/ns_abi/ns_abi.c"
OUT="$PWD/ns_abi/build"
TOOLS=$(echo "$HOME"/.arduino15/packages/esp32/tools/esp-x32/*/bin | tr ' ' '\n' | tail -1)
CC="$TOOLS/xtensa-esp32s3-elf-gcc"
NM="$TOOLS/xtensa-esp32s3-elf-nm"
SIZE="$TOOLS/xtensa-esp32s3-elf-size"

for t in "$CC" "$NM" "$SIZE"; do
    [ -x "$t" ] || { echo "missing: $t" >&2; exit 1; }
done

mkdir -p "$OUT"
for form in 0 1; do
    "$CC" -std=c11 -Os -ffunction-sections -fdata-sections -Wall -Wextra \
        -DPC_NS_FORM=$form -c "$SRC" -o "$OUT/ns_abi_$form.o"
    "$CC" -nostdlib -Wl,--gc-sections -Wl,-e,app_main \
        "$OUT/ns_abi_$form.o" -o "$OUT/ns_abi_$form.elf"
done

echo "form  .text  leaves_kept  table_present"
for form in 0 1; do
    text=$("$SIZE" "$OUT/ns_abi_$form.elf" | awk 'NR==2 {print $1}')
    leaves=$("$NM" "$OUT/ns_abi_$form.elf" | grep -cE ' [tT] f[0-9]{2}' || true)
    table=$("$NM" "$OUT/ns_abi_$form.elf" | grep -cE ' [rRdD] (Network|Server)$' || true)
    echo "$form     $text     $leaves of 24      $table"
done

echo
echo "leaf symbols kept:"
for form in 0 1; do
    printf "  form %s: " "$form"
    "$NM" "$OUT/ns_abi_$form.elf" | grep -E ' [tT] f[0-9]{2}' | awk '{print $3}' | sort | tr '\n' ' '
    echo
done
