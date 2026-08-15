// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the VXI-11 codec (services/instrumentation/vxi11): the ONC RPC record mark and
// the VXI-11 request builders (GetPort via the portmapper, CreateLink). Pure XDR/RPC framing; the TCP
// socket is out of scope.
//
// Build/flash:  idf.py -C test/performance_benching/vxi11 -t upload --upload-port COM7
#include "device_bench.h"
#include "services/instrumentation/vxi11/vxi11.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    for (;;)
    {
        DBENCH_BANNER("vxi11");
        volatile size_t sink = 0;
        static uint8_t buf[128];
        DBENCH_OP("protocore_rpc_record_mark", 200000, sink += protocore_rpc_record_mark(buf, sizeof(buf), 64));
        bool last;
        uint32_t frag;
        DBENCH_OP("protocore_rpc_parse_record_mark", 200000,
                  sink += protocore_rpc_parse_record_mark(buf, 4, &last, &frag));
        DBENCH_OP("protocore_vxi11_build_getport", 200000,
                  sink += protocore_vxi11_build_getport(buf, sizeof(buf), 0x0001, 0x0607AF, 1, 6));
        DBENCH_OP("protocore_vxi11_build_create_link", 200000,
                  sink += protocore_vxi11_build_create_link(buf, sizeof(buf), 0x0002, 42, false, 0, "inst0"));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("vxi11")
