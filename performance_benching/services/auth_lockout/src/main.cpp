// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the per-IP brute-force auth lockout (services/security/auth_lockout):
// auth_lockout_fail() / auth_lockout_remaining_ms() / auth_lockout_succeed() / auth_lockout_reset()
// are the exponential-backoff lockout state machine over a fixed 16-slot BSS bucket table, keyed by
// the full family-tagged pc_ip - pure (the millisecond clock is passed in by the caller), no heap, no
// lwIP, no Arduino I/O. Like performance_benching/device/modbus, this is a worked example of a pure protocol/state
// codec with no hardware involved, so every call here exercises the real production code path
// (contrast with performance_benching/device/ads1115, a peripheral driver where the bus transaction is stubbed) -
// there is nothing to stub for this service.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/auth_lockout -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "network_drivers/network/ip.h"
#include "services/security/auth_lockout/auth_lockout.h"
#include <Arduino.h>

// Build a v4 pc_ip from a host-order word (0x0A000001 -> 10.0.0.1). Mirrors the v4w() helper in
// test/test_auth_lockout/test_auth_lockout.cpp (already known-good).
static pc_ip v4w(uint32_t host_order)
{
    return pc_ip_from_v4_octets((uint8_t)(host_order >> 24), (uint8_t)(host_order >> 16), (uint8_t)(host_order >> 8),
                                (uint8_t)host_order);
}

static void auth_lockout_bench_task(void *)
{
    pc_ip attacker = v4w(0x0A000001u); // 10.0.0.1 - driven past the lockout threshold each cycle
    pc_ip stranger = v4w(0x0A0000FFu); // 10.0.0.255 - never recorded a failure (always a table miss)

    for (;;)
    {
        // Re-arm: reset the whole table, then drive `attacker` past PC_AUTH_LOCKOUT_THRESHOLD so
        // the steady-state benches below sit in the locked (post-threshold, exponential-backoff)
        // code path - the state an active brute-force source occupies for the rest of its window.
        auth_lockout_reset();
        for (int i = 0; i < PC_AUTH_LOCKOUT_THRESHOLD; i++)
            auth_lockout_fail(&attacker, 0);

        Serial.printf("DB ==== auth_lockout device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());

        // Hot path 1: recording a failure from an address already past the threshold (the common
        // case while an attacker's lockout window is active and it keeps retrying).
        DBENCH_OP("auth_lockout_fail (locked)", 50000, auth_lockout_fail(&attacker, 0));

        volatile uint32_t sink = 0;
        // Hot path 2: the per-request lockout check for a locked address (bucket found, still
        // inside its window).
        DBENCH_OP("auth_lockout_remaining_ms (hit)", 200000, sink += auth_lockout_remaining_ms(&attacker, 0));
        // Hot path 3: the per-request lockout check for an address with no bucket at all - the
        // common case for ordinary (non-attacking) traffic, and the full 16-slot scan's worst case.
        DBENCH_OP("auth_lockout_remaining_ms (miss)", 200000, sink += auth_lockout_remaining_ms(&stranger, 0));
        (void)sink;

        // Full-table reset cost (16 slots) - bounds the fixed per-slot clear cost independent of
        // whatever state the table was in.
        DBENCH_OP("auth_lockout_reset (16 slots)", 100000, auth_lockout_reset());

        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: auth_lockout device microbench");
    xTaskCreatePinnedToCore(auth_lockout_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
