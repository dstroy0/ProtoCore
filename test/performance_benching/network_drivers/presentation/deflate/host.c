// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the permessage-deflate codec (RFC 7692): Deflate.raw() + Inflate.raw(),
// the compress/decompress hot ops on every WebSocket data frame when permessage-deflate is negotiated
// (also the SSH zlib@openssh path). Both are pure (no sockets, no heap - a caller scratch), so they link
// standalone. The device number comes from the rig /bench endpoint; this host ns/op + MB/s is a relative
// baseline. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_WEBSOCKET=1 -DPROTOCORE_ENABLE_WS_DEFLATE=1
//   test/performance_benching/network_drivers/presentation/deflate/host.c
//   src/network_drivers/presentation/codec/deflate/deflate.c
//   src/network_drivers/presentation/codec/inflate/inflate.c src/mmgr/protomem.c -o /tmp/bd && /tmp/bd

#include "network_drivers/presentation/codec/deflate/deflate.h"
#include "network_drivers/presentation/codec/inflate/inflate.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

// A realistic WebSocket text frame: a JSON telemetry message (structured + repetitive -> compressible).
static const char *MSG = "{\"type\":\"telemetry\",\"ts\":1720700000,\"sensors\":["
                         "{\"id\":1,\"name\":\"temp\",\"unit\":\"C\",\"value\":21.4},"
                         "{\"id\":2,\"name\":\"humidity\",\"unit\":\"%\",\"value\":48.0},"
                         "{\"id\":3,\"name\":\"pressure\",\"unit\":\"hPa\",\"value\":1013.2}]}";

int main(void)
{
    static uint8_t dscratch[DEFLATE_SCRATCH_SIZE];
    static uint8_t iscratch[INFLATE_SCRATCH_SIZE];
    const size_t n = strlen(MSG);
    uint8_t comp[512];
    uint8_t plain[512];
    size_t clen = 0, plen = 0;

    Deflate.raw((const uint8_t *)MSG, n, comp, sizeof(comp), &clen, dscratch, DEFLATE_SCRATCH_SIZE);
    // permessage-deflate (RFC 7692) strips the 00 00 FF FF sync-flush trailer on send; the receiver
    // appends it back before inflating (Inflate.raw is called with comp_len + 4).
    comp[clen] = 0x00;
    comp[clen + 1] = 0x00;
    comp[clen + 2] = 0xFF;
    comp[clen + 3] = 0xFF;

    hbench_header();

    // deflate: compress the message frame (throughput is over the INPUT bytes).
    {
        volatile int sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            200000,
            {
                size_t o = 0;
                sink +=
                    (int)Deflate.raw((const uint8_t *)MSG, n, comp, sizeof(comp), &o, dscratch, DEFLATE_SCRATCH_SIZE);
            },
            ns);
        hbench_row("ws-deflate", "deflate (json msg)", ns, (double)n);
        (void)sink;
    }
    // inflate: decompress the frame back (throughput over the OUTPUT bytes - the decompressed size).
    {
        volatile int sink = 0;
        double ns = 0.0;
        HBENCH_NS(
            200000,
            {
                plen = 0;
                sink += (int)Inflate.raw(comp, clen + 4, plain, sizeof(plain), &plen, iscratch, INFLATE_SCRATCH_SIZE);
            },
            ns);
        hbench_row("ws-deflate", "inflate (json msg)", ns, (double)plen);
        (void)sink;
    }

    printf("compressed %zu -> %zu bytes (%.0f%%); round-trip %s\n", n, clen, 100.0 * (double)clen / (double)n,
           (plen == n && memcmp(plain, MSG, n) == 0) ? "OK" : "MISMATCH");
    return 0;
}
