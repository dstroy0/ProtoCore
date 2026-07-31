// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the BACnet/IP codec (ASHRAE 135): the BVLC envelope (Annex J) + the NPDU
// network layer (Clause 6) - build + validate/slice. Pure (no socket), so it links standalone. The device
// figure comes from the rig /bench op; this host ns/op + MB/s is a RELATIVE baseline. Build + run:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_BACNET=1
//   performance_benching/services/bacnet/host.cpp \
//       src/services/fieldbus/bacnet/bacnet.cpp -o /tmp/bb && /tmp/bb

#define PC_ENABLE_BACNET 1
#include "services/fieldbus/bacnet/bacnet.h"

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
    printf("| %-8s | %-24s | %10.1f | %9.1f |\n", feature, op, ns_per_op, mbps);
}

int main()
{
    // An 8-octet APDU wrapped in an NPDU (with destination addressing + hop count) wrapped in a BVLC
    // Original-Unicast envelope - a realistic received BACnet/IP datagram.
    uint8_t apdu[8];
    for (int i = 0; i < 8; i++)
        apdu[i] = (uint8_t)(i * 9 + 2);
    const uint8_t dadr[2] = {0x01, 0x02};

    uint8_t npdu[64];
    size_t npdu_len =
        pc_npdu_build(npdu, sizeof(npdu), true, NPDU_PRIO_NORMAL, true, 100, dadr, 2, 255, apdu, sizeof(apdu));

    uint8_t frame[128];
    size_t frame_len = pc_bvlc_build(frame, sizeof(frame), BVLC_FUNC_ORIGINAL_UNICAST, npdu, npdu_len);

    printf("| Feature  | Operation                |     ns/op |    MB/s |\n");
    printf("|----------|--------------------------|-----------|---------|\n");

    // pc_bvlc_parse: validate the Annex-J envelope + slice out the NPDU - the first op on every datagram.
    {
        volatile size_t sink = 0;
        double ns = bench_ns(10000000, [&] {
            uint8_t fn = 0;
            const uint8_t *np = nullptr;
            size_t nl = 0;
            if (pc_bvlc_parse(frame, frame_len, &fn, &np, &nl))
                sink += nl;
        });
        row("bacnet", "pc_bvlc_parse", ns, (double)frame_len);
        (void)sink;
    }

    // pc_npdu_parse: validate version/control + walk the optional addressing + slice the APDU - the receive op.
    {
        volatile size_t sink = 0;
        double ns = bench_ns(10000000, [&] {
            NpduInfo info;
            if (pc_npdu_parse(npdu, npdu_len, &info))
                sink += info.apdu_len;
        });
        row("bacnet", "pc_npdu_parse", ns, (double)npdu_len);
        (void)sink;
    }

    // pc_npdu_build: frame an APDU with destination addressing + hop count - the transmit op.
    {
        volatile size_t sink = 0;
        double ns = bench_ns(5000000, [&] {
            sink +=
                pc_npdu_build(npdu, sizeof(npdu), true, NPDU_PRIO_NORMAL, true, 100, dadr, 2, 255, apdu, sizeof(apdu));
        });
        row("bacnet", "pc_npdu_build", ns, (double)npdu_len);
        (void)sink;
    }

    return 0;
}
