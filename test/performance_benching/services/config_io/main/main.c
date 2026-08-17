// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for schema-driven config export/restore (server/storage/config_io):
// protocore_config_export() serializes a schema's current values from the config store into `key=value`
// lines; protocore_config_import() parses such a blob back into the store. Both functions call through to
// server/storage/config_store, and both call protocore_config_begin() internally on *every* invocation - on
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
//   idf.py -C test/performance_benching/config_io -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/storage/config_io/config_io.h"
#include "server/storage/config_store/config_store.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const protocore_cfg_field SCHEMA[] = {
    {"ssid", PROTOCORE_CFG_STR},
    {"port", PROTOCORE_CFG_U32},
    {"name", PROTOCORE_CFG_STR},
};
static const size_t N_FIELDS = sizeof(SCHEMA) / sizeof(SCHEMA[0]);

// Known-good round-trip blob (test_round_trip in test_config_io.cpp), reused so import parses real,
// spec-conformant "key=value" lines rather than invented data.
static const char IMPORT_BLOB[] = "ssid=abc\nport=1234\nname=x\n";

static uint8_t config_io_work[16]; // the borrow an entry takes; ConfigIo never reads it

void dbench_run(void)
{
    ConfigStore.begin_args.ns = "bench";
    ConfigStore.begin(protocore_config_store_span());
    // Seed the schema's values once, outside the timed loop (mirrors modbus's one-time
    // protocore_modbus_set_holding_reg() seeding) - the export bench below re-serializes these every call.
    ConfigStore.set_str_args.key = "ssid";
    ConfigStore.set_str_args.val = "myssid";
    ConfigStore.set_str(protocore_config_store_span());
    ConfigStore.set_u32_args.key = "port";
    ConfigStore.set_u32_args.val = 8080;
    ConfigStore.set_u32(protocore_config_store_span());
    ConfigStore.set_str_args.key = "name";
    ConfigStore.set_str_args.val = "node1";
    ConfigStore.set_str(protocore_config_store_span());

    static char buf[256];

    for (;;)
    {
        DBENCH_BANNER("config_io");
        volatile size_t sink = 0;
        // The entry call stays inside DBENCH_OP so the timed loop measures the NVS round trip, not
        // the read that follows it. The args do not vary, so they are staged once.
        // Reopens NVS + 3 reads per call; small N bounds real flash latency, not just CPU cycles.
        ConfigIo.export_args.ns = "bench";
        ConfigIo.export_args.fields = SCHEMA;
        ConfigIo.export_args.n = N_FIELDS;
        ConfigIo.export_args.out = buf;
        ConfigIo.export_args.cap = sizeof(buf);
        DBENCH_OP("ConfigIo.export", 50, (ConfigIo.export(config_io_work), sink += ConfigIo.n));
        // Reopens NVS + 3 writes per call (real flash commits); smaller N than export.
        ConfigIo.import_args.ns = "bench";
        ConfigIo.import_args.fields = SCHEMA;
        ConfigIo.import_args.n = N_FIELDS;
        ConfigIo.import_args.text = IMPORT_BLOB;
        ConfigIo.import_args.len = sizeof(IMPORT_BLOB) - 1;
        DBENCH_OP("ConfigIo.import", 20, (ConfigIo.import(config_io_work), sink += ConfigIo.n));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("config_io")
