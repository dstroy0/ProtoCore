// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the AMQP 0-9-1 frame codec (services/iot/amqp): the protocol
// header, the frame + method builders, the heartbeat builder, and the frame/method parsers - all
// pure (no heap, no sockets). Worked pattern for performance_benching/device/<service>/: a pure protocol codec
// with no hardware involved, so every call here exercises the real production code path (contrast
// with performance_benching/device/ads1115, a peripheral driver where the bus transaction itself is stubbed). The
// AMQP outbound client transport (the actual TCP socket a real broker connection rides on) is out
// of scope everywhere on this rig - only the deterministic CPU-side codec is ever benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/amqp -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/iot/amqp/amqp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // A Connection.Start-ish method (class 10, method 10) on channel 1 - mirrors
    // test/test_amqp/test_amqp.cpp::test_build_method_bytes.
    static const uint8_t method_args[] = {0x00};
    static uint8_t hdr_buf[8];
    static uint8_t method_buf[32];
    static uint8_t heartbeat_buf[8];

    // Pre-build a method frame once so the parsers have a real, known-good frame to chew on.
    Amqp.out.buf = method_buf;
    Amqp.out.cap = sizeof(method_buf);
    Amqp.frame.channel = 1;
    Amqp.method.class_id = 10;
    Amqp.method.method_id = 10;
    Amqp.method.args = method_args;
    Amqp.method.args_len = sizeof(method_args);
    Amqp.build_method(Amqp.internal);
    size_t method_len = Amqp.n;

    for (;;)
    {
        DBENCH_BANNER("amqp");
        volatile size_t sink = 0;

        Amqp.out.buf = hdr_buf;
        Amqp.out.cap = sizeof(hdr_buf);
        DBENCH_OP("Amqp.protocol_header", 200000, Amqp.protocol_header(Amqp.internal); sink += Amqp.n);

        Amqp.out.buf = method_buf;
        Amqp.out.cap = sizeof(method_buf);
        Amqp.frame.channel = 1;
        Amqp.method.class_id = 10;
        Amqp.method.method_id = 10;
        Amqp.method.args = method_args;
        Amqp.method.args_len = sizeof(method_args);
        DBENCH_OP("Amqp.build_method", 100000, Amqp.build_method(Amqp.internal); sink += Amqp.n);

        Amqp.out.buf = heartbeat_buf;
        Amqp.out.cap = sizeof(heartbeat_buf);
        DBENCH_OP("Amqp.build_heartbeat", 200000, Amqp.build_heartbeat(Amqp.internal); sink += Amqp.n);

        Amqp.in.buf = method_buf;
        Amqp.in.len = method_len;
        DBENCH_OP("Amqp.parse_frame", 100000, Amqp.parse_frame(Amqp.internal); sink += Amqp.ok ? Amqp.consumed : 0);

        DBENCH_OP("Amqp.parse_method", 100000, Amqp.parse_method(Amqp.internal); sink += Amqp.ok ? 1 : 0);

        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("amqp")
