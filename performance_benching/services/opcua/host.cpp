// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the OPC UA Binary server codec (IEC 62541 / OPC UA Part 6): the UACP
// Hello/Acknowledge handshake (pc_opcua_parse_hello + pc_opcua_build_ack, run on every new connection) and the
// per-node DataValue Variant encode (pc_ua_w_datavalue, the Read-service hot op). All pure (no sockets, no
// heap); opcua_rx() (not benched here) references a few transport symbols, stubbed below. The device
// number comes from the rig /bench endpoint; this host ns/op + MB/s is a RELATIVE baseline. Build + run:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_OPCUA=1
//   performance_benching/services/opcua/host.cpp \
//       src/services/fieldbus/opcua/opcua.cpp -o /tmp/bo && /tmp/bo

#define PC_ENABLE_OPCUA 1
#include "network_drivers/transport/tcp.h" // TcpConn / conn_pool type (for the stubs)
#include "services/fieldbus/opcua/opcua.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

// opcua_rx() (not exercised) references these transport symbols; satisfy the linker with stubs so the
// pure codec benches without pulling in transport + lwIP.
TcpConn conn_pool[CONN_POOL_SLOTS];
bool pc_conn_send(uint8_t, const void *, u16_t)
{
    return true;
}
void pc_conn_flush(uint8_t)
{
}
void pc_conn_close(uint8_t)
{
}

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

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
}

// Build a UACP HEL message (Part 6 7.1.2): "HELF" + MessageSize + 5 sizes + EndpointUrl string.
static size_t build_hel(uint8_t *b, const char *url)
{
    int ul = (int)strlen(url);
    size_t total = 8 + 20 + 4 + (size_t)ul;
    memcpy(b, "HELF", 4);
    put_u32(b + 4, (uint32_t)total);
    put_u32(b + 8, 0);        // ProtocolVersion
    put_u32(b + 12, 65535);   // ReceiveBufferSize
    put_u32(b + 16, 65535);   // SendBufferSize
    put_u32(b + 20, 4 << 20); // MaxMessageSize (4 MiB)
    put_u32(b + 24, 5000);    // MaxChunkCount
    put_u32(b + 28, (uint32_t)ul);
    memcpy(b + 32, url, (size_t)ul);
    return total;
}

int main()
{
    uint8_t hel[128];
    size_t heln = build_hel(hel, "opc.tcp://192.168.1.29:4840");
    OpcUaHello hello;
    uint8_t ack[64];

    printf("| Feature      | Operation                |     ns/op |    MB/s |\n");
    printf("|--------------|--------------------------|-----------|---------|\n");

    // Parse HEL: the first thing every OPC UA connection does.
    {
        volatile int sink = 0;
        double ns = bench_ns(2000000, [&] { sink += pc_opcua_parse_hello(hel, heln, &hello) ? 1 : 0; });
        row("opcua", "parse HELLO", ns, (double)heln);
        (void)sink;
    }
    // Build ACK: negotiate the buffer sizes down to the server limit and emit the reply.
    pc_opcua_parse_hello(hel, heln, &hello);
    {
        volatile size_t sink = 0;
        double ns = bench_ns(2000000, [&] { sink += pc_opcua_build_ack(&hello, ack, sizeof(ack)); });
        size_t n = pc_opcua_build_ack(&hello, ack, sizeof(ack));
        row("opcua", "build ACK", ns, (double)n);
        (void)sink;
    }
    // Encode a scalar DataValue (Variant + status): the per-node Read-response hot op.
    {
        uint8_t out[64];
        OpcUaVariant v{};
        v.type = OpcUaVariantType::OPCUA_VAR_DOUBLE;
        v.f64 = 23.5;
        volatile size_t sink = 0;
        double ns = bench_ns(2000000, [&] {
            UaWriter w{out, sizeof(out), 0, true};
            pc_ua_w_datavalue(&w, &v, 0);
            sink += w.n;
        });
        UaWriter w{out, sizeof(out), 0, true};
        pc_ua_w_datavalue(&w, &v, 0);
        row("opcua", "encode DataValue (f64)", ns, (double)w.n);
        (void)sink;
    }

    return 0;
}
