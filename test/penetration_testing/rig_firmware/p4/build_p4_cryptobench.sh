#!/usr/bin/env bash
# Build the P4 crypto microbench (the ESP32-P4 twin of pio's rig_s3_cryptobench) via arduino-cli, then print
# the esptool flash command. The bench source is the SHARED ../src/main_cryptobench.cpp; this script stages it
# and the RSA host-key fixture into the P4CryptoBench sketch dir so the one bench builds under both toolchains.
#
#   REPO=/path/to/ProtoCore ./build_p4_cryptobench.sh
#
# Build on ext4 (never /mnt/c): the library is rsync'd to ~/Arduino/libraries first. Flash from Windows with
# the printed esptool command (WSL cannot reach the COM port). COM9 = the P4.
set -eu

REPO="${REPO:-$(cd "$(dirname "$0")/../../.." && pwd)}"
HERE="$(cd "$(dirname "$0")" && pwd)"
LIB=~/Arduino/libraries/ProtoCore
STAGE=~/pctest/ex_p4cb
FQBN="esp32:esp32:waveshare_p4_poe_eth"
# Resolve the Windows username by running cmd.exe from a real Windows cwd (/mnt/c) - from an ext4 cwd cmd.exe
# warns about UNC and %USERNAME% comes back empty.
OUT="${OUT:-/mnt/c/Users/$(cd /mnt/c && cmd.exe /c 'echo %USERNAME%' 2>/dev/null | tr -d '\r\n')/pctest/p4cb}"

echo ">> syncing library to ext4 ($LIB)"
mkdir -p "$LIB/src"
rsync -a --delete --exclude .git --exclude .pio "$REPO/src/" "$LIB/src/"
cp "$REPO/library.properties" "$LIB/" 2>/dev/null || true

echo ">> staging sketch (shared main_cryptobench.cpp + RSA fixture, resolved via the sketch dir)"
rm -rf "$STAGE"
mkdir -p "$STAGE/P4CryptoBench"
cp "$HERE/P4CryptoBench/P4CryptoBench.ino" "$STAGE/P4CryptoBench/"
cp "$HERE/P4CryptoBench/build_opt.h" "$STAGE/P4CryptoBench/"
# Optional PC_CRYPTO_OPT_LEVEL sweep (arg 1: 0 = inherit -Os, 2 = -O2, 3 = -O3) - appended to the staged
# build_opt.h so the whole src/crypto tree builds at that level for an apples-to-apples per-variant bench.
if [ -n "${1:-}" ]; then
  echo "-DPC_CRYPTO_OPT_LEVEL=$1" >>"$STAGE/P4CryptoBench/build_opt.h"
fi
cp "$REPO/penetration_testing/rig_firmware/src/main_cryptobench.cpp" "$STAGE/P4CryptoBench/"
cp "$REPO/test/fixtures/ssh_test_host_key/ssh_test_host_key.h" "$STAGE/P4CryptoBench/"
cd "$STAGE/P4CryptoBench"

echo ">> compiling for $FQBN"
# --library: the bench's first library include is crypto/... (not a top-level header like protocore.h), so
# arduino-cli's dependency finder never attaches the library on its own - force it onto the include path.
arduino-cli compile --fqbn "$FQBN" --library "$LIB" --build-path "$STAGE/build" .

mkdir -p "$OUT"
cp "$STAGE"/build/P4CryptoBench.ino.bin "$OUT/"
cp "$STAGE"/build/P4CryptoBench.ino.bootloader.bin "$OUT/"
cp "$STAGE"/build/P4CryptoBench.ino.partitions.bin "$OUT/"
cp "$STAGE"/build/boot_app0.bin "$OUT/"
echo ">> binaries in $OUT"
echo ">> flash from Windows (esptool >= 5.x, esp32p4 support; COM9 = the P4):"
echo "   esptool --chip esp32p4 --port COM9 --baud 921600 write-flash -z \\"
echo "     0x2000 P4CryptoBench.ino.bootloader.bin 0x8000 P4CryptoBench.ino.partitions.bin \\"
echo "     0xe000 boot_app0.bin 0x10000 P4CryptoBench.ino.bin"
