// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the IEEE 1609 WAVE codec (services/transportation/wave): the P-encoded PSID
// encode/decode and the WSMP (WAVE Short Message Protocol) build/parse. Pure; no radio.
//
// Build/flash:  idf.py -C test/performance_benching/wave -t upload --upload-port COM7
#include "device_bench.h"
#include "services/transportation/wave/wave.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    static const uint8_t payload[32] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB,
                                        0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                                        0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    static uint8_t wsmp[64];
    size_t wlen = pc_wsmp_build(0x20, payload, sizeof(payload), wsmp, sizeof(wsmp));

    for (;;)
    {
        DBENCH_BANNER("wave");
        volatile size_t sink = 0;
        static uint8_t out[64];
        DBENCH_OP("pc_wave_encode_psid", 200000, sink += pc_wave_encode_psid(0x20, out, sizeof(out)));
        uint32_t psid;
        DBENCH_OP("pc_wave_decode_psid", 200000, sink += pc_wave_decode_psid(out, 4, &psid));
        DBENCH_OP("pc_wsmp_build (32B)", 200000,
                  sink += pc_wsmp_build(0x20, payload, sizeof(payload), out, sizeof(out)));
        WsmpFrame wf;
        DBENCH_OP("pc_wsmp_parse", 200000, sink += pc_wsmp_parse(wsmp, wlen, &wf));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("wave")
