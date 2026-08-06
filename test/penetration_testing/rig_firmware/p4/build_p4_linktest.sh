#!/usr/bin/env bash
# Build the P4 physical (L1) link bring-up test (the ESP32-P4 twin of pio's rig_s3_linktest) via arduino-cli,
# then print the esptool flash command. The test source is the SHARED ../src/main_linktest.cpp; this script
# stages it into the P4LinkTest sketch dir so the one test builds under both toolchains. build_opt.h sets
# PC_ENABLE_ETHERNET=1 so the RMII PHY (not WiFi) is brought up.
#
#   REPO=/path/to/ProtoCore ./build_p4_linktest.sh
#
# Build on ext4 (never /mnt/c): the library is rsync'd to ~/Arduino/libraries first. Flash from Windows with
# the printed esptool command (WSL cannot reach the COM port). COM9 = the P4.
set -eu

REPO="${REPO:-$(cd "$(dirname "$0")/../../.." && pwd)}"
HERE="$(cd "$(dirname "$0")" && pwd)"
LIB=~/Arduino/libraries/ProtoCore
STAGE=~/pctest/ex_p4lt
FQBN="esp32:esp32:waveshare_p4_poe_eth"
OUT="${OUT:-/mnt/c/Users/Douglas/pctest/p4lt}"

echo ">> syncing library to ext4 ($LIB)"
mkdir -p "$LIB/src"
rsync -a --delete --exclude .git --exclude .pio "$REPO/src/" "$LIB/src/"
cp "$REPO/library.properties" "$LIB/" 2>/dev/null || true

echo ">> staging sketch (shared main_linktest.cpp)"
rm -rf "$STAGE"
mkdir -p "$STAGE/P4LinkTest"
cp "$HERE/P4LinkTest/P4LinkTest.ino" "$STAGE/P4LinkTest/"
cp "$HERE/P4LinkTest/build_opt.h" "$STAGE/P4LinkTest/"
cp "$REPO/penetration_testing/rig_firmware/src/main_linktest.cpp" "$STAGE/P4LinkTest/"
cd "$STAGE/P4LinkTest"

echo ">> compiling for $FQBN"
arduino-cli compile --fqbn "$FQBN" --library "$LIB" --build-path "$STAGE/build" .

mkdir -p "$OUT"
cp "$STAGE"/build/P4LinkTest.ino.bin "$OUT/"
cp "$STAGE"/build/P4LinkTest.ino.bootloader.bin "$OUT/"
cp "$STAGE"/build/P4LinkTest.ino.partitions.bin "$OUT/"
cp "$STAGE"/build/boot_app0.bin "$OUT/"
echo ">> binaries in $OUT"
echo ">> flash from Windows (esp32p4, bootloader @ 0x2000; COM9 = the P4):"
echo "   esptool --chip esp32p4 --port COM9 --baud 921600 write-flash -z \\"
echo "     0x2000 P4LinkTest.ino.bootloader.bin 0x8000 P4LinkTest.ino.partitions.bin \\"
echo "     0xe000 boot_app0.bin 0x10000 P4LinkTest.ino.bin"
