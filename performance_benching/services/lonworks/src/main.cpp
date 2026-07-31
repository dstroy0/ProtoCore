// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the LonWorks / LON-IP network-variable codec
// (services/fieldbus/lonworks): the LonTalk application PDU ([msg-code][14-bit selector][value]) build +
// parse, and the two most-common SNVT scalar encodings, SNVT_temp (0.01 K fixed-point) and
// SNVT_switch (0..100.5% level + state). All pure, zero heap, no stdlib - like performance_benching/device/modbus,
// this is a pure protocol codec with no hardware involved, so every call here exercises the real
// production code path. The LON/IP UDP transport is deliberately out of scope: this rig has no
// network attached, and the codec above is what a NetVar update actually costs on the CPU.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/lonworks -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/lonworks/lonworks.h"
#include <Arduino.h>

static void lonworks_bench_task(void *)
{
    // Known-good, spec-conformant literals lifted from test/test_lonworks/test_lonworks.cpp:
    // an NV-update carrying a 2-byte SNVT value to 14-bit selector 0x1234.
    static const uint8_t nv_value[2] = {0xAB, 0xCD};
    // The wire PDU that pc_lon_build_nv() produces for that update: [0x80][0x12 0x34][0xAB 0xCD].
    static const uint8_t nv_pdu[5] = {Lon::LON_MSG_NV_UPDATE, 0x12, 0x34, 0xAB, 0xCD};
    static uint8_t out[16];
    // A SNVT_temp value encoding 25.0 C, and a SNVT_switch value encoding 50% / state 1.
    static const uint8_t snvt_temp_val[2] = {0x74, 0x77}; // (25 + 273.15)*100 = 29815 = 0x7477
    static const uint8_t snvt_switch_val[2] = {100, 1};   // 50% * 2 = 100, state 1

    for (;;)
    {
        Serial.printf("DB ==== lonworks device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        volatile double sinkd = 0;

        DBENCH_OP("pc_lon_build_nv (upd+2B)", 100000,
                  sink +=
                  pc_lon_build_nv(Lon::LON_MSG_NV_UPDATE, 0x1234, nv_value, sizeof(nv_value), out, sizeof(out)));

        LonNv nv;
        DBENCH_OP("pc_lon_parse_nv", 200000, sink += pc_lon_parse_nv(nv_pdu, sizeof(nv_pdu), &nv) ? 1u : 0u);

        DBENCH_OP("pc_lon_snvt_temp_encode", 100000, pc_lon_snvt_temp_encode(25.0, out));
        DBENCH_OP("pc_lon_snvt_temp_decode", 200000, sinkd += pc_lon_snvt_temp_decode(snvt_temp_val));

        DBENCH_OP("pc_lon_snvt_switch_encode", 100000, pc_lon_snvt_switch_encode(50.0, 1, out));
        {
            double pct = 0;
            uint8_t st = 0;
            DBENCH_OP("pc_lon_snvt_switch_decode", 200000, pc_lon_snvt_switch_decode(snvt_switch_val, &pct, &st));
            sinkd += pct + st;
        }

        (void)sink;
        (void)sinkd;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: lonworks device microbench");
    xTaskCreatePinnedToCore(lonworks_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
