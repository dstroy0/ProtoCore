// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the HART / HART-IP codec (services/fieldbus/hart): building and parsing
// the HART command frame - [delimiter][address...][command][byte-count][data...][checksum] with the
// longitudinal XOR check byte, short (1-byte) and long (5-byte unique-ID) addressing - plus the
// longitudinal XOR checksum itself and the 8-octet HART-IP message header. All pure, zero heap, no
// stdlib. A pure protocol codec with no hardware involved (like performance_benching/device/modbus), so every call
// here exercises the real production code path. The FSK physical layer (a HART modem IC over UART)
// and the HART-IP UDP/TCP transport are deliberately out of scope - this rig has no modem attached
// and does no network I/O; only the deterministic CPU-side wire codec is ever benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/hart -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/hart/hart.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // Command 0 (read unique id), STX, primary-master short address 0x80, no data -> [02 80 00 00 82].
    static const uint8_t addr_short = 0x80;

    // Long-address (5-byte unique-ID) STX request, command 0x03, no data (from test_build_long_address).
    static const uint8_t addr_long[5] = {0x86, 0x01, 0x02, 0x03, 0x04};

    // A known-good short-address frame carrying a 3-byte payload, built once so the parse/checksum
    // benches run over real, spec-conformant bytes (cmd 0x2A, data 11 22 33 - the roundtrip vector).
    static const uint8_t parse_data[3] = {0x11, 0x22, 0x33};
    static uint8_t parse_frame[16];
    size_t parse_len = protocore_hart_build(HART_DELIM_STX, &addr_short, 1, 0x2A, parse_data, sizeof(parse_data),
                                            parse_frame, sizeof(parse_frame));

    static uint8_t out[64];

    for (;;)
    {
        DBENCH_BANNER("hart");
        volatile size_t sink = 0;
        volatile uint8_t sink8 = 0;
        volatile bool sinkb = false;

        // Build a short-address command 0 frame (no data).
        DBENCH_OP("protocore_hart_build short cmd0", 100000,
                  sink += protocore_hart_build(HART_DELIM_STX, &addr_short, 1, 0x00, NULL, 0, out, sizeof(out)));

        // Build a long-address (5-byte) command 3 frame.
        DBENCH_OP("protocore_hart_build long addr", 100000,
                  sink += protocore_hart_build((uint8_t)(HART_DELIM_STX | HART_DELIM_LONG_ADDR), addr_long, 5, 0x03,
                                               NULL, 0, out, sizeof(out)));

        // Parse + checksum-verify a known-good short-address frame with data.
        HartFrame f;
        DBENCH_OP("protocore_hart_parse w/data", 100000, sinkb ^= protocore_hart_parse(parse_frame, parse_len, &f));

        // Longitudinal XOR checksum over the full frame's byte span (bulk throughput).
        DBENCH_BULK("protocore_hart_checksum", 100000, parse_len,
                    sink8 ^= protocore_hart_checksum(parse_frame, parse_len));

        // Build the 8-octet HART-IP message header.
        DBENCH_OP("protocore_hartip_build_header", 200000,
                  sink += protocore_hartip_build_header(HARTIP_MSG_REQUEST, HARTIP_ID_TOKEN_PDU, 0, 0x1234, 13, out,
                                                        sizeof(out)));

        (void)sink;
        (void)sink8;
        (void)sinkb;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("hart")
