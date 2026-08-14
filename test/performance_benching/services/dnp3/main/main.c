// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the DNP3 (IEEE 1815) data-link frame codec (services/energy/dnp3):
// CRC-16/DNP, the frame builder (0x0564 header + CRC'd 16-octet data blocks), and the
// CRC-validating, de-blocking parser - all pure (no sockets, no heap). Worked example for
// performance_benching/device/<service>/: a pure protocol codec with no hardware involved, so every call here
// exercises the real production code path (contrast with performance_benching/device/ads1115, a peripheral driver
// where the bus transaction itself is stubbed). Both a small (5-octet, single-block) and a
// maximum-size (250-octet, 16-block) user-data payload are benched so the per-block CRC/copy
// overhead scales visibly with frame size.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/dnp3 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/energy/dnp3/dnp3.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t s_user_large[DNP3_MAX_USER_DATA]; // 250 octets - the max single-frame payload
static uint8_t s_frame_small[32];
static size_t s_frame_small_len;
static uint8_t s_frame_large[300];
static size_t s_frame_large_len;

void dbench_run(void)
{
    // An app-layer fragment, small enough to fit one 16-octet data block (from test_dnp3.cpp).
    static const uint8_t user_small[] = {0xC0, 0x01, 0x3C, 0x02, 0x06};

    for (size_t i = 0; i < sizeof(s_user_large); i++)
    {
        s_user_large[i] = (uint8_t)(i + 1);
    }

    s_frame_small_len = protocore_dnp3_build_frame(s_frame_small, sizeof(s_frame_small), 0x44, 0x1234, 0x0A0B,
                                                   user_small, sizeof(user_small));
    s_frame_large_len = protocore_dnp3_build_frame(s_frame_large, sizeof(s_frame_large), 0x44, 0x1234, 0x0A0B,
                                                   s_user_large, sizeof(s_user_large));

    static uint8_t out_small[64];
    static uint8_t out_large[DNP3_MAX_USER_DATA];
    static uint8_t build_scratch_small[32];
    static uint8_t build_scratch_large[300];

    for (;;)
    {
        DBENCH_BANNER("dnp3");
        volatile uint16_t sink16 = 0;
        volatile size_t sink = 0;
        Dnp3Frame f;
        size_t user_len;

        DBENCH_BULK("protocore_dnp3_crc (250B)", 2000, sizeof(s_user_large),
                    sink16 += protocore_dnp3_crc(s_user_large, sizeof(s_user_large)));
        DBENCH_OP("protocore_dnp3_build_frame (5B)", 20000,
                  sink += protocore_dnp3_build_frame(build_scratch_small, sizeof(build_scratch_small), 0x44, 0x1234,
                                                     0x0A0B, user_small, sizeof(user_small)));
        DBENCH_OP("protocore_dnp3_build_frame (250B)", 2000,
                  sink += protocore_dnp3_build_frame(build_scratch_large, sizeof(build_scratch_large), 0x44, 0x1234,
                                                     0x0A0B, s_user_large, sizeof(s_user_large)));
        DBENCH_OP("protocore_dnp3_parse_frame (5B)", 20000,
                  sink += protocore_dnp3_parse_frame(s_frame_small, s_frame_small_len, &f, out_small, sizeof(out_small),
                                                     &user_len));
        DBENCH_OP("protocore_dnp3_parse_frame (250B)", 2000,
                  sink += protocore_dnp3_parse_frame(s_frame_large, s_frame_large_len, &f, out_large, sizeof(out_large),
                                                     &user_len));
        (void)sink16;
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("dnp3")
