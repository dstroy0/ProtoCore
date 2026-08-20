// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the S7comm codec (Siemens S7 / ISO-on-TCP application layer): the client
// Setup-Communication + Read-Var request builders and the response-header parser. Pure (no socket), so it
// links standalone. The device figure comes from the rig /bench op; this host ns/op + MB/s is a relative
// baseline. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_S7COMM=1 test/performance_benching/services/s7comm/host.c
//   src/services/fieldbus/s7comm/s7comm.c src/mmgr/protomem.c src/mmgr/protostr.c -o /tmp/bs && /tmp/bs

#define PROTOCORE_ENABLE_S7COMM 1
#include "services/fieldbus/s7comm/s7comm.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

static uint8_t s7comm_work[16]; // the borrow an entry takes; S7comm never reads it

int main(void)
{
    // A 3-item Read Var job (two DB reads + a flag bit) - a realistic PLC poll.
    const S7ReadItem items[3] = {
        {S7_AREA_DB, 1, 0, S7_TS_BYTE, 16},
        {S7_AREA_DB, 2, 4, S7_TS_WORD, 8},
        {S7_AREA_FLAGS, 0, 0, S7_TS_BIT, 1},
    };
    uint8_t req[256];
    S7commV.build_read_request_args.buf = req;
    S7commV.build_read_request_args.cap = sizeof(req);
    S7commV.build_read_request_args.pdu_ref = 0x0002;
    S7commV.build_read_request_args.items = items;
    S7commV.build_read_request_args.n = 3;
    S7comm.build_read_request(s7comm_work);
    size_t req_len = S7commV.n;

    hbench_header();

    // S7comm.build_setup: the Setup-Communication job (negotiate AMQ + PDU size) - once per connection.
    {
        uint8_t buf[64];
        volatile size_t sink = 0;
        double ns = 0.0;
        S7commV.build_setup_args.buf = buf;
        S7commV.build_setup_args.cap = sizeof(buf);
        S7commV.build_setup_args.pdu_ref = 0x0001;
        S7commV.build_setup_args.max_amq_calling = 1;
        S7commV.build_setup_args.max_amq_called = 1;
        S7commV.build_setup_args.pdu_size = 480;
        S7comm.build_setup(s7comm_work);
        HBENCH_NS(5000000, sink += S7commV.n, ns);
        hbench_row("s7comm", "build_setup", ns, (double)(sink ? 22.0 : 0.0));
        (void)sink;
    }

    // S7comm.build_read_request: frame an N-item Read Var job (S7-ANY pointers) - the PLC-poll transmit op.
    {
        uint8_t buf[256];
        volatile size_t sink = 0;
        double ns = 0.0;
        S7commV.build_read_request_args.buf = buf;
        S7commV.build_read_request_args.cap = sizeof(buf);
        S7commV.build_read_request_args.pdu_ref = 0x0002;
        S7commV.build_read_request_args.items = items;
        S7commV.build_read_request_args.n = 3;
        S7comm.build_read_request(s7comm_work);
        HBENCH_NS(2000000, sink += S7commV.n, ns);
        hbench_row("s7comm", "build_read_request (3)", ns, (double)req_len);
        (void)sink;
    }

    // S7comm.parse_header: validate the protocol id + ROSCTR + lengths and slice param/data - the receive op.
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            10000000,
            {
                S7Header h;
                S7commV.parse_header_args.buf = req;
                S7commV.parse_header_args.len = req_len;
                S7commV.parse_header_args.out = &h;
                S7comm.parse_header(s7comm_work);
                if (S7commV.ok)
                {
                    sink += h.header_len;
                }
            },
            ns);
        hbench_row("s7comm", "parse_header", ns, (double)req_len);
        (void)sink;
    }

    return 0;
}
