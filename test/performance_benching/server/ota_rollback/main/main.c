// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the OTA rollback decision core (server/update/ota_rollback):
// OtaRollback.decide is the pure, branch-free-enough decision matrix that each tick maps
// (image state, self-test result, uptime, confirm window) -> WAIT / COMMIT / ROLLBACK, so a bad
// update self-heals instead of soft-bricking. It is a pure function (no partitions, no flash), so
// every call here exercises the real production decision path. Deliberately OUT OF SCOPE: the
// OtaRollback.commit / .rollback / .tick hooks - on ESP32 they wrap esp_ota_ops
// (esp_ota_mark_app_valid_cancel_rollback / esp_ota_mark_app_invalid_rollback_and_reboot, which
// actually reboots into the previous image), so benching them would rewrite the OTA data partition
// and reset the rig; only the deterministic CPU-side decision is ever timed here.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/ota_rollback -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/update/ota_rollback.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t ota_rollback_work[16]; // the borrow an entry takes; OtaRollback never reads it

/** @brief WAIT / COMMIT / ROLLBACK for an image in @p img_state at @p ms_since_boot. */
static protocore_ota_action ota_decide(uint8_t img_state, proto_bool self_test_ok, uint32_t ms_since_boot,
                                       uint32_t window_ms)
{
    OtaRollback.decide_args.img_state = img_state;
    OtaRollback.decide_args.self_test_ok = self_test_ok;
    OtaRollback.decide_args.ms_since_boot = ms_since_boot;
    OtaRollback.decide_args.window_ms = window_ms;
    OtaRollback.decide(ota_rollback_work);
    return OtaRollback.action;
}

void dbench_run(void)
{
    // Realistic decision inputs lifted straight from test/test_ota_rollback/test_ota_rollback.cpp
    // (already known-good, spec-conformant), one per decision branch. window_ms == the library
    // default PROTOCORE_OTA_CONFIRM_WINDOW_MS (30000).
    const uint32_t window_ms = 30000;

    for (;;)
    {
        DBENCH_BANNER("ota_rollback");
        volatile uint32_t sink = 0;

        // Non-pending image (normal boot): nothing to do -> WAIT (the common per-tick case).
        DBENCH_OP("OtaRollback.decide not-pending WAIT", 200000,
                  sink += (uint8_t)ota_decide(PROTOCORE_OTA_IMG_VALID, PROTO_FALSE, 999999, window_ms));

        // Pending + self-test passed -> COMMIT (mark valid).
        DBENCH_OP("OtaRollback.decide pending COMMIT", 200000,
                  sink += (uint8_t)ota_decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_TRUE, 1000, window_ms));

        // Pending, self-test not yet passed, still inside the confirm window -> WAIT.
        DBENCH_OP("OtaRollback.decide pending in-window", 200000,
                  sink += (uint8_t)ota_decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_FALSE, 5000, window_ms));

        // Pending, never confirmed, window elapsed -> ROLLBACK (self-heal decision).
        DBENCH_OP("OtaRollback.decide pending ROLLBACK", 200000,
                  sink += (uint8_t)ota_decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_FALSE, 40000, window_ms));

        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("ota_rollback")
