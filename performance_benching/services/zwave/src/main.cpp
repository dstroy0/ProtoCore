// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Z-Wave serial API codec (services/radio/zwave): the data-frame
// build (SOF + type + cmd + payload + XOR checksum), the validating parse, and the single-byte ACK
// build. Pure; the UART link to the Z-Wave controller is out of scope.
//
// Build/flash:  pio run -d performance_benching/device/zwave -t upload --upload-port COM7
#include "device_bench.h"
#include "services/radio/zwave/zwave.h"
#include <Arduino.h>

static void zwave_bench_task(void *)
{
    static const uint8_t data[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    static uint8_t frame[32];
    uint16_t flen = pc_zwave_build_frame(pc_zwave_type::ZWAVE_REQ, 0x13, data, sizeof(data), frame, sizeof(frame));

    for (;;)
    {
        Serial.printf("DB ==== zwave device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;
        static uint8_t out[32];
        DBENCH_OP("pc_zwave_build_frame", 200000,
                  sink += pc_zwave_build_frame(pc_zwave_type::ZWAVE_REQ, 0x13, data, sizeof(data), out, sizeof(out)));
        DBENCH_OP("pc_zwave_parse_frame", 200000, {
            uint8_t type;
            uint8_t cmd;
            const uint8_t *pdata;
            uint8_t pdata_len;
            sink += pc_zwave_parse_frame(frame, flen, &type, &cmd, &pdata, &pdata_len) >= 0 ? cmd : 0;
        });
        DBENCH_OP("pc_zwave_build_ack", 200000, sink += pc_zwave_build_ack(out, sizeof(out)));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: zwave device microbench");
    xTaskCreatePinnedToCore(zwave_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
