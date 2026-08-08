// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the AD9238 SPI configuration-port codec (services/peripherals/ad9238):
// building the 16-bit instruction word (R/W + 2-bit byte-count + 13-bit address, MSB first), the
// single-register write and read transaction framing, and the "device update" shadow-register
// transfer transaction - all pure (no SPI clocking, no real silicon). Worked example for
// performance_benching/device/<service>/ pure protocol codecs (contrast with performance_benching/device/ads1115, a
// peripheral driver where the bus transaction itself is stubbed): this rig has no AD9238 board attached, and the sample
// data path (parallel CMOS/LVDS bus) is out of scope everywhere - only the deterministic CPU-side SPI
// configuration-port codec is ever benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/ad9238 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/peripherals/ad9238/ad9238.h"
#include <Arduino.h>

static void ad9238_bench_task(void *)
{
    static uint8_t out3[3];
    static uint8_t out2[2];

    for (;;)
    {
        Serial.printf("DB ==== ad9238 device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile bool sinkb = false;
        volatile size_t sinkz = 0;

        DBENCH_OP("pc_ad9238_build_instruction", 200000,
                  sinkb = pc_ad9238_build_instruction(false, (uint16_t)Ad9238Reg::AD9238_REG_POWER_DOWN, 1, out2));
        DBENCH_OP("pc_ad9238_build_write", 200000,
                  sinkz += pc_ad9238_build_write((uint16_t)Ad9238Reg::AD9238_REG_POWER_DOWN, 0x01, out3, sizeof(out3)));
        DBENCH_OP("pc_ad9238_build_read", 200000,
                  sinkz += pc_ad9238_build_read((uint16_t)Ad9238Reg::AD9238_REG_CHIP_ID, out2, sizeof(out2)));
        DBENCH_OP("pc_ad9238_build_transfer", 200000, sinkz += pc_ad9238_build_transfer(out3, sizeof(out3)));

        (void)sinkb;
        (void)sinkz;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: ad9238 device microbench");
    xTaskCreatePinnedToCore(ad9238_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
