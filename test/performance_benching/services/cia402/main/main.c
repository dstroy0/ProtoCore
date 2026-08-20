// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the CiA 402 / IEC 61800-7-201 drive profile
// (services/fieldbus/cia402): the Statusword power-state decode (mask/value table), the Controlword
// enable-sequence step, the CANopen SDO Controlword-write build, the SDO Statusword-read decode,
// and the cyclic PDO command-pack / status-unpack - all pure value logic and codec calls over a
// stack-resident CanFrame (see shared/can/can.h). Same shape as performance_benching/device/modbus: a pure
// protocol/profile layer with no hardware involved, so every call here exercises the real
// production code path - there is no CAN transceiver to stub, and none is touched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/cia402 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/canopen/canopen.h"
#include "services/fieldbus/cia402/cia402.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t cia402_work[16]; // the borrow an entry takes; Cia402 never reads it

void dbench_run(void)
{
    // Statusword 0x0637: Operation Enabled (mask 0x6F == 0x27) plus remote + target-reached high
    // bits, per test_cia402.cpp's test_state_decode_ignores_high_bits / test_sdo_get_roundtrip.
    const uint16_t sw_op_enabled = 0x0637;

    // Crafted SDO server upload response (0x580 + node 7): expedited, 2 data octets (cmd 0x4B),
    // Statusword index 0x6041, value 0x0637 LE - mirrors test_cia402.cpp's test_sdo_get_roundtrip.
    CanFrame sdo_resp = {0};
    sdo_resp.id = 0x587u;
    sdo_resp.extended = false;
    sdo_resp.rtr = false;
    sdo_resp.dlc = 8;
    sdo_resp.data[0] = 0x4B;
    sdo_resp.data[1] = 0x41;
    sdo_resp.data[2] = 0x60;
    sdo_resp.data[3] = 0x00;
    sdo_resp.data[4] = 0x37;
    sdo_resp.data[5] = 0x06;
    sdo_resp.data[6] = 0x00;
    sdo_resp.data[7] = 0x00;

    // Cyclic TPDO payload = Statusword (u16 LE) + Actual (i32 LE), 6 octets, from
    // test_cia402.cpp's test_pdo_pack_unpack (status 0x0637, actual -12345).
    static const uint8_t tpdo[6] = {0x37, 0x06, 0xC7, 0xCF, 0xFF, 0xFF};
    static uint8_t pdo_out[6];
    static uint16_t sdo_val = 0;
    static uint16_t pdo_sw = 0;
    static int32_t pdo_actual = 0;

    for (;;)
    {
        DBENCH_BANNER("cia402");

        volatile uint8_t sink8 = 0;
        volatile uint16_t sink16 = 0;
        volatile size_t sinksz = 0;
        volatile bool sinkb = false;
        CanFrame f = {0};

        Cia402.state_args.statusword = sw_op_enabled;
        DBENCH_OP("Cia402.state", 200000, sink8 += (uint8_t)(Cia402.state(cia402_work), Cia402.value));
        Cia402.enable_sequence_args.state = CIA402_STATE_SWITCHED_ON;
        DBENCH_OP("Cia402.enable_sequence", 200000,
                  sink16 += (Cia402.enable_sequence(cia402_work), Cia402.u16));
        Cia402.sdo_set_controlword_args.out = &f;
        Cia402.sdo_set_controlword_args.node = 5;
        Cia402.sdo_set_controlword_args.controlword = 0x000F;
        DBENCH_OP("Cia402.sdo_set_controlword", 100000,
                  sinkb |= (Cia402.sdo_set_controlword(cia402_work), Cia402.ok));
        Cia402.sdo_get_u16_args.f = &sdo_resp;
        Cia402.sdo_get_u16_args.want_index = CIA402_OD_STATUSWORD;
        Cia402.sdo_get_u16_args.value = &sdo_val;
        DBENCH_OP("Cia402.sdo_get_u16", 100000,
                  sinkb |= (Cia402.sdo_get_u16(cia402_work), Cia402.ok));
        Cia402.pack_command_args.buf = pdo_out;
        Cia402.pack_command_args.cap = sizeof(pdo_out);
        Cia402.pack_command_args.controlword = 0x000F;
        Cia402.pack_command_args.target = -12345;
        DBENCH_BULK("Cia402.pack_command", 100000, 6,
                    sinksz += (Cia402.pack_command(cia402_work), Cia402.n));
        Cia402.unpack_status_args.buf = tpdo;
        Cia402.unpack_status_args.len = sizeof(tpdo);
        Cia402.unpack_status_args.statusword = &pdo_sw;
        Cia402.unpack_status_args.actual = &pdo_actual;
        DBENCH_BULK("Cia402.unpack_status", 100000, 6,
                    sinkb |= (Cia402.unpack_status(cia402_work), Cia402.ok));

        (void)sink8;
        (void)sink16;
        (void)sinksz;
        (void)sinkb;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("cia402")
