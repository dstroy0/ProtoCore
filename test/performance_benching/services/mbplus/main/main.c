// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Modbus Plus HDLC token-bus codec (services/fieldbus/mbplus):
// the CRC-16/X-25 FCS, the HDLC frame builder (7E addr ctrl payload CRClo CRChi 7E), the frame
// validator/parser, and the token-rotation ring helper - all pure (no heap, no stdlib, no bus). Like
// performance_benching/device/modbus, this is a pure protocol codec with no hardware involved: the physical 1 Mbit/s
// Modbus Plus token bus is hardware-gated and out of scope on this rig, so every call here exercises
// the real deterministic frame + token-MAC production code path directly.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/mbplus -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/mbplus/mbplus.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t mbplus_work[16]; // the borrow an entry takes; Mbplus never reads it

void dbench_run(void)
{
    // CRC-16/X-25 check vector: CRC of "123456789" == 0x906E (from test/test_mbplus/test_mbplus.cpp).
    static const uint8_t crc_vec[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    // A realistic data-frame payload: Modbus Plus routing byte + a Modbus read PDU (from the test).
    static const uint8_t payload[3] = {0x10, 0x03, 0x00};
    // A pre-built, known-good frame for the parse bench: station 5, data control, 3-byte payload.
    static uint8_t frame[16];
    static size_t frame_len = 0;
    Mbplus.build_args.address = 5;
    Mbplus.build_args.control = MBPLUS_CTRL_DATA;
    Mbplus.build_args.payload = payload;
    Mbplus.build_args.payload_len = sizeof(payload);
    Mbplus.build_args.out = frame;
    Mbplus.build_args.cap = sizeof(frame);
    Mbplus.build(mbplus_work);
    frame_len = Mbplus.n;

    static uint8_t out[32];

    for (;;)
    {
        DBENCH_BANNER("mbplus");
        volatile size_t sink = 0;
        volatile uint16_t sink16 = 0;
        volatile uint8_t sink8 = 0;
        volatile bool sinkb = false;

        // CRC-16/X-25 FCS over the 9-byte check vector - bulk op, so we also get ns/byte + MB/s.
        Mbplus.crc_args.bytes = crc_vec;
        Mbplus.crc_args.len = sizeof(crc_vec);
        DBENCH_BULK("Mbplus.crc (X-25)", 100000, sizeof(crc_vec),
                    sink16 += (Mbplus.crc(mbplus_work), Mbplus.value));
        // Build a full HDLC data frame (flags + addr + ctrl + payload + CRC).
        Mbplus.build_args.address = 5;
        Mbplus.build_args.control = MBPLUS_CTRL_DATA;
        Mbplus.build_args.payload = payload;
        Mbplus.build_args.payload_len = sizeof(payload);
        Mbplus.build_args.out = out;
        Mbplus.build_args.cap = sizeof(out);
        DBENCH_OP("Mbplus.build (data+3B)", 50000,
                  sink += (Mbplus.build(mbplus_work), Mbplus.n));
        // Validate flags + CRC and parse the pre-built frame.
        MbPlusFrame f;
        Mbplus.parse_args.frame = frame;
        Mbplus.parse_args.len = frame_len;
        Mbplus.parse_args.out = &f;
        DBENCH_OP("Mbplus.parse", 50000, sinkb = (Mbplus.parse(mbplus_work), Mbplus.ok));
        // Token-ring rotation helper (the token-bus MAC's next-holder computation).
        Mbplus.next_token_args.current = sink8;
        Mbplus.next_token_args.max_station = 64;
        DBENCH_OP("Mbplus.next_token", 200000, sink8 += (Mbplus.next_token(mbplus_work), Mbplus.value));

        (void)sink;
        (void)sink16;
        (void)sink8;
        (void)sinkb;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("mbplus")
