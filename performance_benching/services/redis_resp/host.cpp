// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the Redis RESP2/RESP3 codec: pc_resp_encode_command (the device builds an
// outbound command) and pc_resp_parse (the device decodes a server reply - the untrusted-input hot op).
// Both are pure (no sockets, no heap), so they link standalone. The device number comes from the rig
// /bench endpoint; this host ns/op + MB/s is a RELATIVE baseline. Build + run:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_REDIS=1
//   performance_benching/services/redis_resp/host.cpp \
//       src/services/iot/redis_resp/redis_resp.cpp -o /tmp/br && /tmp/br

#define PC_ENABLE_REDIS 1
#include "services/iot/redis_resp/redis_resp.h"

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
    const char *argv[] = {"SET", "pc:sensor:temp", "21.4"};
    char cmd[128];
    size_t clen = pc_resp_encode_command(cmd, sizeof(cmd), argv, nullptr, 3);

    // A representative RESP2 array reply (what a HGETALL / MGET returns).
    const uint8_t reply[] = "*3\r\n$5\r\nhello\r\n:12345\r\n$-1\r\n";
    const size_t rlen = sizeof(reply) - 1;

    printf("| Feature      | Operation                |     ns/op |    MB/s |\n");
    printf("|--------------|--------------------------|-----------|---------|\n");

    // encode a SET command.
    {
        volatile size_t sink = 0;
        double ns = bench_ns(2000000, [&] { sink += pc_resp_encode_command(cmd, sizeof(cmd), argv, nullptr, 3); });
        row("redis-resp", "encode SET command", ns, (double)clen);
        (void)sink;
    }
    // parse a reply: walk every value in the array (header + 3 children), as a client would.
    {
        volatile int sink = 0;
        double ns = bench_ns(2000000, [&] {
            size_t off = 0, used = 0;
            RespReply r;
            while (off < rlen && pc_resp_parse(reply + off, rlen - off, &r, &used) && used)
            {
                off += used;
                sink += (int)r.type;
            }
        });
        row("redis-resp", "parse array reply", ns, (double)rlen);
        (void)sink;
    }

    return 0;
}
