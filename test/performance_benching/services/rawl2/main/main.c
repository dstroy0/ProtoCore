// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the raw L2 Ethernet codec (services/fieldbus/rawl2): the Ethernet II
// and 802.1Q VLAN frame builders, the frame parser, and the IEEE 802.3 FCS (CRC-32). All pure
// (no NIC / no DMA) - this is the per-frame CPU cost of framing a captured or forwarded L2 frame.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/rawl2 -t upload --upload-port COM7
#include "device_bench.h"
#include "services/fieldbus/rawl2/rawl2.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    static const uint8_t DST[6] = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E};
    static const uint8_t SRC[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    static uint8_t payload[64];
    for (int i = 0; i < (int)sizeof(payload); i++)
    {
        payload[i] = (uint8_t)(i * 7 + 1);
    }
    static uint8_t frame[128], vframe[128];
    size_t flen = protocore_eth_build(DST, SRC, 0x88B8, payload, sizeof(payload), frame, sizeof(frame));
    protocore_eth_build_vlan(DST, SRC, 5, false, 100, 0x0800, payload, sizeof(payload), vframe, sizeof(vframe));

    for (;;)
    {
        DBENCH_BANNER("rawl2");
        volatile size_t sink = 0;
        static uint8_t out[128];
        DBENCH_OP("protocore_eth_build (64B payload)", 200000,
                  sink += protocore_eth_build(DST, SRC, 0x88B8, payload, sizeof(payload), out, sizeof(out)));
        DBENCH_OP("protocore_eth_build_vlan (64B)", 200000,
                  sink +=
                  protocore_eth_build_vlan(DST, SRC, 5, false, 100, 0x0800, payload, sizeof(payload), out, sizeof(out)));
        EthFrame ef;
        DBENCH_OP("protocore_eth_parse", 200000, sink += protocore_eth_parse(frame, flen, &ef));
        DBENCH_OP("protocore_eth_parse (vlan)", 200000, sink += protocore_eth_parse(vframe, flen + 4, &ef));
        DBENCH_BULK("protocore_eth_fcs (CRC-32)", 50000, flen, sink += protocore_eth_fcs(frame, flen));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("rawl2")
