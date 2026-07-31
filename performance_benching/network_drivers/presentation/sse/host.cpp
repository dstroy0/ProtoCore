// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the Server-Sent Events framing hot op (sse_format), the pure
// presentation-layer record builder that runs on every sse_send()/sse_broadcast(). The device
// number comes from the rig /bench endpoint; this host ns/op + MB/s is a RELATIVE baseline (a fast
// RPi core), not the device cost. pc_sse_format() is pure (no transport), so it links standalone.
// Build + run (same include roots as the native test env):
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support \
//       performance_benching/network_drivers/presentation/sse/host.cpp
//       src/network_drivers/presentation/http/sse/sse.cpp -o /tmp/bs && /tmp/bs

#include "network_drivers/presentation/http/sse/sse.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

// sse.cpp's pc_sse_write() (not exercised here) references the transport layer; the bench only calls
// the pure pc_sse_format(), so satisfy the linker with stubs rather than pulling in transport + lwIP.
TcpConn conn_pool[CONN_POOL_SLOTS];
bool pc_conn_send(uint8_t, const void *, u16_t)
{
    return true;
}

using clk = std::chrono::steady_clock;

// Time `iters` calls of fn(), return ns per op.
template <typename F> static double bench_ns(uint64_t iters, F fn)
{
    auto t0 = clk::now();
    for (uint64_t i = 0; i < iters; i++)
        fn();
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
    printf("| Feature      | Operation                |     ns/op |    MB/s |\n");
    printf("|--------------|--------------------------|-----------|---------|\n");

    char buf[SSE_BUF_SIZE];

    // data-only: the common broadcast shape (`data: <payload>\n\n`).
    {
        volatile int sink = 0;
        double ns = bench_ns(2000000,
                             [&] { sink += pc_sse_format(buf, sizeof(buf), "sensor=21.4C rh=48%", nullptr, nullptr); });
        int bytes = pc_sse_format(buf, sizeof(buf), "sensor=21.4C rh=48%", nullptr, nullptr);
        row("sse", "format data-only", ns, (double)bytes);
        (void)sink;
    }

    // event + id + data: the fully-addressed record (named event, resumable id).
    {
        volatile int sink = 0;
        double ns = bench_ns(
            2000000, [&] { sink += pc_sse_format(buf, sizeof(buf), "sensor=21.4C rh=48%", "telemetry", "12345"); });
        int bytes = pc_sse_format(buf, sizeof(buf), "sensor=21.4C rh=48%", "telemetry", "12345");
        row("sse", "format event+id+data", ns, (double)bytes);
        (void)sink;
    }

    return 0;
}
