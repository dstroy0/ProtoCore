// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the PCA9685 PWM/servo codec (services/peripherals/pca9685): the pure,
// host-tested CPU-side math and framing - the PRESCALE value for a PWM frequency (with clamping),
// a channel's register base, a servo pulse-width (us) -> 12-bit OFF count (with clamping), and the
// 5-byte channel PWM write encoder. This rig has no PCA9685 breakout wired up, so the I2C-over-Wire
// binding (pc_pca9685_begin/set_pwm/set_servo_us, the half that actually touches the bus) is out of
// scope everywhere here - only the deterministic codec is ever benched, exactly like
// performance_benching/device/ads1115.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/pca9685 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/peripherals/pca9685/pca9685.h"
#include <Arduino.h>

static void pca9685_bench_task(void *)
{
    static uint8_t buf[5];

    for (;;)
    {
        Serial.printf("DB ==== pca9685 device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink8 = 0;
        volatile uint32_t sink16 = 0;
        volatile size_t sinkn = 0;

        // PRESCALE for the classic 50 Hz servo frequency: round(25e6/(4096*freq))-1, clamped 3..255.
        DBENCH_OP("pc_pca9685_prescale 50Hz", 200000, sink8 += pc_pca9685_prescale(50));
        // Channel register base (LED_ON_L = 0x06 + 4*ch); channel 15 -> 0x42.
        DBENCH_OP("pc_pca9685_channel_reg ch15", 200000, sink8 += pc_pca9685_channel_reg(15));
        // Servo mid pulse (1.5 ms of a 20 ms/50 Hz period) -> 12-bit OFF count (307).
        DBENCH_OP("pc_pca9685_us_to_count 1500us", 200000, sink16 += pc_pca9685_us_to_count(1500, 50));
        // 5-byte channel PWM write: [reg, ON_L, ON_H, OFF_L, OFF_H], counts 12-bit little-endian.
        DBENCH_OP("pc_pca9685_set_pwm_bytes ch0", 200000,
                  sinkn += pc_pca9685_set_pwm_bytes(buf, sizeof(buf), 0, 0, 307));

        (void)sink8;
        (void)sink16;
        (void)sinkn;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: pca9685 device microbench");
    xTaskCreatePinnedToCore(pca9685_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
