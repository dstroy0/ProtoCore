// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the parameterized CRC engine (shared_primitives/crc.h): the
// Rocksoft/Williams begin/update/final over a 1 KiB buffer, for two presets (CRC-16/MODBUS reflected,
// CRC-16/XMODEM non-reflected). This is the shared checksum core the fieldbus codecs build on. Pure,
// header-only. Build/flash: pio run -d performance_benching/core/crc -t upload
#include "device_bench.h"
#include "shared_primitives/crc.h"
#include <Arduino.h>

static uint32_t crc_one(const pc_crc_params *p, const uint8_t *d, size_t n)
{
    return pc_crc_final(p, pc_crc_update(p, pc_crc_begin(p), d, n));
}

static void crc_bench_task(void *)
{
    static uint8_t buf[1024];
    for (size_t i = 0; i < sizeof(buf); i++)
        buf[i] = (uint8_t)(i * 31 + 7);
    for (;;)
    {
        Serial.printf("DB ==== crc device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;
        DBENCH_BULK("pc_crc CRC-16/MODBUS (1 KiB)", 20000, 1024, sink += crc_one(&PC_CRC16_MODBUS, buf, 1024));
        DBENCH_BULK("pc_crc CRC-16/XMODEM (1 KiB)", 20000, 1024, sink += crc_one(&PC_CRC16_XMODEM, buf, 1024));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: crc device microbench");
    xTaskCreatePinnedToCore(crc_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}
void loop()
{
    delay(1000);
}
