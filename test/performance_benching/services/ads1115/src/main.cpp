// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the ADS1115 ADC codec (services/peripherals/ads1115): building the
// 16-bit single-shot config word (channel/gain/data-rate) and converting a signed raw sample to
// microvolts - both pure, no I2C. Worked example for performance_benching/device/<service>/ peripheral drivers: this
// rig has no ADS1115 breakout attached, so pc_ads1115_begin/read_raw/read_uv (the I2C-over-Wire
// half) are out of scope everywhere - only the deterministic CPU-side codec is ever benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/ads1115 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines.
#include "device_bench.h"
#include "services/peripherals/ads1115/ads1115.h"
#include <Arduino.h>

static void ads1115_bench_task(void *)
{
    for (;;)
    {
        Serial.printf("DB ==== ads1115 device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint16_t sink16 = 0;
        volatile int32_t sink32 = 0;
        DBENCH_OP("pc_ads1115_config_single", 200000,
                  sink16 += pc_ads1115_config_single(0, ADS1115_GAIN_1, ADS1115_DR_128));
        DBENCH_OP("pc_ads1115_raw_to_uv", 200000, sink32 += pc_ads1115_raw_to_uv(16384, ADS1115_GAIN_2));
        (void)sink16;
        (void)sink32;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: ads1115 device microbench");
    xTaskCreatePinnedToCore(ads1115_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
