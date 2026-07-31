#!/usr/bin/env bash
# Build the P4 HTTPS rig (P4TlsRig.ino) for the Waveshare ESP32-P4-POE-ETH via arduino-cli, then print the
# esptool flash command. The P4 needs the arduino-esp32 3.x core (esp32:esp32), unlike the S3 pio rigs.
#
#   REPO=/path/to/ProtoCore ./build_p4_tls.sh
#
# Build on ext4 (never /mnt/c): the library is rsync'd to ~/Arduino/libraries first. Flash from Windows with
# the printed esptool command (WSL cannot reach the COM port). See README.md.
set -eu

REPO="${REPO:-$(cd "$(dirname "$0")/../../.." && pwd)}"
HERE="$(cd "$(dirname "$0")" && pwd)"
LIB=~/Arduino/libraries/ProtoCore
STAGE=~/pctest/ex_p4tls
FQBN="esp32:esp32:waveshare_p4_poe_eth"
# Copy the built binaries somewhere the Windows esptool can reach (flashing happens from Windows - WSL cannot
# open the COM port). Default to a /mnt/c path; override OUT=... for a different Windows-visible location.
OUT="${OUT:-/mnt/c/Users/$(cmd.exe /c 'echo %USERNAME%' 2>/dev/null | tr -d '\r\n')/pctest/p4tls}"

echo ">> syncing library to ext4 ($LIB)"
mkdir -p "$LIB/src"
rsync -a --delete --exclude .git --exclude .pio "$REPO/src/" "$LIB/src/"
cp "$REPO/library.properties" "$LIB/" 2>/dev/null || true

echo ">> staging sketch"
rm -rf "$STAGE"
mkdir -p "$STAGE/P4TlsRig"
cp "$HERE/P4TlsRig/P4TlsRig.ino" "$STAGE/P4TlsRig/"
cp "$HERE/P4TlsRig/build_opt.h" "$STAGE/P4TlsRig/"
cd "$STAGE/P4TlsRig"

echo ">> compiling for $FQBN"
arduino-cli compile --fqbn "$FQBN" --build-path "$STAGE/build" . 2>&1 | tail -8

mkdir -p "$OUT"
cp "$STAGE"/build/P4TlsRig.ino.bin "$OUT/"
cp "$STAGE"/build/P4TlsRig.ino.bootloader.bin "$OUT/"
cp "$STAGE"/build/P4TlsRig.ino.partitions.bin "$OUT/"
cp "$STAGE"/build/boot_app0.bin "$OUT/"
echo ">> binaries in $OUT"
echo ">> flash from Windows (esptool >= 5.x, esp32p4 support; COM9 = the P4):"
echo "   esptool --chip esp32p4 --port COM9 --baud 921600 write-flash -z \\"
echo "     0x2000 P4TlsRig.ino.bootloader.bin 0x8000 P4TlsRig.ino.partitions.bin \\"
echo "     0xe000 boot_app0.bin 0x10000 P4TlsRig.ino.bin"
