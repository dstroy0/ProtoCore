// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the PROFINET DCP codec (Discovery and Configuration Protocol, the raw-L2
// device-discovery/naming layer): the 10-octet DCP header builder, the header parser, and the block walker
// (`[option][suboption][blockLength][value]` TLVs - the fuzz-target receive op, where a block-length lie must
// not over-read). Pure (no socket), links standalone. The device figure comes from the rig /bench op; this
// host ns/op + MB/s is a RELATIVE baseline. Build + run:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_PROFINET=1
//   performance_benching/services/profinet/host.cpp \
//       src/services/fieldbus/profinet/profinet.cpp -o /tmp/bp && /tmp/bp

#define PC_ENABLE_PROFINET 1
#include "services/fieldbus/profinet/profinet.h"

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

static void walk_cb(uint8_t, uint8_t, const uint8_t *, size_t value_len, void *arg)
{
    *(size_t *)arg += value_len;
}

int main()
{
    // A DCP Identify response naming the station "et200sp" (a NameOfStation block).
    const char *name = "et200sp";
    uint8_t blocks[64];
    size_t blen = pc_pn_dcp_block(Pn::PN_DCP_OPT_DEVICE, Pn::PN_DCP_SUB_DEV_NAME_OF_STATION, (const uint8_t *)name,
                                  strlen(name), blocks, sizeof(blocks));
    uint8_t hdr[16];
    size_t hlen = pc_pn_dcp_header(Pn::PN_FRAMEID_DCP_IDENT_RES, Pn::PN_DCP_SERVICE_IDENTIFY,
                                   Pn::PN_DCP_TYPE_RESPONSE_SUCCESS, 0x12345678u, (uint16_t)blen, hdr, sizeof(hdr));

    printf("| Feature  | Operation                  |     ns/op |    MB/s |\n");
    printf("|----------|----------------------------|-----------|---------|\n");

    // pc_pn_dcp_header: build the 10-octet DCP header (frameID / service / xid / dataLength) - transmit op.
    {
        uint8_t buf[16];
        volatile size_t sink = 0;
        double ns = bench_ns(5000000, [&] {
            sink += pc_pn_dcp_header(Pn::PN_FRAMEID_DCP_IDENT_RES, Pn::PN_DCP_SERVICE_IDENTIFY,
                                     Pn::PN_DCP_TYPE_RESPONSE_SUCCESS, 0x12345678u, (uint16_t)blen, buf, sizeof(buf));
        });
        row("profinet", "dcp_header (build)", ns, (double)hlen);
        (void)sink;
    }

    // pc_pn_dcp_parse_header: validate + decode the 10-octet header (receive op).
    {
        volatile size_t sink = 0;
        double ns = bench_ns(10000000, [&] {
            PnDcpHeader out;
            if (pc_pn_dcp_parse_header(hdr, hlen, &out))
                sink += out.data_length + out.xid;
        });
        row("profinet", "dcp_parse_header", ns, (double)hlen);
        (void)sink;
    }

    // pc_pn_dcp_walk: walk the option/suboption/blockLength TLVs after the header (the fuzz-target parser;
    // a block-length lie must not over-read past the block buffer).
    {
        volatile size_t sink = 0;
        double ns = bench_ns(10000000, [&] {
            size_t acc = 0;
            if (pc_pn_dcp_walk(blocks, blen, walk_cb, &acc))
                sink += acc;
        });
        row("profinet", "dcp_walk (blocks)", ns, (double)blen);
        (void)sink;
    }

    return 0;
}
