// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the INA219 current/power codec (services/peripherals/ina219): decoding the
// bus-voltage register (bits [15:3], LSB 4 mV, status bits ignored), decoding the signed shunt-voltage
// register (LSB 10 uV), computing the calibration register from the current LSB and shunt resistance,
// and scaling the raw current / power registers by the current LSB - all pure integer math, no I2C.
// Peripheral-driver example for performance_benching/device/<service>/: this rig has no INA219 breakout attached, so
// pc_ina219_begin/read_bus_mv/read_shunt_uv/read_current_ua/read_power_uw (the I2C-over-Wire half)
// are out of scope everywhere - only the deterministic CPU-side codec is ever benched. The register
// values below are copied straight from test/test_ina219/test_ina219.cpp (known-good, spec-conformant).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/ina219 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/peripherals/ina219/ina219.h"
#include <Arduino.h>

static void ina219_bench_task(void *)
{
    for (;;)
    {
        Serial.printf("DB ==== ina219 device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile int32_t sink32 = 0;
        volatile uint16_t sink16 = 0;

        // Bus voltage: register 0x19C8 -> 3300 mV (value in bits [15:3], LSB 4 mV, low status bits ignored).
        DBENCH_OP("pc_ina219_bus_mv", 200000, sink32 += pc_ina219_bus_mv(0x19C8));
        // Shunt voltage: signed raw 320 -> 3200 uV (LSB 10 uV).
        DBENCH_OP("pc_ina219_shunt_uv", 200000, sink32 += pc_ina219_shunt_uv((int16_t)320));
        // Calibration register: 100 uA/bit LSB, 100 mohm shunt -> 4096 (32-bit divide, clamped to 16 bits).
        DBENCH_OP("pc_ina219_calibration", 200000, sink16 += pc_ina219_calibration(100, 100));
        // Current scale: raw 1000 * 100 uA/bit -> 100000 uA (100 mA), 64-bit intermediate.
        DBENCH_OP("pc_ina219_current_ua", 200000, sink32 += pc_ina219_current_ua((int16_t)1000, 100));
        // Power scale: raw 500 * 20 * 100 uA/bit -> 1000000 uW (1 W), 64-bit intermediate.
        DBENCH_OP("pc_ina219_power_uw", 200000, sink32 += pc_ina219_power_uw((int16_t)500, 100));

        (void)sink32;
        (void)sink16;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: ina219 device microbench");
    xTaskCreatePinnedToCore(ina219_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
