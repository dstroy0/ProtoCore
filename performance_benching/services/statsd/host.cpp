// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the StatsD metrics client: pc_statsd_format() builds one `name:value|type
// [|@rate][|#tags]` line - the per-metric hot op the device runs before each pc_udp_sendto(). Pure (no
// socket, no heap). statsd.cpp also holds the emit helpers which reference pc_udp_sendto(), so link udp.cpp
// for the host no-op UDP stubs (same pattern as bench_syslog). The device number comes from the rig /bench
// pc_statsd_format op; this host ns/op + MB/s is a RELATIVE baseline:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_STATSD=1
//   performance_benching/services/statsd/host.cpp \
//       src/services/iot/statsd/statsd.cpp src/network_drivers/transport/udp.cpp -o /tmp/bst && /tmp/bst

#define PC_ENABLE_STATSD 1
#include "services/iot/statsd/statsd.h"

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
    char out[256];
    size_t len = pc_statsd_format(out, sizeof(out), "api.requests", "1", StatsdType::STATSD_COUNTER, 0.1f,
                                  "env:prod,host:pc-rig");

    printf("| Feature      | Operation                |     ns/op |    MB/s |\n");
    printf("|--------------|--------------------------|-----------|---------|\n");

    // format one sampled counter line with tags (the per-metric cost before the UDP send).
    {
        volatile size_t sink = 0;
        double ns = bench_ns(3000000, [&] {
            sink += pc_statsd_format(out, sizeof(out), "api.requests", "1", StatsdType::STATSD_COUNTER, 0.1f,
                                     "env:prod,host:pc-rig");
        });
        row("statsd", "pc_statsd_format (counter)", ns, (double)len);
        (void)sink;
    }

    return 0;
}
