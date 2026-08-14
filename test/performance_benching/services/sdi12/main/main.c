// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SDI-12 sensor-bus codec (server/peripherals/sdi12): the command
// builders, the measurement-response parser, the data-value parser, and the SDI-12 CRC-16. All
// pure ASCII/codec logic - the 1200-baud UART line handling is real-hardware and out of scope; only
// the deterministic per-message CPU path is benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/sdi12 -t upload --upload-port COM7
#include "device_bench.h"
#include "server/peripherals/sdi12/sdi12.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // aM! response: "0" + "012" (12 s) + "2" (2 values); aD0! values response (from test/test_sdi12).
    static const char measure_resp[] = "00122\r\n";
    static const char values_resp[] = "0+3.14-2.72\r\n";
    static const uint8_t crcbuf[16] = {'0', '+', '3', '.', '1', '4', '-', '2', '.', '7', '2', 0, 0, 0, 0, 0};

    for (;;)
    {
        DBENCH_BANNER("sdi12");
        volatile size_t sink = 0;
        static char buf[32];
        DBENCH_OP("protocore_sdi12_build_measure (CRC)", 200000,
                  sink += protocore_sdi12_build_measure(buf, sizeof(buf), '3', true));
        char addr;
        uint16_t ready;
        uint8_t nval;
        DBENCH_OP("protocore_sdi12_parse_measure", 200000,
                  sink += protocore_sdi12_parse_measure(measure_resp, sizeof(measure_resp) - 1, &addr, &ready, &nval));
        float vals[8];
        size_t n;
        DBENCH_OP("protocore_sdi12_parse_values", 200000,
                  sink += protocore_sdi12_parse_values(values_resp, sizeof(values_resp) - 1, vals, 8, &n));
        DBENCH_BULK("protocore_sdi12_crc16", 200000, 11, sink += protocore_sdi12_crc16(crcbuf, 11));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("sdi12")
