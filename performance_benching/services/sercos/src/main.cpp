// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SERCOS III codec (services/fieldbus/sercos): the IDN build/parse
// (S/P param-set + data block) and the telegram build/parse. Pure; no fieldbus link.
//
// Build/flash:  pio run -d performance_benching/device/sercos -t upload --upload-port COM7
#include "device_bench.h"
#include "services/fieldbus/sercos/sercos.h"
#include <Arduino.h>

static void sercos_bench_task(void *)
{
    static const uint8_t data[16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                                     0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00};
    static uint8_t frame[64];
    size_t flen = pc_sercos_build(0x02, 4, 1000, data, sizeof(data), frame, sizeof(frame));

    for (;;)
    {
        Serial.printf("DB ==== sercos device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        DBENCH_OP("pc_sercos_idn", 200000, sink += pc_sercos_idn(false, 0, 100));
        bool prod;
        uint8_t ps;
        uint16_t db;
        DBENCH_OP("pc_sercos_idn_parse", 200000, {
            pc_sercos_idn_parse(0x0064, &prod, &ps, &db);
            sink += db;
        });
        static uint8_t out[64];
        DBENCH_OP("pc_sercos_build", 200000,
                  sink += pc_sercos_build(0x02, 4, 1000, data, sizeof(data), out, sizeof(out)));
        SercosTelegram tg;
        DBENCH_OP("pc_sercos_parse", 200000, sink += pc_sercos_parse(frame, flen, &tg));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: sercos device microbench");
    xTaskCreatePinnedToCore(sercos_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
