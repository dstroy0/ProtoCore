// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the route regex matcher (server/regex): regex_match() tests a
// request path against a route pattern - the per-route hot op during dispatch. Pure.
// Build/flash: pio run -d performance_benching/server/regex -t upload
#include "device_bench.h"
#include "protocore.h" // regex_match declaration

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    for (;;)
    {
        DBENCH_BANNER("regex");
        volatile int sink = 0;
        DBENCH_OP("regex_match (hit)", 200000, sink += regex_match("/api/v1/[0-9]+/status", "/api/v1/42/status"));
        DBENCH_OP("regex_match (miss)", 200000, sink += regex_match("/api/v1/[0-9]+/status", "/api/v1/xx/status"));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("regex")
