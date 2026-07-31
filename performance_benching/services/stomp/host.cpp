// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the STOMP 1.2 frame codec: pc_stomp_build_frame (the device emits a SEND) and
// pc_stomp_parse_frame (decode one inbound broker frame - command + headers + content-length body, the
// untrusted-input hot op). Both pure (no sockets, no heap), so they link standalone. The device figure comes
// from the rig /bench pc_stomp_parse_frame op; this host ns/op + MB/s is a RELATIVE baseline. Build + run:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_STOMP=1
//   performance_benching/services/stomp/host.cpp \
//       src/services/iot/stomp/stomp.cpp -o /tmp/bstomp && /tmp/bstomp

#define PC_ENABLE_STOMP 1
#include "services/iot/stomp/stomp.h"

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
    const char *body = "hello-from-pc-rig";
    const size_t blen = strlen(body);
    char frame[384];
    const char *bk[] = {"destination", "content-length"};
    const char *bv[] = {"/topic/pc", "20"};
    size_t flen = pc_stomp_build_frame(frame, sizeof(frame), "SEND", bk, bv, 2, body, blen);

    // A representative inbound MESSAGE frame (what a subscriber receives). Ends at the NUL.
    const char msg[] = "MESSAGE\ndestination:/topic/pc\nmessage-id:007\nsubscription:0\n"
                       "content-length:20\n\nhello-from-pc-rig";
    const size_t mlen = sizeof(msg); // include the terminating NUL that ends the STOMP body

    printf("| Feature      | Operation                |     ns/op |    MB/s |\n");
    printf("|--------------|--------------------------|-----------|---------|\n");

    // build a SEND frame (command + 2 escaped headers + body + NUL).
    {
        volatile size_t sink = 0;
        double ns = bench_ns(
            2000000, [&] { sink += pc_stomp_build_frame(frame, sizeof(frame), "SEND", bk, bv, 2, body, blen); });
        row("stomp", "build SEND frame", ns, (double)flen);
        (void)sink;
    }
    // parse an inbound MESSAGE frame (command + 4 headers + content-length body).
    {
        volatile int sink = 0;
        double ns = bench_ns(2000000, [&] {
            StompFrame f;
            size_t used = 0;
            sink += pc_stomp_parse_frame(msg, mlen, &f, &used) ? (int)used : 0;
        });
        row("stomp", "parse MESSAGE frame", ns, (double)mlen);
        (void)sink;
    }

    return 0;
}
