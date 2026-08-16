// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for HTTP/2 binary framing
// (network_drivers/presentation/http/http2/h2_frame.h).
//
// RFC 9113 sec 3.4 prints the client connection preface as 24 octets in hex notation, and sec 4.1
// fixes the 9-octet frame header as Length(24) Type(8) Flags(8) Reserved(1) Stream Identifier(31).
// test_rfc9113_preface_octets and test_rfc9113_frame_header_layout are the load-bearing pair: the
// first is a byte string the RFC itself publishes, the second places every header field at the
// offset the figure gives it, so a field written one byte over is a failure rather than a frame the
// peer silently rejects. Every SETTINGS bound below is the initial value or the legal range that
// sec 6.5.2 states in words.

#include "network_drivers/presentation/http/http2/h2_frame.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static uint8_t g_out[64];

// RFC 9113 sec 3.4: "The client connection preface starts with a sequence of 24 octets, which in hex
// notation is: 0x505249202a20485454502f322e300d0a0d0a534d0d0a0d0a".
void test_rfc9113_preface_octets(void)
{
    static const uint8_t WANT[24] = {0x50, 0x52, 0x49, 0x20, 0x2a, 0x20, 0x48, 0x54, 0x54, 0x50, 0x2f, 0x32,
                                     0x2e, 0x30, 0x0d, 0x0a, 0x0d, 0x0a, 0x53, 0x4d, 0x0d, 0x0a, 0x0d, 0x0a};
    TEST_ASSERT_EQUAL_UINT(24u, (unsigned)H2_PREFACE_LEN);
    TEST_ASSERT_EQUAL_UINT(24u, strlen(H2_PREFACE));
    TEST_ASSERT_EQUAL_MEMORY(WANT, H2_PREFACE, 24);
}

// RFC 9113 sec 4.1: Length(24), Type(8), Flags(8), Reserved(1), Stream Identifier(31), in that order
// and big-endian. The values chosen here make the nine header octets read 01..09, so a field at the
// wrong offset moves a byte that is easy to name.
void test_rfc9113_frame_header_layout(void)
{
    static const uint8_t WANT[9] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
    TEST_ASSERT_EQUAL_UINT(9u, (H2Frame.write_args.buf = g_out, H2Frame.write_args.cap = sizeof(g_out),
                                H2Frame.write_args.length = 0x010203u, H2Frame.write_args.type = 0x04,
                                H2Frame.write_args.flags = 0x05, H2Frame.write_args.stream_id = 0x06070809u,
                                H2Frame.write_header(NULL), H2Frame.n));
    TEST_ASSERT_EQUAL_MEMORY(WANT, g_out, 9);

    H2FrameHeader h;
    TEST_ASSERT_TRUE((H2Frame.parse_args.buf = WANT, H2Frame.parse_args.len = sizeof(WANT), H2Frame.parse_header(NULL),
                      *(&h) = H2Frame.header, H2Frame.ok));
    TEST_ASSERT_EQUAL_HEX32(0x010203u, h.length);
    TEST_ASSERT_EQUAL_HEX8(0x04, h.type);
    TEST_ASSERT_EQUAL_HEX8(0x05, h.flags);
    TEST_ASSERT_EQUAL_HEX32(0x06070809u, h.stream_id);
}

// sec 4.1: the reserved bit "MUST remain unset (0x00) when sending and MUST be ignored when
// receiving", so it is masked out of a written id and off a read one.
void test_rfc9113_reserved_bit(void)
{
    static const uint8_t WITH_R[9] = {0, 0, 0, H2_DATA, 0, 0x80, 0x00, 0x00, 0x01};
    H2FrameHeader h;
    TEST_ASSERT_TRUE((H2Frame.parse_args.buf = WITH_R, H2Frame.parse_args.len = sizeof(WITH_R),
                      H2Frame.parse_header(NULL), *(&h) = H2Frame.header, H2Frame.ok));
    TEST_ASSERT_EQUAL_HEX32(1u, h.stream_id);

    TEST_ASSERT_EQUAL_UINT(9u, (H2Frame.write_args.buf = g_out, H2Frame.write_args.cap = sizeof(g_out),
                                H2Frame.write_args.length = 0, H2Frame.write_args.type = H2_DATA,
                                H2Frame.write_args.flags = 0, H2Frame.write_args.stream_id = 0x80000001u,
                                H2Frame.write_header(NULL), H2Frame.n));
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[5]);
    TEST_ASSERT_EQUAL_HEX8(0x01, g_out[8]);
}

