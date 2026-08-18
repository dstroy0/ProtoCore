// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for HTTP/3 framing (network_drivers/presentation/http/http3/h3_frame.h).
//
// An HTTP/3 frame header is two QUIC varints (RFC 9114 sec 7.1), so its correctness is the varint
// coding's correctness. RFC 9000 Appendix A.1 publishes four decoded sample sequences, and
// test_rfc9000_sample_varints_as_frame_lengths is the load-bearing case: it drives each of them
// through the frame header as a Length and requires the exact octets the RFC prints. The frame type
// and settings identifier values come from the IANA tables RFC 9114 sec 11.2.1 and sec 11.2.2
// register, and the settings defaults from sec 7.2.4.1 and RFC 9204 sec 5.

#include "network_drivers/presentation/http/http3/h3_frame.h"
#include <string.h>

#include <unity.h>

static uint8_t h3_frame_work[16]; // the borrow an entry takes; H3Frame never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static uint8_t g_out[64];

// RFC 9000 Appendix A.1: "the eight-byte sequence 0xc2197c5eff14e88c decodes to the decimal value
// 151,288,809,941,952,652; the four-byte sequence 0x9d7f3e7d decodes to 494,878,333; the two-byte
// sequence 0x7bbd decodes to 15,293; and the single byte 0x25 decodes to 37".
//
// Each is used here as a DATA frame's Length, so the frame header is 0x00 followed by that sequence.
void test_rfc9000_sample_varints_as_frame_lengths(void)
{
    struct
    {
        uint64_t length;
        const uint8_t *bytes;
        size_t n;
    } static const CASES[] = {
        {37u, (const uint8_t *)"\x25", 1},
        {15293u, (const uint8_t *)"\x7b\xbd", 2},
        {494878333u, (const uint8_t *)"\x9d\x7f\x3e\x7d", 4},
        {151288809941952652ULL, (const uint8_t *)"\xc2\x19\x7c\x5e\xff\x14\xe8\x8c", 8},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        H3Frame.write_header_args.out = g_out;
        H3Frame.write_header_args.cap = sizeof(g_out);
        H3Frame.write_header_args.type = H3_DATA;
        H3Frame.write_header_args.length = CASES[i].length;
        H3Frame.write_header(h3_frame_work);
        size_t n = H3Frame.n;
        TEST_ASSERT_EQUAL_UINT(1u + CASES[i].n, n);
        TEST_ASSERT_EQUAL_HEX8(0x00, g_out[0]);
        TEST_ASSERT_EQUAL_MEMORY(CASES[i].bytes, g_out + 1, CASES[i].n);

        H3FrameHeader f;
        H3Frame.parse_header_args.buf = g_out;
        H3Frame.parse_header_args.len = n;
        H3Frame.parse_header_args.out = &f;
        H3Frame.parse_header(h3_frame_work);
        TEST_ASSERT_TRUE(H3Frame.ok);
        TEST_ASSERT_EQUAL_HEX64(H3_DATA, f.type);
        TEST_ASSERT_EQUAL_HEX64(CASES[i].length, f.length);
        TEST_ASSERT_EQUAL_UINT(n, f.header_len);
    }
}

// RFC 9000 sec 16: a decoder takes the length from the first byte's two most significant bits, so
// 0x4025 is a legal, longer spelling of the value 37 that the encoder never chooses. Appendix A.1
// names both. Accepting the long form while emitting the short one is what the two directions owe.
void test_rfc9000_long_spelling_decodes_but_is_not_emitted(void)
{
    static const uint8_t LONG_FORM[3] = {0x00, 0x40, 0x25};
    H3FrameHeader f;
    H3Frame.parse_header_args.buf = LONG_FORM;
    H3Frame.parse_header_args.len = sizeof(LONG_FORM);
    H3Frame.parse_header_args.out = &f;
    H3Frame.parse_header(h3_frame_work);
    TEST_ASSERT_TRUE(H3Frame.ok);
    TEST_ASSERT_EQUAL_HEX64(37u, f.length);
    TEST_ASSERT_EQUAL_UINT(3u, f.header_len);

    H3Frame.write_header_args.out = g_out;
    H3Frame.write_header_args.cap = sizeof(g_out);
    H3Frame.write_header_args.type = H3_DATA;
    H3Frame.write_header_args.length = 37;
    H3Frame.write_header(h3_frame_work);
    TEST_ASSERT_EQUAL_UINT(2u, H3Frame.n);
    TEST_ASSERT_EQUAL_HEX8(0x25, g_out[1]);
}

