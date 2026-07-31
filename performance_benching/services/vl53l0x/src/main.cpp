// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the VL53L0X codec (services/peripherals/vl53l0x): the range-register
// decode (hi/lo -> mm), the data-ready check, and the range-status decode/validity. Pure register
// math - the I2C transfer is real-hardware and out of scope.
//
// Build/flash:  pio run -d performance_benching/device/vl53l0x -t upload --upload-port COM7
#include "device_bench.h"
#include "services/peripherals/vl53l0x/vl53l0x.h"
#include <Arduino.h>

static void vl53l0x_bench_task(void *)
{
    for (;;)
    {
        Serial.printf("DB ==== vl53l0x device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;
        DBENCH_OP("pc_vl53l0x_range_mm", 200000, sink += pc_vl53l0x_range_mm(0x03, 0xE8));
        DBENCH_OP("pc_vl53l0x_data_ready", 200000, sink += pc_vl53l0x_data_ready(0x01));
        DBENCH_OP("pc_vl53l0x_range_status", 200000, sink += pc_vl53l0x_range_status(0x58));
        DBENCH_OP("pc_vl53l0x_range_valid", 200000, sink += pc_vl53l0x_range_valid(0x58));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: vl53l0x device microbench");
    xTaskCreatePinnedToCore(vl53l0x_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
