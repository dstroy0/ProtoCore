// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Ethernet POWERLINK basic frame codec (services/fieldbus/powerlink):
// building the four cyclic EPL messages - SoC / PReq / PRes via protocore_epl_soc/preq/pres (each a thin
// wrapper over protocore_epl_build, which lays down [messageType][dest][source][payload...]) - and parsing a
// received frame back into an EplFrame with protocore_epl_parse. All pure, zero heap, no stdlib: like
// services/fieldbus/modbus this is a pure protocol codec, so every call here exercises the real production code
// path. Deliberately out of scope: the raw-L2 (ethertype 0x88AB) transmit and the isochronous MN cycle
// timing (the preempting-task model) - those are the transport/scheduling half, not the frame codec, and
// this rig drives no network, so nothing hardware/socket-facing is touched or stubbed.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/powerlink -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/powerlink/powerlink.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t powerlink_work[16]; // the borrow an entry takes; Powerlink never reads it

void dbench_run(void)
{
    // Realistic 4-byte output/input process image (PDO), matching test/test_powerlink's roundtrip vector.
    static const uint8_t pdo[4] = {0x11, 0x22, 0x33, 0x44};
    static uint8_t out[64];

    // A pre-built PReq (MN 240 -> CN 5, 4-byte PDO) to feed the parse bench: [PREQ][dest][src][payload].
    static uint8_t preq_frame[16];
    Powerlink.preq_args.dest_cn = 5;
    Powerlink.preq_args.source = EPL_NODE_MN;
    Powerlink.preq_args.pdo = pdo;
    Powerlink.preq_args.pdo_len = sizeof(pdo);
    Powerlink.preq_args.out = preq_frame;
    Powerlink.preq_args.cap = sizeof(preq_frame);
    Powerlink.preq(powerlink_work);
    size_t preq_len = Powerlink.n;

    for (;;)
    {
        DBENCH_BANNER("powerlink");
        volatile size_t sink = 0;
        volatile bool bsink = false;

        // SoC: MN -> broadcast, no payload (start of the isochronous cycle).
        Powerlink.soc_args.source = EPL_NODE_MN;
        Powerlink.soc_args.out = out;
        Powerlink.soc_args.cap = sizeof(out);
        DBENCH_OP("Powerlink.soc", 200000, sink += (Powerlink.soc(powerlink_work), Powerlink.n));
        // PReq: MN -> CN 5 carrying the 4-byte output PDO.
        Powerlink.preq_args.dest_cn = 5;
        Powerlink.preq_args.source = EPL_NODE_MN;
        Powerlink.preq_args.pdo = pdo;
        Powerlink.preq_args.pdo_len = sizeof(pdo);
        Powerlink.preq_args.out = out;
        Powerlink.preq_args.cap = sizeof(out);
        DBENCH_OP("Powerlink.preq x4B", 200000,
                  sink += (Powerlink.preq(powerlink_work), Powerlink.n));
        // PRes: CN 5 -> broadcast carrying its 4-byte input PDO.
        Powerlink.pres_args.source_cn = 5;
        Powerlink.pres_args.pdo = pdo;
        Powerlink.pres_args.pdo_len = sizeof(pdo);
        Powerlink.pres_args.out = out;
        Powerlink.pres_args.cap = sizeof(out);
        DBENCH_OP("Powerlink.pres x4B", 200000, sink += (Powerlink.pres(powerlink_work), Powerlink.n));
        // Parse the pre-built PReq back into an EplFrame.
        EplFrame f;
        Powerlink.parse_args.frame = preq_frame;
        Powerlink.parse_args.len = preq_len;
        Powerlink.parse_args.out = &f;
        DBENCH_OP("Powerlink.parse preq", 200000, bsink |= (Powerlink.parse(powerlink_work), Powerlink.ok));

        (void)sink;
        (void)bsink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("powerlink")
