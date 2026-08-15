// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for services/upload_service. NOTE: upload_service is a thin
// *server + filesystem binding* - protocore_upload_begin() registers a route that streams an HTTP multipart
// upload straight to a file. The actual per-byte parsing work is done by the multipart presentation
// codec (benched at performance_benching/host_microbench / the multipart path) and the streaming body hook; this
// service adds only the route wiring and a byte counter. There is no standalone pure hot-path to
// time, so this sketch benches only the last-size getter, for suite completeness (not a throughput
// number).
//
// Build/flash:  idf.py -C test/performance_benching/upload_service -t upload --upload-port COM7
#include "device_bench.h"
#include "network_drivers/application/upload_service/upload_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    for (;;)
    {
        DBENCH_BANNER("upload_service");
        volatile size_t sink = 0;
        DBENCH_OP("protocore_upload_last_size (getter)", 200000, sink += protocore_upload_last_size());
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("upload_service")
