// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the EtherNet/IP (CIP encapsulation over TCP/44818) codec: the 24-octet
// encapsulation-header builder, the header parser (command/length/session validate + data slice - the
// fuzz-target receive op), and the RegisterSession builder (the session handshake). Pure (no socket), so it
// links standalone. The device figure comes from the rig /bench op; this host ns/op + MB/s is a RELATIVE
// baseline. Build + run:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_ENIP=1 performance_benching/services/enip/host.cpp
//   \
//       src/services/fieldbus/enip/enip.cpp -o /tmp/be && /tmp/be

#define PC_ENABLE_ENIP 1
#include "services/fieldbus/enip/enip.h"

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
    printf("| %-7s | %-26s | %10.1f | %9.1f |\n", feature, op, ns_per_op, mbps);
}

int main()
{
    // A SendRRData frame carrying an 8-octet CIP message (a typical unconnected explicit request).
    const uint8_t cip[8] = {0x54, 0x03, 0x20, 0x02, 0x24, 0x01, 0x00, 0x00};
    EipHeader h = {};
    h.command = EIP_CMD_SEND_RR_DATA;
    h.length = sizeof(cip);
    h.session_handle = 0x01020304;
    h.status = EIP_STATUS_SUCCESS;
    h.options = 0;
    uint8_t frame[64];
    size_t frame_len = pc_eip_build(frame, sizeof(frame), &h, cip, sizeof(cip));

    uint8_t rs[64];
    const uint8_t ctx[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    size_t rs_len = pc_eip_build_register_session(rs, sizeof(rs), ctx);

    printf("| Feature | Operation                  |     ns/op |    MB/s |\n");
    printf("|---------|----------------------------|-----------|---------|\n");

    // pc_eip_build: lay down the 24-octet encapsulation header + command data (the transmit op).
    {
        uint8_t buf[64];
        volatile size_t sink = 0;
        double ns = bench_ns(5000000, [&] { sink += pc_eip_build(buf, sizeof(buf), &h, cip, sizeof(cip)); });
        row("enip", "pc_eip_build (encap)", ns, (double)frame_len);
        (void)sink;
    }

    // pc_eip_parse: validate the encapsulation header (command/length/status) + slice the command data (receive
    // op; this is the parser the fuzz attack targets - a length lie must not over-read past the 24-octet header).
    {
        volatile size_t sink = 0;
        double ns = bench_ns(10000000, [&] {
            EipHeader out;
            const uint8_t *data = nullptr;
            size_t dlen = 0;
            if (pc_eip_parse(frame, frame_len, &out, &data, &dlen))
                sink += dlen + out.command;
        });
        row("enip", "pc_eip_parse (encap)", ns, (double)frame_len);
        (void)sink;
    }

    // pc_eip_build_register_session: the session-open handshake frame (fixed 28 octets).
    {
        uint8_t buf[64];
        volatile size_t sink = 0;
        double ns = bench_ns(5000000, [&] { sink += pc_eip_build_register_session(buf, sizeof(buf), ctx); });
        row("enip", "register_session (build)", ns, (double)rs_len);
        (void)sink;
    }

    return 0;
}
