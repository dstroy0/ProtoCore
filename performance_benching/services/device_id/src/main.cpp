// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the MAC-derived device UUID codec (server/signaling/device_id):
// pc_uuid_from_mac() builds an RFC 4122 version-5 UUID from a 6-byte MAC (namespace = the RFC
// 4122 DNS namespace, name = lowercase MAC hex) via a single-block SHA-1 - pure (no heap, no
// hardware). Worked example for performance_benching/device/<service>/: like services/fieldbus/modbus, this is a pure
// protocol/format codec, so every call here exercises the real production code path. Out of
// scope: pc_device_uuid(), the ARDUINO-gated wrapper that reads this chip's factory MAC via
// esp_read_mac() - that's a one-time cold read of a provisioned eFuse value, not a repeatable
// codec operation, and it is exactly the part test_matrix.json carves out as untestable on the
// host (only pc_uuid_from_mac() is checked there against Python's uuid.uuid5 reference values).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/device_id -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/signaling/device_id.h"
#include <Arduino.h>

static void device_id_bench_task(void *)
{
    // Known-good MAC vectors from test/test_device_id/test_device_id.cpp (checked against
    // Python's uuid.uuid5 reference values).
    static const uint8_t mac_a[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    static const uint8_t mac_b[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    static char out[PC_UUID_STR_LEN];

    for (;;)
    {
        Serial.printf("DB ==== device_id device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        DBENCH_OP("pc_uuid_from_mac (aabbccddeeff)", 20000, pc_uuid_from_mac(mac_a, out));
        DBENCH_OP("pc_uuid_from_mac (001122334455)", 20000, pc_uuid_from_mac(mac_b, out));
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: device_id device microbench");
    xTaskCreatePinnedToCore(device_id_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
