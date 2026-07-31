// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the IEC 61850 GOOSE publisher codec (Generic Object Oriented Substation Event,
// the raw-L2 multicast IEC 61850 uses for protection trips): the BER `IECGoosePdu` builder and the full
// Ethernet-frame wrap (dst/src/0x88B8 + 8-octet GOOSE header + PDU). GOOSE is publish-only here (no parser -
// so no fuzz-attack surface; a subscriber peer would need raw-L2 multicast HW), making this a bench-only
// codec. Pure (no socket), links standalone. Build + run:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_GOOSE=1 -DPC_ENABLE_RAWL2=1 \
//       performance_benching/services/goose/host.cpp src/services/energy/goose/goose.cpp -o /tmp/bg && /tmp/bg

#define PC_ENABLE_GOOSE 1
#define PC_ENABLE_RAWL2 1
#include "services/energy/goose/goose.h"

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
    printf("| %-7s | %-24s | %10.1f | %9.1f |\n", feature, op, ns_per_op, mbps);
}

int main()
{
    // A protection-trip GOOSE control block (two boolean dataset entries in allData: 83 01 00, 83 01 01).
    const uint8_t utc[8] = {0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a};
    const uint8_t all_data[] = {0x83, 0x01, 0x00, 0x83, 0x01, 0x01};
    pc_goose g = {};
    g.gocb_ref = "IED1LD0/LLN0$GO$gcb01";
    g.time_allowed_to_live = 2000;
    g.dat_set = "IED1LD0/LLN0$DataSet1";
    g.go_id = "IED1_GOOSE";
    g.t = utc;
    g.st_num = 1;
    g.sq_num = 0;
    g.simulation = false;
    g.conf_rev = 1;
    g.nds_com = false;
    g.num_entries = 2;
    g.all_data = all_data;
    g.all_data_len = sizeof(all_data);

    uint8_t pdu[256];
    size_t pdu_len = pc_goose_pdu(&g, pdu, sizeof(pdu));

    const uint8_t dst[6] = {0x01, 0x0c, 0xcd, 0x01, 0x00, 0x01}; // IEC 61850 GOOSE multicast
    const uint8_t src[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t frame[300];
    size_t frame_len = pc_goose_frame(dst, src, 0x0001, &g, frame, sizeof(frame));

    printf("| Feature | Operation                |     ns/op |    MB/s |\n");
    printf("|---------|--------------------------|-----------|---------|\n");

    // pc_goose_pdu: BER-encode the IECGoosePdu (11 control fields + the allData blob) - the publish core.
    {
        uint8_t buf[256];
        volatile size_t sink = 0;
        double ns = bench_ns(5000000, [&] { sink += pc_goose_pdu(&g, buf, sizeof(buf)); });
        row("goose", "goose_pdu (build)", ns, (double)pdu_len);
        (void)sink;
    }

    // pc_goose_frame: wrap the PDU in the Ethernet header + 8-octet GOOSE header (the full L2 datagram).
    {
        uint8_t buf[300];
        volatile size_t sink = 0;
        double ns = bench_ns(5000000, [&] { sink += pc_goose_frame(dst, src, 0x0001, &g, buf, sizeof(buf)); });
        row("goose", "goose_frame (build)", ns, (double)frame_len);
        (void)sink;
    }

    return 0;
}
