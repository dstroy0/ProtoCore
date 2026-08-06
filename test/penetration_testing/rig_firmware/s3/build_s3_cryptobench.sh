#!/usr/bin/env bash
# Build the S3 crypto microbench (see ../BENCH.md).
#
# Same shape as build_s3_poolbench.sh: the bench body is the SHARED ../src/main_cryptobench.cpp,
# staged next to its sketch anchor before compiling. Used here to A/B the word-width XOR in gctr and
# the GHASH absorb - "pc_aesgcm seal" and "pc_chacha20" are the lines that move.
set -euo pipefail
cd "$(dirname "$0")"

ROOT=$(git rev-parse --show-toplevel)
SKETCH="$PWD/S3CryptoBench"
SHARED="$ROOT/test/penetration_testing/rig_firmware/src/main_cryptobench.cpp"
ACLI=$(command -v arduino-cli || echo "$HOME/bin/arduino-cli")
FQBN="${PC_FQBN:-esp32:esp32:esp32s3:PSRAM=opi,FlashMode=qio,FlashSize=16M,CDCOnBoot=cdc,USBMode=hwcdc}"
LIBDIR="$HOME/Arduino/libraries"
OUTDIR="${PC_OUTDIR:-$SKETCH/build}"

mkdir -p "$LIBDIR"
# Idempotent: $LIBDIR is shared by every arduino-cli build on this machine, so deleting and
# recreating the link races any concurrent compile - which fails as an internal compiler error
# in an unrelated header, not as a missing file. The link always points at the same tree, so
# only touch it when it is absent or wrong.
if [ "$(readlink -f "$LIBDIR/ProtoCore" 2>/dev/null)" != "$(readlink -f "$ROOT")" ]; then
    rm -rf "$LIBDIR/ProtoCore"
    ln -s "$ROOT" "$LIBDIR/ProtoCore" 2>/dev/null || cp -r "$ROOT" "$LIBDIR/ProtoCore"
fi

cp "$SHARED" "$SKETCH/main_cryptobench.cpp"
# main_cryptobench.cpp includes the RSA host-key fixture; the P4 script stages it the same way.
cp "$ROOT/test/fixtures/ssh_test_host_key/ssh_test_host_key.h" "$SKETCH/"
trap 'rm -f "$SKETCH/main_cryptobench.cpp" "$SKETCH/ssh_test_host_key.h"' EXIT

echo ">> compiling for $FQBN"
"$ACLI" compile --fqbn "$FQBN" --build-property "compiler.cpp.extra_flags=-I$ROOT/test/performance_benching/common" --libraries "$LIBDIR" --build-path "$OUTDIR" "$SKETCH" 2>&1 | tail -20
echo ">> compile rc=${PIPESTATUS[0]}"
ls -la "$OUTDIR"/*.ino.bin 2>/dev/null
