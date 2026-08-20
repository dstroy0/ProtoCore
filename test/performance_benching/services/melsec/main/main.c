// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Mitsubishi MELSEC MC binary 3E codec (services/fieldbus/melsec):
// Melsec.build_read emits a QnA-compatible batch-read request (little-endian subheader 0x5000,
// command 0x0401, 24-bit head device + device code) into a caller buffer, and Melsec.parse_response
// validates a 0xD000 response subheader/length/end-code and returns a view onto the read payload - both
// pure (no sockets, no heap). Like performance_benching/device/modbus, this is a pure protocol codec with no hardware
// involved, so every call here exercises the real production code path; the TCP/UDP send half is the
// application's and is deliberately out of scope on this peripheral-less rig.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/melsec -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/melsec/melsec.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t melsec_work[16]; // the borrow an entry takes; Melsec never reads it

void dbench_run(void)
{
    static uint8_t req[32];

    // Known-good 0xD000 response (5 word values), copied verbatim from test/test_melsec (test_parse_response_ok):
    // subheader D0 00, routing, data length = 12 (end code + 10 data), end code 0x0000 success, 5 LE words.
    static const uint8_t resp_ok[] = {
        0xD0, 0x00,                                                // subheader (response)
        0x00, 0xFF, 0xFF, 0x03, 0x00,                              // routing
        0x0C, 0x00,                                                // data length = 12 (end code + 10 data)
        0x00, 0x00,                                                // end code = success
        0x11, 0x11, 0x22, 0x22, 0x33, 0x33, 0x44, 0x44, 0x55, 0x55 // 5 word values
    };

    for (;;)
    {
        DBENCH_BANNER("melsec");
        volatile size_t sink = 0;
        volatile bool sinkb = false;
        MelsecResponse parsed;

        // Batch-read request build: read 5 words of D100 (data register), monitoring timer 0x0010 -
        // the exact args from test_melsec's test_build_read_bytes. Cheap pure builder -> large N.
        Melsec.build_read_args.buf = req;
        Melsec.build_read_args.cap = sizeof(req);
        Melsec.build_read_args.device_code = MELSEC_DEV_D;
        Melsec.build_read_args.head_device = 100;
        Melsec.build_read_args.points = 5;
        Melsec.build_read_args.monitoring_timer = 0x0010;
        DBENCH_OP("Melsec.build_read D100 x5", 200000,
                  sink += (Melsec.build_read(melsec_work), Melsec.n));

        // Response parse + validate over the known-good 0xD000 frame. Cheap pure parser -> large N.
        Melsec.parse_response_args.buf = resp_ok;
        Melsec.parse_response_args.len = sizeof(resp_ok);
        Melsec.parse_response_args.out = &parsed;
        DBENCH_OP("Melsec.parse_response ok", 200000,
                  sinkb ^= (Melsec.parse_response(melsec_work), Melsec.ok));

        (void)sink;
        (void)sinkb;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("melsec")
