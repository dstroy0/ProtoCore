// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the MTConnect agent response codec (services/machine_tool/mtconnect): building a
// streams document of 20 observations. A deterministic ns/op + MB/s baseline complementing the
// on-device ESP32-S3 number; the host figure is a relative baseline, not the device cost. Build:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/performance_benching/common -DPC_ENABLE_MTCONNECT=1
//   test/performance_benching/services/mtconnect/host.c
//   src/network_drivers/presentation/codec/base64/base64.c src/services/machine_tool/mtconnect/mtconnect.c
//   src/mmgr/protomem.c src/mmgr/protostr.c -o /tmp/hb && /tmp/hb

#include "services/machine_tool/mtconnect/mtconnect.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

int main(void)
{
    hbench_header();

    char buf[4096];
    volatile size_t sink = 0;
    double ns = 0.0;
    HBENCH_NS(
        200000,
        {
            pc_mtc_streams s;
            pc_mtc_streams_begin(&s, buf, sizeof(buf), 1500, 20, "cnc1");
            for (int i = 0; i < 20; i++)
            {
                pc_mtc_streams_add(&s, PC_MTC_SAMPLE, "Position", "xpos", (uint64_t)i, "2026-07-09T00:00:00Z", "12.5");
            }
            sink += pc_mtc_streams_end(&s);
        },
        ns);
    hbench_row("mtconnect", "streams doc (20 obs)", ns, (double)sink / 200000.0);
    (void)sink;
    return 0;
}
