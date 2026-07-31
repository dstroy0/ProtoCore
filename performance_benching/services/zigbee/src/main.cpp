// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Zigbee ASH codec (services/radio/zigbee): the ASH CRC-16 and
// the byte-stuffed ASH frame encode/decode that carries EZSP to an NCP. Pure; the UART is elsewhere.
//
// Build/flash:  pio run -d performance_benching/device/zigbee -t upload --upload-port COM7
#include "device_bench.h"
#include "services/radio/zigbee/zigbee.h"
#include <Arduino.h>

static void zigbee_bench_task(void *)
{
    static const uint8_t payload[16] = {0x00, 0x00, 0x00, 0x02, 0x11, 0x22, 0x33, 0x44,
                                        0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC};
    static uint8_t frame[64];
    uint16_t flen = pc_ash_frame_encode(0x25, payload, sizeof(payload), frame, sizeof(frame));

    for (;;)
    {
        Serial.printf("DB ==== zigbee device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;
        DBENCH_BULK("pc_ash_crc16 (16B)", 200000, sizeof(payload), sink += pc_ash_crc16(payload, sizeof(payload)));
        static uint8_t out[64];
        DBENCH_OP("pc_ash_frame_encode", 200000,
                  sink += pc_ash_frame_encode(0x25, payload, sizeof(payload), out, sizeof(out)));
        DBENCH_OP("pc_ash_frame_decode", 200000, {
            uint8_t ctl;
            uint8_t pay[64];
            uint16_t plen = 0;
            sink += pc_ash_frame_decode(frame, flen, &ctl, pay, sizeof(pay), &plen) >= 0 ? plen : 0;
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
    Serial.println("\nDB boot: zigbee device microbench");
    xTaskCreatePinnedToCore(zigbee_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
