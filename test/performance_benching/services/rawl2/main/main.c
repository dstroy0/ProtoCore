// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

static uint8_t rawl2_work[16]; // the borrow an entry takes; Rawl2 never reads it

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
    Rawl2.build_args.dst = DST;
    Rawl2.build_args.src = SRC;
    Rawl2.build_args.ethertype = 0x88B8;
    Rawl2.build_args.payload = payload;
    Rawl2.build_args.payload_len = sizeof(payload);
    Rawl2.build_args.out = frame;
    Rawl2.build_args.cap = sizeof(frame);
    Rawl2.build(rawl2_work);
    size_t flen = Rawl2.n;
    Rawl2.build_vlan_args.dst = DST;
    Rawl2.build_vlan_args.src = SRC;
    Rawl2.build_vlan_args.pcp = 5;
    Rawl2.build_vlan_args.dei = false;
    Rawl2.build_vlan_args.vid = 100;
    Rawl2.build_vlan_args.ethertype = 0x0800;
    Rawl2.build_vlan_args.payload = payload;
    Rawl2.build_vlan_args.payload_len = sizeof(payload);
    Rawl2.build_vlan_args.out = vframe;
    Rawl2.build_vlan_args.cap = sizeof(vframe);
    Rawl2.build_vlan(rawl2_work);

    for (;;)
    {
        DBENCH_BANNER("rawl2");
        volatile size_t sink = 0;
        static uint8_t out[128];
        Rawl2.build_args.dst = DST;
        Rawl2.build_args.src = SRC;
        Rawl2.build_args.ethertype = 0x88B8;
        Rawl2.build_args.payload = payload;
        Rawl2.build_args.payload_len = sizeof(payload);
        Rawl2.build_args.out = out;
        Rawl2.build_args.cap = sizeof(out);
        DBENCH_OP("Rawl2.build (64B payload)", 200000,
                  sink += (Rawl2.build(rawl2_work), Rawl2.n));
        Rawl2.build_vlan_args.dst = DST;
        Rawl2.build_vlan_args.src = SRC;
        Rawl2.build_vlan_args.pcp = 5;
        Rawl2.build_vlan_args.dei = false;
        Rawl2.build_vlan_args.vid = 100;
        Rawl2.build_vlan_args.ethertype = 0x0800;
        Rawl2.build_vlan_args.payload = payload;
        Rawl2.build_vlan_args.payload_len = sizeof(payload);
        Rawl2.build_vlan_args.out = out;
        Rawl2.build_vlan_args.cap = sizeof(out);
        DBENCH_OP("Rawl2.build_vlan (64B)", 200000,
                  sink += (Rawl2.build_vlan(rawl2_work), Rawl2.n));
        EthFrame ef;
        Rawl2.parse_args.frame = frame;
        Rawl2.parse_args.len = flen;
        Rawl2.parse_args.out = &ef;
        DBENCH_OP("Rawl2.parse", 200000, sink += (Rawl2.parse(rawl2_work), Rawl2.ok));
        Rawl2.parse_args.frame = vframe;
        Rawl2.parse_args.len = flen + 4;
        Rawl2.parse_args.out = &ef;
        DBENCH_OP("Rawl2.parse (vlan)", 200000, sink += (Rawl2.parse(rawl2_work), Rawl2.ok));
        Rawl2.fcs_args.bytes = frame;
        Rawl2.fcs_args.len = flen;
        DBENCH_BULK("Rawl2.fcs (CRC-32)", 50000, flen, sink += (Rawl2.fcs(rawl2_work), Rawl2.u32));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("rawl2")
