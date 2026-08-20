// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the BACnet/IP BVLC + NPDU codec (services/fieldbus/bacnet):
// Bacnet.bvlc_build/Bacnet.bvlc_parse frame and slice the Annex J BVLL envelope (type + function +
// big-endian length), and Bacnet.npdu_build/Bacnet.npdu_parse frame and slice the Clause 6 NPDU
// header (version + NPCI control, with optional DNET/DADR/hop-count addressing) around an APDU -
// all pure (no sockets, no heap). Worked example for performance_benching/device/<service>/: a pure protocol codec
// with no hardware involved, so every call here exercises the real production code path (contrast
// with performance_benching/device/ads1115, a peripheral driver where the bus transaction itself is stubbed).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/bacnet -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/bacnet/bacnet.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t bacnet_work[16]; // the borrow an entry takes; Bacnet never reads it

void dbench_run(void)
{
    // Sample bytes lifted straight from test/test_bacnet/test_bacnet.cpp (known-good, spec-conformant).
    static const uint8_t npdu_local[] = {0x01, 0x00, 0xAA}; // version, control(0), 1-byte NSDU tail
    static const uint8_t apdu_local[] = {0x41, 0x42, 0x43};
    static const uint8_t dadr_routed[] = {0x0A};
    static const uint8_t apdu_routed[] = {0x10};
    // A full dest+source NPDU frame (parser exercises both address slices + hop count skip).
    static const uint8_t npdu_full[] = {
        0x01, 0x28,             // version, control: dest + source present
        0x00, 0x05, 0x01, 0x0A, // DNET 5, DLEN 1, DADR 0A
        0x00, 0x03, 0x01, 0x0B, // SNET 3, SLEN 1, SADR 0B
        0xFF,                   // hop count
        0x30, 0x31              // apdu
    };

    static uint8_t bvlc_buf[32];
    static uint8_t npdu_buf[32];

    for (;;)
    {
        DBENCH_BANNER("bacnet");
        volatile size_t sinkz = 0;
        volatile bool sinkb = false;

        BacnetV.bvlc_build_args.buf = bvlc_buf;
        BacnetV.bvlc_build_args.cap = sizeof(bvlc_buf);
        BacnetV.bvlc_build_args.function = BVLC_FUNC_ORIGINAL_UNICAST;
        BacnetV.bvlc_build_args.npdu = npdu_local;
        BacnetV.bvlc_build_args.npdu_len = sizeof(npdu_local);
        DBENCH_OP("Bacnet.bvlc_build", 50000, sinkz += (Bacnet.bvlc_build(bacnet_work), BacnetV.n));

        {
            uint8_t func;
            const uint8_t *p;
            size_t plen;
            BacnetV.bvlc_parse_args.buf = bvlc_buf;
            BacnetV.bvlc_parse_args.len = sizeof(bvlc_buf);
            BacnetV.bvlc_parse_args.function = &func;
            BacnetV.bvlc_parse_args.npdu = &p;
            BacnetV.bvlc_parse_args.npdu_len = &plen;
            DBENCH_OP("Bacnet.bvlc_parse", 50000, sinkb = (Bacnet.bvlc_parse(bacnet_work), BacnetV.ok));
        }

        BacnetV.npdu_build_args.buf = npdu_buf;
        BacnetV.npdu_build_args.cap = sizeof(npdu_buf);
        BacnetV.npdu_build_args.expecting_reply = false;
        BacnetV.npdu_build_args.priority = NPDU_PRIO_NORMAL;
        BacnetV.npdu_build_args.has_dest = false;
        BacnetV.npdu_build_args.dnet = 0;
        BacnetV.npdu_build_args.dadr = NULL;
        BacnetV.npdu_build_args.dadr_len = 0;
        BacnetV.npdu_build_args.hop_count = 0;
        BacnetV.npdu_build_args.apdu = apdu_local;
        BacnetV.npdu_build_args.apdu_len = sizeof(apdu_local);
        DBENCH_OP("Bacnet.npdu_build local", 50000, sinkz += (Bacnet.npdu_build(bacnet_work), BacnetV.n));

        BacnetV.npdu_build_args.buf = npdu_buf;
        BacnetV.npdu_build_args.cap = sizeof(npdu_buf);
        BacnetV.npdu_build_args.expecting_reply = true;
        BacnetV.npdu_build_args.priority = NPDU_PRIO_NORMAL;
        BacnetV.npdu_build_args.has_dest = true;
        BacnetV.npdu_build_args.dnet = 0x0005;
        BacnetV.npdu_build_args.dadr = dadr_routed;
        BacnetV.npdu_build_args.dadr_len = sizeof(dadr_routed);
        BacnetV.npdu_build_args.hop_count = 0xFF;
        BacnetV.npdu_build_args.apdu = apdu_routed;
        BacnetV.npdu_build_args.apdu_len = sizeof(apdu_routed);
        DBENCH_OP("Bacnet.npdu_build routed", 50000, sinkz += (Bacnet.npdu_build(bacnet_work), BacnetV.n));

        {
            NpduInfo info;
            BacnetV.npdu_parse_args.buf = npdu_full;
            BacnetV.npdu_parse_args.len = sizeof(npdu_full);
            BacnetV.npdu_parse_args.out = &info;
            DBENCH_OP("Bacnet.npdu_parse dest+src", 50000, sinkb = (Bacnet.npdu_parse(bacnet_work), BacnetV.ok));
        }

        (void)sinkz;
        (void)sinkb;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("bacnet")
