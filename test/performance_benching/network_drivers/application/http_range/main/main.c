// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the HTTP byte-range parser (server/http_range):
// http_parse_byte_range() decodes a `Range: bytes=...` header against a known resource size - the
// per-request hot op for 206 Partial Content. Pure. Build/flash: pio run -d
// performance_benching/network_drivers/application/http_range -t upload
#include "device_bench.h"
#include "network_drivers/application/http_range/http_range.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t http_range_work[16]; // the borrow an entry takes; HttpRange never reads it

void dbench_run(void)
{
    for (;;)
    {
        DBENCH_BANNER("http_range");
        volatile int sink = 0;
        size_t start, end;
        DBENCH_OP("http_parse_byte_range (500-999)", 200000,
                  sink += http_parse_byte_range("bytes=500-999", 65536, &start, &end));
        DBENCH_OP("http_parse_byte_range (suffix)", 200000,
                  sink += http_parse_byte_range("bytes=-500", 65536, &start, &end));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("http_range")
