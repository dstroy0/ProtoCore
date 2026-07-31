// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the NATS client codec: pc_nats_build_pub (the device publishes) and pc_nats_parse
// (decode one inbound server frame - the untrusted-input hot op). Both pure (no sockets, no heap), so they
// link standalone. The device figure comes from the rig /bench pc_nats_parse op; this host ns/op + MB/s is a
// RELATIVE baseline. Build + run:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_NATS=1 performance_benching/services/nats/host.cpp
//   \
//       src/services/iot/nats/nats.cpp -o /tmp/bnats && /tmp/bnats

#define PC_ENABLE_NATS 1
#include "services/iot/nats/nats.h"

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

int main()
{
    const uint8_t payload[] = "{\"v\":21.4,\"u\":\"C\",\"ts\":1720700000}";
    const size_t plen = sizeof(payload) - 1;
    char pub[128];
    size_t publen = pc_nats_build_pub(pub, sizeof(pub), "factory.line1.temp", nullptr, payload, plen);

    // A representative inbound MSG frame (what a subscriber receives).
    const char msg[] = "MSG factory.line1.temp 1 34\r\n{\"v\":21.4,\"u\":\"C\",\"ts\":1720700000}\r\n";
    const size_t mlen = sizeof(msg) - 1;

    printf("| Feature      | Operation                |     ns/op |    MB/s |\n");
    printf("|--------------|--------------------------|-----------|---------|\n");

    // build a PUB frame.
    {
        volatile size_t sink = 0;
        double ns = bench_ns(3000000, [&] {
            sink += pc_nats_build_pub(pub, sizeof(pub), "factory.line1.temp", nullptr, payload, plen);
        });
        row("nats", "build PUB", ns, (double)publen);
        (void)sink;
    }
    // parse an inbound MSG frame (control line + payload).
    {
        volatile int sink = 0;
        double ns = bench_ns(3000000, [&] {
            NatsMsg m;
            size_t used = 0;
            sink += pc_nats_parse(msg, mlen, &m, &used) ? (int)used : 0;
        });
        row("nats", "parse MSG", ns, (double)mlen);
        (void)sink;
    }

    return 0;
}
