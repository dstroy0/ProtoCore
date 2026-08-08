// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the OTA rollback decision core (server/update/ota_rollback):
// pc_ota_decide() is the pure, branch-free-enough decision matrix that each tick maps
// (image state, self-test result, uptime, confirm window) -> WAIT / COMMIT / ROLLBACK, so a bad
// update self-heals instead of soft-bricking. It is a pure function (no partitions, no flash), so
// every call here exercises the real production decision path. Deliberately OUT OF SCOPE: the
// pc_ota_commit / pc_ota_rollback / pc_ota_rollback_tick hooks - on ESP32 they wrap esp_ota_ops
// (esp_ota_mark_app_valid_cancel_rollback / esp_ota_mark_app_invalid_rollback_and_reboot, which
// actually reboots into the previous image), so benching them would rewrite the OTA data partition
// and reset the rig; only the deterministic CPU-side decision is ever timed here.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/ota_rollback -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/update/ota_rollback.h"
#include <Arduino.h>

static void ota_rollback_bench_task(void *)
{
    // Realistic decision inputs lifted straight from test/test_ota_rollback/test_ota_rollback.cpp
    // (already known-good, spec-conformant), one per decision branch. window_ms == the library
    // default PC_OTA_CONFIRM_WINDOW_MS (30000).
    const uint32_t window_ms = 30000;

    for (;;)
    {
        Serial.printf("DB ==== ota_rollback device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;

        // Non-pending image (normal boot): nothing to do -> WAIT (the common per-tick case).
        DBENCH_OP("pc_ota_decide not-pending WAIT", 200000,
                  sink += (uint8_t)pc_ota_decide(pc_ota_img::PC_OTA_IMG_VALID, false, 999999, window_ms));

        // Pending + self-test passed -> COMMIT (mark valid).
        DBENCH_OP("pc_ota_decide pending COMMIT", 200000,
                  sink += (uint8_t)pc_ota_decide(pc_ota_img::PC_OTA_IMG_PENDING_VERIFY, true, 1000, window_ms));

        // Pending, self-test not yet passed, still inside the confirm window -> WAIT.
        DBENCH_OP("pc_ota_decide pending in-window", 200000,
                  sink += (uint8_t)pc_ota_decide(pc_ota_img::PC_OTA_IMG_PENDING_VERIFY, false, 5000, window_ms));

        // Pending, never confirmed, window elapsed -> ROLLBACK (self-heal decision).
        DBENCH_OP("pc_ota_decide pending ROLLBACK", 200000,
                  sink += (uint8_t)pc_ota_decide(pc_ota_img::PC_OTA_IMG_PENDING_VERIFY, false, 40000, window_ms));

        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: ota_rollback device microbench");
    xTaskCreatePinnedToCore(ota_rollback_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
