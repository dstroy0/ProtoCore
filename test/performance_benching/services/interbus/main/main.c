// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the INTERBUS summation-frame codec (services/fieldbus/interbus):
// Interbus.build assembles a summation frame (loopback word + per-device 16-bit process-image
// slices + CRC-16/CCITT FCS) from a word list, Interbus.parse validates the loopback + FCS and
// disassembles it back into device words, and Interbus.fcs is the underlying CRC-16/CCITT-FALSE
// (poly 0x1021, init 0xFFFF) run bit-by-bit over the frame bytes. All three are pure (zero heap, no
// stdlib). Like performance_benching/device/modbus, this is a pure protocol codec with no hardware involved, so every
// call exercises the real production code path. The physical ring - the shift-register clocking that
// actually circulates the frame around the fieldbus - is hardware-gated and deliberately out of scope
// here: this rig has no INTERBUS transceiver attached, and only the deterministic summation-frame +
// process-image layer is benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/interbus -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/interbus/interbus.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t interbus_work[16]; // the borrow an entry takes; Interbus never reads it

void dbench_run(void)
{
    // A realistic process image: 8 device slices of two 16-bit words each (16 words total). The
    // literal word pattern mirrors the known-good slices exercised by test/test_interbus (0x1111,
    // 0x2222, 0x3333, ...), just widened to a fuller ring so the FCS covers a representative payload.
    static const uint16_t words[16] = {0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x6666, 0x7777, 0x8888,
                                       0x9999, 0xAAAA, 0xBBBB, 0xCCCC, 0xDDDD, 0xEEEE, 0xF0F0, 0x0F0F};
    static const size_t word_count = sizeof(words) / sizeof(words[0]);

    // Pre-build a valid frame once so parse() and fcs() bench against a real, spec-conformant frame.
    static uint8_t frame[2 + 16 * 2 + 2]; // loopback(2) + words(32) + FCS(2) = 36 bytes
    Interbus.build_args.words = words;
    Interbus.build_args.word_count = word_count;
    Interbus.build_args.out = frame;
    Interbus.build_args.cap = sizeof(frame);
    Interbus.build(interbus_work);
    size_t frame_len = Interbus.n;

    static uint8_t buf[64];  // build() output scratch
    static uint16_t out[32]; // parse() decoded-words scratch

    for (;;)
    {
        DBENCH_BANNER("interbus");
        volatile size_t sink = 0;
        volatile uint16_t sink16 = 0;

        // Assemble the summation frame (build words -> loopback + big-endian words + FCS).
        Interbus.build_args.words = words;
        Interbus.build_args.word_count = word_count;
        Interbus.build_args.out = buf;
        Interbus.build_args.cap = sizeof(buf);
        DBENCH_OP("Interbus.build x16w", 20000,
                  sink += (Interbus.build(interbus_work), Interbus.n));

        // Disassemble + validate a received frame (loopback + FCS check, then split into words).
        DBENCH_OP("Interbus.parse x16w", 20000, {
            size_t _cnt = 0;
            Interbus.parse_args.frame = frame;
            Interbus.parse_args.len = frame_len;
            Interbus.parse_args.out_words = out;
            Interbus.parse_args.max_words = 32;
            Interbus.parse_args.out_count = &_cnt;
            Interbus.parse(interbus_work);
            sink += Interbus.ok ? _cnt : 0;
        });

        // The raw CRC-16/CCITT FCS over the frame bytes - bulk throughput (MB/s) over the payload.
        Interbus.fcs_args.bytes = frame;
        Interbus.fcs_args.len = frame_len;
        DBENCH_BULK("Interbus.fcs", 20000, frame_len, sink16 += (Interbus.fcs(interbus_work), Interbus.value));

        (void)sink;
        (void)sink16;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("interbus")
