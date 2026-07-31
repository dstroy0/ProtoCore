// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the RFC 5424 syslog client formatter: pc_syslog_format() builds one
// `<PRI>1 - HOST APP - - - MSG` line into a caller buffer - the per-log-line hot op the device runs before
// each pc_udp_sendto(). Pure (no socket, no heap). syslog.cpp also holds pc_syslog_log() which references
// pc_udp_sendto(), so this bench stubs it below rather than linking udp.cpp (pc_syslog_log() is not
// exercised here - only the pure pc_syslog_format() is benched).
// The device number comes from the rig /bench pc_syslog_format op; this host ns/op is a RELATIVE baseline:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_SYSLOG=1
//   performance_benching/services/syslog/host.cpp \
//       src/services/net/syslog/syslog.cpp -o /tmp/bsl && /tmp/bsl

#define PC_ENABLE_SYSLOG 1
#include "services/net/syslog/syslog.h"

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

// syslog.cpp's pc_syslog_log() (not exercised here) sends over UDP; the bench only calls the pure
// pc_syslog_format(), so stub the transport to satisfy the linker rather than pulling in udp + lwIP.
bool pc_udp_sendto(const char *, uint16_t, const uint8_t *, size_t)
{
    return true;
}

int main()
{
    char out[256];
    const char *msg = "sensor=21.4C rh=48% link=up heap=131072";
    size_t len = pc_syslog_format(out, sizeof(out), SyslogFacility::SYSLOG_FAC_LOCAL0, SyslogSeverity::SYSLOG_INFO,
                                  "pc-rig", "rig-app", msg);

    printf("| Feature      | Operation                |     ns/op |    MB/s |\n");
    printf("|--------------|--------------------------|-----------|---------|\n");

    // format one RFC 5424 line (the per-log-line cost before the UDP send).
    {
        volatile size_t sink = 0;
        double ns = bench_ns(2000000, [&] {
            sink += pc_syslog_format(out, sizeof(out), SyslogFacility::SYSLOG_FAC_LOCAL0, SyslogSeverity::SYSLOG_INFO,
                                     "pc-rig", "rig-app", msg);
        });
        row("syslog", "pc_syslog_format (RFC 5424)", ns, (double)len);
        (void)sink;
    }

    return 0;
}
