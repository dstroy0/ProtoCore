// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
#include "services/fieldbus/cia402/cia402.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

        DBENCH_OP("protocore_cia402_state", 200000, sink8 += (uint8_t)protocore_cia402_state(sw_op_enabled));
        DBENCH_OP("protocore_cia402_enable_sequence", 200000,
                  sink16 += protocore_cia402_enable_sequence(CIA402_STATE_SWITCHED_ON));
        DBENCH_OP("protocore_cia402_sdo_set_controlword", 100000,
                  sinkb |= protocore_cia402_sdo_set_controlword(&f, 5, 0x000F));
        DBENCH_OP("protocore_cia402_sdo_get_u16", 100000,
                  sinkb |= protocore_cia402_sdo_get_u16(&sdo_resp, CIA402_OD_STATUSWORD, &sdo_val));
        DBENCH_BULK("protocore_cia402_pack_command", 100000, 6,
                    sinksz += protocore_cia402_pack_command(pdo_out, sizeof(pdo_out), 0x000F, -12345));
        DBENCH_BULK("protocore_cia402_unpack_status", 100000, 6,
                    sinkb |= protocore_cia402_unpack_status(tpdo, sizeof(tpdo), &pdo_sw, &pdo_actual));

        (void)sink8;
        (void)sink16;
        (void)sinksz;
        (void)sinkb;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("cia402")
