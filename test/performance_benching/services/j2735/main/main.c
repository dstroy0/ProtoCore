// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SAE J2735 V2X codec (services/transportation/j2735): the ASN.1 UPER
// (Unaligned Packed Encoding Rules) bit-level encode/decode of the three high-rate V2X blocks a
// roadside unit / vehicle actually runs - the BSMcore safety kernel (msgCnt/id/secMark/lat/long/
// elev/speed/heading, 162 bits), the SPaT MovementState list (signal-group phases + countdown
// timers), and the MAP intersection geometry (id + lane offsets). All pure, zero heap, no stdlib.
// Worked example for performance_benching/device/<service>/: a pure protocol codec with no hardware involved, so
// every call here exercises the real production code path. The DSRC / C-V2X radio (the module that
// would put these octets on the air) is an external transceiver and is deliberately out of scope -
// this rig has no radio attached and only the deterministic CPU-side message codec is ever benched.
//
// Sample data (NYC BSM position, a 3-state SPaT, a 3-lane MAP) is copied straight out of the
// known-good, spec-conformant vectors in test/test_j2735/test_j2735.cpp.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/j2735 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/transportation/j2735/j2735.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // --- BSMcore: the NYC position sample from test_bsm_core_roundtrip. ---
    J2735BsmCore bsm;
    bsm.msg_count = 12;
    bsm.id = 0xDEADBEEF;
    bsm.sec_mark = 34000;
    bsm.lat = 407127370;  // ~40.7127370 N
    bsm.lon = -740059730; // ~-74.0059730 W
    bsm.elev = 100;
    bsm.speed = 500;    // 10 m/s
    bsm.heading = 7200; // 90 deg

    // --- SPaT: the 3-state MovementState list from test_spat_roundtrip. ---
    J2735MovementState spat[3];
    spat[0] = (J2735MovementState){1, (uint8_t)J2735_PHASE_PROTECTED_MOVEMENT_ALLOWED, 100, 250};
    spat[1] = (J2735MovementState){2, (uint8_t)J2735_PHASE_STOP_AND_REMAIN, 0, 36000};
    spat[2] = (J2735MovementState){17, (uint8_t)J2735_PHASE_PERMISSIVE_CLEARANCE, 300, 320};

    // --- MAP: the 3-lane intersection from test_map_roundtrip. ---
    J2735MapIntersection isect = {12345, 40000, 55000};
    J2735Lane lanes[3];
    lanes[0] = (J2735Lane){1, true, -100, 200};
    lanes[1] = (J2735Lane){2, false, 2047, -2048};
    lanes[2] = (J2735Lane){9, true, 0, 0};

    // Pre-encode each block once so the decode benches read a real, spec-conformant octet stream.
    static uint8_t bsm_buf[64];
    static uint8_t spat_buf[64];
    static uint8_t map_buf[64];
    size_t bsm_n = protocore_j2735_bsm_core_encode(&bsm, bsm_buf, sizeof(bsm_buf));
    size_t spat_n = protocore_j2735_spat_encode(spat, 3, spat_buf, sizeof(spat_buf));
    size_t map_n = protocore_j2735_map_encode(&isect, lanes, 3, map_buf, sizeof(map_buf));

    static uint8_t out[64];

    for (;;)
    {
        DBENCH_BANNER("j2735");
        volatile size_t sink = 0;
        volatile bool bsink = false;

        // BSMcore encode/decode - the high-rate (10 Hz) safety kernel every BSM carries.
        DBENCH_OP("protocore_j2735_bsm_core_encode", 50000, sink += protocore_j2735_bsm_core_encode(&bsm, out, sizeof(out)));
        {
            J2735BsmCore d;
            DBENCH_OP("protocore_j2735_bsm_core_decode", 50000, bsink ^= protocore_j2735_bsm_core_decode(bsm_buf, bsm_n, &d));
        }

        // SPaT MovementState list (3 states) encode/decode.
        DBENCH_OP("protocore_j2735_spat_encode x3", 30000, sink += protocore_j2735_spat_encode(spat, 3, out, sizeof(out)));
        {
            J2735MovementState so[8];
            size_t sc = 0;
            DBENCH_OP("protocore_j2735_spat_decode x3", 30000, bsink ^= protocore_j2735_spat_decode(spat_buf, spat_n, so, 8, &sc));
        }

        // MAP intersection geometry (3 lanes) encode/decode.
        DBENCH_OP("protocore_j2735_map_encode x3", 30000, sink += protocore_j2735_map_encode(&isect, lanes, 3, out, sizeof(out)));
        {
            J2735MapIntersection di;
            J2735Lane lo[8];
            size_t lc = 0;
            DBENCH_OP("protocore_j2735_map_decode x3", 30000, bsink ^= protocore_j2735_map_decode(map_buf, map_n, &di, lo, 8, &lc));
        }

        (void)sink;
        (void)bsink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("j2735")