// sec 4.1: Length is 24 bits, so 2^24-1 is the largest it can carry and anything wider has no
// representation. A destination shorter than the fixed header holds none of it.
void test_rfc9113_length_is_24_bits(void)
{
    TEST_ASSERT_EQUAL_UINT(9u, (H2Frame.write_args.buf = g_out, H2Frame.write_args.cap = sizeof(g_out),
                                H2Frame.write_args.length = 0xFFFFFFu, H2Frame.write_args.type = H2_DATA,
                                H2Frame.write_args.flags = 0, H2Frame.write_args.stream_id = 1,
                                H2Frame.write_header(NULL), H2Frame.n));
    TEST_ASSERT_EQUAL_HEX8(0xFF, g_out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, g_out[1]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, g_out[2]);
    TEST_ASSERT_EQUAL_UINT(0u, (H2Frame.write_args.buf = g_out, H2Frame.write_args.cap = sizeof(g_out),
                                H2Frame.write_args.length = 0x1000000u, H2Frame.write_args.type = H2_DATA,
                                H2Frame.write_args.flags = 0, H2Frame.write_args.stream_id = 1,
                                H2Frame.write_header(NULL), H2Frame.n));
    TEST_ASSERT_EQUAL_UINT(0u,
                           (H2Frame.write_args.buf = g_out, H2Frame.write_args.cap = 8, H2Frame.write_args.length = 0,
                            H2Frame.write_args.type = H2_DATA, H2Frame.write_args.flags = 0,
                            H2Frame.write_args.stream_id = 1, H2Frame.write_header(NULL), H2Frame.n));

    H2FrameHeader h;
    TEST_ASSERT_FALSE((H2Frame.parse_args.buf = g_out, H2Frame.parse_args.len = 8, H2Frame.parse_header(NULL),
                       *(&h) = H2Frame.header, H2Frame.ok));
}

// RFC 9113 sec 6.5.2 states each initial value in words: HEADER_TABLE_SIZE 4,096; ENABLE_PUSH 1;
// INITIAL_WINDOW_SIZE 2^16-1 = 65,535; MAX_FRAME_SIZE 2^14 = 16,384; MAX_CONCURRENT_STREAMS and
// MAX_HEADER_LIST_SIZE unlimited, which this module spells as the all-ones 32-bit value.
void test_rfc9113_settings_initial_values(void)
{
    H2Settings s;
    (H2Frame.settings_args.s = &s, H2Frame.settings_defaults(NULL));
    TEST_ASSERT_EQUAL_UINT32(4096u, s.header_table_size);
    TEST_ASSERT_EQUAL_UINT32(1u, s.enable_push);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, s.max_concurrent_streams);
    TEST_ASSERT_EQUAL_UINT32(65535u, s.initial_window_size);
    TEST_ASSERT_EQUAL_UINT32(16384u, s.max_frame_size);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, s.max_header_list_size);
}

