// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SunSpec codec (services/energy/sunspec): the "SunS" marker check
// and the big-endian register accessors on the read side, plus a small writer sequence on the build
// side. Pure register math over a byte buffer; the Modbus transport is elsewhere.
//
// Build/flash:  pio run -d performance_benching/device/sunspec -t upload --upload-port COM7
#include "device_bench.h"
#include "services/energy/sunspec/sunspec.h"
#include <Arduino.h>

static void sunspec_bench_task(void *)
{
    // A register image: "SunS" marker (0x5375 0x6E53) then some model body registers.
    static const uint8_t regs[16] = {0x53, 0x75, 0x6E, 0x53, 0x00, 0x01, 0x00, 0x42,
                                     0xFF, 0x9C, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};

    for (;;)
    {
        Serial.printf("DB ==== sunspec device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;
        DBENCH_OP("pc_sunspec_check_marker", 200000, sink += pc_sunspec_check_marker(regs, sizeof(regs)));
        DBENCH_OP("pc_sunspec_u16", 200000, sink += pc_sunspec_u16(regs, 3));
        DBENCH_OP("pc_sunspec_i16", 200000, sink += (uint32_t)pc_sunspec_i16(regs, 4));
        DBENCH_OP("pc_sunspec_u32", 200000, sink += pc_sunspec_u32(regs, 5));
        SunSpecWriter w;
        static uint8_t out[64];
        DBENCH_OP("pc_sunspec writer (marker+hdr+2)", 200000, {
            pc_sunspec_writer_init(&w, out, sizeof(out));
            pc_sunspec_write_marker(&w);
            pc_sunspec_write_model_header(&w, 1, 66);
            pc_sunspec_write_u16(&w, 0x1234);
            sink += pc_sunspec_write_i16(&w, -5) ? 1 : 0;
        });
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: sunspec device microbench");
    xTaskCreatePinnedToCore(sunspec_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
