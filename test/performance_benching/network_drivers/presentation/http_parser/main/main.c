// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the HTTP/1.1 request parser (network_drivers/presentation/
// http_parser): feed a whole request through the per-byte state machine (the real ingest hot path).
// Pure. Build/flash: pio run -d performance_benching/network_drivers/presentation/http_parser -t upload
#include "device_bench.h"
#include "network_drivers/presentation/http/http_parser/http_parser.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const char *GET_REQ = "GET /api/v1/status?verbose=1 HTTP/1.1\r\n"
                             "Host: pc.local\r\nUser-Agent: curl/8.5\r\nAccept: */*\r\n"
                             "Connection: keep-alive\r\nAccept-Encoding: gzip\r\nX-Trace: abc123\r\n\r\n";

void dbench_run(void)
{
    static HttpReq req;
    const size_t n = strlen(GET_REQ);
    for (;;)
    {
        DBENCH_BANNER("http_parser");
        volatile int sink = 0;
        DBENCH_BULK("http_parser feed GET (6 hdrs)", 50000, n, {
            http_parser_reset(&req);
            for (size_t i = 0; i < n; i++)
            {
                http_parser_feed(&req, (uint8_t)GET_REQ[i]);
            }
            sink += (int)req.parse_state;
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("http_parser")
