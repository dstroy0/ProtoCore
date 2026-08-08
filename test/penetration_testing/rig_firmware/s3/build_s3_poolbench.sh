#!/bin/bash
# Build the S3 memory-manager microbench (see ../BENCH.md).
#
# Stages the shared bench source next to its sketch anchor, then compiles with arduino-cli. The S3 on
# this rig (MAC ...:73:1c) is an OCTAL-PSRAM DevKitC-1, so PSRAM=opi; the quad board needs
# PSRAM=enabled and giving it opi stops it booting with
#   E (189) quad_psram: PSRAM chip is not connected, or wrong PSRAM line mode
# Override with PC_FQBN if flashing the other board.
set -euo pipefail
cd "$(dirname "$0")"

ROOT=$(git rev-parse --show-toplevel)
SKETCH="$PWD/S3PoolBench"
SHARED="$ROOT/test/penetration_testing/rig_firmware/src/main_poolbench.cpp"
ACLI=$(command -v arduino-cli || echo "$HOME/bin/arduino-cli")
FQBN="${PC_FQBN:-esp32:esp32:esp32s3:PSRAM=opi,FlashMode=qio,FlashSize=16M,CDCOnBoot=cdc,USBMode=hwcdc}"
LIBDIR="$HOME/Arduino/libraries"

# arduino-cli resolves a library by name from the libraries dir, so the tree is exposed under its own.
mkdir -p "$LIBDIR"
# Idempotent: $LIBDIR is shared by every arduino-cli build on this machine, so deleting and
# recreating the link races any concurrent compile - which fails as an internal compiler error
# in an unrelated header, not as a missing file. The link always points at the same tree, so
# only touch it when it is absent or wrong.
if [ "$(readlink -f "$LIBDIR/ProtoCore" 2>/dev/null)" != "$(readlink -f "$ROOT")" ]; then
    rm -rf "$LIBDIR/ProtoCore"
    ln -s "$ROOT" "$LIBDIR/ProtoCore" 2>/dev/null || cp -r "$ROOT" "$LIBDIR/ProtoCore"
fi

cp "$SHARED" "$SKETCH/main_poolbench.cpp"
trap 'rm -f "$SKETCH/main_poolbench.cpp"' EXIT

echo ">> compiling for $FQBN"
"$ACLI" compile --fqbn "$FQBN" --build-property "compiler.cpp.extra_flags=-I$ROOT/test/performance_benching/common" --libraries "$LIBDIR" --build-path "$SKETCH/build" "$SKETCH" 2>&1 | tail -30
echo ">> compile rc=${PIPESTATUS[0]}"
ls -la "$SKETCH/build"/*.bin 2>/dev/null | head