// sec 6.5: a Setting is a 16-bit Identifier and a 32-bit Value, so a payload that is not a multiple
// of six octets is malformed. An identifier this endpoint does not know is ignored, not refused.
void test_rfc9113_settings_payload_shape(void)
{
    H2Settings s;
    (H2Frame.settings_args.s = &s, H2Frame.settings_defaults(NULL));

    static const uint8_t ODD[5] = {0, 1, 0, 0, 0};
    TEST_ASSERT_FALSE((H2Frame.settings_args.payload = ODD, H2Frame.settings_args.len = sizeof(ODD),
                       H2Frame.settings_args.s = &s, H2Frame.parse_settings(NULL), H2Frame.ok));

    // id 0x1 = 8192, then an unknown id 0xabcd
    static const uint8_t PAY[12] = {0x00, 0x01, 0x00, 0x00, 0x20, 0x00, 0xab, 0xcd, 0xde, 0xad, 0xbe, 0xef};
    TEST_ASSERT_TRUE((H2Frame.settings_args.payload = PAY, H2Frame.settings_args.len = sizeof(PAY),
                      H2Frame.settings_args.s = &s, H2Frame.parse_settings(NULL), H2Frame.ok));
    TEST_ASSERT_EQUAL_UINT32(8192u, s.header_table_size);
    TEST_ASSERT_EQUAL_UINT32(1u, s.enable_push); // untouched by the unknown entry

    TEST_ASSERT_TRUE((H2Frame.settings_args.payload = PAY, H2Frame.settings_args.len = 0, H2Frame.settings_args.s = &s,
                      H2Frame.parse_settings(NULL), H2Frame.ok)); // an empty SETTINGS is legal
}

// sec 6.5.2 draws three bounds in words, and each is tested at the value on either side of it:
//   ENABLE_PUSH           "Any value other than 0 or 1 MUST be treated as a connection error"
//   INITIAL_WINDOW_SIZE   "Values above the maximum flow-control window size of 2^31-1 MUST be
//                          treated as a connection error"
//   MAX_FRAME_SIZE        "MUST be between this initial value [2^14] and the maximum allowed frame
//                          size (2^24-1 or 16,777,215 octets), inclusive"
void test_rfc9113_settings_bounds(void)
{
    struct
    {
        uint16_t id;
        uint32_t val;
        proto_bool want;
    } static const CASES[] = {
        {H2_SETTINGS_ENABLE_PUSH, 0, PROTO_TRUE},
        {H2_SETTINGS_ENABLE_PUSH, 1, PROTO_TRUE},
        {H2_SETTINGS_ENABLE_PUSH, 2, PROTO_FALSE},
        {H2_SETTINGS_INITIAL_WINDOW_SIZE, 0x7FFFFFFFu, PROTO_TRUE},
        {H2_SETTINGS_INITIAL_WINDOW_SIZE, 0x80000000u, PROTO_FALSE},
        {H2_SETTINGS_MAX_FRAME_SIZE, 16383u, PROTO_FALSE},
        {H2_SETTINGS_MAX_FRAME_SIZE, 16384u, PROTO_TRUE},
        {H2_SETTINGS_MAX_FRAME_SIZE, 16777215u, PROTO_TRUE},
        {H2_SETTINGS_MAX_FRAME_SIZE, 16777216u, PROTO_FALSE},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        uint8_t pay[6];
        H2Settings s;
        (H2Frame.settings_args.s = &s, H2Frame.settings_defaults(NULL));
        pay[0] = (uint8_t)(CASES[i].id >> 8);
        pay[1] = (uint8_t)CASES[i].id;
        pay[2] = (uint8_t)(CASES[i].val >> 24);
        pay[3] = (uint8_t)(CASES[i].val >> 16);
        pay[4] = (uint8_t)(CASES[i].val >> 8);
        pay[5] = (uint8_t)CASES[i].val;
        TEST_ASSERT_EQUAL_INT(CASES[i].want,
                              (H2Frame.settings_args.payload = pay, H2Frame.settings_args.len = sizeof(pay),
                               H2Frame.settings_args.s = &s, H2Frame.parse_settings(NULL), H2Frame.ok));
    }
}

