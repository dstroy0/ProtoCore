// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the DeviceNet link-adaptation codec (services/fieldbus/devicenet):
// the 4-group 11-bit CAN identifier encode/decode, the explicit-message header + fragmentation
// octets, a single-frame explicit-message build, and the fragmentation reassembler - all pure (no
// heap, no bus). Same shape as performance_benching/device/modbus: a pure protocol codec with no hardware involved,
// so every call here exercises the real production code path. The physical CAN transaction (ESP32
// TWAI peripheral or an MCP2515 over SPI) is explicitly out of scope - this rig has no CAN
// transceiver attached, and Devicenet.* never touches the bus itself, only the CanFrame struct
// (shared/can/can.h) and its own reassembly buffer.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/devicenet -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/devicenet/devicenet.h"
#include "shared/can/can.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t devicenet_work[16]; // the borrow an entry takes; Devicenet never reads it

void dbench_run(void)
{
    // Group 2 unconnected explicit request, mac 0x21 (test_devicenet.cpp: test_id_group2).
    const uint32_t decode_test_id = 0x400u | (0x21u << 3) | DEVICENET_G2_UNCONNECTED_EXPLICIT_REQ;
    // A tiny CIP get-attribute-ish body (test_devicenet.cpp: test_build_explicit_single_frame).
    const uint8_t cip[3] = {0x0E, 0x20, 0x01};

    // Three-frame fragmentation roundtrip, 14 octets of reassembled data
    // (test_devicenet.cpp: test_frag_reassembly_roundtrip).
    Devicenet.frag_octet_args.type = DEVICENET_FRAG_FIRST;
    Devicenet.frag_octet_args.count = 0;
    Devicenet.frag_octet(devicenet_work);
    const uint8_t f0[8] = {0x80 | 0x21, Devicenet.value, 1, 2, 3, 4, 5, 6};
    Devicenet.frag_octet_args.type = DEVICENET_FRAG_MIDDLE;
    Devicenet.frag_octet_args.count = 1;
    Devicenet.frag_octet(devicenet_work);
    const uint8_t f1[8] = {0x80 | 0x21, Devicenet.value, 7, 8, 9, 10, 11, 12};
    Devicenet.frag_octet_args.type = DEVICENET_FRAG_LAST;
    Devicenet.frag_octet_args.count = 2;
    Devicenet.frag_octet(devicenet_work);
    const uint8_t f2[4] = {0x80 | 0x21, Devicenet.value, 13, 14};

    for (;;)
    {
        DBENCH_BANNER("devicenet");

        volatile bool sinkb = false;
        volatile uint8_t sink8 = 0;
        uint32_t id = 0;
        DeviceNetId d;
        CanFrame frame;
        DeviceNetFragRx rx;

        Devicenet.encode_id_args.id = &id;
        Devicenet.encode_id_args.group = DEVICENET_GROUP_2;
        Devicenet.encode_id_args.msg_id = DEVICENET_G2_UNCONNECTED_EXPLICIT_REQ;
        Devicenet.encode_id_args.mac_id = 0x21;
        DBENCH_OP("Devicenet.encode_id", 100000,
                  sinkb |=
                  (Devicenet.encode_id(devicenet_work), Devicenet.ok));
        Devicenet.decode_id_args.can_id = decode_test_id;
        Devicenet.decode_id_args.out = &d;
        DBENCH_OP("Devicenet.decode_id", 100000, sinkb |= (Devicenet.decode_id(devicenet_work), Devicenet.ok));
        Devicenet.msg_header_args.frag = true;
        Devicenet.msg_header_args.xid = false;
        Devicenet.msg_header_args.mac_id = 0x21;
        DBENCH_OP("Devicenet.msg_header", 200000, sink8 += (Devicenet.msg_header(devicenet_work), Devicenet.value));
        Devicenet.frag_octet_args.type = DEVICENET_FRAG_LAST;
        Devicenet.frag_octet_args.count = 5;
        DBENCH_OP("Devicenet.frag_octet", 200000,
                  sink8 += (Devicenet.frag_octet(devicenet_work), Devicenet.value));
        Devicenet.build_explicit_args.out = &frame;
        Devicenet.build_explicit_args.group = DEVICENET_GROUP_2;
        Devicenet.build_explicit_args.msg_id = DEVICENET_G2_UNCONNECTED_EXPLICIT_REQ;
        Devicenet.build_explicit_args.mac_id = 0x21;
        Devicenet.build_explicit_args.body = cip;
        Devicenet.build_explicit_args.body_len = 3;
        DBENCH_OP("Devicenet.build_explicit", 100000,
                  sinkb |= (Devicenet.build_explicit(devicenet_work), Devicenet.ok));
        // 3-frame reassembly per iteration: reset + FIRST + MIDDLE + LAST, 14 octets of payload data.
        Devicenet.frag_reset_args.rx = &rx;
        Devicenet.frag_feed_args.rx = &rx;
        Devicenet.frag_feed_args.body = f0;
        Devicenet.frag_feed_args.body_len = sizeof(f0);
        Devicenet.frag_feed_args.rx = &rx;
        Devicenet.frag_feed_args.body = f1;
        Devicenet.frag_feed_args.body_len = sizeof(f1);
        Devicenet.frag_feed_args.rx = &rx;
        Devicenet.frag_feed_args.body = f2;
        Devicenet.frag_feed_args.body_len = sizeof(f2);
        DBENCH_BULK("Devicenet.frag_feed (3-frame reasm)", 50000, 14,
                    ((Devicenet.frag_reset(devicenet_work), Devicenet.ok), (Devicenet.frag_feed(devicenet_work), Devicenet.frag),
                     (Devicenet.frag_feed(devicenet_work), Devicenet.frag),
                     (Devicenet.frag_feed(devicenet_work), Devicenet.frag)));

        (void)sinkb;
        (void)sink8;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("devicenet")
