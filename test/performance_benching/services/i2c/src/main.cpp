// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for services/i2c - EXCEPT there is nothing pure to benchmark here,
// so this sketch deliberately benches nothing and exists only to keep services/i2c under the same
// performance_benching/device/<service>/ umbrella and to prove the header still compiles and links.
//
// Why nothing is benched: services/peripherals/i2c.h is a single header whose entire public surface is one inline
// function, pc_i2c_begin(), which is a direct pass-through to Wire.begin(PC_I2C_SDA_PIN,
// PC_I2C_SCL_PIN) - it brings up the shared I2C bus for the peripheral drivers (RTC, SHT3x, MPR121,
// ADS1115, INA219, PCA9685). There is no CPU-side codec, no encode/decode/parse/checksum/CRC, no
// config-word build, no conversion math - nothing separable from the real hardware bus. Calling
// pc_i2c_begin() would perform an actual I2C bring-up on GPIO 21/22, which is exactly the kind of real
// hardware I/O this rig (no peripherals attached) must never do. Unlike performance_benching/device/ads1115 - where the
// I2C transaction is stubbed but a deterministic config-word/conversion codec remains to bench - i2c.h
// has no such codec to isolate, so the honest result is an empty benchmark. We still #include the header
// and take (never call) the address of pc_i2c_begin() so the compiler/linker prove the real production
// symbol is valid in this context.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/i2c -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a capture
// opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/peripherals/i2c.h"
#include <Arduino.h>

static void i2c_bench_task(void *)
{
    // Reference the real production symbol so it is compiled/linked, but NEVER call it: invoking
    // pc_i2c_begin() would drive a live I2C bus bring-up (Wire.begin()), which this rig has no
    // peripherals for and which is out of scope for a pure microbench.
    void (*volatile fn)() = &pc_i2c_begin;
    (void)fn;

    for (;;)
    {
        Serial.printf("DB ==== i2c device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        // NOTE-4 service: services/peripherals/i2c.h is 100% a hardware bus bring-up (Wire.begin()) with no pure
        // CPU-side operation to time. Nothing is benched here on purpose - see the file header.
        Serial.println("DB (no pure op to bench: services/peripherals/i2c.h is hardware bus bring-up only)");
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: i2c device microbench (no-op: hardware bring-up only)");
    xTaskCreatePinnedToCore(i2c_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
