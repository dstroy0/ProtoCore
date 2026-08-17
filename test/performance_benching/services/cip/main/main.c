// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the CIP message codec (services/fieldbus/cip): the EPATH
// logical-segment builder (8-bit and 16-bit segment forms), the Get_Attribute_Single /
// Set_Attribute_Single request builders, and the response parser (service / status /
// additional-status / data) - pure, no heap, no sockets. This is the CIP message that rides
// inside an EtherNet/IP Unconnected Data item (services/fieldbus/enip); the ENIP/socket transport is
// out of scope here, exactly as services/fieldbus/modbus's TCP transport is out of scope for
// performance_benching/device/modbus, the worked example for a pure protocol codec with no hardware involved.
// Sample bytes are taken straight from test/test_cip/test_cip.cpp (already known-good,
// Wireshark-verified).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/cip -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/cip/cip.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t cip_work[16]; // the borrow an entry takes; Cip never reads it

void dbench_run(void)
{
    // class 1 / instance 1 / attribute 7 -> all 8-bit logical segments.
    static uint8_t epath8[16];
    // class 0x0100 (>0xFF) / instance 1, no attribute -> 16-bit class segment + 8-bit instance segment.
    static uint8_t epath16[16];
    static uint8_t req[16];
    const uint8_t set_epath[] = {0x20, 0x01, 0x24, 0x01, 0x30, 0x07};
    const uint8_t set_data[] = {0xAB, 0xCD};

    // Get_Attribute_Single reply: service|0x80, reserved, status OK, no addl status, 4B data ("Acme").
    static const uint8_t resp_ok[] = {0x8E, 0x00, 0x00, 0x00, 'A', 'c', 'm', 'e'};
    // Reply with 1 word of additional status ahead of the data.
    static const uint8_t resp_addl[] = {0x8E, 0x00, 0x1F, 0x01, 0xAA, 0xBB, 0x12, 0x34};

    for (;;)
    {
        DBENCH_BANNER("cip");
        volatile size_t sink = 0;
        CipResponse r;

        Cip.build_epath_args.buf = epath8;
        Cip.build_epath_args.cap = sizeof(epath8);
        Cip.build_epath_args.class_id = 0x01;
        Cip.build_epath_args.instance_id = 0x01;
        Cip.build_epath_args.attribute_id = 0x07;
        Cip.build_epath_args.with_attribute = true;
        DBENCH_OP("Cip.build_epath 8bit", 100000,
                  sink += (Cip.build_epath(cip_work), Cip.n));
        Cip.build_epath_args.buf = epath16;
        Cip.build_epath_args.cap = sizeof(epath16);
        Cip.build_epath_args.class_id = 0x0100;
        Cip.build_epath_args.instance_id = 0x01;
        Cip.build_epath_args.attribute_id = 0;
        Cip.build_epath_args.with_attribute = false;
        DBENCH_OP("Cip.build_epath 16bit", 100000,
                  sink += (Cip.build_epath(cip_work), Cip.n));
        Cip.build_get_attr_single_args.buf = req;
        Cip.build_get_attr_single_args.cap = sizeof(req);
        Cip.build_get_attr_single_args.class_id = 0x01;
        Cip.build_get_attr_single_args.instance_id = 0x01;
        Cip.build_get_attr_single_args.attribute_id = 0x07;
        DBENCH_OP("Cip.build_get_attr_single", 100000,
                  sink += (Cip.build_get_attr_single(cip_work), Cip.n));
        Cip.build_request_args.buf = req;
        Cip.build_request_args.cap = sizeof(req);
        Cip.build_request_args.service = CIP_SC_SET_ATTR_SINGLE;
        Cip.build_request_args.epath = set_epath;
        Cip.build_request_args.epath_len = sizeof(set_epath);
        Cip.build_request_args.data = set_data;
        Cip.build_request_args.data_len = sizeof(set_data);
        DBENCH_OP("Cip.build_request (SET)", 100000,
                  sink += (Cip.build_request(cip_work), Cip.n));
        Cip.parse_response_args.buf = resp_ok;
        Cip.parse_response_args.len = sizeof(resp_ok);
        Cip.parse_response_args.out = &r;
        DBENCH_OP("Cip.parse_response ok", 100000,
                  sink += (Cip.parse_response(cip_work), Cip.ok) ? 1 : 0);
        Cip.parse_response_args.buf = resp_addl;
        Cip.parse_response_args.len = sizeof(resp_addl);
        Cip.parse_response_args.out = &r;
        DBENCH_OP("Cip.parse_response addl", 100000,
                  sink += (Cip.parse_response(cip_work), Cip.ok) ? 1 : 0);
        (void)sink;

        DBENCH_DONE();
    }
}

DBENCH_MAIN("cip")
