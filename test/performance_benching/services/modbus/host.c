// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the Modbus TCP slave codec (Modbus Application Protocol):
// protocore_modbus_process_adu() takes a complete ADU (MBAP header + PDU) and produces the response ADU - parse
// the MBAP header, dispatch the function code against the data model, build the reply. Pure (no
// sockets, no heap); the PROTO_MODBUS rx handler (not benched) references a few transport symbols,
// stubbed below. The device number comes from the rig /bench endpoint; this host ns/op + MB/s is a
// relative baseline. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_MODBUS=1 test/performance_benching/services/modbus/host.c
//   src/services/fieldbus/modbus/modbus.c src/mmgr/protomem.c src/mmgr/protostr.c -o /tmp/bm && /tmp/bm

#define PROTOCORE_ENABLE_MODBUS 1
#include "network_drivers/transport/tcp/tcp.h" // TcpConn / conn_pool type (for the stubs)
#include "services/fieldbus/modbus/modbus.h"

#include "host_bench.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// The PROTO_MODBUS rx handler (not exercised) references these transport symbols; stub them so the pure
// codec benches without pulling in transport + lwIP.
TcpConn conn_pool[CONN_POOL_SLOTS];

bool protocore_conn_send(uint8_t slot, const void *data, uint16_t len)
{
    (void)slot;
    (void)data;
    (void)len;
    return true;
}

void protocore_conn_flush(uint8_t slot)
{
    (void)slot;
}

void protocore_conn_close(uint8_t slot)
{
    (void)slot;
}

int main(void)
{
    protocore_modbus_server_init();
    for (int i = 0; i < 16; i++)
    {
        protocore_modbus_set_holding_reg((uint16_t)i, (uint16_t)(0x1000 + i));
    }

    // Read Holding Registers (FC 0x03), 8 regs from addr 0: MBAP(txn,proto,len,unit) + PDU(fc,addr,qty).
    const uint8_t rd8[] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x00, 0x00, 0x08};
    // Write Multiple Registers (FC 0x10), 2 regs from addr 0.
    const uint8_t wr2[] = {0x00, 0x02, 0x00, 0x00, 0x00, 0x0B, 0x01, 0x10, 0x00,
                           0x00, 0x00, 0x02, 0x04, 0xAB, 0xCD, 0xEF, 0x01};

    uint8_t resp[260];

    hbench_header();

    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(1000000, sink += protocore_modbus_process_adu(rd8, sizeof(rd8), resp, sizeof(resp)), ns);
        hbench_row("modbus", "read holding x8 (FC3)", ns, (double)sizeof(rd8));
        (void)sink;
    }
    {
        volatile size_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(1000000, sink += protocore_modbus_process_adu(wr2, sizeof(wr2), resp, sizeof(resp)), ns);
        hbench_row("modbus", "write multi x2 (FC16)", ns, (double)sizeof(wr2));
        (void)sink;
    }

    return 0;
}
