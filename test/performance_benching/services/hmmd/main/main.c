// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Waveshare HMMD 24GHz mmWave micro-motion radar codec
// (server/peripherals/hmmd): the LD2410-family little-endian framing. Everything benched here is the pure,
// deterministic CPU-side codec -
//   * protocore_hmmd_parse_report  - decode a whole 45-octet report frame (detect flag, distance, all 16
//                              gate energies), validating header/footer/length (bulk, so MB/s too);
//   * protocore_hmmd_stream_push   - drive one full frame, octet-by-octet, through the resyncing stream
//                              reassembler (reset + 45 pushes per op);
//   * protocore_hmmd_cmd_open      - build a full FD FC FB FA .. 04 03 02 01 command frame;
//   * protocore_hmmd_parse_ack     - decode one command-ACK frame.
// The UART half (protocore_hmmd_begin/poll/last, Serial2 on real hardware) and the module's bare GPIO OUT
// presence line are deliberately OUT OF SCOPE: this rig has no HMMD radar wired up, so no real UART
// transaction is ever issued - only the codec that runs on captured bytes is timed.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/hmmd -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/peripherals/hmmd/hmmd.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Build one spec-conformant report frame in place (mirrors test/test_hmmd build_report): a target
// detected at `dist` cm with gate energies gate0..gate0+15. len = 35 = detect(1)+dist(2)+16*2.
static void hmmd_build_report(uint8_t *f, uint8_t detect, uint16_t dist, uint16_t gate0)
{
    memset(f, 0, PROTOCORE_HMMD_FRAME_MAX);
    f[0] = 0xF4;
    f[1] = 0xF3;
    f[2] = 0xF2;
    f[3] = 0xF1;
    f[4] = (uint8_t)(PROTOCORE_HMMD_REPORT_LEN & 0xFF);
    f[5] = (uint8_t)(PROTOCORE_HMMD_REPORT_LEN >> 8);
    f[6] = detect;
    f[7] = (uint8_t)(dist & 0xFF);
    f[8] = (uint8_t)(dist >> 8);
    for (int i = 0; i < PROTOCORE_HMMD_GATES; i++)
    {
        uint16_t e = (uint16_t)(gate0 + i);
        f[9 + 2 * i] = (uint8_t)(e & 0xFF);
        f[10 + 2 * i] = (uint8_t)(e >> 8);
    }
    f[41] = 0xF8;
    f[42] = 0xF7;
    f[43] = 0xF6;
    f[44] = 0xF5;
}

// Drive one whole frame through the byte-by-byte reassembler; returns frames completed (0 or 1).
static int hmmd_reassemble(const uint8_t *frame, HmmdReport *out)
{
    HmmdStream s;
    Hmmd.stream_reset_args.s = &s;
    Hmmd.stream_reset(protocore_hmmd_span());
    int n = 0;
    for (int i = 0; i < PROTOCORE_HMMD_FRAME_MAX; i++)
    {
        Hmmd.stream_push_args.s = &s;
        Hmmd.stream_push_args.byte = frame[i];
        Hmmd.stream_push_args.out = out;
        Hmmd.stream_push(protocore_hmmd_span());
        if (Hmmd.ok)
        {
            n++;
        }
    }
    return n;
}

void dbench_run(void)
{
    // A known-good report frame (target at 137 cm, gate energies 100..115), same as test_hmmd.
    static uint8_t REPORT[PROTOCORE_HMMD_FRAME_MAX];
    hmmd_build_report(REPORT, 0x01, 137, 100);

    // A known-good command-ACK frame (reply to read-config: word 0x0108, two data octets), from
    // test/test_hmmd/test_hmmd.cpp - already spec-conformant.
    static const uint8_t ACK[14] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0x08, 0x01, 0xAB, 0xCD, 0x04, 0x03, 0x02, 0x01};
    static uint8_t cmd[32];

    HmmdReport r;
    HmmdAck a;

    for (;;)
    {
        DBENCH_BANNER("hmmd");
        volatile size_t sink = 0;
        volatile bool bsink = false;

        // parse a whole report frame (header/footer/length checks + 16 gate energies) - bulk over 45B
        Hmmd.parse_report_args.frame = REPORT;
        Hmmd.parse_report_args.len = sizeof(REPORT);
        Hmmd.parse_report_args.out = &r;
        // Each entry call stays inside its DBENCH_OP / DBENCH_BULK so the timed loop measures the
        // codec, not the read that follows it. Args that do not vary are staged once, above.
        DBENCH_BULK("Hmmd.parse_report", 100000, sizeof(REPORT),
                    (Hmmd.parse_report(protocore_hmmd_span()), bsink ^= Hmmd.ok));
        // reassemble one full frame octet-by-octet through the resyncing stream (reset + 45 pushes)
        DBENCH_OP("Hmmd.stream_push x45", 20000, sink += hmmd_reassemble(REPORT, &r));
        // build a full open-command-mode frame (FD FC FB FA .. 04 03 02 01)
        Hmmd.cmd_open_args.buf = cmd;
        Hmmd.cmd_open_args.cap = sizeof(cmd);
        DBENCH_OP("Hmmd.cmd_open", 100000, (Hmmd.cmd_open(protocore_hmmd_span()), sink += Hmmd.n));
        // decode one command-ACK frame
        Hmmd.parse_ack_args.frame = ACK;
        Hmmd.parse_ack_args.len = sizeof(ACK);
        Hmmd.parse_ack_args.out = &a;
        DBENCH_OP("Hmmd.parse_ack", 100000, (Hmmd.parse_ack(protocore_hmmd_span()), bsink ^= Hmmd.ok));

        (void)sink;
        (void)bsink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("hmmd")
