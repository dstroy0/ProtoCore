// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the no-stdlib number parsers, reached through the str
// namespace (cellularum_laboro/cellularum_laboro.h): cellul.to_long / cellul.to_ulong / cellul.to_float. These are the
// hot ops behind every header value, query parameter and JSON number the server decodes.
//
// Build/flash: idf.py -C test/performance_benching/core/numparse flash monitor
#include "cellularum_laboro/cellularum_laboro.h"
#include "device_bench.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    for (;;)
    {
        DBENCH_BANNER("numparse");
        volatile long sink = 0;
        const char *e;
        DBENCH_OP("str.to_long (-1234567)", 200000,
                  sink += EMBED_CALL(cellul.to_long, TransfiguroCfg, .src = "-1234567", .end = &e));
        DBENCH_OP("str.to_ulong (4000000000)", 200000,
                  sink += (long)EMBED_CALL(cellul.to_ulong, TransfiguroCfg, .src = "4000000000", .end = &e));
        DBENCH_OP("str.to_float (3.14159265)", 200000,
                  sink += (long)(EMBED_CALL(cellul.to_float, TransfiguroCfg, .src = "3.14159265", .end = &e) * 100));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("numparse")
