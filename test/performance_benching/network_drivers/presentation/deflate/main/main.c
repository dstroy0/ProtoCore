// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the permessage-deflate codec (network_drivers/presentation/
// deflate): Deflate.raw() + Inflate.raw() over a JSON telemetry frame. Pure (caller scratch).
// Build/flash: pio run -d performance_benching/network_drivers/presentation/deflate -t upload
#include "device_bench.h"
#include "network_drivers/presentation/codec/deflate/deflate/deflate.h"
#include "network_drivers/presentation/codec/inflate/inflate.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t inflate_work[16]; // the borrow an entry takes; Inflate never reads it

static uint8_t deflate_work[16]; // the borrow an entry takes; Deflate never reads it

static const char *MSG = "{\"type\":\"telemetry\",\"ts\":1720700000,\"sensors\":["
                         "{\"id\":1,\"name\":\"temp\",\"unit\":\"C\",\"value\":21.4},"
                         "{\"id\":2,\"name\":\"humidity\",\"unit\":\"%\",\"value\":48.0}]}";

void dbench_run(void)
{
    static uint8_t dscratch[DEFLATE_SCRATCH_SIZE];
    static uint8_t iscratch[INFLATE_SCRATCH_SIZE];
    const size_t n = strlen(MSG);
    static uint8_t comp[512], plain[512];
    size_t clen = 0;
    DeflateV.raw_args.src = (const uint8_t *)MSG;
    DeflateV.raw_args.src_len = n;
    DeflateV.raw_args.dst = comp;
    DeflateV.raw_args.dst_cap = sizeof(comp);
    DeflateV.raw_args.out_len = &clen;
    DeflateV.raw_args.scratch = dscratch;
    DeflateV.raw_args.scratch_len = DEFLATE_SCRATCH_SIZE;
    Deflate.raw(deflate_work);
    comp[clen] = 0x00;
    comp[clen + 1] = 0x00;
    comp[clen + 2] = 0xFF;
    comp[clen + 3] = 0xFF;
    for (;;)
    {
        DBENCH_BANNER("deflate");
        volatile int sink = 0;
        DBENCH_BULK("Deflate.raw (json msg)", 20000, n, {
            size_t o = 0;
            DeflateV.raw_args.src = (const uint8_t *)MSG;
            DeflateV.raw_args.src_len = n;
            DeflateV.raw_args.dst = comp;
            DeflateV.raw_args.dst_cap = sizeof(comp);
            DeflateV.raw_args.out_len = &o;
            DeflateV.raw_args.scratch = dscratch;
            DeflateV.raw_args.scratch_len = DEFLATE_SCRATCH_SIZE;
            Deflate.raw(deflate_work);
            sink += (int)DeflateV.value;
        });
        DBENCH_BULK("Inflate.raw (json msg)", 20000, n, {
            size_t plen = 0;
            Inflate.raw_args.src = comp;
            Inflate.raw_args.src_len = clen + 4;
            Inflate.raw_args.dst = plain;
            Inflate.raw_args.dst_cap = sizeof(plain);
            Inflate.raw_args.out_len = &plen;
            Inflate.raw_args.scratch = iscratch;
            Inflate.raw_args.scratch_len = INFLATE_SCRATCH_SIZE;
            Inflate.raw(inflate_work);
            sink += (int)Inflate.value;
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("deflate")
