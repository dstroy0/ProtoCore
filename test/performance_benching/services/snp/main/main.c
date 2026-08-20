// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

static uint8_t snp_work[16]; // the borrow an entry takes; Snp never reads it

void dbench_run(void)
{
    static const uint8_t data[24] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
                                     0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18};
    static uint8_t frame[64];
    Snp.build_args.control = 0x03;
    Snp.build_args.data = data;
    Snp.build_args.data_len = sizeof(data);
    Snp.build_args.out = frame;
    Snp.build_args.cap = sizeof(frame);
    Snp.build(snp_work);
    size_t flen = Snp.n;

    for (;;)
    {
        DBENCH_BANNER("snp");
        volatile size_t sink = 0;
        Snp.bcc_args.bytes = data;
        Snp.bcc_args.len = sizeof(data);
        DBENCH_BULK("Snp.bcc", 200000, sizeof(data), sink += (Snp.bcc(snp_work), Snp.value));
        static uint8_t out[64];
        Snp.build_args.control = 0x03;
        Snp.build_args.data = data;
        Snp.build_args.data_len = sizeof(data);
        Snp.build_args.out = out;
        Snp.build_args.cap = sizeof(out);
        DBENCH_OP("Snp.build", 200000,
                  sink += (Snp.build(snp_work), Snp.n));
        SnpFrame sf;
        Snp.parse_args.frame = frame;
        Snp.parse_args.len = flen;
        Snp.parse_args.out = &sf;
        DBENCH_OP("Snp.parse", 200000, sink += (Snp.parse(snp_work), Snp.ok));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("snp")