// RFC 9114 sec 11.2.1 Table 2 registers the frame types this document defines.
void test_rfc9114_frame_type_registry(void)
{
    TEST_ASSERT_EQUAL_HEX64(0x00u, (uint64_t)H3_DATA);
    TEST_ASSERT_EQUAL_HEX64(0x01u, (uint64_t)H3_HEADERS);
    TEST_ASSERT_EQUAL_HEX64(0x03u, (uint64_t)H3_CANCEL_PUSH);
    TEST_ASSERT_EQUAL_HEX64(0x04u, (uint64_t)H3_SETTINGS);
    TEST_ASSERT_EQUAL_HEX64(0x05u, (uint64_t)H3_PUSH_PROMISE);
    TEST_ASSERT_EQUAL_HEX64(0x07u, (uint64_t)H3_GOAWAY);
    TEST_ASSERT_EQUAL_HEX64(0x0du, (uint64_t)H3_MAX_PUSH_ID);
}

// Table 2 marks 0x02, 0x06, 0x08 and 0x09 "Reserved" - the HTTP/2 frame types with no HTTP/3
// meaning. sec 7.2.8: "their receipt MUST be treated as a connection error of type
// H3_FRAME_UNEXPECTED". Everything Table 2 assigns is not reserved, and neither is a greased type.
void test_rfc9114_reserved_http2_frame_types(void)
{
    static const uint64_t RESERVED[4] = {0x02, 0x06, 0x08, 0x09};
    static const uint64_t ASSIGNED[7] = {0x00, 0x01, 0x03, 0x04, 0x05, 0x07, 0x0d};
    for (size_t i = 0; i < 4; i++)
    {
        H3Frame.type_reserved_args.type = RESERVED[i];
        H3Frame.type_reserved(h3_frame_work);
        TEST_ASSERT_TRUE(H3Frame.ok);
    }
    for (size_t i = 0; i < 7; i++)
    {
        H3Frame.type_reserved_args.type = ASSIGNED[i];
        H3Frame.type_reserved(h3_frame_work);
        TEST_ASSERT_FALSE(H3Frame.ok);
    }
    // sec 7.2.8 grease: 0x1f * N + 0x21, here N = 0 and N = 1
    H3Frame.type_reserved_args.type = 0x21;
    H3Frame.type_reserved(h3_frame_work);
    TEST_ASSERT_FALSE(H3Frame.ok);
    H3Frame.type_reserved_args.type = 0x40;
    H3Frame.type_reserved(h3_frame_work);
    TEST_ASSERT_FALSE(H3Frame.ok);
}

// RFC 9114 sec 7.2.4.1: SETTINGS_MAX_FIELD_SECTION_SIZE (0x06) defaults to unlimited, which this
// module spells as the all-ones 64-bit value. RFC 9204 sec 5: SETTINGS_QPACK_MAX_TABLE_CAPACITY
// (0x01) and SETTINGS_QPACK_BLOCKED_STREAMS (0x07) both default to zero.
void test_rfc9114_settings_defaults(void)
{
    H3Settings s;
    H3Frame.settings_defaults_args.s = &s;
    H3Frame.settings_defaults(h3_frame_work);
    TEST_ASSERT_EQUAL_HEX64(0u, s.qpack_max_table_capacity);
    TEST_ASSERT_EQUAL_HEX64(0xFFFFFFFFFFFFFFFFULL, s.max_field_section_size);
    TEST_ASSERT_EQUAL_HEX64(0u, s.qpack_blocked_streams);

    TEST_ASSERT_EQUAL_HEX64(0x01u, (uint64_t)H3_SETTINGS_QPACK_MAX_TABLE_CAPACITY);
    TEST_ASSERT_EQUAL_HEX64(0x06u, (uint64_t)H3_SETTINGS_MAX_FIELD_SECTION_SIZE);
    TEST_ASSERT_EQUAL_HEX64(0x07u, (uint64_t)H3_SETTINGS_QPACK_BLOCKED_STREAMS);
}

