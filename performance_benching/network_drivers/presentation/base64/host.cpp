// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the base64 codec (network_drivers/presentation/codec/base64): encode +
// decode of a 1 KiB payload. A deterministic ns/op + MB/s baseline that complements the on-device
// ESP32-S3 numbers; the host figure is a RELATIVE baseline (a fast desktop/RPi core), not the device
// cost. Build + run:
//   g++ -O2 -std=c++17 -Isrc performance_benching/network_drivers/presentation/base64/host.cpp \
//       src/network_drivers/presentation/codec/base64/base64.cpp -o /tmp/hb && /tmp/hb

#include "network_drivers/presentation/codec/base64/base64.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

using clk = std::chrono::steady_clock;

template <typename F> static double bench_ns(uint64_t iters, F fn)
{
    auto t0 = clk::now();
    for (uint64_t i = 0; i < iters; i++)
        fn();
    auto t1 = clk::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / (double)iters;
}

static void row(const char *feature, const char *op, double ns_per_op, double bytes_per_op)
{
    double mbps = bytes_per_op > 0 ? (bytes_per_op / (ns_per_op * 1e-9)) / 1e6 : 0.0;
    printf("| %-12s | %-22s | %10.1f | %9.1f |\n", feature, op, ns_per_op, mbps);
}

int main()
{
    printf("| Feature      | Operation              |     ns/op |    MB/s |\n");
    printf("|--------------|------------------------|-----------|---------|\n");

    const size_t N = 1024;
    uint8_t src[N];
    for (size_t i = 0; i < N; i++)
        src[i] = (uint8_t)(i * 31 + 7);
    char enc[((N + 2) / 3) * 4 + 1];
    uint8_t dec[N];
    volatile size_t sink = 0;
    double ns_e = bench_ns(200000, [&] { pc_base64_encode(src, N, enc); });
    row("base64", "encode 1 KiB", ns_e, (double)N);
    double ns_d = bench_ns(200000, [&] { sink += pc_base64_decode(enc, dec, sizeof(dec)); });
    row("base64", "decode 1 KiB", ns_d, (double)N);
    (void)sink;
    return 0;
}
