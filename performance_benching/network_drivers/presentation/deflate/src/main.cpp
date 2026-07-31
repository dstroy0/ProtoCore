// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the permessage-deflate codec (network_drivers/presentation/
// deflate): deflate_raw() + inflate_raw() over a JSON telemetry frame. Pure (caller scratch).
// Build/flash: pio run -d performance_benching/network_drivers/presentation/deflate -t upload
#include "device_bench.h"
#include "network_drivers/presentation/codec/deflate/deflate.h"
#include "network_drivers/presentation/codec/inflate/inflate.h"
#include <Arduino.h>
#include <string.h>

static const char *MSG = "{\"type\":\"telemetry\",\"ts\":1720700000,\"sensors\":["
                         "{\"id\":1,\"name\":\"temp\",\"unit\":\"C\",\"value\":21.4},"
                         "{\"id\":2,\"name\":\"humidity\",\"unit\":\"%\",\"value\":48.0}]}";

static void deflate_bench_task(void *)
{
    static uint8_t dscratch[DEFLATE_SCRATCH_SIZE];
    static uint8_t iscratch[INFLATE_SCRATCH_SIZE];
    const size_t n = strlen(MSG);
    static uint8_t comp[512], plain[512];
    size_t clen = 0;
    deflate_raw((const uint8_t *)MSG, n, comp, sizeof(comp), &clen, dscratch, DEFLATE_SCRATCH_SIZE);
    comp[clen] = 0x00;
    comp[clen + 1] = 0x00;
    comp[clen + 2] = 0xFF;
    comp[clen + 3] = 0xFF;
    for (;;)
    {
        Serial.printf("DB ==== deflate device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile int sink = 0;
        DBENCH_BULK("deflate_raw (json msg)", 20000, n, {
            size_t o = 0;
            sink += (int)deflate_raw((const uint8_t *)MSG, n, comp, sizeof(comp), &o, dscratch, DEFLATE_SCRATCH_SIZE);
        });
        DBENCH_BULK("inflate_raw (json msg)", 20000, n, {
            size_t plen = 0;
            sink += (int)inflate_raw(comp, clen + 4, plain, sizeof(plain), &plen, iscratch, INFLATE_SCRATCH_SIZE);
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
    Serial.println("\nDB boot: deflate device microbench");
    xTaskCreatePinnedToCore(deflate_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}
void loop()
{
    delay(1000);
}
