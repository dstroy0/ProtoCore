// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the DShot ESC throttle codec (services/peripherals/dshot):
// pc_dshot_encode()/pc_dshot_decode() build and validate the 16-bit frame (11-bit value +
// telemetry bit + 4-bit nibble-xor CRC, including the bidirectional inverted-CRC variant), and
// pc_dshot_bit_ns()/pc_esc_pwm_ns() are the pure bit-timing / throttle->pulse-width math a
// driver needs to program the RMT peripheral or MCPWM. All four are pure integer math - no heap,
// no stdlib, no RMT/MCPWM touched. Worked example for performance_benching/device/<service>/: like
// performance_benching/device/modbus, this is a pure protocol codec with no hardware involved, so every call here
// exercises the real production code path; there is nothing to stub because dshot.cpp never
// references the RMT/MCPWM peripherals it is meant to feed - it only computes the numbers a
// caller would hand to them.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/dshot -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/peripherals/dshot/dshot.h"
#include <Arduino.h>

static void dshot_bench_task(void *)
{
    // Known-good vectors lifted straight from test/test_dshot/test_dshot.cpp.
    // value 1046, no telemetry, standard CRC -> 0x82C6.
    static const uint16_t kValue = 1046;
    // Frame 0x82C6 decodes (standard CRC) to value 1046, telemetry clear.
    static const uint16_t kFrameStd = 0x82C6;
    // Frame 0x82C9 decodes (bidirectional/inverted CRC) to value 1046.
    static const uint16_t kFrameBidir = 0x82C9;

    for (;;)
    {
        Serial.printf("DB ==== dshot device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());

        volatile uint16_t sink16 = 0;
        volatile uint32_t sink32 = 0;
        volatile bool sinkb = false;

        DBENCH_OP("pc_dshot_encode std", 200000, sink16 += pc_dshot_encode(kValue, false, false));
        DBENCH_OP("pc_dshot_encode bidir", 200000, sink16 += pc_dshot_encode(kValue, true, true));

        uint16_t val = 0;
        bool tel = false;
        DBENCH_OP("pc_dshot_decode std", 200000, sinkb |= pc_dshot_decode(kFrameStd, &val, &tel, false));
        DBENCH_OP("pc_dshot_decode bidir", 200000, sinkb |= pc_dshot_decode(kFrameBidir, &val, &tel, true));

        DBENCH_OP("pc_dshot_bit_ns", 200000, sink32 += pc_dshot_bit_ns(600, true));
        DBENCH_OP("pc_esc_pwm_ns", 200000, sink32 += pc_esc_pwm_ns(500, pc_esc_pwm::PC_ESC_ONESHOT125));

        (void)sink16;
        (void)sink32;
        (void)sinkb;
        (void)val;
        (void)tel;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: dshot device microbench");
    xTaskCreatePinnedToCore(dshot_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
