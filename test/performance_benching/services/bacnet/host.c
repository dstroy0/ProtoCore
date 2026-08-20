// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the BACnet/IP codec (ASHRAE 135): the BVLC envelope (Annex J) + the NPDU
// network layer (Clause 6) - build + validate/slice. Pure (no socket), so it links standalone. The device
// figure comes from the rig /bench op; this host ns/op + MB/s is a relative baseline. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_BACNET=1 test/performance_benching/services/bacnet/host.c
//   src/services/fieldbus/bacnet/bacnet.c src/mmgr/protomem.c -o /tmp/bb && /tmp/bb

#define PROTOCORE_ENABLE_BACNET 1
#include "services/fieldbus/bacnet/bacnet.h"

#include "host_bench.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static uint8_t bacnet_work[16]; // the borrow an entry takes; Bacnet never reads it

int main(void)
{
    // An 8-octet APDU wrapped in an NPDU (with destination addressing + hop count) wrapped in a BVLC
    // Original-Unicast envelope - a realistic received BACnet/IP datagram.
    uint8_t apdu[8];
    for (int i = 0; i < 8; i++)
    {
        apdu[i] = (uint8_t)(i * 9 + 2);
    }
    const uint8_t dadr[2] = {0x01, 0x02};

    uint8_t npdu[64];
    BacnetV.npdu_build_args.buf = npdu;
    BacnetV.npdu_build_args.cap = sizeof(npdu);
    BacnetV.npdu_build_args.expecting_reply = true;
    BacnetV.npdu_build_args.priority = NPDU_PRIO_NORMAL;
    BacnetV.npdu_build_args.has_dest = true;
    BacnetV.npdu_build_args.dnet = 100;
    BacnetV.npdu_build_args.dadr = dadr;
    BacnetV.npdu_build_args.dadr_len = 2;
    BacnetV.npdu_build_args.hop_count = 255;
    BacnetV.npdu_build_args.apdu = apdu;
    BacnetV.npdu_build_args.apdu_len = sizeof(apdu);
    Bacnet.npdu_build(bacnet_work);
    size_t npdu_len = BacnetV.n;

    uint8_t frame[128];
    BacnetV.bvlc_build_args.buf = frame;
    BacnetV.bvlc_build_args.cap = sizeof(frame);
    BacnetV.bvlc_build_args.function = BVLC_FUNC_ORIGINAL_UNICAST;
    BacnetV.bvlc_build_args.npdu = npdu;
    BacnetV.bvlc_build_args.npdu_len = npdu_len;
    Bacnet.bvlc_build(bacnet_work);
    size_t frame_len = BacnetV.n;

    hbench_header();

    // Bacnet.bvlc_parse: validate the Annex-J envelope + slice out the NPDU - the first op on every datagram.
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            10000000,
            {
                uint8_t fn = 0;
                const uint8_t *np = NULL;
                size_t nl = 0;
                BacnetV.bvlc_parse_args.buf = frame;
                BacnetV.bvlc_parse_args.len = frame_len;
                BacnetV.bvlc_parse_args.function = &fn;
                BacnetV.bvlc_parse_args.npdu = &np;
                BacnetV.bvlc_parse_args.npdu_len = &nl;
                Bacnet.bvlc_parse(bacnet_work);
                if (BacnetV.ok)
                {
                    sink += nl;
                }
            },
            ns);
        hbench_row("bacnet", "Bacnet.bvlc_parse", ns, (double)frame_len);
        (void)sink;
    }

    // Bacnet.npdu_parse: validate version/control + walk the optional addressing + slice the APDU - the receive op.
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            10000000,
            {
                NpduInfo info;
                BacnetV.npdu_parse_args.buf = npdu;
                BacnetV.npdu_parse_args.len = npdu_len;
                BacnetV.npdu_parse_args.out = &info;
                Bacnet.npdu_parse(bacnet_work);
                if (BacnetV.ok)
                {
                    sink += info.apdu_len;
                }
            },
            ns);
        hbench_row("bacnet", "Bacnet.npdu_parse", ns, (double)npdu_len);
        (void)sink;
    }

    // Bacnet.npdu_build: frame an APDU with destination addressing + hop count - the transmit op.
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        BacnetV.npdu_build_args.buf = npdu;
        BacnetV.npdu_build_args.cap = sizeof(npdu);
        BacnetV.npdu_build_args.expecting_reply = true;
        BacnetV.npdu_build_args.priority = NPDU_PRIO_NORMAL;
        BacnetV.npdu_build_args.has_dest = true;
        BacnetV.npdu_build_args.dnet = 100;
        BacnetV.npdu_build_args.dadr = dadr;
        BacnetV.npdu_build_args.dadr_len = 2;
        BacnetV.npdu_build_args.hop_count = 255;
        BacnetV.npdu_build_args.apdu = apdu;
        BacnetV.npdu_build_args.apdu_len = sizeof(apdu);
        Bacnet.npdu_build(bacnet_work);
        HBENCH_NS(5000000, sink += BacnetV.n, ns);
        hbench_row("bacnet", "Bacnet.npdu_build", ns, (double)npdu_len);
        (void)sink;
    }

    return 0;
}
