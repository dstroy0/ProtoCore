// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the CC-Link cyclic fieldbus frame codec (services/fieldbus/cclink):
// Cclink.build/Cclink.parse frame and validate the cyclic
// [station][command][bit_data][word_data][checksum] exchange, Cclink.sum is the arithmetic
// checksum on its own, and Cclink.get_bit/Cclink.get_word are the RX/RY bit-device and
// RWr/RWw word-device process-image accessors. Everything here is pure (no heap, no RS-485, no
// CC-Link IE Field PHY) - worked example for performance_benching/device/<service>/: a pure protocol codec with no
// hardware involved, so every call exercises the real production code path (contrast with
// performance_benching/device/ads1115, a peripheral driver where the bus transaction itself is stubbed).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/cclink -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/cclink/cclink.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t cclink_work[16]; // the borrow an entry takes; Cclink never reads it

void dbench_run(void)
{
    // Known-good sample data lifted from test/test_cclink/test_cclink.cpp.
    static const uint8_t bits[2] = {0xA5, 0x00};
    static const uint8_t words[4] = {0x34, 0x12, 0x78, 0x56}; // 0x1234, 0x5678
    static uint8_t frame[16];
    CclinkV.build_args.station = 5;
    CclinkV.build_args.command = CCLINK_CMD_REFRESH;
    CclinkV.build_args.bits = bits;
    CclinkV.build_args.bit_len = sizeof(bits);
    CclinkV.build_args.words = words;
    CclinkV.build_args.word_len = sizeof(words);
    CclinkV.build_args.out = frame;
    CclinkV.build_args.cap = sizeof(frame);
    Cclink.build(cclink_work);
    size_t frame_len = CclinkV.n;

    for (;;)
    {
        DBENCH_BANNER("cclink");
        volatile size_t sinkz = 0;
        volatile uint8_t sink8 = 0;
        volatile uint16_t sink16 = 0;
        volatile bool sinkb = false;

        CclinkV.sum_args.bytes = frame;
        CclinkV.sum_args.len = frame_len;
        DBENCH_OP("Cclink.sum", 100000, sink8 += (Cclink.sum(cclink_work), CclinkV.value));

        CclinkV.build_args.station = 5;
        CclinkV.build_args.command = CCLINK_CMD_REFRESH;
        CclinkV.build_args.bits = bits;
        CclinkV.build_args.bit_len = sizeof(bits);
        CclinkV.build_args.words = words;
        CclinkV.build_args.word_len = sizeof(words);
        CclinkV.build_args.out = frame;
        CclinkV.build_args.cap = sizeof(frame);
        DBENCH_OP("Cclink.build", 100000, sinkz += (Cclink.build(cclink_work), CclinkV.n));

        static CcLinkFrame parsed;
        CclinkV.parse_args.frame = frame;
        CclinkV.parse_args.len = frame_len;
        CclinkV.parse_args.out = &parsed;
        DBENCH_OP("Cclink.parse", 100000, sinkb ^= (Cclink.parse(cclink_work), CclinkV.ok));

        CclinkV.get_bit_args.bits = bits;
        CclinkV.get_bit_args.bit_len = sizeof(bits);
        CclinkV.get_bit_args.index = 7;
        DBENCH_OP("Cclink.get_bit", 200000, sinkb ^= (Cclink.get_bit(cclink_work), CclinkV.ok));

        CclinkV.get_word_args.words = words;
        CclinkV.get_word_args.word_len = sizeof(words);
        CclinkV.get_word_args.index = 1;
        DBENCH_OP("Cclink.get_word", 200000, sink16 += (Cclink.get_word(cclink_work), CclinkV.u16));

        (void)sinkz;
        (void)sink8;
        (void)sink16;
        (void)sinkb;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("cclink")