// sec 7.2.4: a SETTINGS payload is a sequence of identifier / value varint pairs. What the builder
// writes, the parser reads, and a greased identifier the parser does not know is ignored rather than
// refused (sec 7.2.4.1: "Endpoints MUST NOT consider such settings to have any meaning").
void test_rfc9114_settings_round_trip(void)
{
    static const uint64_t IDS[4] = {H3_SETTINGS_QPACK_MAX_TABLE_CAPACITY, H3_SETTINGS_MAX_FIELD_SECTION_SIZE,
                                    H3_SETTINGS_QPACK_BLOCKED_STREAMS, 0x21};
    static const uint64_t VALS[4] = {4096u, 16384u, 100u, 0u};
    // varint widths: 1+2 (id 0x01, 4096) + 1+4 (id 0x06, 16384 exceeds the 14-bit form)
    //              + 1+2 (id 0x07, 100)  + 1+1 (id 0x21, 0)  = 13 payload octets
    H3Frame.build_settings_args.out = g_out;
    H3Frame.build_settings_args.cap = sizeof(g_out);
    H3Frame.build_settings_args.ids = IDS;
    H3Frame.build_settings_args.vals = VALS;
    H3Frame.build_settings_args.n = 4;
    H3Frame.build_settings(h3_frame_work);
    size_t n = H3Frame.n;
    TEST_ASSERT_EQUAL_UINT(2u + 13u, n);

    H3FrameHeader f;
    H3Frame.parse_header_args.buf = g_out;
    H3Frame.parse_header_args.len = n;
    H3Frame.parse_header_args.out = &f;
    H3Frame.parse_header(h3_frame_work);
    TEST_ASSERT_TRUE(H3Frame.ok);
    TEST_ASSERT_EQUAL_HEX64(H3_SETTINGS, f.type);
    TEST_ASSERT_EQUAL_HEX64(13u, f.length);
    TEST_ASSERT_EQUAL_UINT(2u, f.header_len);

    H3Settings s;
    H3Frame.settings_defaults_args.s = &s;
    H3Frame.settings_defaults(h3_frame_work);
    H3Frame.parse_settings_args.payload = g_out + f.header_len;
    H3Frame.parse_settings_args.len = (size_t)f.length;
    H3Frame.parse_settings_args.s = &s;
    H3Frame.parse_settings(h3_frame_work);
    TEST_ASSERT_TRUE(H3Frame.ok);
    TEST_ASSERT_EQUAL_HEX64(4096u, s.qpack_max_table_capacity);
    TEST_ASSERT_EQUAL_HEX64(16384u, s.max_field_section_size);
    TEST_ASSERT_EQUAL_HEX64(100u, s.qpack_blocked_streams);
}

// RFC 9114 sec 11.2.2 Table 3 marks 0x02, 0x03, 0x04 and 0x05 "Reserved", and sec 7.2.4.1 says
// their receipt MUST be a connection error of type H3_SETTINGS_ERROR.
void test_rfc9114_reserved_settings_identifiers(void)
{
    for (uint8_t id = 0x02; id <= 0x05; id++)
    {
        uint8_t pay[2] = {id, 0x00};
        H3Settings s;
        H3Frame.settings_defaults_args.s = &s;
        H3Frame.settings_defaults(h3_frame_work);
        H3Frame.parse_settings_args.payload = pay;
        H3Frame.parse_settings_args.len = sizeof(pay);
        H3Frame.parse_settings_args.s = &s;
        H3Frame.parse_settings(h3_frame_work);
        TEST_ASSERT_FALSE(H3Frame.ok);
    }
    // an empty SETTINGS payload is legal and changes nothing
    H3Settings s;
    H3Frame.settings_defaults_args.s = &s;
    H3Frame.settings_defaults(h3_frame_work);
    H3Frame.parse_settings_args.payload = g_out;
    H3Frame.parse_settings_args.len = 0;
    H3Frame.parse_settings_args.s = &s;
    H3Frame.parse_settings(h3_frame_work);
    TEST_ASSERT_TRUE(H3Frame.ok);
    TEST_ASSERT_EQUAL_HEX64(0xFFFFFFFFFFFFFFFFULL, s.max_field_section_size);
}

