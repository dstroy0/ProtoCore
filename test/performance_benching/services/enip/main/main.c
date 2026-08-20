// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the EtherNet/IP encapsulation codec (services/fieldbus/enip):
// building a RegisterSession request, building a SendRRData request that wraps a CIP message
// (Common Packet Format: Null Address item + Unconnected Data item), parsing the 24-octet
// encapsulation header back off the wire, and extracting the CIP reply out of a SendRRData
// command-data block - all pure (no heap, no sockets). Worked example for performance_benching/device/<service>/:
// a pure protocol codec with no hardware involved (contrast with performance_benching/device/ads1115, a peripheral
// driver where the bus transaction itself is stubbed), so every call here exercises the real
// production code path. Sample byte layouts are taken from test/test_enip/test_enip.cpp
// (already known-good, spec-conformant per the Wireshark ENIP dissector).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/enip -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/enip/enip.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t enip_work[16]; // the borrow an entry takes; Enip never reads it

void dbench_run(void)
{
    // A (stub) CIP request, same bytes used by test_send_rr_data_round_trip.
    static const uint8_t cip[] = {0x4C, 0x20, 0x01, 0x24, 0x01};
    static const uint8_t sender_context[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    static uint8_t reg_buf[32];
    static uint8_t rr_buf[64];

    // Pre-build a SendRRData block once so parse/extract bench a real, valid wire capture.
    EnipV.build_send_rr_data_args.buf = rr_buf;
    EnipV.build_send_rr_data_args.cap = sizeof(rr_buf);
    EnipV.build_send_rr_data_args.session_handle = 0x12345678;
    EnipV.build_send_rr_data_args.sender_context = sender_context;
    EnipV.build_send_rr_data_args.timeout = 5;
    EnipV.build_send_rr_data_args.cip = cip;
    EnipV.build_send_rr_data_args.cip_len = sizeof(cip);
    Enip.build_send_rr_data(enip_work);
    size_t rr_len = EnipV.n;

    for (;;)
    {
        DBENCH_BANNER("enip");
        volatile size_t sink = 0;

        EnipV.build_register_session_args.buf = reg_buf;
        EnipV.build_register_session_args.cap = sizeof(reg_buf);
        EnipV.build_register_session_args.sender_context = sender_context;
        DBENCH_OP("Enip.build_register_session", 100000, sink += (Enip.build_register_session(enip_work), EnipV.n));

        EnipV.build_send_rr_data_args.buf = rr_buf;
        EnipV.build_send_rr_data_args.cap = sizeof(rr_buf);
        EnipV.build_send_rr_data_args.session_handle = 0x12345678;
        EnipV.build_send_rr_data_args.sender_context = sender_context;
        EnipV.build_send_rr_data_args.timeout = 5;
        EnipV.build_send_rr_data_args.cip = cip;
        EnipV.build_send_rr_data_args.cip_len = sizeof(cip);
        DBENCH_OP("Enip.build_send_rr_data", 50000, sink += (Enip.build_send_rr_data(enip_work), EnipV.n));

        DBENCH_OP("Enip.parse", 100000, {
            EipHeader h;
            const uint8_t *data;
            size_t data_len;
            EnipV.parse_args.buf = rr_buf;
            EnipV.parse_args.len = rr_len;
            EnipV.parse_args.out = &h;
            EnipV.parse_args.data = &data;
            EnipV.parse_args.data_len = &data_len;
            Enip.parse(enip_work);
            sink += EnipV.ok ? 1 : 0;
        });

        DBENCH_OP("Enip.parse_send_rr_data", 100000, {
            EipHeader h;
            const uint8_t *data;
            size_t data_len;
            EnipV.parse_args.buf = rr_buf;
            EnipV.parse_args.len = rr_len;
            EnipV.parse_args.out = &h;
            EnipV.parse_args.data = &data;
            EnipV.parse_args.data_len = &data_len;
            Enip.parse(enip_work);
            const uint8_t *out_cip;
            size_t out_len;
            EnipV.parse_send_rr_data_args.data = data;
            EnipV.parse_send_rr_data_args.data_len = data_len;
            EnipV.parse_send_rr_data_args.cip = &out_cip;
            EnipV.parse_send_rr_data_args.cip_len = &out_len;
            Enip.parse_send_rr_data(enip_work);
            sink += EnipV.ok ? 1 : 0;
        });

        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("enip")
