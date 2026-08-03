#!/usr/bin/env bash
# Ground-truth check for src/board_drivers/hal/esp/esp_crypto_hal.h: compile HalRegmapVerify for every supported die. Each build
# static_asserts our PC_ register values EQUAL the manufacturer's soc macros for that die, so a clean compile
# proves the map matches Espressif's own implementation - including dies we have no board for. Run in WSL with
# the arduino-esp32 3.x core installed (arduino-cli on PATH):
#
#   bash penetration_testing/rig_firmware/hal_verify/verify_regmaps.sh
#
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
STAGE=~/pctest/hal_verify
DIES="esp32s3 esp32s2 esp32c3 esp32p4 esp32c6 esp32c5 esp32h2"

rm -rf "$STAGE"
mkdir -p "$STAGE/HalRegmapVerify"
cp "$HERE/HalRegmapVerify/HalRegmapVerify.ino" "$STAGE/HalRegmapVerify/"
# The HAL is self-contained; stage just it next to the sketch (no library attach -> no WiFi drag-in on H2).
cp "$REPO/src/hal/esp/esp_crypto_hal.h" "$STAGE/HalRegmapVerify/esp_crypto_hal.h"

rc=0
for fqbn in $DIES; do
  echo "==================== $fqbn ===================="
  if arduino-cli compile --fqbn "esp32:esp32:$fqbn" --build-path "$STAGE/build_$fqbn" "$STAGE/HalRegmapVerify" \
       >"$STAGE/$fqbn.log" 2>&1; then
    echo "  $fqbn: REGMAP MATCHES MANUFACTURER (static_asserts pass)"
  else
    grep -iE "static_assert|static assertion|error:" "$STAGE/$fqbn.log" | head -8
    echo "  $fqbn: MISMATCH / FAILED"
    rc=1
  fi
done
echo ">> verify_regmaps rc=$rc"
exit $rc