// A payload that stops between an identifier and its value is malformed, not half applied.
void test_rfc9114_settings_truncated_pair(void)
{
    static const uint8_t ODD[1] = {H3_SETTINGS_MAX_FIELD_SECTION_SIZE}; // an id with no value
    H3Settings s;
    H3Frame.settings_defaults_args.s = &s;
    H3Frame.settings_defaults(h3_frame_work);
    H3Frame.parse_settings_args.payload = ODD;
    H3Frame.parse_settings_args.len = sizeof(ODD);
    H3Frame.parse_settings_args.s = &s;
    H3Frame.parse_settings(h3_frame_work);
    TEST_ASSERT_FALSE(H3Frame.ok);

    static const uint8_t SHORT_VAL[2] = {H3_SETTINGS_MAX_FIELD_SECTION_SIZE, 0x40}; // 2-byte value, 1 present
    H3Frame.settings_defaults_args.s = &s;
    H3Frame.settings_defaults(h3_frame_work);
    H3Frame.parse_settings_args.payload = SHORT_VAL;
    H3Frame.parse_settings_args.len = sizeof(SHORT_VAL);
    H3Frame.parse_settings_args.s = &s;
    H3Frame.parse_settings(h3_frame_work);
    TEST_ASSERT_FALSE(H3Frame.ok);
}

// sec 7.2.1 / 7.2.2: DATA (0x00) and HEADERS (0x01) each wrap their payload behind the type and a
// Length that counts exactly the payload octets.
void test_rfc9114_data_and_headers_builders(void)
{
    static const uint8_t BODY[5] = {'h', 'e', 'l', 'l', 'o'};
    H3FrameHeader f;

    H3Frame.build_data_args.out = g_out;
    H3Frame.build_data_args.cap = sizeof(g_out);
    H3Frame.build_data_args.data = BODY;
    H3Frame.build_data_args.len = sizeof(BODY);
    H3Frame.build_data(h3_frame_work);
    size_t n = H3Frame.n;
    TEST_ASSERT_EQUAL_UINT(2u + 5u, n);
    H3Frame.parse_header_args.buf = g_out;
    H3Frame.parse_header_args.len = n;
    H3Frame.parse_header_args.out = &f;
    H3Frame.parse_header(h3_frame_work);
    TEST_ASSERT_TRUE(H3Frame.ok);
    TEST_ASSERT_EQUAL_HEX64(H3_DATA, f.type);
    TEST_ASSERT_EQUAL_HEX64(5u, f.length);
    TEST_ASSERT_EQUAL_MEMORY(BODY, g_out + f.header_len, 5);

    H3Frame.build_headers_args.out = g_out;
    H3Frame.build_headers_args.cap = sizeof(g_out);
    H3Frame.build_headers_args.block = BODY;
    H3Frame.build_headers_args.len = sizeof(BODY);
    H3Frame.build_headers(h3_frame_work);
    n = H3Frame.n;
    TEST_ASSERT_EQUAL_UINT(2u + 5u, n);
    H3Frame.parse_header_args.buf = g_out;
    H3Frame.parse_header_args.len = n;
    H3Frame.parse_header_args.out = &f;
    H3Frame.parse_header(h3_frame_work);
    TEST_ASSERT_TRUE(H3Frame.ok);
    TEST_ASSERT_EQUAL_HEX64(H3_HEADERS, f.type);
    TEST_ASSERT_EQUAL_HEX64(5u, f.length);

    // a zero-length DATA frame is the two header varints and nothing else
    H3Frame.build_data_args.out = g_out;
    H3Frame.build_data_args.cap = sizeof(g_out);
    H3Frame.build_data_args.data = NULL;
    H3Frame.build_data_args.len = 0;
    H3Frame.build_data(h3_frame_work);
    n = H3Frame.n;
    TEST_ASSERT_EQUAL_UINT(2u, n);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[1]);
}

// sec 7.2.6: GOAWAY (0x07) carries one varint, so its Length is that varint's own width.
void test_rfc9114_goaway_builder(void)
{
    H3Frame.build_goaway_args.out = g_out;
    H3Frame.build_goaway_args.cap = sizeof(g_out);
    H3Frame.build_goaway_args.stream_id = 15293u;
    H3Frame.build_goaway(h3_frame_work);
    size_t n = H3Frame.n;
    TEST_ASSERT_EQUAL_UINT(2u + 2u, n);
    TEST_ASSERT_EQUAL_HEX8(0x07, g_out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, g_out[1]); // Length: the stream id encodes to two octets
    TEST_ASSERT_EQUAL_HEX8(0x7b, g_out[2]);
    TEST_ASSERT_EQUAL_HEX8(0xbd, g_out[3]);

    H3FrameHeader f;
    H3Frame.parse_header_args.buf = g_out;
    H3Frame.parse_header_args.len = n;
    H3Frame.parse_header_args.out = &f;
    H3Frame.parse_header(h3_frame_work);
    TEST_ASSERT_TRUE(H3Frame.ok);
    TEST_ASSERT_EQUAL_HEX64(H3_GOAWAY, f.type);
    TEST_ASSERT_EQUAL_HEX64(2u, f.length);
}

