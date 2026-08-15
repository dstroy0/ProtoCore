// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the base64 codec (network_drivers/presentation/codec/base64):
// encode + decode of a 1 KiB payload. Pure. Build/flash: pio run -d
// performance_benching/network_drivers/presentation/base64 -t upload
#include "device_bench.h"
#include "network_drivers/presentation/codec/base64/base64.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    static uint8_t src[1024];
    for (size_t i = 0; i < sizeof(src); i++)
    {
        src[i] = (uint8_t)(i * 31 + 7);
    }
    static char enc[((1024 + 2) / 3) * 4 + 1];
    static uint8_t dec[1024];
    for (;;)
    {
        DBENCH_BANNER("base64");
        volatile size_t sink = 0;
        DBENCH_BULK("Base64.encode (1 KiB)", 100000, 1024, {
            Base64.encode(src, 1024, enc);
            sink += 1;
        });
        DBENCH_BULK("Base64.decode (1 KiB)", 100000, 1024, sink += Base64.decode(enc, dec, sizeof(dec)));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("base64")
