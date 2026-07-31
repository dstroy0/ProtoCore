// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the typed NVS config store (services/storage/config_store): the
// string/u32/blob getters' bounds-checked default-fallback path, and the string setter's guarded
// length scan. On ESP32 (ARDUINO) config_store's get/set functions are thin wrappers directly over
// Arduino `Preferences` (real NVS flash) - unlike performance_benching/device/ads1115 there is no separable pure
// codec step to call instead, so actually opening NVS here would make every op below a real flash
// transaction (slow, and repeated writes wear flash - unacceptable for a block that loops forever).
// This rig's NVS partition is therefore never opened: pc_config_begin() is deliberately never
// called, so `Preferences`'s own internal "not started" guard makes every call below return its
// safe default/no-op immediately - real production code, zero flash access - mirroring how
// performance_benching/device/ads1115 simply never calls the I2C-touching pc_ads1115_begin/read_raw/read_uv. The
// CPU cost this rig CAN measure is what get/set do before (or instead of) touching flash: the
// bounds-checked copy-the-default logic in pc_config_get_str, and the length scan in
// pc_config_set_str. Sample keys/values are copied from test/test_config_store/test_config_store.cpp
// (already known-good).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/config_store -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/storage/config_store/config_store.h"
#include <Arduino.h>

static void config_store_bench_task(void *)
{
    // pc_config_begin() is deliberately never called - see file header. Every op below therefore
    // takes Preferences' "not started" guard path; NVS flash is never touched.
    static char out_str[4];
    static uint8_t out_blob[5];

    for (;;)
    {
        Serial.printf("DB ==== config_store device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());

        volatile size_t sink_sz = 0;
        volatile bool sinkb = false;
        volatile uint32_t sink32 = 0;

        // Missing-key default fallback: strnlen(def) + capacity clamp + memcpy + null-terminate.
        // out_cap=4 truncates the 9-char default to 3 chars - the same clamp path exercised by
        // test_str_truncates_to_capacity (same literal "123456789" / 4-byte buffer).
        DBENCH_OP("pc_config_get_str (default, trunc)", 50000,
                  sink_sz += pc_config_get_str("k", out_str, sizeof(out_str), "123456789"));

        // Setter's guarded path: strnlen(val, PC_CONFIG_VAL_MAX+1) is computed and compared
        // against Preferences::putString's (guarded, no-op) return - the real per-call cost of a
        // set() before NVS has ever been opened. Key/value from test_str_round_trip.
        DBENCH_OP("pc_config_set_str (guarded)", 50000, sinkb |= pc_config_set_str("ssid", "myssid"));

        // Missing-key u32 default (key/default from test_u32_round_trip's sample IP, 192.168.1.100).
        DBENCH_OP("pc_config_get_u32 (default)", 100000, sink32 += pc_config_get_u32("ip", 0xC0A80164u));

        // Missing-key blob lookup (key from test_blob_missing_returns_zero / test_blob_round_trip).
        DBENCH_OP("pc_config_get_blob (missing)", 100000,
                  sink_sz += pc_config_get_blob("psk", out_blob, sizeof(out_blob)));

        (void)sink_sz;
        (void)sinkb;
        (void)sink32;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: config_store device microbench");
    xTaskCreatePinnedToCore(config_store_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
