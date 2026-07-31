// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the authoritative DNS server (RFC 1035): pc_dns_server_build_response() parses
// an A/IN query and appends the compressed A answer (or NXDOMAIN on a miss). Pure (no clock, no socket), so
// it links standalone; the ARDUINO UDP binding is compiled out on host. The device figure comes from the rig
// /bench op; this host ns/op + MB/s is a RELATIVE baseline. Build + run:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_DNS_SERVER=1
//   performance_benching/services/dns_server/host.cpp \
//       src/services/net/dns_server/dns_server.cpp -o /tmp/bd && /tmp/bd

#define PC_ENABLE_DNS_SERVER 1
#include "services/net/dns_server/dns_server.h"

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

// Always resolves, so build_response takes the A-answer path (the heaviest).
static uint32_t resolve_hit(const char *)
{
    return 0xC0A80105u; // 192.168.1.5
}
static uint32_t resolve_miss(const char *)
{
    return 0; // -> NXDOMAIN
}

int main()
{
    // "test.lan" A IN: 12-byte header + question (04 test 03 lan 00 + qtype/qclass).
    const uint8_t query[] = {0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04,
                             't',  'e',  's',  't',  0x03, 'l',  'a',  'n',  0x00, 0x00, 0x01, 0x00, 0x01};
    const size_t qlen = sizeof(query);
    uint8_t out[192]; // >= PC_DNS_NAME_MAX (128) + one A answer; protocore_config.h not pulled in here

    printf("| Feature      | Operation                |     ns/op |    MB/s |\n");
    printf("|--------------|--------------------------|-----------|---------|\n");

    // A hit: parse question + append the compressed A answer.
    {
        volatile size_t sink = 0;
        double ns = bench_ns(
            5000000, [&] { sink += pc_dns_server_build_response(query, qlen, 60, resolve_hit, out, sizeof(out)); });
        row("dns", "build_response (A hit)", ns, (double)qlen);
        (void)sink;
    }
    // A miss -> NXDOMAIN (parse only, no answer RR).
    {
        volatile size_t sink = 0;
        double ns = bench_ns(
            5000000, [&] { sink += pc_dns_server_build_response(query, qlen, 60, resolve_miss, out, sizeof(out)); });
        row("dns", "build_response (NXDOMAIN)", ns, (double)qlen);
        (void)sink;
    }

    return 0;
}
