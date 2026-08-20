// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the DShot ESC throttle codec (server/peripherals/dshot):
// protocore_dshot_encode()/protocore_dshot_decode() build and validate the 16-bit frame (11-bit value +
// telemetry bit + 4-bit nibble-xor CRC, including the bidirectional inverted-CRC variant), and
// protocore_dshot_bit_ns()/protocore_esc_pwm_ns() are the pure bit-timing / throttle->pulse-width math a
// driver needs to program the RMT peripheral or MCPWM. All four are pure integer math - no heap,
// no stdlib, no RMT/MCPWM touched. Worked example for performance_benching/device/<service>/: like
// performance_benching/device/modbus, this is a pure protocol codec with no hardware involved, so every call here
// exercises the real production code path; there is nothing to stub because dshot.cpp never
// references the RMT/MCPWM peripherals it is meant to feed - it only computes the numbers a
// caller would hand to them.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/dshot -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/peripherals/dshot/dshot.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t dshot_work[16]; // the borrow an entry takes; Dshot never reads it

void dbench_run(void)
{
    // Known-good vectors lifted straight from test/test_dshot/test_dshot.cpp.
    // value 1046, no telemetry, standard CRC -> 0x82C6.
    static const uint16_t kValue = 1046;
    // Frame 0x82C6 decodes (standard CRC) to value 1046, telemetry clear.
    static const uint16_t kFrameStd = 0x82C6;
    // Frame 0x82C9 decodes (bidirectional/inverted CRC) to value 1046.
    static const uint16_t kFrameBidir = 0x82C9;

    for (;;)
    {
        DBENCH_BANNER("dshot");

        volatile uint16_t sink16 = 0;
        volatile uint32_t sink32 = 0;
        volatile bool sinkb = false;

        // Every entry call stays inside DBENCH_OP so the timed loop measures the codec, not the
        // read that follows it. The args do not vary across iterations, so they are staged once.
        DshotV.encode_args.value11 = kValue;
        DshotV.encode_args.telemetry = PROTO_FALSE;
        DshotV.encode_args.bidirectional = PROTO_FALSE;
        DBENCH_OP("Dshot.encode std", 200000, (Dshot.encode(dshot_work), sink16 += DshotV.frame));
        DshotV.encode_args.telemetry = PROTO_TRUE;
        DshotV.encode_args.bidirectional = PROTO_TRUE;
        DBENCH_OP("Dshot.encode bidir", 200000, (Dshot.encode(dshot_work), sink16 += DshotV.frame));

        uint16_t val = 0;
        proto_bool tel = PROTO_FALSE;
        DshotV.decode_args.frame = kFrameStd;
        DshotV.decode_args.value11 = &val;
        DshotV.decode_args.telemetry = &tel;
        DshotV.decode_args.bidirectional = PROTO_FALSE;
        DBENCH_OP("Dshot.decode std", 200000, (Dshot.decode(dshot_work), sinkb |= DshotV.ok));
        DshotV.decode_args.frame = kFrameBidir;
        DshotV.decode_args.bidirectional = PROTO_TRUE;
        DBENCH_OP("Dshot.decode bidir", 200000, (Dshot.decode(dshot_work), sinkb |= DshotV.ok));

        DshotV.bit_ns_args.rate_kbit = 600;
        DshotV.bit_ns_args.bit = PROTO_TRUE;
        DBENCH_OP("Dshot.bit_ns", 200000, (Dshot.bit_ns(dshot_work), sink32 += DshotV.ns));
        DshotV.esc_pwm_ns_args.throttle_1000 = 500;
        DshotV.esc_pwm_ns_args.mode = PROTOCORE_ESC_ONESHOT125;
        DBENCH_OP("Dshot.esc_pwm_ns", 200000, (Dshot.esc_pwm_ns(dshot_work), sink32 += DshotV.ns));

        (void)sink16;
        (void)sink32;
        (void)sinkb;
        (void)val;
        (void)tel;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("dshot")
