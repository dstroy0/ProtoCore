// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for QUIC frame coding (network_drivers/presentation/http/http3/quic_frame.h).
//
// RFC 9000 sec 19 prints one figure per frame giving its fields in order, and Table 3 in that
// section assigns every type value. test_rfc9000_stream_frame_type_bits is the load-bearing case:
// sec 19.8 makes the three low bits of the STREAM type decide which fields are on the wire at all,
// so a parser that misreads OFF (0x04), LEN (0x02) or FIN (0x01) reads the following varint as the
// wrong field and every later frame in the packet lands at the wrong offset. Field values that are
// varints use the sample sequences RFC 9000 Appendix A.1 publishes, so the octets are the RFC's.

#include "network_drivers/presentation/http/http3/quic_frame/quic_frame.h"
#include <string.h>

#include <unity.h>

static uint8_t quic_frame_work[16]; // the borrow an entry takes; QuicFrame never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static uint8_t g_out[64];

// RFC 9000 sec 19 Table 3: the type value each frame is assigned.
void test_rfc9000_frame_type_table(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00, QUIC_FT_PADDING);
    TEST_ASSERT_EQUAL_HEX8(0x01, QUIC_FT_PING);
    TEST_ASSERT_EQUAL_HEX8(0x02, QUIC_FT_ACK);
    TEST_ASSERT_EQUAL_HEX8(0x03, QUIC_FT_ACK_ECN);
    TEST_ASSERT_EQUAL_HEX8(0x04, QUIC_FT_RESET_STREAM);
    TEST_ASSERT_EQUAL_HEX8(0x05, QUIC_FT_STOP_SENDING);
    TEST_ASSERT_EQUAL_HEX8(0x06, QUIC_FT_CRYPTO);
    TEST_ASSERT_EQUAL_HEX8(0x07, QUIC_FT_NEW_TOKEN);
    TEST_ASSERT_EQUAL_HEX8(0x08, QUIC_FT_STREAM);
    TEST_ASSERT_EQUAL_HEX8(0x10, QUIC_FT_MAX_DATA);
    TEST_ASSERT_EQUAL_HEX8(0x11, QUIC_FT_MAX_STREAM_DATA);
    TEST_ASSERT_EQUAL_HEX8(0x12, QUIC_FT_MAX_STREAMS_BIDI);
    TEST_ASSERT_EQUAL_HEX8(0x13, QUIC_FT_MAX_STREAMS_UNI);
    TEST_ASSERT_EQUAL_HEX8(0x14, QUIC_FT_DATA_BLOCKED);
    TEST_ASSERT_EQUAL_HEX8(0x15, QUIC_FT_STREAM_DATA_BLOCKED);
    TEST_ASSERT_EQUAL_HEX8(0x16, QUIC_FT_STREAMS_BLOCKED_BIDI);
    TEST_ASSERT_EQUAL_HEX8(0x17, QUIC_FT_STREAMS_BLOCKED_UNI);
    TEST_ASSERT_EQUAL_HEX8(0x18, QUIC_FT_NEW_CONNECTION_ID);
    TEST_ASSERT_EQUAL_HEX8(0x19, QUIC_FT_RETIRE_CONNECTION_ID);
    TEST_ASSERT_EQUAL_HEX8(0x1a, QUIC_FT_PATH_CHALLENGE);
    TEST_ASSERT_EQUAL_HEX8(0x1b, QUIC_FT_PATH_RESPONSE);
    TEST_ASSERT_EQUAL_HEX8(0x1c, QUIC_FT_CONNECTION_CLOSE);
    TEST_ASSERT_EQUAL_HEX8(0x1d, QUIC_FT_CONNECTION_CLOSE_APP);
    TEST_ASSERT_EQUAL_HEX8(0x1e, QUIC_FT_HANDSHAKE_DONE);
    // sec 19.8: the STREAM type bits
    TEST_ASSERT_EQUAL_HEX8(0x01, QUIC_STREAM_FIN);
    TEST_ASSERT_EQUAL_HEX8(0x02, QUIC_STREAM_LEN);
    TEST_ASSERT_EQUAL_HEX8(0x04, QUIC_STREAM_OFF);
}