// sec 6.5: SETTINGS is type 0x04 on stream 0. What the builder writes, the parser reads.
void test_rfc9113_settings_round_trip(void)
{
    static const uint16_t IDS[3] = {H2_SETTINGS_MAX_CONCURRENT_STREAMS, H2_SETTINGS_INITIAL_WINDOW_SIZE,
                                    H2_SETTINGS_MAX_FRAME_SIZE};
    static const uint32_t VALS[3] = {100u, 1048576u, 16384u};
    size_t n = (H2Frame.build_settings_args.buf = g_out, H2Frame.build_settings_args.cap = sizeof(g_out),
                H2Frame.build_settings_args.ids = IDS, H2Frame.build_settings_args.vals = VALS,
                H2Frame.build_settings_args.n = 3, H2Frame.build_settings(NULL), H2Frame.n);
    TEST_ASSERT_EQUAL_UINT(9u + 18u, n);

    H2FrameHeader h;
    TEST_ASSERT_TRUE((H2Frame.parse_args.buf = g_out, H2Frame.parse_args.len = n, H2Frame.parse_header(NULL),
                      *(&h) = H2Frame.header, H2Frame.ok));
    TEST_ASSERT_EQUAL_HEX8(H2_SETTINGS, h.type);
    TEST_ASSERT_EQUAL_HEX8(0x00, h.flags);
    TEST_ASSERT_EQUAL_HEX32(0u, h.stream_id);
    TEST_ASSERT_EQUAL_UINT32(18u, h.length);

    H2Settings s;
    (H2Frame.settings_args.s = &s, H2Frame.settings_defaults(NULL));
    TEST_ASSERT_TRUE((H2Frame.settings_args.payload = g_out + 9, H2Frame.settings_args.len = h.length,
                      H2Frame.settings_args.s = &s, H2Frame.parse_settings(NULL), H2Frame.ok));
    TEST_ASSERT_EQUAL_UINT32(100u, s.max_concurrent_streams);
    TEST_ASSERT_EQUAL_UINT32(1048576u, s.initial_window_size);
    TEST_ASSERT_EQUAL_UINT32(16384u, s.max_frame_size);
}

