// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the BACnet/IP codec (ASHRAE 135): the BVLC envelope (Annex J) + the NPDU
// network layer (Clause 6) - build + validate/slice. Pure (no socket), so it links standalone. The device
// figure comes from the rig /bench op; this host ns/op + MB/s is a relative baseline. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPC_ENABLE_BACNET=1 test/performance_benching/services/bacnet/host.c
//   src/services/fieldbus/bacnet/bacnet.c src/mmgr/protomem.c -o /tmp/bb && /tmp/bb

#define PC_ENABLE_BACNET 1
#include "services/fieldbus/bacnet/bacnet.h"

#include "host_bench.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

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
    size_t npdu_len =
        pc_npdu_build(npdu, sizeof(npdu), true, NPDU_PRIO_NORMAL, true, 100, dadr, 2, 255, apdu, sizeof(apdu));

    uint8_t frame[128];
    size_t frame_len = pc_bvlc_build(frame, sizeof(frame), BVLC_FUNC_ORIGINAL_UNICAST, npdu, npdu_len);

    hbench_header();

    // pc_bvlc_parse: validate the Annex-J envelope + slice out the NPDU - the first op on every datagram.
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            10000000,
            {
                uint8_t fn = 0;
                const uint8_t *np = NULL;
                size_t nl = 0;
                if (pc_bvlc_parse(frame, frame_len, &fn, &np, &nl))
                {
                    sink += nl;
                }
            },
            ns);
        hbench_row("bacnet", "pc_bvlc_parse", ns, (double)frame_len);
        (void)sink;
    }

    // pc_npdu_parse: validate version/control + walk the optional addressing + slice the APDU - the receive op.
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            10000000,
            {
                NpduInfo info;
                if (pc_npdu_parse(npdu, npdu_len, &info))
                {
                    sink += info.apdu_len;
                }
            },
            ns);
        hbench_row("bacnet", "pc_npdu_parse", ns, (double)npdu_len);
        (void)sink;
    }

    // pc_npdu_build: frame an APDU with destination addressing + hop count - the transmit op.
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(5000000,
                  sink += pc_npdu_build(npdu, sizeof(npdu), true, NPDU_PRIO_NORMAL, true, 100, dadr, 2, 255, apdu,
                                        sizeof(apdu)),
                  ns);
        hbench_row("bacnet", "pc_npdu_build", ns, (double)npdu_len);
        (void)sink;
    }

    return 0;
}