// sec 19.1 / 19.2 / 19.20: PADDING, PING and HANDSHAKE_DONE are a type varint and nothing else.
void test_rfc9000_single_octet_frames(void)
{
    QuicFrameHeader f;
    QuicFrameV.build_ping_args.out = g_out;
    QuicFrameV.build_ping_args.cap = sizeof(g_out);
    QuicFrame.build_ping(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(1u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_HEX8(0x01, g_out[0]);
    QuicFrameV.parse_args.buf = g_out;
    QuicFrameV.parse_args.len = 1;
    QuicFrameV.parse_args.out = &f;
    QuicFrame.parse(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(1u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_HEX64(QUIC_FT_PING, f.type);

    QuicFrameV.build_handshake_done_args.out = g_out;
    QuicFrameV.build_handshake_done_args.cap = sizeof(g_out);
    QuicFrame.build_handshake_done(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(1u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_HEX8(0x1e, g_out[0]);
    QuicFrameV.parse_args.buf = g_out;
    QuicFrameV.parse_args.len = 1;
    QuicFrameV.parse_args.out = &f;
    QuicFrame.parse(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(1u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_HEX64(QUIC_FT_HANDSHAKE_DONE, f.type);

    // sec 19.1: PADDING has no semantic value; n of them are n zero octets, each parsed on its own
    static const uint8_t ZEROS[5] = {0, 0, 0, 0, 0};
    QuicFrameV.build_padding_args.out = g_out;
    QuicFrameV.build_padding_args.cap = sizeof(g_out);
    QuicFrameV.build_padding_args.n = 5;
    QuicFrame.build_padding(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(5u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_MEMORY(ZEROS, g_out, 5);
    QuicFrameV.parse_args.buf = g_out;
    QuicFrameV.parse_args.len = 5;
    QuicFrameV.parse_args.out = &f;
    QuicFrame.parse(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(1u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_HEX64(QUIC_FT_PADDING, f.type);
}

// sec 19.3 Figure 25: Type 0x02..0x03, Largest Acknowledged, ACK Delay, ACK Range Count, First ACK
// Range. The values here are RFC 9000 Appendix A.1's samples: 15,293 encodes as 0x7bbd and 37 as
// 0x25, so the whole frame's octets follow from the figure's field order.
void test_rfc9000_ack_frame_fields(void)
{
    static const uint8_t WANT[6] = {0x02, 0x7b, 0xbd, 0x25, 0x00, 0x01};
    QuicFrameHeader f;
    QuicFrameV.build_ack_args.out = g_out;
    QuicFrameV.build_ack_args.cap = sizeof(g_out);
    QuicFrameV.build_ack_args.largest = 15293u;
    QuicFrameV.build_ack_args.delay = 37u;
    QuicFrameV.build_ack_args.first_range = 1u;
    QuicFrame.build_ack(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(6u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_MEMORY(WANT, g_out, 6);

    QuicFrameV.parse_args.buf = WANT;
    QuicFrameV.parse_args.len = sizeof(WANT);
    QuicFrameV.parse_args.out = &f;
    QuicFrame.parse(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(6u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_HEX64(QUIC_FT_ACK, f.type);
    TEST_ASSERT_EQUAL_HEX64(15293u, f.ack.largest);
    TEST_ASSERT_EQUAL_HEX64(37u, f.ack.delay);
    TEST_ASSERT_EQUAL_HEX64(0u, f.ack.range_count);
    TEST_ASSERT_EQUAL_HEX64(1u, f.ack.first_range);
}

// sec 19.3.1: each of the ACK Range Count entries is a Gap and an ACK Range Length, so the frame
// ends after 2 * count more varints. sec 19.3.2: type 0x03 appends three ECN counts.
void test_rfc9000_ack_ranges_and_ecn_are_consumed(void)
{
    // largest 10, delay 0, range count 2, first range 1, then (gap, len) twice
    static const uint8_t WITH_RANGES[9] = {0x02, 0x0a, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00};
    QuicFrameHeader f;
    QuicFrameV.parse_args.buf = WITH_RANGES;
    QuicFrameV.parse_args.len = sizeof(WITH_RANGES);
    QuicFrameV.parse_args.out = &f;
    QuicFrame.parse(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(9u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_HEX64(2u, f.ack.range_count);
    TEST_ASSERT_EQUAL_HEX64(1u, f.ack.first_range);

    // ACK_ECN: no ranges, then ECT0 / ECT1 / CE
    static const uint8_t WITH_ECN[8] = {0x03, 0x0a, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
    QuicFrameV.parse_args.buf = WITH_ECN;
    QuicFrameV.parse_args.len = sizeof(WITH_ECN);
    QuicFrameV.parse_args.out = &f;
    QuicFrame.parse(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(8u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_HEX64(QUIC_FT_ACK_ECN, f.type);
    TEST_ASSERT_EQUAL_HEX64(10u, f.ack.largest);

    // the same field sequence under type 0x02 ends before the ECN counts, so the type bit decides
    // where the next frame begins
    static const uint8_t NO_ECN[8] = {0x02, 0x0a, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
    QuicFrameV.parse_args.buf = NO_ECN;
    QuicFrameV.parse_args.len = sizeof(NO_ECN);
    QuicFrameV.parse_args.out = &f;
    QuicFrame.parse(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(5u, QuicFrameV.n);
}

// sec 19.6 Figure 30: Type 0x06, Offset, Length, Crypto Data. The data is a view into the caller's
// packet buffer, not a copy.
void test_rfc9000_crypto_frame(void)
{
    static const uint8_t DATA[3] = {0xAB, 0xCD, 0xEF};
    static const uint8_t WANT[7] = {0x06, 0x7b, 0xbd, 0x03, 0xAB, 0xCD, 0xEF};
    QuicFrameHeader f;
    QuicFrameV.build_crypto_args.out = g_out;
    QuicFrameV.build_crypto_args.cap = sizeof(g_out);
    QuicFrameV.build_crypto_args.offset = 15293u;
    QuicFrameV.build_crypto_args.data = DATA;
    QuicFrameV.build_crypto_args.len = sizeof(DATA);
    QuicFrame.build_crypto(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(7u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_MEMORY(WANT, g_out, 7);

    QuicFrameV.parse_args.buf = WANT;
    QuicFrameV.parse_args.len = sizeof(WANT);
    QuicFrameV.parse_args.out = &f;
    QuicFrame.parse(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(7u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_HEX64(QUIC_FT_CRYPTO, f.type);
    TEST_ASSERT_EQUAL_HEX64(15293u, f.crypto.offset);
    TEST_ASSERT_EQUAL_HEX64(3u, f.crypto.length);
    TEST_ASSERT_EQUAL_PTR(&WANT[4], f.crypto.data);
}

// sec 19.8: "The OFF bit (0x04) ... indicates that there is an Offset field present", "The LEN bit
// (0x02) ... a Length field present. If this bit is set to 0 ... the Stream Data field extends to
// the end of the packet", "The FIN bit (0x01) indicates that the frame marks the end of the stream."
void test_rfc9000_stream_frame_type_bits(void)
{
    QuicFrameHeader f;

    // LEN only: 0x08 | 0x02 = 0x0a, then id, length, data
    static const uint8_t LEN_ONLY[5] = {0x0a, 0x04, 0x02, 'h', 'i'};
    QuicFrameV.build_stream_args.out = g_out;
    QuicFrameV.build_stream_args.cap = sizeof(g_out);
    QuicFrameV.build_stream_args.id = 4u;
    QuicFrameV.build_stream_args.offset = 0u;
    QuicFrameV.build_stream_args.data = (const uint8_t *)"hi";
    QuicFrameV.build_stream_args.len = 2;
    QuicFrameV.build_stream_args.fin = PROTO_FALSE;
    QuicFrame.build_stream(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(5u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_MEMORY(LEN_ONLY, g_out, 5);
    QuicFrameV.parse_args.buf = LEN_ONLY;
    QuicFrameV.parse_args.len = sizeof(LEN_ONLY);
    QuicFrameV.parse_args.out = &f;
    QuicFrame.parse(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(5u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_HEX64(0x0au, f.type);
    TEST_ASSERT_EQUAL_HEX64(4u, f.stream.id);
    TEST_ASSERT_EQUAL_HEX64(0u, f.stream.offset);
    TEST_ASSERT_EQUAL_HEX64(2u, f.stream.length);
    TEST_ASSERT_EQUAL_HEX8(0, f.stream.fin);
    TEST_ASSERT_EQUAL_MEMORY("hi", f.stream.data, 2);

    // OFF | LEN | FIN = 0x08 | 0x04 | 0x02 | 0x01 = 0x0f, so the Offset varint sits between id and
    // length
    static const uint8_t ALL_BITS[6] = {0x0f, 0x04, 0x08, 0x02, 'h', 'i'};
    QuicFrameV.build_stream_args.out = g_out;
    QuicFrameV.build_stream_args.cap = sizeof(g_out);
    QuicFrameV.build_stream_args.id = 4u;
    QuicFrameV.build_stream_args.offset = 8u;
    QuicFrameV.build_stream_args.data = (const uint8_t *)"hi";
    QuicFrameV.build_stream_args.len = 2;
    QuicFrameV.build_stream_args.fin = PROTO_TRUE;
    QuicFrame.build_stream(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(6u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_MEMORY(ALL_BITS, g_out, 6);
    QuicFrameV.parse_args.buf = ALL_BITS;
    QuicFrameV.parse_args.len = sizeof(ALL_BITS);
    QuicFrameV.parse_args.out = &f;
    QuicFrame.parse(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(6u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_HEX64(0x0fu, f.type);
    TEST_ASSERT_EQUAL_HEX64(8u, f.stream.offset);
    TEST_ASSERT_EQUAL_HEX64(2u, f.stream.length);
    TEST_ASSERT_EQUAL_HEX8(1, f.stream.fin);

    // LEN clear: the Stream Data runs to the end of what was handed in
    static const uint8_t NO_LEN[5] = {0x08, 0x04, 'a', 'b', 'c'};
    QuicFrameV.parse_args.buf = NO_LEN;
    QuicFrameV.parse_args.len = sizeof(NO_LEN);
    QuicFrameV.parse_args.out = &f;
    QuicFrame.parse(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(5u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_HEX64(4u, f.stream.id);
    TEST_ASSERT_EQUAL_HEX64(0u, f.stream.offset);
    TEST_ASSERT_EQUAL_HEX64(3u, f.stream.length);
    TEST_ASSERT_EQUAL_HEX8(0, f.stream.fin);
    TEST_ASSERT_EQUAL_MEMORY("abc", f.stream.data, 3);

    // FIN alone, with no data at all: sec 19.8 allows a zero-length frame that only ends the stream
    static const uint8_t FIN_ONLY[2] = {0x09, 0x04};
    QuicFrameV.parse_args.buf = FIN_ONLY;
    QuicFrameV.parse_args.len = sizeof(FIN_ONLY);
    QuicFrameV.parse_args.out = &f;
    QuicFrame.parse(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(2u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_HEX64(0u, f.stream.length);
    TEST_ASSERT_EQUAL_HEX8(1, f.stream.fin);
}

// sec 19.9 Figure 33: Type 0x10 then Maximum Data. 1,048,576 needs the 30-bit varint form, whose
// first byte carries the 0b10 prefix: 0x80 0x10 0x00 0x00.
void test_rfc9000_max_data(void)
{
    static const uint8_t WANT[5] = {0x10, 0x80, 0x10, 0x00, 0x00};
    QuicFrameHeader f;
    QuicFrameV.build_max_data_args.out = g_out;
    QuicFrameV.build_max_data_args.cap = sizeof(g_out);
    QuicFrameV.build_max_data_args.max = 1048576u;
    QuicFrame.build_max_data(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(5u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_MEMORY(WANT, g_out, 5);
    QuicFrameV.parse_args.buf = WANT;
    QuicFrameV.parse_args.len = sizeof(WANT);
    QuicFrameV.parse_args.out = &f;
    QuicFrame.parse(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(5u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_HEX64(QUIC_FT_MAX_DATA, f.type);
    TEST_ASSERT_EQUAL_HEX64(1048576u, f.max_data.max);
}

// sec 19.19 Figure 43: Error Code, [Frame Type], Reason Phrase Length, Reason Phrase - and "The
// application-specific variant of CONNECTION_CLOSE (type 0x1d) does not include this field",
// meaning the Frame Type. The two variants therefore differ by one varint at the same offset, which
// is the whole reason they are tested against each other.
void test_rfc9000_connection_close_variants(void)
{
    QuicFrameHeader f;

    // transport variant: 0x1c, error PROTOCOL_VIOLATION (0x0a), triggering frame type CRYPTO (0x06)
    static const uint8_t TRANSPORT[6] = {0x1c, 0x0a, 0x06, 0x02, 'n', 'o'};
    QuicFrameV.build_connection_close_args.out = g_out;
    QuicFrameV.build_connection_close_args.cap = sizeof(g_out);
    QuicFrameV.build_connection_close_args.app = PROTO_FALSE;
    QuicFrameV.build_connection_close_args.error_code = QUIC_ERR_PROTOCOL_VIOLATION;
    QuicFrameV.build_connection_close_args.frame_type = QUIC_FT_CRYPTO;
    QuicFrameV.build_connection_close_args.reason = "no";
    QuicFrameV.build_connection_close_args.reason_len = 2;
    QuicFrame.build_connection_close(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(6u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_MEMORY(TRANSPORT, g_out, 6);
    QuicFrameV.parse_args.buf = TRANSPORT;
    QuicFrameV.parse_args.len = sizeof(TRANSPORT);
    QuicFrameV.parse_args.out = &f;
    QuicFrame.parse(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(6u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_HEX64(QUIC_FT_CONNECTION_CLOSE, f.type);
    TEST_ASSERT_EQUAL_HEX8(0, f.close.app);
    TEST_ASSERT_EQUAL_HEX64(0x0au, f.close.error_code);
    TEST_ASSERT_EQUAL_HEX64(0x06u, f.close.frame_type);
    TEST_ASSERT_EQUAL_HEX64(2u, f.close.reason_len);
    TEST_ASSERT_EQUAL_MEMORY("no", f.close.reason, 2);

    // application variant: 0x1d with an application error code. 0x0100 needs the 14-bit varint
    // form, whose first byte carries the 0b01 prefix: 0x41 0x00. No Frame Type follows it.
    static const uint8_t APP[4] = {0x1d, 0x41, 0x00, 0x00};
    QuicFrameV.build_connection_close_args.out = g_out;
    QuicFrameV.build_connection_close_args.cap = sizeof(g_out);
    QuicFrameV.build_connection_close_args.app = PROTO_TRUE;
    QuicFrameV.build_connection_close_args.error_code = 0x0100u;
    QuicFrameV.build_connection_close_args.frame_type = QUIC_FT_CRYPTO;
    QuicFrameV.build_connection_close_args.reason = NULL;
    QuicFrameV.build_connection_close_args.reason_len = 0;
    QuicFrame.build_connection_close(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(4u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_MEMORY(APP, g_out, 4);
    QuicFrameV.parse_args.buf = APP;
    QuicFrameV.parse_args.len = sizeof(APP);
    QuicFrameV.parse_args.out = &f;
    QuicFrame.parse(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(4u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_HEX64(QUIC_FT_CONNECTION_CLOSE_APP, f.type);
    TEST_ASSERT_EQUAL_HEX8(1, f.close.app);
    TEST_ASSERT_EQUAL_HEX64(0x0100u, f.close.error_code);
    TEST_ASSERT_EQUAL_HEX64(0u, f.close.frame_type);
    TEST_ASSERT_EQUAL_HEX64(0u, f.close.reason_len);
}

// sec 20.1 assigns the transport error codes this module names.
void test_rfc9000_transport_error_codes(void)
{
    TEST_ASSERT_EQUAL_HEX64(0x00u, (uint64_t)QUIC_ERR_NO_ERROR);
    TEST_ASSERT_EQUAL_HEX64(0x01u, (uint64_t)QUIC_ERR_INTERNAL);
    TEST_ASSERT_EQUAL_HEX64(0x03u, (uint64_t)QUIC_ERR_FLOW_CONTROL);
    TEST_ASSERT_EQUAL_HEX64(0x04u, (uint64_t)QUIC_ERR_STREAM_LIMIT);
    TEST_ASSERT_EQUAL_HEX64(0x07u, (uint64_t)QUIC_ERR_FRAME_ENCODING);
    TEST_ASSERT_EQUAL_HEX64(0x0au, (uint64_t)QUIC_ERR_PROTOCOL_VIOLATION);
    TEST_ASSERT_EQUAL_HEX64(0x0cu, (uint64_t)QUIC_ERR_APPLICATION);
    // RFC 9001 sec 4.8: a TLS alert travels as 0x0100 + the alert description
    TEST_ASSERT_EQUAL_HEX64(0x0100u, (uint64_t)QUIC_ERR_CRYPTO_BASE);
}

// sec 12.4: a frame this server does not act on still has to be walked past by its own wire shape,
// or the frames behind it in the packet are read at the wrong offset. Each length below is the
// figure's field count, and the parse must consume exactly it.
void test_rfc9000_unhandled_frames_consume_their_shape(void)
{
    struct
    {
        const uint8_t *buf;
        size_t len;
        uint64_t type;
    } static const CASES[] = {
        // 19.4 RESET_STREAM: Stream ID, Application Error Code, Final Size
        {(const uint8_t *)"\x04\x04\x00\x0a", 4, 0x04},
        // 19.5 STOP_SENDING: Stream ID, Application Error Code
        {(const uint8_t *)"\x05\x04\x00", 3, 0x05},
        // 19.7 NEW_TOKEN: Token Length then the token
        {(const uint8_t *)"\x07\x02\xaa\xbb", 4, 0x07},
        // 19.10 MAX_STREAM_DATA: Stream ID, Maximum Stream Data
        {(const uint8_t *)"\x11\x04\x40\x64", 4, 0x11},
        // 19.11 MAX_STREAMS (bidi / uni): Maximum Streams
        {(const uint8_t *)"\x12\x10", 2, 0x12},
        {(const uint8_t *)"\x13\x10", 2, 0x13},
        // 19.12 DATA_BLOCKED: Maximum Data
        {(const uint8_t *)"\x14\x10", 2, 0x14},
        // 19.13 STREAM_DATA_BLOCKED: Stream ID, Maximum Stream Data
        {(const uint8_t *)"\x15\x04\x10", 3, 0x15},
        // 19.14 STREAMS_BLOCKED (bidi / uni): Maximum Streams
        {(const uint8_t *)"\x16\x10", 2, 0x16},
        {(const uint8_t *)"\x17\x10", 2, 0x17},
        // 19.16 RETIRE_CONNECTION_ID: Sequence Number
        {(const uint8_t *)"\x19\x01", 2, 0x19},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        QuicFrameHeader f;
        QuicFrameV.parse_args.buf = CASES[i].buf;
        QuicFrameV.parse_args.len = CASES[i].len;
        QuicFrameV.parse_args.out = &f;
        QuicFrame.parse(quic_frame_work);
        TEST_ASSERT_EQUAL_UINT(CASES[i].len, QuicFrameV.n);
        TEST_ASSERT_EQUAL_HEX64(CASES[i].type, f.type);
    }
}

// sec 19.15 Figure 39: Sequence Number, Retire Prior To, an 8-bit Length, the Connection ID of that
// length, and a 128-bit Stateless Reset Token - the one frame here whose length field is not a
// varint. sec 19.17 / 19.18: PATH_CHALLENGE and PATH_RESPONSE carry 64 bits of opaque data.
void test_rfc9000_fixed_width_frames(void)
{
    uint8_t buf[24];
    QuicFrameHeader f;
    memset(buf, 0, sizeof(buf));
    buf[0] = QUIC_FT_NEW_CONNECTION_ID;
    buf[1] = 0x01; // Sequence Number
    buf[2] = 0x00; // Retire Prior To
    buf[3] = 0x04; // Length: a 4-octet connection id, then 16 octets of reset token
    QuicFrameV.parse_args.buf = buf;
    QuicFrameV.parse_args.len = sizeof(buf);
    QuicFrameV.parse_args.out = &f;
    QuicFrame.parse(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(24u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_HEX64(QUIC_FT_NEW_CONNECTION_ID, f.type);

    uint8_t path[9];
    memset(path, 0x5a, sizeof(path));
    path[0] = QUIC_FT_PATH_CHALLENGE;
    QuicFrameV.parse_args.buf = path;
    QuicFrameV.parse_args.len = sizeof(path);
    QuicFrameV.parse_args.out = &f;
    QuicFrame.parse(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(9u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_HEX64(QUIC_FT_PATH_CHALLENGE, f.type);
    path[0] = QUIC_FT_PATH_RESPONSE;
    QuicFrameV.parse_args.buf = path;
    QuicFrameV.parse_args.len = sizeof(path);
    QuicFrameV.parse_args.out = &f;
    QuicFrame.parse(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(9u, QuicFrameV.n);
    TEST_ASSERT_EQUAL_HEX64(QUIC_FT_PATH_RESPONSE, f.type);
}

// A frame whose declared payload runs past the packet is refused with 0 consumed. sec 12.4 makes
// that a FRAME_ENCODING_ERROR for the caller to raise, which it cannot do if the parser guesses.
void test_truncated_frames_are_refused(void)
{
    QuicFrameHeader f;
    struct
    {
        const uint8_t *buf;
        size_t len;
    } static const BAD[] = {
        {(const uint8_t *)"\x06\x00\x05\x61", 4},         // CRYPTO length 5, one octet present
        {(const uint8_t *)"\x0a\x04\x05\x61", 4},         // STREAM length 5, one octet present
        {(const uint8_t *)"\x1c\x00\x00\x05\x61", 5},     // CONNECTION_CLOSE reason 5, one present
        {(const uint8_t *)"\x02\x0a", 2},                 // ACK stops after Largest Acknowledged
        {(const uint8_t *)"\x02\x0a\x00\x01\x01", 5},     // ACK Range Count 1 with no range behind it
        {(const uint8_t *)"\x07\x04\xaa", 3},             // NEW_TOKEN length 4, one octet present
        {(const uint8_t *)"\x18\x01\x00\x04\x01\x02", 6}, // NEW_CONNECTION_ID with no reset token
        {(const uint8_t *)"\x1a\x00\x00\x00", 4},         // PATH_CHALLENGE with 3 of its 8 octets
        {(const uint8_t *)"\x10", 1},                     // MAX_DATA with no Maximum Data
        {(const uint8_t *)"\x30", 1},                     // a type this parser does not define
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        QuicFrameV.parse_args.buf = BAD[i].buf;
        QuicFrameV.parse_args.len = BAD[i].len;
        QuicFrameV.parse_args.out = &f;
        QuicFrame.parse(quic_frame_work);
        TEST_ASSERT_EQUAL_UINT(0u, QuicFrameV.n);
    }
    QuicFrameV.parse_args.buf = g_out;
    QuicFrameV.parse_args.len = 0;
    QuicFrameV.parse_args.out = &f;
    QuicFrame.parse(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicFrameV.n);
}

// A destination that cannot hold the whole frame yields 0, never a frame that stops mid field.
void test_builders_refuse_a_short_destination(void)
{
    static const uint8_t DATA[5] = {1, 2, 3, 4, 5};
    QuicFrameV.build_ping_args.out = g_out;
    QuicFrameV.build_ping_args.cap = 0;
    QuicFrame.build_ping(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicFrameV.n);
    QuicFrameV.build_handshake_done_args.out = g_out;
    QuicFrameV.build_handshake_done_args.cap = 0;
    QuicFrame.build_handshake_done(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicFrameV.n);
    QuicFrameV.build_padding_args.out = g_out;
    QuicFrameV.build_padding_args.cap = 4;
    QuicFrameV.build_padding_args.n = 5;
    QuicFrame.build_padding(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicFrameV.n);
    QuicFrameV.build_ack_args.out = g_out;
    QuicFrameV.build_ack_args.cap = 3;
    QuicFrameV.build_ack_args.largest = 15293u;
    QuicFrameV.build_ack_args.delay = 37u;
    QuicFrameV.build_ack_args.first_range = 1u;
    QuicFrame.build_ack(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicFrameV.n);
    QuicFrameV.build_crypto_args.out = g_out;
    QuicFrameV.build_crypto_args.cap = 4;
    QuicFrameV.build_crypto_args.offset = 0;
    QuicFrameV.build_crypto_args.data = DATA;
    QuicFrameV.build_crypto_args.len = sizeof(DATA);
    QuicFrame.build_crypto(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicFrameV.n);
    QuicFrameV.build_stream_args.out = g_out;
    QuicFrameV.build_stream_args.cap = 4;
    QuicFrameV.build_stream_args.id = 4u;
    QuicFrameV.build_stream_args.offset = 0;
    QuicFrameV.build_stream_args.data = DATA;
    QuicFrameV.build_stream_args.len = sizeof(DATA);
    QuicFrameV.build_stream_args.fin = PROTO_FALSE;
    QuicFrame.build_stream(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicFrameV.n);
    QuicFrameV.build_max_data_args.out = g_out;
    QuicFrameV.build_max_data_args.cap = 1;
    QuicFrameV.build_max_data_args.max = 1048576u;
    QuicFrame.build_max_data(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicFrameV.n);
    QuicFrameV.build_connection_close_args.out = g_out;
    QuicFrameV.build_connection_close_args.cap = 3;
    QuicFrameV.build_connection_close_args.app = PROTO_FALSE;
    QuicFrameV.build_connection_close_args.error_code = 0;
    QuicFrameV.build_connection_close_args.frame_type = 0;
    QuicFrameV.build_connection_close_args.reason = "reason";
    QuicFrameV.build_connection_close_args.reason_len = 6;
    QuicFrame.build_connection_close(quic_frame_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicFrameV.n);
}
