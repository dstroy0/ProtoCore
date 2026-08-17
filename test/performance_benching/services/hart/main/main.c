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

static uint8_t hart_work[16]; // the borrow an entry takes; Hart never reads it

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
    Hart.build_args.delimiter = HART_DELIM_STX;
    Hart.build_args.addr = &addr_short;
    Hart.build_args.addr_len = 1;
    Hart.build_args.command = 0x2A;
    Hart.build_args.data = parse_data;
    Hart.build_args.data_len = sizeof(parse_data);
    Hart.build_args.out = parse_frame;
    Hart.build_args.cap = sizeof(parse_frame);
    Hart.build(hart_work);
    size_t parse_len = Hart.n;

    static uint8_t out[64];

    for (;;)
    {
        DBENCH_BANNER("hart");
        volatile size_t sink = 0;
        volatile uint8_t sink8 = 0;
        volatile bool sinkb = false;

        // Build a short-address command 0 frame (no data).
        Hart.build_args.delimiter = HART_DELIM_STX;
        Hart.build_args.addr = &addr_short;
        Hart.build_args.addr_len = 1;
        Hart.build_args.command = 0x00;
        Hart.build_args.data = NULL;
        Hart.build_args.data_len = 0;
        Hart.build_args.out = out;
        Hart.build_args.cap = sizeof(out);
        DBENCH_OP("Hart.build short cmd0", 100000,
                  sink += (Hart.build(hart_work), Hart.n));

        // Build a long-address (5-byte) command 3 frame.
        Hart.build_args.delimiter = (uint8_t)(HART_DELIM_STX | HART_DELIM_LONG_ADDR);
        Hart.build_args.addr = addr_long;
        Hart.build_args.addr_len = 5;
        Hart.build_args.command = 0x03;
        Hart.build_args.data = NULL;
        Hart.build_args.data_len = 0;
        Hart.build_args.out = out;
        Hart.build_args.cap = sizeof(out);
        DBENCH_OP("Hart.build long addr", 100000,
                  sink += (Hart.build(hart_work), Hart.n));

        // Parse + checksum-verify a known-good short-address frame with data.
        HartFrame f;
        Hart.parse_args.frame = parse_frame;
        Hart.parse_args.len = parse_len;
        Hart.parse_args.out = &f;
        DBENCH_OP("Hart.parse w/data", 100000, sinkb ^= (Hart.parse(hart_work), Hart.ok));

        // Longitudinal XOR checksum over the full frame's byte span (bulk throughput).
        Hart.checksum_args.bytes = parse_frame;
        Hart.checksum_args.len = parse_len;
        DBENCH_BULK("Hart.checksum", 100000, parse_len,
                    sink8 ^= (Hart.checksum(hart_work), Hart.value));

        // Build the 8-octet HART-IP message header.
        Hart.ip_build_header_args.msg_type = HARTIP_MSG_REQUEST;
        Hart.ip_build_header_args.msg_id = HARTIP_ID_TOKEN_PDU;
        Hart.ip_build_header_args.status = 0;
        Hart.ip_build_header_args.seq = 0x1234;
        Hart.ip_build_header_args.total_len = 13;
        Hart.ip_build_header_args.out = out;
        Hart.ip_build_header_args.cap = sizeof(out);
        DBENCH_OP("Hart.ip_build_header", 200000,
                  sink += (Hart.ip_build_header(hart_work), Hart.n));

        (void)sink;
        (void)sink8;
        (void)sinkb;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("hart")
