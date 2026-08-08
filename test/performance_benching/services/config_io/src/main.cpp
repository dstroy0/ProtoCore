// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for schema-driven config export/restore (services/storage/config_io):
// pc_config_export() serializes a schema's current values from the config store into `key=value`
// lines; pc_config_import() parses such a blob back into the store. Both functions call through to
// services/storage/config_store, and both call pc_config_begin() internally on *every* invocation - on
// ESP32 that is the real Arduino `Preferences` NVS wrapper, so (unlike modbus's pure protocol codec)
// each iteration here really does close/reopen the NVS namespace handle, and import performs a real
// flash write per field. There is no missing/unattached peripheral to work around here (contrast
// with performance_benching/device/ads1115, where the I2C bus is skipped entirely because no ADS1115 breakout is
// attached to this rig): NVS is on-die and always present, so the calls run for real rather than
// being stubbed. But they are genuinely expensive (flash open/write latency, not just CPU cycles),
// so N is kept small (tens, not thousands) to bound both wall-clock time and NVS flash wear, and a
// dedicated "bench" NVS namespace is used so this never touches the device's real wifi/net config
// keys. Sample schema/values are copied from test/test_config_io/test_config_io.cpp (already
// known-good, host-tested).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/config_io -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/storage/config_io/config_io.h"
#include "services/storage/config_store/config_store.h"
#include <Arduino.h>

static const pc_cfg_field SCHEMA[] = {
    {"ssid", pc_cfg_type::PC_CFG_STR},
    {"port", pc_cfg_type::PC_CFG_U32},
    {"name", pc_cfg_type::PC_CFG_STR},
};
static const size_t N_FIELDS = sizeof(SCHEMA) / sizeof(SCHEMA[0]);

// Known-good round-trip blob (test_round_trip in test_config_io.cpp), reused so import parses real,
// spec-conformant "key=value" lines rather than invented data.
static const char IMPORT_BLOB[] = "ssid=abc\nport=1234\nname=x\n";

static void config_io_bench_task(void *)
{
    // Seed the schema's values once, outside the timed loop (mirrors modbus's one-time
    // pc_modbus_set_holding_reg() seeding) - the export bench below re-serializes these every call.
    pc_config_begin("bench");
    pc_config_set_str("ssid", "myssid");
    pc_config_set_u32("port", 8080);
    pc_config_set_str("name", "node1");

    static char buf[256];

    for (;;)
    {
        Serial.printf("DB ==== config_io device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        // Reopens NVS + 3 reads per call; small N bounds real flash latency, not just CPU cycles.
        DBENCH_OP("pc_config_export", 50, sink += pc_config_export("bench", SCHEMA, N_FIELDS, buf, sizeof(buf)));
        // Reopens NVS + 3 writes per call (real flash commits); smaller N than export.
        DBENCH_OP("pc_config_import", 20,
                  sink += pc_config_import("bench", SCHEMA, N_FIELDS, IMPORT_BLOB, sizeof(IMPORT_BLOB) - 1));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: config_io device microbench");
    xTaskCreatePinnedToCore(config_io_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
