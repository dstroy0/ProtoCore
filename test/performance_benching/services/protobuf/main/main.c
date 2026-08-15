// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Protocol Buffers wire codec (services/iot/protobuf): the
// zero-heap streaming writer (varint / ZigZag / fixed32 / fixed64 / length-delimited) encoding a
// mixed 5-field message into a caller buffer, and the cursor reader parsing that message back out -
// both pure (no heap, no sockets), so every call here exercises the real production code path. Like
// performance_benching/device/modbus, this is a pure protocol codec: there is no transport or peripheral to stub,
// the whole point of the service is deterministic in-buffer encode/decode. The gRPC framing (Protobuf
// over HTTP/2) is a separate roadmap item and is deliberately out of scope here.
//
// The sample message and its literal wire bytes are lifted straight from
// test/test_protobuf/test_protobuf.cpp (test_round_trip_reader / test_varint_and_overflow), so the
// data being encoded/decoded is already known-good and spec-conformant.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/protobuf -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/iot/protobuf/protobuf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The encoder and decoder rows this bench seats.
#define PB_WRITER_SLOT 0
#define PB_READER_SLOT 0

// Build the spec-vector 5-field message (uint64 / string / fixed32 / double / sint64) into `buf`;
// returns the encoded byte count (0 on overflow). Mirrors test_round_trip_reader.
static size_t pb_encode_sample(uint8_t *buf, size_t cap)
{
    Protobuf.slot = PB_WRITER_SLOT;
    Protobuf.writer.buf = buf;
    Protobuf.writer.cap = cap;
    Protobuf.writer_open(Protobuf.internal);

    Protobuf.tag.field_number = 1;
    Protobuf.value.u64 = 150;
    Protobuf.write_uint64(Protobuf.internal);

    Protobuf.tag.field_number = 2;
    Protobuf.value.text = "hi";
    Protobuf.write_string(Protobuf.internal);

    Protobuf.tag.field_number = 3;
    Protobuf.value.u32 = 0x01020304;
    Protobuf.write_fixed32(Protobuf.internal);

    Protobuf.tag.field_number = 4;
    Protobuf.value.f64 = 2.5;
    Protobuf.write_double(Protobuf.internal);

    Protobuf.tag.field_number = 5;
    Protobuf.value.i64 = -1234567;
    Protobuf.write_sint64(Protobuf.internal);

    Protobuf.writer_finish(Protobuf.internal);
    return Protobuf.n;
}

// Cursor-read every field in `buf`; returns the field count (the reader stops at end-of-buffer).
static size_t pb_decode_all(const uint8_t *buf, size_t len)
{
    Protobuf.slot = PB_READER_SLOT;
    Protobuf.source.buf = buf;
    Protobuf.source.len = len;
    Protobuf.source.pos = 0;
    Protobuf.reader_open(Protobuf.internal);

    size_t count = 0;
    for (;;)
    {
        Protobuf.read_record(Protobuf.internal);
        if (!Protobuf.ok)
        {
            return count;
        }
        count++;
    }
}

// Encode a single raw varint into `buf`; returns the byte count.
static size_t pb_write_one_varint(uint8_t *buf, size_t cap, uint64_t v)
{
    Protobuf.slot = PB_WRITER_SLOT;
    Protobuf.writer.buf = buf;
    Protobuf.writer.cap = cap;
    Protobuf.writer_open(Protobuf.internal);
    Protobuf.value.u64 = v;
    Protobuf.write_varint(Protobuf.internal);
    Protobuf.writer_finish(Protobuf.internal);
    return Protobuf.n;
}

// Decode a single raw varint from `buf`; returns its value (0 on malformed).
static uint64_t pb_read_one_varint(const uint8_t *buf, size_t len)
{
    Protobuf.slot = PB_READER_SLOT;
    Protobuf.source.buf = buf;
    Protobuf.source.len = len;
    Protobuf.source.pos = 0;
    Protobuf.reader_open(Protobuf.internal);
    Protobuf.read_varint(Protobuf.internal);
    return Protobuf.u64;
}

// The sint64 the ZigZag varint `v` stands for.
static int64_t pb_zigzag64(uint64_t v)
{
    Protobuf.value.u64 = v;
    Protobuf.zigzag64(Protobuf.internal);
    return Protobuf.i64;
}

void dbench_run(void)
{
    static uint8_t enc[64]; // 5-field message encodes to ~25 bytes
    static uint8_t vbuf[16];
    // 300 -> 0xAC 0x02 (test_varint_and_overflow), a known-good 2-byte varint on the wire.
    static const uint8_t varint300[] = {0xAC, 0x02};

    // Warm build once so DBENCH_BULK can report ns/byte + MB/s against the real encoded length.
    const size_t enc_len = pb_encode_sample(enc, sizeof(enc));

    for (;;)
    {
        DBENCH_BANNER("protobuf");
        volatile uint64_t sink = 0;

        // Encode the whole 5-field message (writer: tag + varint + ZigZag + fixed32 + fixed64 + LEN).
        DBENCH_BULK("Protobuf encode 5-field msg", 50000, enc_len, sink += pb_encode_sample(enc, sizeof(enc)));
        // Cursor-parse the whole message back out (reader).
        DBENCH_BULK("Protobuf.read_record 5-field msg", 50000, enc_len, sink += pb_decode_all(enc, enc_len));
        // Cheap primitives on their own.
        DBENCH_OP("Protobuf.write_varint", 200000, sink += pb_write_one_varint(vbuf, sizeof(vbuf), 300));
        DBENCH_OP("Protobuf.read_varint", 200000, sink += pb_read_one_varint(varint300, sizeof(varint300)));
        DBENCH_OP("Protobuf.zigzag64", 200000, sink += (uint64_t)pb_zigzag64((uint64_t)sink | 1));

        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("protobuf")
