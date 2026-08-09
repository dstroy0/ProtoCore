// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SCPI codec (services/instrumentation/scpi): the command builder, the
// real-number formatter, the number parser, and the header pattern matcher (SCPI short/long-form
// matching) - all pure string logic (no instrument link), the hot path a SCPI-over-LAN endpoint
// runs per command.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/scpi -t upload --upload-port COM7
#include "device_bench.h"
#include "services/instrumentation/scpi/scpi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    static const char *args2[] = {"1", "MAX"};

    for (;;)
    {
        DBENCH_BANNER("scpi");
        volatile size_t sink = 0;
        static char buf[64];
        DBENCH_OP("pc_scpi_build (2 args)", 200000, sink += pc_scpi_build(buf, sizeof(buf), "SOUR:VOLT", args2, 2));
        DBENCH_OP("pc_scpi_fmt_real", 200000, sink += pc_scpi_fmt_real(buf, sizeof(buf), 3.14159265));
        double d = 0;
        DBENCH_OP("pc_scpi_parse_number", 200000, sink += pc_scpi_parse_number("1.2345E+3", 9, &d));
        DBENCH_OP("pc_scpi_match (short/long)", 200000, sink += pc_scpi_match("SOUR:VOLT", 9, "SOURce:VOLTage"));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("scpi")
