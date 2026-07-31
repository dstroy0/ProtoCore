// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the TI LDC1614 inductance-to-digital codec
// (services/peripherals/ldc1614): combining a DATA MSB/LSB register pair into the 28-bit result
// (pc_ldc1614_data), pulling the 4 error flags off the MSB register (pc_ldc1614_error),
// scaling a 28-bit result to a sensor frequency in Hz (pc_ldc1614_sensor_freq_hz, the
// data / 2^28 * fref math), and emitting the single-channel CH0 continuous-conversion
// bring-up as (reg, msb, lsb) triples (pc_ldc1614_build_config). All four are pure - no
// I2C, no heap. This rig has no LDC1614 breakout attached, so the Wire binding
// (pc_ldc1614_begin / pc_ldc1614_read_ch0, the I2C-over-Wire half) is deliberately out of
// scope everywhere here; only the deterministic CPU-side codec is ever benched (same posture
// as performance_benching/device/ads1115, the peripheral-driver worked example).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/ldc1614 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/peripherals/ldc1614/ldc1614.h"
#include <Arduino.h>

static void ldc1614_bench_task(void *)
{
    // Known-good sample vectors lifted straight from test/test_ldc1614/test_ldc1614.cpp:
    // MSB reg 0xF123 (error nibble 0xF, data MSB 0x123) + LSB reg 0x4567 -> 28-bit 0x01234567.
    const uint16_t msb_reg = 0xF123;
    const uint16_t lsb_reg = 0x4567;
    // A quarter-scale result (1<<27) against a 40 MHz reference clock scales to 20 MHz.
    const uint32_t data28 = 1u << 27;
    const uint32_t fref_hz = 40000000u;
    static uint8_t cfg[LDC1614_CONFIG_MAX];

    for (;;)
    {
        Serial.printf("DB ==== ldc1614 device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink32 = 0;
        volatile uint8_t sink8 = 0;
        volatile uint64_t sink64 = 0;
        volatile size_t sinksz = 0;

        DBENCH_OP("pc_ldc1614_data (28b combine)", 200000, sink32 += pc_ldc1614_data(msb_reg, lsb_reg));
        DBENCH_OP("pc_ldc1614_error (flag nibble)", 200000, sink8 += pc_ldc1614_error(msb_reg));
        DBENCH_OP("pc_ldc1614_sensor_freq_hz", 200000, sink64 += pc_ldc1614_sensor_freq_hz(data28, fref_hz));
        // Config builder emits 21 bytes (7 register writes * 3 bytes) - benched as a bulk producer.
        DBENCH_BULK("pc_ldc1614_build_config", 100000, LDC1614_CONFIG_MAX,
                    sinksz += pc_ldc1614_build_config(cfg, sizeof(cfg), 0xFFFF, 0x0400));

        (void)sink32;
        (void)sink8;
        (void)sink64;
        (void)sinksz;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: ldc1614 device microbench");
    xTaskCreatePinnedToCore(ldc1614_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
