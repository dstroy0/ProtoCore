// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the AutomationDirect / Koyo DirectNET serial frame codec
// (services/fieldbus/directnet): building the SOH..ETB header (enquiry) frame, building and parsing the
// STX..ETX data frame, and the LRC (longitudinal XOR) checksum both frames rely on - all pure,
// zero heap, no UART involved. Worked example for performance_benching/device/<service>/: a pure protocol codec,
// so every call here exercises the real production code path (contrast with performance_benching/device/ads1115,
// a peripheral driver where the bus transaction itself is stubbed). The UART transport and the
// ENQ/ACK/NAK handshake sequencing are the device step and are out of scope on this rig, which has
// no DirectLOGIC PLC or RS-232/RS-485 transceiver attached.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/directnet -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/directnet/directnet.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t directnet_work[16]; // the borrow an entry takes; Directnet never reads it

void dbench_run(void)
{
    // Header/enquiry frame: slave 1, READ, V-memory address 0x0040, 2 blocks (matches
    // test/test_directnet/test_directnet.cpp's test_header_frame).
    static uint8_t hdr[16];
    // Data frame: STX + "ABCD" + ETX + LRC (matches test_data_frame_roundtrip).
    static const uint8_t payload[4] = {'A', 'B', 'C', 'D'};
    static uint8_t data_frame[16];
    DirectnetV.data_args.data = payload;
    DirectnetV.data_args.data_len = sizeof(payload);
    DirectnetV.data_args.out = data_frame;
    DirectnetV.data_args.cap = sizeof(data_frame);
    DirectnetV.data(directnet_work);
    size_t data_frame_len = DirectnetV.n;

    // Buffer for the standalone LRC bulk bench (10 bytes of frame body).
    static const uint8_t lrc_buf[10] = {'0', '1', 0x30, '0', '0', '4', '0', '0', '2', DNET_ETB};

    for (;;)
    {
        DBENCH_BANNER("directnet");
        volatile uint8_t sink8 = 0;
        volatile size_t sinkz = 0;
        volatile bool sinkb = false;

        DirectnetV.lrc_args.bytes = lrc_buf;
        DirectnetV.lrc_args.len = sizeof(lrc_buf);
        DBENCH_BULK("Directnet.lrc", 100000, sizeof(lrc_buf),
                    sink8 += (Directnet.lrc(directnet_work), DirectnetV.value));

        DirectnetV.header_args.slave = 1;
        DirectnetV.header_args.type = DNET_READ;
        DirectnetV.header_args.address = 0x0040;
        DirectnetV.header_args.blocks = 2;
        DirectnetV.header_args.out = hdr;
        DirectnetV.header_args.cap = sizeof(hdr);
        DBENCH_OP("Directnet.header build", 100000, sinkz += (Directnet.header(directnet_work), DirectnetV.n));

        DirectnetV.data_args.data = payload;
        DirectnetV.data_args.data_len = sizeof(payload);
        DirectnetV.data_args.out = data_frame;
        DirectnetV.data_args.cap = sizeof(data_frame);
        DBENCH_OP("Directnet.data build (4B)", 100000, sinkz += (DirectnetV.data(directnet_work), DirectnetV.n));

        {
            const uint8_t *d = NULL;
            size_t dl = 0;
            DirectnetV.data_parse_args.frame = data_frame;
            DirectnetV.data_parse_args.len = data_frame_len;
            DirectnetV.data_parse_args.data = &d;
            DirectnetV.data_parse_args.data_len = &dl;
            DBENCH_OP("Directnet.data_parse", 100000, sinkb = (Directnet.data_parse(directnet_work), DirectnetV.ok));
        }

        (void)sink8;
        (void)sinkz;
        (void)sinkb;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("directnet")
