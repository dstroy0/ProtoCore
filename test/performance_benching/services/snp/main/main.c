// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the GE SNP codec (services/fieldbus/snp): the BCC checksum and the
// frame build/parse. Pure; no serial link.
//
// Build/flash:  idf.py -C test/performance_benching/snp -t upload --upload-port COM7
#include "device_bench.h"
#include "services/fieldbus/snp/snp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    static const uint8_t data[24] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
                                     0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18};
    static uint8_t frame[64];
    size_t flen = pc_snp_build(0x03, data, sizeof(data), frame, sizeof(frame));

    for (;;)
    {
        DBENCH_BANNER("snp");
        volatile size_t sink = 0;
        DBENCH_BULK("pc_snp_bcc", 200000, sizeof(data), sink += pc_snp_bcc(data, sizeof(data)));
        static uint8_t out[64];
        DBENCH_OP("pc_snp_build", 200000, sink += pc_snp_build(0x03, data, sizeof(data), out, sizeof(out)));
        SnpFrame sf;
        DBENCH_OP("pc_snp_parse", 200000, sink += pc_snp_parse(frame, flen, &sf));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("snp")
