// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the LonWorks / LON-IP network-variable codec
// (services/fieldbus/lonworks): the LonTalk application PDU ([msg-code][14-bit selector][value]) build +
// parse, and the two most-common SNVT scalar encodings, SNVT_temp (0.01 K fixed-point) and
// SNVT_switch (0..100.5% level + state). All pure, zero heap, no stdlib - like performance_benching/device/modbus,
// this is a pure protocol codec with no hardware involved, so every call here exercises the real
// production code path. The LON/IP UDP transport is deliberately out of scope: this rig has no
// network attached, and the codec above is what a NetVar update actually costs on the CPU.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/lonworks -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/lonworks/lonworks.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t lonworks_work[16]; // the borrow an entry takes; Lonworks never reads it

void dbench_run(void)
{
    // Known-good, spec-conformant literals lifted from test/test_lonworks/test_lonworks.cpp:
    // an NV-update carrying a 2-byte SNVT value to 14-bit selector 0x1234.
    static const uint8_t nv_value[2] = {0xAB, 0xCD};
    // The wire PDU that protocore_lon_build_nv() produces for that update: the message bit and the
    // direction bit off LON_MSG_NV_UPDATE with selector bits 13..8, then bits 7..0, then the value.
    // 0x80 | (0x1234 >> 8) = 0x92.
    static const uint8_t nv_pdu[4] = {0x92, 0x34, 0xAB, 0xCD};
    static uint8_t out[16];
    // A SNVT_temp value encoding 25.0 C, and a SNVT_switch value encoding 50% / state 1.
    static const uint8_t snvt_temp_val[2] = {0x74, 0x77}; // (25 + 273.15)*100 = 29815 = 0x7477
    static const uint8_t snvt_switch_val[2] = {100, 1};   // 50% * 2 = 100, state 1

    for (;;)
    {
        DBENCH_BANNER("lonworks");
        volatile size_t sink = 0;
        volatile double sinkd = 0;

        LonworksV.build_nv_args.msg_code = LON_MSG_NV_UPDATE;
        LonworksV.build_nv_args.selector = 0x1234;
        LonworksV.build_nv_args.value = nv_value;
        LonworksV.build_nv_args.value_len = sizeof(nv_value);
        LonworksV.build_nv_args.out = out;
        LonworksV.build_nv_args.cap = sizeof(out);
        DBENCH_OP("Lonworks.build_nv (upd+2B)", 100000, sink += (Lonworks.build_nv(lonworks_work), LonworksV.n));

        LonNv nv;
        LonworksV.parse_nv_args.pdu = nv_pdu;
        LonworksV.parse_nv_args.len = sizeof(nv_pdu);
        LonworksV.parse_nv_args.out = &nv;
        DBENCH_OP("Lonworks.parse_nv", 200000, sink += (Lonworks.parse_nv(lonworks_work), LonworksV.ok) ? 1u : 0u);

        LonworksV.snvt_temp_encode_args.celsius = 25.0;
        LonworksV.snvt_temp_encode_args.out = out;
        DBENCH_OP("Lonworks.snvt_temp_encode", 100000, (Lonworks.snvt_temp_encode(lonworks_work), LonworksV.ok));
        LonworksV.snvt_temp_decode_args.in = snvt_temp_val;
        DBENCH_OP("Lonworks.snvt_temp_decode", 200000,
                  sinkd += (Lonworks.snvt_temp_decode(lonworks_work), LonworksV.value));

        LonworksV.snvt_switch_encode_args.percent = 50.0;
        LonworksV.snvt_switch_encode_args.state = 1;
        LonworksV.snvt_switch_encode_args.out = out;
        DBENCH_OP("Lonworks.snvt_switch_encode", 100000, (Lonworks.snvt_switch_encode(lonworks_work), LonworksV.ok));
        {
            double pct = 0;
            uint8_t st = 0;
            LonworksV.snvt_switch_decode_args.in = snvt_switch_val;
            LonworksV.snvt_switch_decode_args.percent = &pct;
            LonworksV.snvt_switch_decode_args.state = &st;
            DBENCH_OP("Lonworks.snvt_switch_decode", 200000,
                      (Lonworks.snvt_switch_decode(lonworks_work), LonworksV.ok));
            sinkd += pct + st;
        }

        (void)sink;
        (void)sinkd;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("lonworks")
