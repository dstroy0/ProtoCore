// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Omron Host Link (C-mode) frame codec (services/fieldbus/hostlink):
// the FCS (an 8-bit XOR over the ASCII body), the command builder (Hostlink.build: @UU + header +
// text + FCS + *CR), the FCS-validating parser (Hostlink.parse), and the response end-code reader
// (Hostlink.end_code). Every operation here is pure - no heap, no sockets, no UART - so this is
// like performance_benching/device/modbus, a pure protocol codec where each call exercises the real production code
// path. The RS-232/485 serial transport is the application's responsibility and is deliberately out
// of scope (nothing is wired to this rig); only the deterministic CPU-side framing is benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/hostlink -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/hostlink/hostlink.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t hostlink_work[16]; // the borrow an entry takes; Hostlink never reads it

void dbench_run(void)
{
    // Known-good, spec-conformant vectors straight out of test/test_hostlink/test_hostlink.cpp.
    // Body "@00RD00000010" has FCS 0x57; the DM-read frame is "@00RD0000001057*\r" (17 chars).
    static const char body[] = "@00RD00000010";
    const size_t body_len = sizeof(body) - 1; // exclude the NUL

    static char frame[32];
    Hostlink.build_args.buf = frame;
    Hostlink.build_args.cap = sizeof(frame);
    Hostlink.build_args.node = 0;
    Hostlink.build_args.header_code = "RD";
    Hostlink.build_args.text = "00000010";
    Hostlink.build_args.text_len = 8;
    Hostlink.build(hostlink_work);
    const size_t frame_len = Hostlink.n;

    static char outbuf[32];

    for (;;)
    {
        DBENCH_BANNER("hostlink");
        volatile uint32_t sink = 0;

        // FCS: 8-bit XOR over the ASCII body (throughput over the frame body bytes).
        Hostlink.fcs_args.data = body;
        Hostlink.fcs_args.len = body_len;
        DBENCH_BULK("Hostlink.fcs", 100000, body_len, sink += (Hostlink.fcs(hostlink_work), Hostlink.value));

        // Build a full DM-read command frame (@UU + header + text + FCS + *CR).
        Hostlink.build_args.buf = outbuf;
        Hostlink.build_args.cap = sizeof(outbuf);
        Hostlink.build_args.node = 0;
        Hostlink.build_args.header_code = "RD";
        Hostlink.build_args.text = "00000010";
        Hostlink.build_args.text_len = 8;
        DBENCH_OP("Hostlink.build RD", 100000,
                  sink += (Hostlink.build(hostlink_work), Hostlink.n));

        // Parse + FCS-validate a complete frame.
        HostlinkFrame f;
        Hostlink.parse_args.buf = frame;
        Hostlink.parse_args.len = frame_len;
        Hostlink.parse_args.out = &f;
        DBENCH_OP("Hostlink.parse", 100000, sink += (Hostlink.parse(hostlink_work), Hostlink.ok));

        // Read the response end code (first 2 text chars) off an already-parsed frame.
        Hostlink.parse_args.buf = frame;
        Hostlink.parse_args.len = frame_len;
        Hostlink.parse_args.out = &f;
        Hostlink.parse(hostlink_work);
        (void)Hostlink.ok;
        uint8_t code = 0;
        Hostlink.end_code_args.f = &f;
        Hostlink.end_code_args.code = &code;
        DBENCH_OP("Hostlink.end_code", 200000, sink += (Hostlink.end_code(hostlink_work), Hostlink.ok));

        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("hostlink")
