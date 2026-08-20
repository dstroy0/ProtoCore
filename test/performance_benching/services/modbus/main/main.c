// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Modbus TCP slave codec (services/fieldbus/modbus):
// Modbus.process_adu takes a complete ADU (MBAP header + PDU), dispatches the function code
// against the data model, and builds the response - pure (no sockets, no heap). Worked example for
// performance_benching/device/<service>/: a pure protocol codec with no hardware involved, so every call here
// exercises the real production code path (contrast with performance_benching/device/ads1115, a peripheral driver
// where the bus transaction itself is stubbed).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/modbus -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/modbus/modbus/modbus.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    uint8_t *w = protocore_modbus_span();

    Modbus.server_init(w);
    for (int i = 0; i < 16; i++)
    {
        ModbusV.set_holding_reg_args.addr = (uint16_t)i;
        ModbusV.set_holding_reg_args.value = (uint16_t)(0x1000 + i);
        Modbus.set_holding_reg(w);
    }

    // Read Holding Registers (FC 0x03), 8 regs from addr 0: MBAP(txn,proto,len,unit) + PDU(fc,addr,qty).
    static const uint8_t rd8[] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x00, 0x00, 0x08};
    // Write Multiple Registers (FC 0x10), 2 regs from addr 0.
    static const uint8_t wr2[] = {0x00, 0x02, 0x00, 0x00, 0x00, 0x0B, 0x01, 0x10, 0x00,
                                  0x00, 0x00, 0x02, 0x04, 0xAB, 0xCD, 0xEF, 0x01};
    static uint8_t resp[260];

    for (;;)
    {
        DBENCH_BANNER("modbus");
        volatile size_t sink = 0;
        // The args do not vary across iterations, so they are staged once above the timed loop and
        // only the call and its result read sit inside it.
        ModbusV.process_adu_args.req = rd8;
        ModbusV.process_adu_args.req_len = sizeof(rd8);
        ModbusV.process_adu_args.resp = resp;
        ModbusV.process_adu_args.protocore_resp_cap = sizeof(resp);
        DBENCH_OP("Modbus.process_adu read x8 (FC3)", 20000, sink += (Modbus.process_adu(w), ModbusV.n));
        ModbusV.process_adu_args.req = wr2;
        ModbusV.process_adu_args.req_len = sizeof(wr2);
        ModbusV.process_adu_args.resp = resp;
        ModbusV.process_adu_args.protocore_resp_cap = sizeof(resp);
        DBENCH_OP("Modbus.process_adu write x2 (FC16)", 20000, sink += (Modbus.process_adu(w), ModbusV.n));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("modbus")
