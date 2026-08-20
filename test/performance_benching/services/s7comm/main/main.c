// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Siemens S7comm codec (services/fieldbus/s7comm): S7comm.build_setup
// (negotiate PDU size, once per connection), S7comm.build_read_request (frame an N-item Read Var job -
// the PLC-poll transmit op) and S7comm.parse_header (validate protocol id / ROSCTR / lengths and slice
// param+data - the receive op). Pure; the ISO-on-TCP socket is out of scope.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/s7comm -t upload --upload-port COM7
#include "device_bench.h"
#include "services/fieldbus/s7comm/s7comm.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t s7comm_work[16]; // the borrow an entry takes; S7comm never reads it

void dbench_run(void)
{
    // A 3-item Read Var job (two DB reads + a flag bit) - a realistic PLC poll (from test/host bench).
    static const S7ReadItem items[3] = {
        {S7_AREA_DB, 1, 0, S7_TS_BYTE, 16},
        {S7_AREA_DB, 2, 4, S7_TS_WORD, 8},
        {S7_AREA_FLAGS, 0, 0, S7_TS_BIT, 1},
    };
    static uint8_t req[256];
    S7commV.build_read_request_args.buf = req;
    S7commV.build_read_request_args.cap = sizeof(req);
    S7commV.build_read_request_args.pdu_ref = 0x0002;
    S7commV.build_read_request_args.items = items;
    S7commV.build_read_request_args.n = 3;
    S7comm.build_read_request(s7comm_work);
    size_t req_len = S7commV.n;

    for (;;)
    {
        DBENCH_BANNER("s7comm");
        volatile size_t sink = 0;
        static uint8_t buf[256];
        S7commV.build_setup_args.buf = buf;
        S7commV.build_setup_args.cap = sizeof(buf);
        S7commV.build_setup_args.pdu_ref = 0x0001;
        S7commV.build_setup_args.max_amq_calling = 1;
        S7commV.build_setup_args.max_amq_called = 1;
        S7commV.build_setup_args.pdu_size = 480;
        DBENCH_OP("S7comm.build_setup", 200000, sink += (S7comm.build_setup(s7comm_work), S7commV.n));
        S7commV.build_read_request_args.buf = buf;
        S7commV.build_read_request_args.cap = sizeof(buf);
        S7commV.build_read_request_args.pdu_ref = 0x0002;
        S7commV.build_read_request_args.items = items;
        S7commV.build_read_request_args.n = 3;
        DBENCH_OP("S7comm.build_read_request x3", 200000, sink += (S7comm.build_read_request(s7comm_work), S7commV.n));
        S7Header h;
        S7commV.parse_header_args.buf = req;
        S7commV.parse_header_args.len = req_len;
        S7commV.parse_header_args.out = &h;
        DBENCH_OP("S7comm.parse_header", 200000, sink += (S7comm.parse_header(s7comm_work), S7commV.ok));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("s7comm")
