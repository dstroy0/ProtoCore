// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the Modbus TCP slave codec (Modbus Application Protocol):
// pc_modbus_process_adu() takes a complete ADU (MBAP header + PDU) and produces the response ADU - parse
// the MBAP header, dispatch the function code against the data model, build the reply. Pure (no
// sockets, no heap); the PROTO_MODBUS rx handler (not benched) references a few transport symbols,
// stubbed below. The device number comes from the rig /bench endpoint; this host ns/op + MB/s is a
// RELATIVE baseline. Build + run:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_MODBUS=1
//   performance_benching/services/modbus/host.cpp \
//       src/services/fieldbus/modbus/modbus.cpp -o /tmp/bm && /tmp/bm

#define PC_ENABLE_MODBUS 1
#include "network_drivers/transport/tcp.h" // TcpConn / conn_pool type (for the stubs)
#include "services/fieldbus/modbus/modbus.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

// The PROTO_MODBUS rx handler (not exercised) references these transport symbols; stub them so the pure
// codec benches without pulling in transport + lwIP.
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

int main()
{
    pc_modbus_server_init();
    for (int i = 0; i < 16; i++)
    {
        pc_modbus_set_holding_reg((uint16_t)i, (uint16_t)(0x1000 + i));
    }

    // Read Holding Registers (FC 0x03), 8 regs from addr 0: MBAP(txn,proto,len,unit) + PDU(fc,addr,qty).
    const uint8_t rd8[] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x00, 0x00, 0x08};
    // Write Multiple Registers (FC 0x10), 2 regs from addr 0.
    const uint8_t wr2[] = {0x00, 0x02, 0x00, 0x00, 0x00, 0x0B, 0x01, 0x10, 0x00,
                           0x00, 0x00, 0x02, 0x04, 0xAB, 0xCD, 0xEF, 0x01};

    uint8_t resp[260];

    printf("| Feature      | Operation                |     ns/op |    MB/s |\n");
    printf("|--------------|--------------------------|-----------|---------|\n");

    {
        volatile size_t sink = 0;
        double ns = bench_ns(1000000, [&] { sink += pc_modbus_process_adu(rd8, sizeof(rd8), resp, sizeof(resp)); });
        row("modbus", "read holding x8 (FC3)", ns, (double)sizeof(rd8));
        (void)sink;
    }
    {
        volatile size_t sink = 0;
        double ns = bench_ns(1000000, [&] { sink += pc_modbus_process_adu(wr2, sizeof(wr2), resp, sizeof(resp)); });
        row("modbus", "write multi x2 (FC16)", ns, (double)sizeof(wr2));
        (void)sink;
    }

    return 0;
}
