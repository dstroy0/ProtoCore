// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the OpenThread Spinel codec (services/radio/thread): the Spinel FCS
// (HDLC-Lite CRC-16), the packed-unsigned-int encode/decode, and the command-frame build. Pure; the
// NCP UART link is out of scope.
//
// Build/flash:  idf.py -C test/performance_benching/thread -t upload --upload-port COM7
#include "device_bench.h"
#include "services/radio/thread/thread.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t thread_work[16]; // the borrow an entry takes; Thread never reads it

void dbench_run(void)
{
    static const uint8_t val[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    for (;;)
    {
        DBENCH_BANNER("thread");
        volatile uint32_t sink = 0;
        DBENCH_BULK("protocore_spinel_fcs (8B)", 200000, sizeof(val), sink += protocore_spinel_fcs(val, sizeof(val)));
        static uint8_t pk[8];
        DBENCH_OP("protocore_spinel_pack_uint", 200000, sink += protocore_spinel_pack_uint(0x1234u, pk, sizeof(pk)));
        uint32_t uv;
        DBENCH_OP("protocore_spinel_unpack_uint", 200000, sink += protocore_spinel_unpack_uint(pk, sizeof(pk), &uv));
        static uint8_t out[64];
        DBENCH_OP("protocore_spinel_command_build", 200000,
                  sink += protocore_spinel_command_build(0x81, 0x02 /*PROP_VALUE_SET*/, 0x24 /*PROP_MAC_15_4_PANID*/,
                                                         val, sizeof(val), out, sizeof(out)));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("thread")
