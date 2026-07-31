// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the permessage-deflate codec (RFC 7692): deflate_raw() + inflate_raw(),
// the compress/decompress hot ops on every WebSocket data frame when permessage-deflate is negotiated
// (also the SSH zlib@openssh path). Both are pure (no sockets, no heap - a caller scratch), so they link
// standalone. The device number comes from the rig /bench endpoint; this host ns/op + MB/s is a RELATIVE
// baseline. Build + run:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_WS_DEFLATE=1
//   performance_benching/network_drivers/presentation/deflate/host.cpp \
//       src/network_drivers/presentation/codec/deflate/deflate.cpp \
//       src/network_drivers/presentation/codec/inflate/inflate.cpp -o /tmp/bd && /tmp/bd

#include "network_drivers/presentation/codec/deflate/deflate.h"
#include "network_drivers/presentation/codec/inflate/inflate.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

using clk = std::chrono::steady_clock;

template <typename F> static double bench_ns(uint64_t iters, F fn)
{
    auto t0 = clk::now();
    for (uint64_t i = 0; i < iters; i++)
    {
        fn();
    }
    auto t1 = clk::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    return ns / (double)iters;
}

static void row(const char *feature, const char *op, double ns_per_op, double bytes_per_op)
{
    double mbps = bytes_per_op > 0 ? (bytes_per_op / (ns_per_op * 1e-9)) / 1e6 : 0.0;
    printf("| %-12s | %-24s | %10.1f | %9.1f |\n", feature, op, ns_per_op, mbps);
}

// A realistic WebSocket text frame: a JSON telemetry message (structured + repetitive -> compressible).
static const char *MSG = "{\"type\":\"telemetry\",\"ts\":1720700000,\"sensors\":["
                         "{\"id\":1,\"name\":\"temp\",\"unit\":\"C\",\"value\":21.4},"
                         "{\"id\":2,\"name\":\"humidity\",\"unit\":\"%\",\"value\":48.0},"
                         "{\"id\":3,\"name\":\"pressure\",\"unit\":\"hPa\",\"value\":1013.2}]}";

int main()
{
    static uint8_t dscratch[DEFLATE_SCRATCH_SIZE];
    static uint8_t iscratch[INFLATE_SCRATCH_SIZE];
    const size_t n = strlen(MSG);
    uint8_t comp[512];
    uint8_t plain[512];
    size_t clen = 0, plen = 0;

    deflate_raw((const uint8_t *)MSG, n, comp, sizeof(comp), &clen, dscratch, DEFLATE_SCRATCH_SIZE);
    // permessage-deflate (RFC 7692) strips the 00 00 FF FF sync-flush trailer on send; the receiver
    // appends it back before inflating (inflate_raw is called with comp_len + 4).
    comp[clen] = 0x00;
    comp[clen + 1] = 0x00;
    comp[clen + 2] = 0xFF;
    comp[clen + 3] = 0xFF;

    printf("| Feature      | Operation                |     ns/op |    MB/s |\n");
    printf("|--------------|--------------------------|-----------|---------|\n");

    // deflate: compress the message frame (throughput is over the INPUT bytes).
    {
        volatile int sink = 0;
        double ns = bench_ns(200000, [&] {
            size_t o = 0;
            sink += (int)deflate_raw((const uint8_t *)MSG, n, comp, sizeof(comp), &o, dscratch, DEFLATE_SCRATCH_SIZE);
        });
        row("ws-deflate", "deflate (json msg)", ns, (double)n);
        (void)sink;
    }
    // inflate: decompress the frame back (throughput over the OUTPUT bytes - the decompressed size).
    {
        volatile int sink = 0;
        double ns = bench_ns(200000, [&] {
            plen = 0;
            sink += (int)inflate_raw(comp, clen + 4, plain, sizeof(plain), &plen, iscratch, INFLATE_SCRATCH_SIZE);
        });
        row("ws-deflate", "inflate (json msg)", ns, (double)plen);
        (void)sink;
    }

    printf("compressed %zu -> %zu bytes (%.0f%%); round-trip %s\n", n, clen, 100.0 * (double)clen / (double)n,
           (plen == n && memcmp(plain, MSG, n) == 0) ? "OK" : "MISMATCH");
    return 0;
}
