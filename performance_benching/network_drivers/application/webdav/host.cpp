// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the WebDAV 207 Multi-Status builder (RFC 4918) - the pure,
// transport/filesystem-free hot op that runs on every PROPFIND response (one pc_webdav_ms_entry per
// directory child) plus the XML escaping on each href/property. The device number comes from the rig
// /bench endpoint; this host ns/op + MB/s is a RELATIVE baseline (a fast RPi core), not the device
// cost. The 207 builder is pure, so it links standalone. Build + run:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_WEBDAV=1 \
//       performance_benching/network_drivers/application/webdav/host.cpp src/network_drivers/application/webdav/webdav.cpp -o /tmp/bw &&
//       /tmp/bw

#define PC_ENABLE_WEBDAV 1
#include "network_drivers/application/webdav/webdav.h"

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
    printf("| Feature      | Operation                |     ns/op |    MB/s |\n");
    printf("|--------------|--------------------------|-----------|---------|\n");

    static char buf[8192];
    const char *mtime = "Mon, 07 Jul 2026 12:00:00 GMT";

    // One <response> entry for a file (the per-child PROPFIND hot op).
    {
        volatile size_t sink = 0;
        double ns = bench_ns(1000000, [&] {
            size_t n = pc_webdav_ms_entry(buf, sizeof(buf), 0, "/dav/report.txt", false, 4096, mtime, "text/plain");
            sink += n;
        });
        size_t bytes = pc_webdav_ms_entry(buf, sizeof(buf), 0, "/dav/report.txt", false, 4096, mtime, "text/plain");
        row("webdav", "ms_entry file", ns, (double)bytes);
        (void)sink;
    }

    // A whole Depth-1 directory listing: prolog + 8 children + epilog (a realistic PROPFIND body).
    {
        volatile size_t sink = 0;
        double ns = bench_ns(200000, [&] {
            size_t len = pc_webdav_ms_begin(buf, sizeof(buf), 0);
            len = pc_webdav_ms_entry(buf, sizeof(buf), len, "/dav/", true, 0, mtime, "");
            for (int k = 0; k < 8; k++)
            {
                len = pc_webdav_ms_entry(buf, sizeof(buf), len, "/dav/sensor-log.csv", false, 12800, mtime, "text/csv");
            }
            len = pc_webdav_ms_end(buf, sizeof(buf), len);
            sink += len;
        });
        size_t len = pc_webdav_ms_begin(buf, sizeof(buf), 0);
        len = pc_webdav_ms_entry(buf, sizeof(buf), len, "/dav/", true, 0, mtime, "");
        for (int k = 0; k < 8; k++)
        {
            len = pc_webdav_ms_entry(buf, sizeof(buf), len, "/dav/sensor-log.csv", false, 12800, mtime, "text/csv");
        }
        len = pc_webdav_ms_end(buf, sizeof(buf), len);
        row("webdav", "PROPFIND depth-1 (8)", ns, (double)len);
        (void)sink;
    }

    // XML-escape an href containing the five escapable characters (per href / property tag).
    {
        char esc[256];
        volatile size_t sink = 0;
        double ns = bench_ns(2000000, [&] { sink += pc_webdav_xml_escape(esc, sizeof(esc), "/dav/a&b<c>\"d'e.txt"); });
        size_t bytes = pc_webdav_xml_escape(esc, sizeof(esc), "/dav/a&b<c>\"d'e.txt");
        row("webdav", "xml_escape", ns, (double)bytes);
        (void)sink;
    }

    return 0;
}
