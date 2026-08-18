// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SCP/RCP control-line codec (network_drivers/session/scp): parse and
// build the `C<mode> <size> <name>` transfer control line (octal mode, decimal size, name). Pure
// string logic - the SSH channel/file plumbing is elsewhere; only the per-file control-line codec
// is benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/scp -t upload --upload-port COM7
#include "device_bench.h"
#include "network_drivers/session/scp/scp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t scp_work[16]; // the borrow an entry takes; Scp never reads it

void dbench_run(void)
{
    static const char cline[] = "C0644 262144 firmware.bin\n";

    for (;;)
    {
        DBENCH_BANNER("scp");
        volatile size_t sink = 0;
        uint32_t mode;
        uint64_t size;
        char name[64];
        DBENCH_OP("protocore_scp_parse_cline", 200000,
                  sink += protocore_scp_parse_cline(cline, sizeof(cline) - 1, &mode, &size, name, sizeof(name)));
        static char out[64];
        DBENCH_OP("protocore_scp_build_cline", 200000,
                  sink += protocore_scp_build_cline(0644, 262144, "firmware.bin", out, sizeof(out)));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("scp")
