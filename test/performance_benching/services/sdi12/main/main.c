// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

static uint8_t sdi12_work[16]; // the borrow an entry takes; Sdi12 never reads it

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
        // Every entry call stays inside DBENCH_OP so the timed loop measures the codec, not the
        // read that follows it. The args do not vary across iterations, so they are staged once.
        Sdi12V.build_measure_args.buf = buf;
        Sdi12V.build_measure_args.cap = sizeof(buf);
        Sdi12V.build_measure_args.addr = '3';
        Sdi12V.build_measure_args.with_crc = PROTO_TRUE;
        DBENCH_OP("Sdi12.build_measure (CRC)", 200000, (Sdi12.build_measure(sdi12_work), sink += Sdi12V.n));
        char addr;
        uint16_t ready;
        uint8_t nval;
        Sdi12V.parse_measure_args.resp = measure_resp;
        Sdi12V.parse_measure_args.len = sizeof(measure_resp) - 1;
        Sdi12V.parse_measure_args.addr = &addr;
        Sdi12V.parse_measure_args.ready_sec = &ready;
        Sdi12V.parse_measure_args.num_values = &nval;
        DBENCH_OP("Sdi12.parse_measure", 200000, (Sdi12.parse_measure(sdi12_work), sink += Sdi12V.ok));
        float vals[8];
        size_t n;
        Sdi12V.parse_values_args.resp = values_resp;
        Sdi12V.parse_values_args.len = sizeof(values_resp) - 1;
        Sdi12V.parse_values_args.out = vals;
        Sdi12V.parse_values_args.max = 8;
        Sdi12V.parse_values_args.n = &n;
        DBENCH_OP("Sdi12.parse_values", 200000, (Sdi12.parse_values(sdi12_work), sink += Sdi12V.ok));
        Sdi12V.crc16_args.data = crcbuf;
        Sdi12V.crc16_args.len = 11;
        DBENCH_BULK("Sdi12.crc16", 200000, 11, (Sdi12.crc16(sdi12_work), sink += Sdi12V.crc));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("sdi12")
