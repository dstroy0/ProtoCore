#!/usr/bin/env bash
# Build S3NsBench twice - flat calls, then namespace structs - and print both sizes.
#
# Same staging shape as build_s3_cryptobench.sh. The two builds differ only in -DPC_NS_FORM, so any
# size difference is what the namespace struct costs: the pointer table itself, plus every leaf the
# const initializer names that nothing else calls.
set -euo pipefail
cd "$(dirname "$0")"

ROOT=$(git rev-parse --show-toplevel)
SKETCH="$PWD/S3NsBench"
ACLI=$(command -v arduino-cli || echo "$HOME/bin/arduino-cli")
FQBN="${PC_FQBN:-esp32:esp32:esp32s3:PSRAM=opi,FlashMode=qio,FlashSize=16M,CDCOnBoot=cdc,USBMode=hwcdc}"
LIBDIR="$HOME/Arduino/libraries"

mkdir -p "$LIBDIR"
if [ "$(readlink -f "$LIBDIR/ProtoCore" 2>/dev/null)" != "$(readlink -f "$ROOT")" ]; then
    rm -rf "$LIBDIR/ProtoCore"
    ln -s "$ROOT" "$LIBDIR/ProtoCore" 2>/dev/null || cp -r "$ROOT" "$LIBDIR/ProtoCore"
fi

build_one() {
    local form="$1" out="$SKETCH/build_form$1"
    rm -rf "$out"
    "$ACLI" compile --fqbn "$FQBN" --libraries "$LIBDIR" --build-path "$out" \
        --build-property "compiler.c.extra_flags=-DPC_NS_FORM=$form" \
        --build-property "compiler.cpp.extra_flags=-DPC_NS_FORM=$form" \
        "$SKETCH" 2>&1 | grep -E "Sketch uses|Global variables|error:" || true
}

echo ">> PC_NS_FORM=0 (flat calls)"
build_one 0
echo ">> PC_NS_FORM=1 (namespace structs)"
build_one 1

echo
echo ">> .text of the 24 leaves kept in each image"
for f in 0 1; do
    elf="$SKETCH/build_form$f/S3NsBench.ino.elf"
    if [ -f "$elf" ]; then
        NM=$(command -v xtensa-esp32s3-elf-nm || echo "$HOME/.arduino15/packages/esp32/tools/esp-x32/*/bin/xtensa-esp32s3-elf-nm")
        n=$(eval "$NM" "$elf" 2>/dev/null | grep -cE ' [tT] f[0-9]{2}(\.[0-9]+)?$' || true)
        echo "   PC_NS_FORM=$f: $n of 24 leaves present in .text"
    fi
done
