// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the HLK-LD2410 24 GHz mmWave radar codec (server/peripherals/ld2410):
// a pure protocol codec, so every call here exercises the real production code path (contrast with
// performance_benching/device/ads1115, a peripheral driver where the bus transaction is stubbed). What is benched:
//   - protocore_ld2410_parse_report over a basic (13B payload) and an engineering-mode (35B payload)
//     report frame - decode + full header/footer/length/marker validation;
//   - protocore_ld2410_stream_push, the byte-by-byte UART reassembler, driven over a whole basic frame so
//     DBENCH_BULK reports the per-byte reassembly throughput (MB/s) of the real serial hot path;
//   - protocore_ld2410_parse_ack decoding a get-MAC command-ACK frame;
//   - protocore_ld2410_cmd_get_mac building a config-command frame.
// Deliberately out of scope: the ESP32 UART binding (protocore_ld2410_begin/poll/last, which pump a
// HardwareSerial at 256000 baud) - this rig has no LD2410 wired to it, so only the deterministic
// CPU-side codec is ever benched. The sample frames are the byte-for-byte, spec-conformant vectors
// lifted straight from test/test_ld2410/test_ld2410.cpp (already known-good).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/ld2410 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/peripherals/ld2410/ld2410.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A basic (target) report frame: moving target, moving 250cm/80, static 300cm/40, detect 250cm.
static const uint8_t BASIC[] = {
    0xF4, 0xF3, 0xF2, 0xF1, // header
    0x0D, 0x00,             // length = 13
    0x02, 0xAA, 0x01,       // type basic, head, state = moving
    0xFA, 0x00, 0x50,       // moving 250cm, energy 80
    0x2C, 0x01, 0x28,       // static 300cm, energy 40
    0xFA, 0x00,             // detect 250cm
    0x55, 0x00,             // tail, check
    0xF8, 0xF7, 0xF6, 0xF5, // footer
};

// An engineering report frame: both targets, with per-gate energies and light/out.
static const uint8_t ENG[] = {
    0xF4, 0xF3, 0xF2, 0xF1,                     //
    0x23, 0x00,                                 // length = 35
    0x01, 0xAA, 0x03,                           // type engineering, head, state = both
    0x64, 0x00, 0x4B,                           // moving 100cm, energy 75
    0x96, 0x00, 0x3C,                           // static 150cm, energy 60
    0xC8, 0x00,                                 // detect 200cm
    0x08, 0x08,                                 // max moving gate 8, max static gate 8
    10,   20,   30,   40,   50, 60, 70, 80, 90, // moving gate energies (9)
    5,    15,   25,   35,   45, 55, 65, 75, 85, // static gate energies (9)
    0x80, 0x01,                                 // light 128, out pin 1
    0x55, 0x00,                                 // tail, check
    0xF8, 0xF7, 0xF6, 0xF5,                     //
};

// A get-MAC command-ACK frame (word 0x01A5, status 0, 6-octet MAC payload).
static const uint8_t MAC_ACK[] = {0xFD, 0xFC, 0xFB, 0xFA, 0x0A, 0x00, 0xA5, 0x01, 0x00, 0x00,
                                  0x8F, 0x27, 0x2E, 0xB8, 0x0F, 0x65, 0x04, 0x03, 0x02, 0x01};

// Drive the byte-by-byte stream reassembler over a whole frame; returns true if it completed one.
// protocore_ld2410_stream_push resets the stream itself on completion, so feeding a frame repeatedly is
// safe. Kept out-of-line so the loop body is a single expression for DBENCH_BULK.
static bool feed_frame(Ld2410Stream *s, const uint8_t *f, size_t n, Ld2410Report *out)
{
    bool got = false;
    for (size_t i = 0; i < n; i++)
    {
        if (protocore_ld2410_stream_push(s, f[i], out))
        {
            got = true;
        }
    }
    return got;
}

void dbench_run(void)
{
    static Ld2410Report rep;
    static Ld2410Ack ack;
    static Ld2410Stream stream;
    static uint8_t cmd[32];
    protocore_ld2410_stream_reset(&stream);

    for (;;)
    {
        DBENCH_BANNER("ld2410");
        volatile size_t sink = 0;

        DBENCH_OP("protocore_ld2410_parse_report basic", 100000, sink += protocore_ld2410_parse_report(BASIC, sizeof(BASIC), &rep));
        DBENCH_OP("protocore_ld2410_parse_report eng", 100000, sink += protocore_ld2410_parse_report(ENG, sizeof(ENG), &rep));
        // Per-byte UART reassembly throughput over a whole basic frame (reports ns/B, MB/s).
        DBENCH_BULK("protocore_ld2410_stream_push basic", 20000, sizeof(BASIC),
                    sink += feed_frame(&stream, BASIC, sizeof(BASIC), &rep));
        DBENCH_OP("protocore_ld2410_parse_ack mac", 100000, sink += protocore_ld2410_parse_ack(MAC_ACK, sizeof(MAC_ACK), &ack));
        DBENCH_OP("protocore_ld2410_cmd_get_mac build", 100000, sink += protocore_ld2410_cmd_get_mac(cmd, sizeof(cmd)));

        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("ld2410")
