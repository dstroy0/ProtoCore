// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the MTConnect agent response codec (services/machine_tool/mtconnect): building a
// streams document of 20 observations. A deterministic ns/op + MB/s baseline complementing the
// on-device ESP32-S3 number; the host figure is a RELATIVE baseline, not the device cost. Build:
//   g++ -O2 -std=c++17 -Isrc -DPC_ENABLE_MTCONNECT=1 performance_benching/services/mtconnect/host.cpp \
//       src/network_drivers/presentation/codec/base64/base64.cpp src/services/machine_tool/mtconnect/mtconnect.cpp \
//       -o /tmp/hb && /tmp/hb

#include "services/machine_tool/mtconnect/mtconnect.h"
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

    char buf[4096];
    volatile size_t sink = 0;
    double ns = bench_ns(200000, [&] {
        pc_mtc_streams s;
        pc_mtc_streams_begin(&s, buf, sizeof(buf), 1500, 20, "cnc1");
        for (int i = 0; i < 20; i++)
        {
            pc_mtc_streams_add(&s, pc_mtc_category::PC_MTC_SAMPLE, "Position", "xpos", (uint64_t)i,
                               "2026-07-09T00:00:00Z", "12.5");
        }
        sink += pc_mtc_streams_end(&s);
    });
    row("mtconnect", "streams doc (20 obs)", ns, (double)sink / 200000.0);
    (void)sink;
    return 0;
}
