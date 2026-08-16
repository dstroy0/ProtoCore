// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SSE codec (network_drivers/presentation/http/sse):
// protocore_sse_format() builds one `event:/id:/data:` frame into a caller buffer - the per-event hot op.
// Pure; the protocore_conn_send() wrapper (protocore_sse_write) is out of scope. Build/flash: pio run -d
// performance_benching/network_drivers/presentation/sse -t upload
#include "device_bench.h"
#include "network_drivers/presentation/http/sse/sse.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    for (;;)
    {
        DBENCH_BANNER("sse");
        volatile int sink = 0;
        static char buf[256];
        Sse.out.buf = buf;
        Sse.out.cap = sizeof(buf);
        Sse.event_args.data = "{\"temp\":21.4,\"rh\":48}";
        Sse.event_args.event = "telemetry";
        Sse.event_args.event_id = "42";
        DBENCH_OP("Sse.format (data+event+id)", 200000, Sse.format(protocore_sse_span()); sink += Sse.n);
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("sse")
