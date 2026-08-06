// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SHT3x codec (services/peripherals/sht3x): the CRC-8 (used to validate
// each I2C word), the raw->milli-degree / raw->milli-percent conversions, and the 6-byte response
// parser. Pure integer math - the I2C read is real-hardware and out of scope.
//
// Build/flash:  pio run -d performance_benching/device/sht3x -t upload --upload-port COM7
#include "device_bench.h"
#include "services/peripherals/sht3x/sht3x.h"
#include <Arduino.h>

static void sht3x_bench_task(void *)
{
    // A real SHT3x measurement response: T word + CRC, RH word + CRC.
    static const uint8_t resp[6] = {0x61, 0x3D, 0x42, 0x5C, 0xE7, 0x3E};

    for (;;)
    {
        Serial.printf("DB ==== sht3x device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile int32_t sink = 0;
        DBENCH_OP("pc_sht3x_crc8 (2 bytes)", 200000, sink += pc_sht3x_crc8(resp, 2));
        DBENCH_OP("pc_sht3x_temp_mc", 200000, sink += pc_sht3x_temp_mc(0x613D));
        DBENCH_OP("pc_sht3x_rh_mpct", 200000, sink += pc_sht3x_rh_mpct(0x425C));
        int32_t t, rh;
        DBENCH_OP("pc_sht3x_parse (6B resp)", 200000, { sink += pc_sht3x_parse(resp, &t, &rh) ? t : 0; });
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: sht3x device microbench");
    xTaskCreatePinnedToCore(sht3x_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
