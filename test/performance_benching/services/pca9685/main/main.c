// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the PCA9685 PWM/servo codec (server/peripherals/pca9685): the pure,
// host-tested CPU-side math and framing - the PRESCALE value for a PWM frequency (with clamping),
// a channel's register base, a servo pulse-width (us) -> 12-bit OFF count (with clamping), and the
// 5-byte channel PWM write encoder. This rig has no PCA9685 breakout wired up, so the I2C-over-Wire
// binding (protocore_pca9685_begin/set_pwm/set_servo_us, the half that actually touches the bus) is out of
// scope everywhere here - only the deterministic codec is ever benched, exactly like
// performance_benching/device/ads1115.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/pca9685 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/peripherals/pca9685/pca9685.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    static uint8_t buf[5];
    // The bytes the module runs out of, taken once. Every entry below is called with it.
    uint8_t *w = protocore_pca9685_span();

    for (;;)
    {
        DBENCH_BANNER("pca9685");
        volatile uint32_t sink8 = 0;
        volatile uint32_t sink16 = 0;
        volatile size_t sinkn = 0;

        // The arguments are set outside the timed expression and the entry is called inside it, so
        // what is timed is one call and not the staging that precedes it.

        // PRESCALE for the classic 50 Hz servo frequency: round(25e6/(4096*freq))-1, clamped 3..255.
        Pca9685V.prescale_args.freq_hz = 50;
        DBENCH_OP("Pca9685.prescale 50Hz", 200000, (Pca9685.prescale(w), sink8 += Pca9685V.value));
        // Channel register base (LED_ON_L = 0x06 + 4*ch); channel 15 -> 0x42.
        Pca9685V.channel_reg_args.channel = 15;
        DBENCH_OP("Pca9685.channel_reg ch15", 200000, (Pca9685.channel_reg(w), sink8 += Pca9685V.value));
        // Servo mid pulse (1.5 ms of a 20 ms/50 Hz period) -> 12-bit OFF count (307).
        Pca9685V.us_to_count_args.microseconds = 1500;
        Pca9685V.us_to_count_args.freq_hz = 50;
        DBENCH_OP("Pca9685.us_to_count 1500us", 200000, (Pca9685.us_to_count(w), sink16 += Pca9685V.count));
        // 5-byte channel PWM write: [reg, ON_L, ON_H, OFF_L, OFF_H], counts 12-bit little-endian.
        Pca9685V.set_pwm_bytes_args.buf = buf;
        Pca9685V.set_pwm_bytes_args.cap = sizeof(buf);
        Pca9685V.set_pwm_bytes_args.channel = 0;
        Pca9685V.set_pwm_bytes_args.on = 0;
        Pca9685V.set_pwm_bytes_args.off = 307;
        DBENCH_OP("Pca9685.set_pwm_bytes ch0", 200000, (Pca9685.set_pwm_bytes(w), sinkn += Pca9685V.n));

        (void)sink8;
        (void)sink16;
        (void)sinkn;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("pca9685")