// A header cut short of the width its first byte announces is refused, never completed from
// whatever follows the buffer.
void test_truncated_header_is_refused(void)
{
    H3FrameHeader f;
    static const uint8_t TYPE_ONLY[1] = {0x00};       // a type with no length
    static const uint8_t SHORT_LEN[2] = {0x00, 0x40}; // length announces 2 octets, 1 present
    static const uint8_t SHORT_TYPE[1] = {0x9d};      // type announces 4 octets, 1 present
    H3Frame.parse_header_args.buf = TYPE_ONLY;
    H3Frame.parse_header_args.len = sizeof(TYPE_ONLY);
    H3Frame.parse_header_args.out = &f;
    H3Frame.parse_header(h3_frame_work);
    TEST_ASSERT_FALSE(H3Frame.ok);
    H3Frame.parse_header_args.buf = SHORT_LEN;
    H3Frame.parse_header_args.len = sizeof(SHORT_LEN);
    H3Frame.parse_header_args.out = &f;
    H3Frame.parse_header(h3_frame_work);
    TEST_ASSERT_FALSE(H3Frame.ok);
    H3Frame.parse_header_args.buf = SHORT_TYPE;
    H3Frame.parse_header_args.len = sizeof(SHORT_TYPE);
    H3Frame.parse_header_args.out = &f;
    H3Frame.parse_header(h3_frame_work);
    TEST_ASSERT_FALSE(H3Frame.ok);
    H3Frame.parse_header_args.buf = TYPE_ONLY;
    H3Frame.parse_header_args.len = 0;
    H3Frame.parse_header_args.out = &f;
    H3Frame.parse_header(h3_frame_work);
    TEST_ASSERT_FALSE(H3Frame.ok);
}

// A destination too small for the whole frame yields 0 rather than a frame the peer cannot finish
// reading.
void test_builders_refuse_a_short_destination(void)
{
    static const uint8_t BODY[5] = {'h', 'e', 'l', 'l', 'o'};
    static const uint64_t IDS[1] = {H3_SETTINGS_MAX_FIELD_SECTION_SIZE};
    static const uint64_t VALS[1] = {16384u};
    H3Frame.write_header_args.out = g_out;
    H3Frame.write_header_args.cap = 0;
    H3Frame.write_header_args.type = H3_DATA;
    H3Frame.write_header_args.length = 5;
    H3Frame.write_header(h3_frame_work);
    TEST_ASSERT_EQUAL_UINT(0u, H3Frame.n);
    H3Frame.write_header_args.out = g_out;
    H3Frame.write_header_args.cap = 1;
    H3Frame.write_header_args.type = H3_DATA;
    H3Frame.write_header_args.length = 15293;
    H3Frame.write_header(h3_frame_work);
    TEST_ASSERT_EQUAL_UINT(0u, H3Frame.n);
    H3Frame.build_data_args.out = g_out;
    H3Frame.build_data_args.cap = 6;
    H3Frame.build_data_args.data = BODY;
    H3Frame.build_data_args.len = sizeof(BODY);
    H3Frame.build_data(h3_frame_work);
    TEST_ASSERT_EQUAL_UINT(0u, H3Frame.n);
    H3Frame.build_headers_args.out = g_out;
    H3Frame.build_headers_args.cap = 6;
    H3Frame.build_headers_args.block = BODY;
    H3Frame.build_headers_args.len = sizeof(BODY);
    H3Frame.build_headers(h3_frame_work);
    TEST_ASSERT_EQUAL_UINT(0u, H3Frame.n);
    H3Frame.build_settings_args.out = g_out;
    H3Frame.build_settings_args.cap = 4;
    H3Frame.build_settings_args.ids = IDS;
    H3Frame.build_settings_args.vals = VALS;
    H3Frame.build_settings_args.n = 1;
    H3Frame.build_settings(h3_frame_work);
    TEST_ASSERT_EQUAL_UINT(0u, H3Frame.n);
    H3Frame.build_goaway_args.out = g_out;
    H3Frame.build_goaway_args.cap = 2;
    H3Frame.build_goaway_args.stream_id = 15293u;
    H3Frame.build_goaway(h3_frame_work);
    TEST_ASSERT_EQUAL_UINT(0u, H3Frame.n);
}
