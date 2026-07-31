// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the S7comm codec (Siemens S7 / ISO-on-TCP application layer): the client
// Setup-Communication + Read-Var request builders and the response-header parser. Pure (no socket), so it
// links standalone. The device figure comes from the rig /bench op; this host ns/op + MB/s is a RELATIVE
// baseline. Build + run:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_S7COMM=1
//   performance_benching/services/s7comm/host.cpp \
//       src/services/fieldbus/s7comm/s7comm.cpp -o /tmp/bs && /tmp/bs

#define PC_ENABLE_S7COMM 1
#include "services/fieldbus/s7comm/s7comm.h"

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
    printf("| %-8s | %-26s | %10.1f | %9.1f |\n", feature, op, ns_per_op, mbps);
}

int main()
{
    // A 3-item Read Var job (two DB reads + a flag bit) - a realistic PLC poll.
    const S7ReadItem items[3] = {
        {S7_AREA_DB, 1, 0, S7_TS_BYTE, 16},
        {S7_AREA_DB, 2, 4, S7_TS_WORD, 8},
        {S7_AREA_FLAGS, 0, 0, S7_TS_BIT, 1},
    };
    uint8_t req[256];
    size_t req_len = pc_s7_build_read_request(req, sizeof(req), 0x0002, items, 3);

    printf("| Feature  | Operation                  |     ns/op |    MB/s |\n");
    printf("|----------|----------------------------|-----------|---------|\n");

    // pc_s7_build_setup: the Setup-Communication job (negotiate AMQ + PDU size) - once per connection.
    {
        uint8_t buf[64];
        volatile size_t sink = 0;
        double ns = bench_ns(5000000, [&] { sink += pc_s7_build_setup(buf, sizeof(buf), 0x0001, 1, 1, 480); });
        row("s7comm", "build_setup", ns, (double)(sink ? 22.0 : 0.0));
        (void)sink;
    }

    // pc_s7_build_read_request: frame an N-item Read Var job (S7-ANY pointers) - the PLC-poll transmit op.
    {
        uint8_t buf[256];
        volatile size_t sink = 0;
        double ns = bench_ns(2000000, [&] { sink += pc_s7_build_read_request(buf, sizeof(buf), 0x0002, items, 3); });
        row("s7comm", "build_read_request (3)", ns, (double)req_len);
        (void)sink;
    }

    // pc_s7_parse_header: validate the protocol id + ROSCTR + lengths and slice param/data - the receive op.
    {
        volatile size_t sink = 0;
        double ns = bench_ns(10000000, [&] {
            S7Header h;
            if (pc_s7_parse_header(req, req_len, &h))
                sink += h.header_len;
        });
        row("s7comm", "parse_header", ns, (double)req_len);
        (void)sink;
    }

    return 0;
}
