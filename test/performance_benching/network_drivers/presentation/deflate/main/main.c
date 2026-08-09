// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the permessage-deflate codec (network_drivers/presentation/
// deflate): Deflate.raw() + Inflate.raw() over a JSON telemetry frame. Pure (caller scratch).
// Build/flash: pio run -d performance_benching/network_drivers/presentation/deflate -t upload
#include "device_bench.h"
#include "network_drivers/presentation/codec/deflate/deflate.h"
#include "network_drivers/presentation/codec/inflate/inflate.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
    Deflate.raw((const uint8_t *)MSG, n, comp, sizeof(comp), &clen, dscratch, DEFLATE_SCRATCH_SIZE);
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
            sink += (int)Deflate.raw((const uint8_t *)MSG, n, comp, sizeof(comp), &o, dscratch, DEFLATE_SCRATCH_SIZE);
        });
        DBENCH_BULK("Inflate.raw (json msg)", 20000, n, {
            size_t plen = 0;
            sink += (int)Inflate.raw(comp, clen + 4, plain, sizeof(plain), &plen, iscratch, INFLATE_SCRATCH_SIZE);
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("deflate")
