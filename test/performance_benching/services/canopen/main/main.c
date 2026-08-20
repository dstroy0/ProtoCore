// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the CANopen (CiA 301) message codec (services/fieldbus/canopen):
// building NMT/heartbeat/EMCY/SDO frames onto shared/can/can.h's CanFrame, classifying a
// received frame's COB-ID back to its function + node, and decoding EMCY / expedited SDO server
// responses - all pure (no TWAI/MCP2515 bus transaction, no heap). Worked example for
// performance_benching/device/<service>/: a pure protocol codec with no hardware involved, so every call here
// exercises the real production code path (contrast with performance_benching/device/ads1115, a peripheral driver
// where the bus transaction itself is stubbed). Sample frames are copied from
// test/test_canopen/test_canopen.cpp (already known-good, CiA 301 default-identifier-conformant).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/canopen -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/canopen/canopen.h"
#include "shared/can/can.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t canopen_work[16]; // the borrow an entry takes; Canopen never reads it

void dbench_run(void)
{
    // EMCY build sample (test_emcy_roundtrip): node 3, error code 0x8130, register 0x11.
    static const uint8_t msef[5] = {0xDE, 0xAD, 0xBE, 0xEF, 0x42};
    // SDO expedited write sample (test_sdo_write_expedited): object 0x6040/00, value 0x1234 LE.
    static const uint8_t sdo_val[2] = {0x34, 0x12};
    static CanFrame f;

    // Pre-built frame for the EMCY parse bench (same shape as test_emcy_roundtrip).
    static CanFrame emcy_frame;
    CanopenV.build_emcy_args.out = &emcy_frame;
    CanopenV.build_emcy_args.node_id = 3;
    CanopenV.build_emcy_args.error_code = 0x8130;
    CanopenV.build_emcy_args.error_reg = 0x11;
    CanopenV.build_emcy_args.msef = msef;
    Canopen.build_emcy(canopen_work);

    // COB-ID classifier bench frame: TPDO1 + node 10 (test_parse_all_function_codes).
    static CanFrame classify_frame;
    memset(&classify_frame, 0, sizeof(classify_frame));
    classify_frame.id = 0x18A;

    // Expedited SDO upload response sample (test_sdo_upload_response_expedited): node 0x20,
    // object 0x1018/01, value 0x029A little-endian.
    static CanFrame sdo_resp_frame;
    memset(&sdo_resp_frame, 0, sizeof(sdo_resp_frame));
    sdo_resp_frame.id = 0x580 + 0x20;
    sdo_resp_frame.dlc = 8;
    sdo_resp_frame.data[0] = 0x4B;
    sdo_resp_frame.data[1] = 0x18;
    sdo_resp_frame.data[2] = 0x10;
    sdo_resp_frame.data[3] = 0x01;
    sdo_resp_frame.data[4] = 0x9A;
    sdo_resp_frame.data[5] = 0x02;

    for (;;)
    {
        DBENCH_BANNER("canopen");
        volatile int sink = 0;
        CanopenMsg msg;
        uint8_t node = 0, reg = 0, out_msef[5];
        uint16_t code = 0;
        CanopenSdoResponse resp;

        CanopenV.build_heartbeat_args.out = &f;
        CanopenV.build_heartbeat_args.node_id = 10;
        CanopenV.build_heartbeat_args.state = CANOPEN_STATE_OPERATIONAL;
        DBENCH_OP("Canopen.build_heartbeat", 100000, sink += (int)(Canopen.build_heartbeat(canopen_work), CanopenV.ok));
        CanopenV.build_emcy_args.out = &f;
        CanopenV.build_emcy_args.node_id = 3;
        CanopenV.build_emcy_args.error_code = 0x8130;
        CanopenV.build_emcy_args.error_reg = 0x11;
        CanopenV.build_emcy_args.msef = msef;
        DBENCH_OP("Canopen.build_emcy", 100000, sink += (int)(Canopen.build_emcy(canopen_work), CanopenV.ok));
        CanopenV.build_sdo_write_args.out = &f;
        CanopenV.build_sdo_write_args.node_id = 5;
        CanopenV.build_sdo_write_args.index = 0x6040;
        CanopenV.build_sdo_write_args.sub = 0;
        CanopenV.build_sdo_write_args.data = sdo_val;
        CanopenV.build_sdo_write_args.len = 2;
        DBENCH_OP("Canopen.build_sdo_write", 100000, sink += (int)(Canopen.build_sdo_write(canopen_work), CanopenV.ok));
        CanopenV.parse_args.f = &classify_frame;
        CanopenV.parse_args.out = &msg;
        DBENCH_OP("Canopen.parse (classify)", 100000, sink += (int)(Canopen.parse(canopen_work), CanopenV.ok));
        CanopenV.parse_emcy_args.f = &emcy_frame;
        CanopenV.parse_emcy_args.node_id = &node;
        CanopenV.parse_emcy_args.error_code = &code;
        CanopenV.parse_emcy_args.error_reg = &reg;
        CanopenV.parse_emcy_args.msef = out_msef;
        DBENCH_OP("Canopen.parse_emcy", 100000, sink += (int)(Canopen.parse_emcy(canopen_work), CanopenV.ok));
        CanopenV.parse_sdo_response_args.f = &sdo_resp_frame;
        CanopenV.parse_sdo_response_args.out = &resp;
        DBENCH_OP("Canopen.parse_sdo_response", 100000,
                  sink += (int)(Canopen.parse_sdo_response(canopen_work), CanopenV.ok));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("canopen")
