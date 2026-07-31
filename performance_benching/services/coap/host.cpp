// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the CoAP server codec (RFC 7252): pc_coap_server_process() takes a full
// request datagram and produces the response datagram - parse header + options, reconstruct the
// Uri-Path, dispatch against the resource table, encode a piggybacked reply. Pure (no sockets, no
// heap), so it links standalone (the UDP transport symbols it references are stubbed below). The
// device number comes from the rig /bench endpoint; this host ns/op + MB/s is a RELATIVE baseline.
// Build + run (udp.cpp provides the host no-op UDP stubs coap.cpp's transport path references):
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_COAP=1 \
//       performance_benching/services/coap/host.cpp src/services/iot/coap/coap.cpp
//       src/network_drivers/transport/udp.cpp \ -o /tmp/bc && /tmp/bc

#define PC_ENABLE_COAP 1
#include "services/iot/coap/coap.h"

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

static void h_info(const CoapRequest *, CoapResponse *resp)
{
    static const char body[] = "{\"uptime_ms\":123456,\"free_heap\":204800}";
    size_t n = sizeof(body) - 1;
    if (n > resp->payload_cap)
    {
        n = resp->payload_cap;
    }
    memcpy(resp->payload, body, n);
    resp->payload_len = n;
    resp->content_format = CoapContentFormat::COAP_CF_JSON;
    resp->code = (uint8_t)CoapResponseCode::COAP_RSP_CONTENT;
}

int main()
{
    pc_coap_server_reset();
    pc_coap_server_add_resource("/info", CoapMethodMask::COAP_ALLOW_GET, h_info);
    pc_coap_server_add_resource("/a/b/c", CoapMethodMask::COAP_ALLOW_GET, h_info);

    // A CON GET /info: ver=1 type=CON tkl=4, code=0.01 GET, MID, 4-byte token, Uri-Path "info".
    const uint8_t get_info[] = {0x44, 0x01, 0x12, 0x34, 0xAA, 0xBB, 0xCC, 0xDD, 0xB4, 'i', 'n', 'f', 'o'};
    // A CON GET /a/b/c: three Uri-Path options (each delta 11, len 1).
    const uint8_t get_abc[] = {0x40, 0x01, 0x56, 0x78, 0xB1, 'a', 0x11, 'b', 0x11, 'c'};

    uint8_t resp[256];

    printf("| Feature      | Operation                |     ns/op |    MB/s |\n");
    printf("|--------------|--------------------------|-----------|---------|\n");

    {
        volatile size_t sink = 0;
        double ns =
            bench_ns(1000000, [&] { sink += pc_coap_server_process(get_info, sizeof(get_info), resp, sizeof(resp)); });
        row("coap", "process GET /info", ns, (double)sizeof(get_info));
        (void)sink;
    }
    {
        volatile size_t sink = 0;
        double ns =
            bench_ns(1000000, [&] { sink += pc_coap_server_process(get_abc, sizeof(get_abc), resp, sizeof(resp)); });
        row("coap", "process GET /a/b/c", ns, (double)sizeof(get_abc));
        (void)sink;
    }

    return 0;
}
