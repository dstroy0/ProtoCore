// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the IEC 60870-5-104 codec (SCADA telecontrol over TCP): the APCI I-format
// builder + parser (68 LEN + 4 control octets) and the ASDU header parser (type/COT/common-address). Pure
// (no socket), so it links standalone. The device figure comes from the rig /bench op; this host ns/op +
// MB/s is a RELATIVE baseline. Build + run:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_IEC60870=1
//   performance_benching/services/iec60870/host.cpp \
//       src/services/energy/iec60870/iec60870.cpp -o /tmp/bi && /tmp/bi

#define PC_ENABLE_IEC60870 1
#include "services/energy/iec60870/iec60870.h"

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
    printf("| %-9s | %-24s | %10.1f | %9.1f |\n", feature, op, ns_per_op, mbps);
}

int main()
{
    // A spontaneous measured-value ASDU (type 9, one info object at IOA 100) inside a 104 I-frame - a
    // typical outstation report.
    IecAsduHeader ah = {};
    ah.type_id = IEC_TYPE_M_ME_NA_1;
    ah.count = 1;
    ah.cot = 3; // spontaneous
    ah.common_addr = 1;
    uint8_t asdu[32];
    size_t al = pc_iec_asdu_build_header(asdu, sizeof(asdu), &ah);
    al += pc_iec_put_ioa(asdu + al, sizeof(asdu) - al, 100);
    asdu[al++] = 0x12; // normalized value LSB
    asdu[al++] = 0x34; // normalized value MSB
    asdu[al++] = 0x00; // quality descriptor

    uint8_t frame[64];
    size_t frame_len = pc_iec104_build_i(frame, sizeof(frame), 0, 0, asdu, al);

    printf("| Feature   | Operation                |     ns/op |    MB/s |\n");
    printf("|-----------|--------------------------|-----------|---------|\n");

    // pc_iec104_build_i: frame an ASDU in an I-format APCI (start + length + Ns/Nr) - the transmit op.
    {
        uint8_t buf[64];
        volatile size_t sink = 0;
        double ns = bench_ns(5000000, [&] { sink += pc_iec104_build_i(buf, sizeof(buf), 0, 0, asdu, al); });
        row("iec60870", "build_i (104 I-frame)", ns, (double)frame_len);
        (void)sink;
    }

    // pc_iec104_parse: validate the APCI start/length + decode the I/S/U format and slice the ASDU - receive op.
    {
        volatile size_t sink = 0;
        double ns = bench_ns(10000000, [&] {
            Iec104Apci a;
            size_t used = 0;
            if (pc_iec104_parse(frame, frame_len, &a, &used))
                sink += a.asdu_len;
        });
        row("iec60870", "parse (104 APCI)", ns, (double)frame_len);
        (void)sink;
    }

    // pc_iec_asdu_parse_header: decode the 6-octet ASDU header (type / SQ / count / COT / common address).
    {
        volatile size_t sink = 0;
        double ns = bench_ns(10000000, [&] {
            IecAsduHeader h;
            size_t used = 0;
            if (pc_iec_asdu_parse_header(asdu, al, &h, &used))
                sink += used;
        });
        row("iec60870", "asdu_parse_header", ns, (double)al);
        (void)sink;
    }

    return 0;
}
