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
//   pio run -d performance_benching/device/protobuf -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/iot/protobuf/protobuf.h"
#include <Arduino.h>

// Build the spec-vector 5-field message (uint64 / string / fixed32 / double / sint64) into `buf`;
// returns the encoded byte count (0 on overflow). Mirrors test_round_trip_reader.
static size_t pb_encode_sample(uint8_t *buf, size_t cap)
{
    PbWriter w;
    pc_pb_writer_init(&w, buf, cap);
    pc_pb_uint64(&w, 1, 150);
    pc_pb_string(&w, 2, "hi");
    pc_pb_fixed32(&w, 3, 0x01020304);
    pc_pb_double(&w, 4, 2.5);
    pc_pb_sint64(&w, 5, -1234567);
    return pc_pb_writer_finish(&w);
}

// Cursor-read every field in `buf`; returns the field count (the reader stops at end-of-buffer).
static size_t pb_decode_all(const uint8_t *buf, size_t len)
{
    size_t pos = 0;
    size_t count = 0;
    PbField f;
    while (pc_pb_read_field(buf, len, &pos, &f))
        count++;
    return count;
}

// Encode a single raw varint into `buf`; returns the byte count.
static size_t pb_write_one_varint(uint8_t *buf, size_t cap, uint64_t v)
{
    PbWriter w;
    pc_pb_writer_init(&w, buf, cap);
    pc_pb_write_varint(&w, v);
    return pc_pb_writer_finish(&w);
}

// Decode a single raw varint from `buf`; returns its value (0 on malformed).
static uint64_t pb_read_one_varint(const uint8_t *buf, size_t len)
{
    size_t pos = 0;
    uint64_t v = 0;
    pc_pb_read_varint(buf, len, &pos, &v);
    return v;
}

static void protobuf_bench_task(void *)
{
    static uint8_t enc[64]; // 5-field message encodes to ~25 bytes
    static uint8_t vbuf[16];
    // 300 -> 0xAC 0x02 (test_varint_and_overflow), a known-good 2-byte varint on the wire.
    static const uint8_t varint300[] = {0xAC, 0x02};

    // Warm build once so DBENCH_BULK can report ns/byte + MB/s against the real encoded length.
    const size_t enc_len = pb_encode_sample(enc, sizeof(enc));

    for (;;)
    {
        Serial.printf("DB ==== protobuf device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint64_t sink = 0;

        // Encode the whole 5-field message (writer: tag + varint + ZigZag + fixed32 + fixed64 + LEN).
        DBENCH_BULK("pc_pb encode 5-field msg", 50000, enc_len, sink += pb_encode_sample(enc, sizeof(enc)));
        // Cursor-parse the whole message back out (reader).
        DBENCH_BULK("pc_pb_read_field 5-field msg", 50000, enc_len, sink += pb_decode_all(enc, enc_len));
        // Cheap primitives on their own.
        DBENCH_OP("pc_pb_write_varint", 200000, sink += pb_write_one_varint(vbuf, sizeof(vbuf), 300));
        DBENCH_OP("pc_pb_read_varint", 200000, sink += pb_read_one_varint(varint300, sizeof(varint300)));
        DBENCH_OP("pc_pb_zigzag64", 200000, sink += (uint64_t)pc_pb_zigzag64((uint64_t)sink | 1));

        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: protobuf device microbench");
    xTaskCreatePinnedToCore(protobuf_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
