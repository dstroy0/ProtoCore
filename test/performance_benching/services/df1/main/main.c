// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Allen-Bradley DF1 full-duplex frame codec
// (services/fieldbus/df1): the BCC and CRC-16/ARC checksums, the frame builder (DLE STX + DLE
// byte-stuffing + DLE ETX + check), and the validating, un-stuffing parser - all pure (no
// heap, no UART). Worked example for performance_benching/device/<service>/: a pure protocol codec with no
// hardware involved, so every call here exercises the real production code path (contrast
// with performance_benching/device/ads1115, a peripheral driver where the bus transaction itself is
// stubbed). Vectors below are copied straight out of test/test_df1/test_df1.cpp
// (test_bcc_vector, test_crc_vector, test_round_trip_bcc, test_round_trip_crc) - already
// known-good against AB pub. 1770-6.5.16; out of scope: the serial UART transport itself,
// which DF1 rides on but this codec never touches.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/df1 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/df1/df1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t df1_work[16]; // the borrow an entry takes; Df1 never reads it

void dbench_run(void)
{
    // test_bcc_vector: 0x07 + 0x19 = 0x20 -> BCC 0xE0.
    static const uint8_t bcc_data[] = {0x07, 0x19};
    // test_crc_vector: CRC-16/ARC of "123456789" is the standard check-value string.
    static const uint8_t crc_data[] = "123456789";

    // test_round_trip_bcc: two embedded DLE (0x10) bytes, exercises byte-stuffing.
    static const uint8_t bcc_frame_data[] = {0x10, 0x05, 0x10, 0xAB};
    // test_round_trip_crc: includes one embedded DLE (0x10) byte.
    static const uint8_t crc_frame_data[] = {0x00, 0x05, 0x0F, 0x00, 0x10, 0x42};

    static uint8_t bcc_frame[32];
    static uint8_t crc_frame[32];
    Df1.build_frame_args.buf = bcc_frame;
    Df1.build_frame_args.cap = sizeof(bcc_frame);
    Df1.build_frame_args.data = bcc_frame_data;
    Df1.build_frame_args.data_len = sizeof(bcc_frame_data);
    Df1.build_frame_args.check = DF1_CHECK_BCC;
    Df1.build_frame(df1_work);
    size_t bcc_frame_len = Df1.n;
    Df1.build_frame_args.buf = crc_frame;
    Df1.build_frame_args.cap = sizeof(crc_frame);
    Df1.build_frame_args.data = crc_frame_data;
    Df1.build_frame_args.data_len = sizeof(crc_frame_data);
    Df1.build_frame_args.check = DF1_CHECK_CRC;
    Df1.build_frame(df1_work);
    size_t crc_frame_len = Df1.n;

    static uint8_t out[32];

    for (;;)
    {
        DBENCH_BANNER("df1");
        volatile size_t sink = 0;
        volatile uint16_t sink16 = 0;
        size_t out_len;

        Df1.bcc_args.data = bcc_data;
        Df1.bcc_args.len = sizeof(bcc_data);
        DBENCH_BULK("Df1.bcc", 200000, sizeof(bcc_data),
                    sink += (Df1.bcc(df1_work), Df1.value));

        Df1.crc_args.data = crc_data;
        Df1.crc_args.len = sizeof(crc_data) - 1;
        DBENCH_BULK("Df1.crc", 100000, sizeof(crc_data) - 1,
                    sink16 += (Df1.crc(df1_work), Df1.u16));

        Df1.build_frame_args.buf = bcc_frame;
        Df1.build_frame_args.cap = sizeof(bcc_frame);
        Df1.build_frame_args.data = bcc_frame_data;
        Df1.build_frame_args.data_len = sizeof(bcc_frame_data);
        Df1.build_frame_args.check = DF1_CHECK_BCC;
        DBENCH_OP("Df1.build_frame BCC", 50000,
                  sink += (Df1.build_frame(df1_work), Df1.n));

        Df1.build_frame_args.buf = crc_frame;
        Df1.build_frame_args.cap = sizeof(crc_frame);
        Df1.build_frame_args.data = crc_frame_data;
        Df1.build_frame_args.data_len = sizeof(crc_frame_data);
        Df1.build_frame_args.check = DF1_CHECK_CRC;
        DBENCH_OP("Df1.build_frame CRC", 50000,
                  sink += (Df1.build_frame(df1_work), Df1.n));

        Df1.parse_frame_args.buf = bcc_frame;
        Df1.parse_frame_args.len = bcc_frame_len;
        Df1.parse_frame_args.check = DF1_CHECK_BCC;
        Df1.parse_frame_args.out = out;
        Df1.parse_frame_args.out_cap = sizeof(out);
        Df1.parse_frame_args.out_len = &out_len;
        DBENCH_OP("Df1.parse_frame BCC", 50000,
                  sink +=
                  (Df1.parse_frame(df1_work), Df1.ok));

        Df1.parse_frame_args.buf = crc_frame;
        Df1.parse_frame_args.len = crc_frame_len;
        Df1.parse_frame_args.check = DF1_CHECK_CRC;
        Df1.parse_frame_args.out = out;
        Df1.parse_frame_args.out_cap = sizeof(out);
        Df1.parse_frame_args.out_len = &out_len;
        DBENCH_OP("Df1.parse_frame CRC", 50000,
                  sink +=
                  (Df1.parse_frame(df1_work), Df1.ok));

        (void)sink;
        (void)sink16;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("df1")
