// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the CC-Link cyclic fieldbus frame codec (services/fieldbus/cclink):
// pc_cclink_build()/pc_cclink_parse() frame and validate the cyclic
// [station][command][bit_data][word_data][checksum] exchange, pc_cclink_sum() is the arithmetic
// checksum on its own, and pc_cclink_get_bit()/pc_cclink_get_word() are the RX/RY bit-device and
// RWr/RWw word-device process-image accessors. Everything here is pure (no heap, no RS-485, no
// CC-Link IE Field PHY) - worked example for performance_benching/device/<service>/: a pure protocol codec with no
// hardware involved, so every call exercises the real production code path (contrast with
// performance_benching/device/ads1115, a peripheral driver where the bus transaction itself is stubbed).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/cclink -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/cclink/cclink.h"
#include <Arduino.h>

static void cclink_bench_task(void *)
{
    // Known-good sample data lifted from test/test_cclink/test_cclink.cpp.
    static const uint8_t bits[2] = {0xA5, 0x00};
    static const uint8_t words[4] = {0x34, 0x12, 0x78, 0x56}; // 0x1234, 0x5678
    static uint8_t frame[16];
    size_t frame_len = pc_cclink_build(5, CclinkCmd::CCLINK_CMD_REFRESH, bits, sizeof(bits), words, sizeof(words),
                                       frame, sizeof(frame));

    for (;;)
    {
        Serial.printf("DB ==== cclink device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sinkz = 0;
        volatile uint8_t sink8 = 0;
        volatile uint16_t sink16 = 0;
        volatile bool sinkb = false;

        DBENCH_OP("pc_cclink_sum", 100000, sink8 += pc_cclink_sum(frame, frame_len));

        DBENCH_OP("pc_cclink_build", 100000,
                  sinkz += pc_cclink_build(5, CclinkCmd::CCLINK_CMD_REFRESH, bits, sizeof(bits), words, sizeof(words),
                                           frame, sizeof(frame)));

        static CcLinkFrame parsed;
        DBENCH_OP("pc_cclink_parse", 100000, sinkb ^= pc_cclink_parse(frame, frame_len, &parsed));

        DBENCH_OP("pc_cclink_get_bit", 200000, sinkb ^= pc_cclink_get_bit(bits, sizeof(bits), 7));

        DBENCH_OP("pc_cclink_get_word", 200000, sink16 += pc_cclink_get_word(words, sizeof(words), 1));

        (void)sinkz;
        (void)sink8;
        (void)sink16;
        (void)sinkb;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: cclink device microbench");
    xTaskCreatePinnedToCore(cclink_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