// sec 6.5: the ACK is an empty SETTINGS frame with the ACK flag (0x01) on stream 0.
void test_rfc9113_settings_ack_bytes(void)
{
    static const uint8_t WANT[9] = {0x00, 0x00, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT(9u, (H2Frame.ack_args.buf = g_out, H2Frame.ack_args.cap = sizeof(g_out),
                                H2Frame.build_settings_ack(NULL), H2Frame.n));
    TEST_ASSERT_EQUAL_MEMORY(WANT, g_out, 9);
}

// sec 6.9: WINDOW_UPDATE is type 0x08 with Length 0x04, and its payload is one reserved bit plus an
// unsigned 31-bit Window Size Increment, so the top bit of the increment never reaches the wire.
void test_rfc9113_window_update_bytes(void)
{
    static const uint8_t WANT[13] = {0x00, 0x00, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x01, 0x7F, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_UINT(13u, (H2Frame.window_args.buf = g_out, H2Frame.window_args.cap = sizeof(g_out),
                                 H2Frame.window_args.stream_id = 1u, H2Frame.window_args.increment = 0xFFFFFFFFu,
                                 H2Frame.build_window_update(NULL), H2Frame.n));
    TEST_ASSERT_EQUAL_MEMORY(WANT, g_out, 13);
}

// sec 6.4: RST_STREAM is type 0x03 with a 32-bit Error Code, on the stream it resets. sec 7 assigns
// CANCEL the code 0x08.
void test_rfc9113_rst_stream_bytes(void)
{
    static const uint8_t WANT[13] = {0x00, 0x00, 0x04, 0x03, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x08};
    TEST_ASSERT_EQUAL_UINT(13u, (H2Frame.rst_args.buf = g_out, H2Frame.rst_args.cap = sizeof(g_out),
                                 H2Frame.rst_args.stream_id = 3u, H2Frame.rst_args.error = H2_CANCEL,
                                 H2Frame.build_rst_stream(NULL), H2Frame.n));
    TEST_ASSERT_EQUAL_MEMORY(WANT, g_out, 13);
    TEST_ASSERT_EQUAL_HEX32(0x08u, (uint32_t)H2_CANCEL);
}

// sec 6.8: GOAWAY is type 0x07 on stream 0, and its payload is a reserved bit plus a 31-bit
// Last-Stream-ID followed by a 32-bit Error Code. sec 7 assigns ENHANCE_YOUR_CALM the code 0x0b.
void test_rfc9113_goaway_bytes(void)
{
    static const uint8_t WANT[17] = {0x00, 0x00, 0x08, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x0B};
    TEST_ASSERT_EQUAL_UINT(17u,
                           (H2Frame.goaway_args.buf = g_out, H2Frame.goaway_args.cap = sizeof(g_out),
                            H2Frame.goaway_args.last_stream_id = 0x8000000Fu,
                            H2Frame.goaway_args.error = H2_ENHANCE_YOUR_CALM, H2Frame.build_goaway(NULL), H2Frame.n));
    TEST_ASSERT_EQUAL_MEMORY(WANT, g_out, 17);
}

// sec 6.7: PING carries 8 octets of opaque data on stream 0, and a response sets the ACK flag and
// echoes those same 8 octets unchanged.
void test_rfc9113_ping_ack_echoes_the_payload(void)
{
    static const uint8_t OPAQUE[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
    static const uint8_t WANT[9] = {0x00, 0x00, 0x08, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT(17u, (H2Frame.ping_args.buf = g_out, H2Frame.ping_args.cap = sizeof(g_out),
                                 H2Frame.ping_args.opaque = OPAQUE, H2Frame.build_ping_ack(NULL), H2Frame.n));
    TEST_ASSERT_EQUAL_MEMORY(WANT, g_out, 9);
    TEST_ASSERT_EQUAL_MEMORY(OPAQUE, g_out + 9, 8);
}

// sec 6.2: HEADERS is type 0x01; END_HEADERS (0x04) says the field block ends here and END_STREAM
// (0x01) that the message does. sec 6.1: DATA is type 0x00 and carries the content itself.
void test_rfc9113_headers_and_data(void)
{
    static const uint8_t BLOCK[3] = {0x88, 0x0F, 0x0D};
    H2FrameHeader h;

    size_t n = (H2Frame.headers_args.buf = g_out, H2Frame.headers_args.cap = sizeof(g_out),
                H2Frame.headers_args.stream_id = 5u, H2Frame.headers_args.block = BLOCK,
                H2Frame.headers_args.block_len = sizeof(BLOCK), H2Frame.headers_args.end_stream = PROTO_FALSE,
                H2Frame.build_headers(NULL), H2Frame.n);
    TEST_ASSERT_EQUAL_UINT(12u, n);
    TEST_ASSERT_TRUE((H2Frame.parse_args.buf = g_out, H2Frame.parse_args.len = n, H2Frame.parse_header(NULL),
                      *(&h) = H2Frame.header, H2Frame.ok));
    TEST_ASSERT_EQUAL_HEX8(H2_HEADERS, h.type);
    TEST_ASSERT_EQUAL_HEX8(H2_FLAG_END_HEADERS, h.flags);
    TEST_ASSERT_EQUAL_HEX32(5u, h.stream_id);
    TEST_ASSERT_EQUAL_UINT32(3u, h.length);
    TEST_ASSERT_EQUAL_MEMORY(BLOCK, g_out + 9, 3);

    n = (H2Frame.headers_args.buf = g_out, H2Frame.headers_args.cap = sizeof(g_out),
         H2Frame.headers_args.stream_id = 5u, H2Frame.headers_args.block = BLOCK,
         H2Frame.headers_args.block_len = sizeof(BLOCK), H2Frame.headers_args.end_stream = PROTO_TRUE,
         H2Frame.build_headers(NULL), H2Frame.n);
    TEST_ASSERT_TRUE((H2Frame.parse_args.buf = g_out, H2Frame.parse_args.len = n, H2Frame.parse_header(NULL),
                      *(&h) = H2Frame.header, H2Frame.ok));
    TEST_ASSERT_EQUAL_HEX8(H2_FLAG_END_HEADERS | H2_FLAG_END_STREAM, h.flags);

    n = (H2Frame.data_args.buf = g_out, H2Frame.data_args.cap = sizeof(g_out), H2Frame.data_args.stream_id = 7u,
         H2Frame.data_args.data = (const uint8_t *)"hi", H2Frame.data_args.data_len = 2,
         H2Frame.data_args.end_stream = PROTO_TRUE, H2Frame.build_data(NULL), H2Frame.n);
    TEST_ASSERT_EQUAL_UINT(11u, n);
    TEST_ASSERT_TRUE((H2Frame.parse_args.buf = g_out, H2Frame.parse_args.len = n, H2Frame.parse_header(NULL),
                      *(&h) = H2Frame.header, H2Frame.ok));
    TEST_ASSERT_EQUAL_HEX8(H2_DATA, h.type);
    TEST_ASSERT_EQUAL_HEX8(H2_FLAG_END_STREAM, h.flags);
    TEST_ASSERT_EQUAL_UINT32(2u, h.length);
    TEST_ASSERT_EQUAL_MEMORY("hi", g_out + 9, 2);

    // an empty DATA frame is a header and nothing else
    n = (H2Frame.data_args.buf = g_out, H2Frame.data_args.cap = sizeof(g_out), H2Frame.data_args.stream_id = 7u,
         H2Frame.data_args.data = NULL, H2Frame.data_args.data_len = 0, H2Frame.data_args.end_stream = PROTO_TRUE,
         H2Frame.build_data(NULL), H2Frame.n);
    TEST_ASSERT_EQUAL_UINT(9u, n);
}

// A destination that cannot hold the whole frame yields 0 rather than a partial one, since a
// truncated frame on the wire desynchronizes the peer's parser for the rest of the connection.
void test_builders_refuse_a_short_destination(void)
{
    static const uint8_t OPAQUE[8] = {0};
    static const uint16_t ID = H2_SETTINGS_ENABLE_PUSH;
    static const uint32_t VAL = 0;
    TEST_ASSERT_EQUAL_UINT(0u, (H2Frame.build_settings_args.buf = g_out, H2Frame.build_settings_args.cap = 14,
                                H2Frame.build_settings_args.ids = &ID, H2Frame.build_settings_args.vals = &VAL,
                                H2Frame.build_settings_args.n = 1, H2Frame.build_settings(NULL), H2Frame.n));
    TEST_ASSERT_EQUAL_UINT(
        0u, (H2Frame.ack_args.buf = g_out, H2Frame.ack_args.cap = 8, H2Frame.build_settings_ack(NULL), H2Frame.n));
    TEST_ASSERT_EQUAL_UINT(0u, (H2Frame.window_args.buf = g_out, H2Frame.window_args.cap = 12,
                                H2Frame.window_args.stream_id = 1, H2Frame.window_args.increment = 1,
                                H2Frame.build_window_update(NULL), H2Frame.n));
    TEST_ASSERT_EQUAL_UINT(0u, (H2Frame.rst_args.buf = g_out, H2Frame.rst_args.cap = 12, H2Frame.rst_args.stream_id = 1,
                                H2Frame.rst_args.error = H2_NO_ERROR, H2Frame.build_rst_stream(NULL), H2Frame.n));
    TEST_ASSERT_EQUAL_UINT(0u, (H2Frame.goaway_args.buf = g_out, H2Frame.goaway_args.cap = 16,
                                H2Frame.goaway_args.last_stream_id = 1, H2Frame.goaway_args.error = H2_NO_ERROR,
                                H2Frame.build_goaway(NULL), H2Frame.n));
    TEST_ASSERT_EQUAL_UINT(0u, (H2Frame.ping_args.buf = g_out, H2Frame.ping_args.cap = 16,
                                H2Frame.ping_args.opaque = OPAQUE, H2Frame.build_ping_ack(NULL), H2Frame.n));
    TEST_ASSERT_EQUAL_UINT(0u,
                           (H2Frame.data_args.buf = g_out, H2Frame.data_args.cap = 10, H2Frame.data_args.stream_id = 1,
                            H2Frame.data_args.data = (const uint8_t *)"xx", H2Frame.data_args.data_len = 2,
                            H2Frame.data_args.end_stream = PROTO_FALSE, H2Frame.build_data(NULL), H2Frame.n));
    TEST_ASSERT_EQUAL_UINT(0u, (H2Frame.headers_args.buf = g_out, H2Frame.headers_args.cap = 10,
                                H2Frame.headers_args.stream_id = 1, H2Frame.headers_args.block = (const uint8_t *)"xx",
                                H2Frame.headers_args.block_len = 2, H2Frame.headers_args.end_stream = PROTO_FALSE,
                                H2Frame.build_headers(NULL), H2Frame.n));
}

// RFC 9113 sec 6 assigns each frame type its octet and sec 7 each error condition its 32-bit code.
// These are the numbers a peer compares against, so they are pinned here rather than left to a
// header edit to change quietly.
void test_rfc9113_registry_values(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00, H2_DATA);
    TEST_ASSERT_EQUAL_HEX8(0x01, H2_HEADERS);
    TEST_ASSERT_EQUAL_HEX8(0x02, H2_PRIORITY);
    TEST_ASSERT_EQUAL_HEX8(0x03, H2_RST_STREAM);
    TEST_ASSERT_EQUAL_HEX8(0x04, H2_SETTINGS);
    TEST_ASSERT_EQUAL_HEX8(0x05, H2_PUSH_PROMISE);
    TEST_ASSERT_EQUAL_HEX8(0x06, H2_PING);
    TEST_ASSERT_EQUAL_HEX8(0x07, H2_GOAWAY);
    TEST_ASSERT_EQUAL_HEX8(0x08, H2_WINDOW_UPDATE);
    TEST_ASSERT_EQUAL_HEX8(0x09, H2_CONTINUATION);

    TEST_ASSERT_EQUAL_HEX32(0x00u, (uint32_t)H2_NO_ERROR);
    TEST_ASSERT_EQUAL_HEX32(0x01u, (uint32_t)H2_PROTOCOL_ERROR);
    TEST_ASSERT_EQUAL_HEX32(0x02u, (uint32_t)H2_INTERNAL_ERROR);
    TEST_ASSERT_EQUAL_HEX32(0x03u, (uint32_t)H2_FLOW_CONTROL_ERROR);
    TEST_ASSERT_EQUAL_HEX32(0x04u, (uint32_t)H2_SETTINGS_TIMEOUT);
    TEST_ASSERT_EQUAL_HEX32(0x05u, (uint32_t)H2_STREAM_CLOSED);
    TEST_ASSERT_EQUAL_HEX32(0x06u, (uint32_t)H2_FRAME_SIZE_ERROR);
    TEST_ASSERT_EQUAL_HEX32(0x07u, (uint32_t)H2_REFUSED_STREAM);
    TEST_ASSERT_EQUAL_HEX32(0x08u, (uint32_t)H2_CANCEL);
    TEST_ASSERT_EQUAL_HEX32(0x09u, (uint32_t)H2_COMPRESSION_ERROR);
    TEST_ASSERT_EQUAL_HEX32(0x0au, (uint32_t)H2_CONNECT_ERROR);
    TEST_ASSERT_EQUAL_HEX32(0x0bu, (uint32_t)H2_ENHANCE_YOUR_CALM);
    TEST_ASSERT_EQUAL_HEX32(0x0cu, (uint32_t)H2_INADEQUATE_SECURITY);
    TEST_ASSERT_EQUAL_HEX32(0x0du, (uint32_t)H2_HTTP_1_1_REQUIRED);

    // sec 6.5.2 / sec 6.5: the SETTINGS identifiers are 16-bit wire values
    TEST_ASSERT_EQUAL_HEX16(0x01, H2_SETTINGS_HEADER_TABLE_SIZE);
    TEST_ASSERT_EQUAL_HEX16(0x02, H2_SETTINGS_ENABLE_PUSH);
    TEST_ASSERT_EQUAL_HEX16(0x03, H2_SETTINGS_MAX_CONCURRENT_STREAMS);
    TEST_ASSERT_EQUAL_HEX16(0x04, H2_SETTINGS_INITIAL_WINDOW_SIZE);
    TEST_ASSERT_EQUAL_HEX16(0x05, H2_SETTINGS_MAX_FRAME_SIZE);
    TEST_ASSERT_EQUAL_HEX16(0x06, H2_SETTINGS_MAX_HEADER_LIST_SIZE);
}
