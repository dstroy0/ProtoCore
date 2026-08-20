// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the gRPC-Web framing codec (services/iot/grpcweb/grpcweb.h).
//
// The governing documents are the gRPC project's own, not IETF RFCs: PROTOCOL-HTTP2.md publishes the
// frame grammar "Length-Prefixed-Message -> Compressed-Flag Message-Length Message" with
// Message-Length "encoded as 4 byte unsigned integer (big endian)", and PROTOCOL-WEB.md publishes
// the 8th (MSB) bit of the 1st gRPC frame byte as 0 for data and 1 for trailers, spelled out as
// "10000000b: an uncompressed trailer" and "10000001b: a compressed trailer". The trailer-section
// itself is RFC 9112 sec 7.1.2 field-lines, which PROTOCOL-WEB.md requires to carry lower-case names.
//
// test_message_length_is_four_octets_big_endian is load-bearing: a 258-octet Message pins all four
// octets of the published big-endian length field, and an implementation that writes it in any other
// width or order desynchronizes the stream on the very first frame.

#include "services/iot/grpcweb/grpcweb.h"
#include <string.h>

#include <unity.h>

static uint8_t grpcweb_work[16]; // the borrow an entry takes; GrpcWeb never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static uint8_t g_buf[1024];

static size_t frame_message(const uint8_t *body, size_t len, proto_bool compressed)
{
    GrpcWebV.out.buf = g_buf;
    GrpcWebV.out.cap = sizeof(g_buf);
    GrpcWebV.msg.body = body;
    GrpcWebV.msg.body_len = len;
    GrpcWebV.msg.compressed = compressed;
    GrpcWeb.frame_message(grpcweb_work);
    return GrpcWebV.n;
}

static size_t frame_trailers(int32_t status, const char *message)
{
    GrpcWebV.out.buf = g_buf;
    GrpcWebV.out.cap = sizeof(g_buf);
    GrpcWebV.trailers.status = status;
    GrpcWebV.trailers.message = message;
    GrpcWeb.frame_trailers(grpcweb_work);
    return GrpcWebV.n;
}

static proto_bool parse(const uint8_t *data, size_t len)
{
    GrpcWebV.in.data = data;
    GrpcWebV.in.len = len;
    GrpcWeb.parse(grpcweb_work);
    return GrpcWebV.ok;
}

// PROTOCOL-HTTP2.md "Requests": Compressed-Flag is 1 byte, Message-Length is 4 bytes big endian.
// 258 = 0x00000102, so the four octets are 00 00 00 01 02 and not the little-endian spelling.
void test_message_length_is_four_octets_big_endian(void)
{
    static uint8_t body[258];
    for (size_t i = 0; i < sizeof(body); i++)
    {
        body[i] = (uint8_t)i;
    }
    TEST_ASSERT_EQUAL_UINT(5u, (unsigned)PROTOCORE_GRPCWEB_PREFIX_LEN);
    TEST_ASSERT_EQUAL_UINT(5u + 258u, frame_message(body, sizeof(body), PROTO_FALSE));
    TEST_ASSERT_TRUE(GrpcWebV.ok);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_buf[0]); // Compressed-Flag 0
    TEST_ASSERT_EQUAL_HEX8(0x00, g_buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01, g_buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x02, g_buf[4]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, g_buf + 5, sizeof(body));

    // The same octets read back give the length again.
    TEST_ASSERT_TRUE(parse(g_buf, 5 + sizeof(body)));
    TEST_ASSERT_EQUAL_UINT(258u, GrpcWebV.parsed.body_len);
    TEST_ASSERT_EQUAL_UINT(5u + 258u, GrpcWebV.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, GrpcWebV.parsed.body, sizeof(body));
}

// PROTOCOL-HTTP2.md: "Compressed-Flag -> 0 / 1 ; encoded as 1 byte unsigned integer", bit 0 of the
// frame byte, with the MSB left clear on a data frame.
void test_compressed_flag_is_bit_zero_of_the_frame_byte(void)
{
    static const uint8_t BODY[3] = {0x0a, 0x0b, 0x0c};
    TEST_ASSERT_EQUAL_HEX8(0x01, PROTOCORE_GRPCWEB_COMPRESSED);

    TEST_ASSERT_EQUAL_UINT(8u, frame_message(BODY, sizeof(BODY), PROTO_FALSE));
    TEST_ASSERT_EQUAL_HEX8(0x00, g_buf[0]);
    TEST_ASSERT_TRUE(parse(g_buf, 8));
    TEST_ASSERT_FALSE(GrpcWebV.parsed.compressed);
    TEST_ASSERT_FALSE(GrpcWebV.parsed.trailers);

    TEST_ASSERT_EQUAL_UINT(8u, frame_message(BODY, sizeof(BODY), PROTO_TRUE));
    TEST_ASSERT_EQUAL_HEX8(0x01, g_buf[0]);
    TEST_ASSERT_TRUE(parse(g_buf, 8));
    TEST_ASSERT_TRUE(GrpcWebV.parsed.compressed);
    TEST_ASSERT_FALSE(GrpcWebV.parsed.trailers);
}

// PROTOCOL-WEB.md, "Message framing": the 8th (MSB) bit of the 1st gRPC frame byte is 0 for data and
// 1 for trailers, and the two published spellings are 10000000b and 10000001b.
void test_msb_of_the_frame_byte_is_the_trailers_bit(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x80, PROTOCORE_GRPCWEB_TRAILERS);

    static const uint8_t UNCOMPRESSED_TRAILER[6] = {0x80, 0x00, 0x00, 0x00, 0x01, 'x'};
    TEST_ASSERT_TRUE(parse(UNCOMPRESSED_TRAILER, sizeof(UNCOMPRESSED_TRAILER)));
    TEST_ASSERT_TRUE(GrpcWebV.parsed.trailers);
    TEST_ASSERT_FALSE(GrpcWebV.parsed.compressed);
    TEST_ASSERT_EQUAL_HEX8(0x80, GrpcWebV.parsed.flags);

    static const uint8_t COMPRESSED_TRAILER[6] = {0x81, 0x00, 0x00, 0x00, 0x01, 'x'};
    TEST_ASSERT_TRUE(parse(COMPRESSED_TRAILER, sizeof(COMPRESSED_TRAILER)));
    TEST_ASSERT_TRUE(GrpcWebV.parsed.trailers);
    TEST_ASSERT_TRUE(GrpcWebV.parsed.compressed);
    TEST_ASSERT_EQUAL_HEX8(0x81, GrpcWebV.parsed.flags);

    // A build stamps the same MSB.
    TEST_ASSERT_TRUE(frame_trailers(0, NULL) > 0);
    TEST_ASSERT_EQUAL_HEX8(0x80, g_buf[0]);
}

// PROTOCOL-WEB.md "HTTP wire protocols" item 2: the trailers in the last length-prefixed message
// "must always use lower-case names", laid out as RFC 9112 sec 7.1.2 `*( field-line CRLF )`, and
// PROTOCOL-HTTP2.md "Responses" names the two fields grpc-status and grpc-message.
void test_trailer_section_is_lower_case_field_lines(void)
{
    static const char WANT[] = "grpc-status:0\r\ngrpc-message:ok\r\n";
    const size_t want_len = sizeof(WANT) - 1;
    TEST_ASSERT_EQUAL_UINT(5u + want_len, frame_trailers(0, "ok"));
    TEST_ASSERT_TRUE(GrpcWebV.ok);
    TEST_ASSERT_EQUAL_HEX8(0x80, g_buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_buf[3]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)want_len, g_buf[4]);
    TEST_ASSERT_EQUAL_MEMORY(WANT, g_buf + 5, want_len);

    // A null or empty Status-Message omits the whole grpc-message line.
    static const char ONLY_STATUS[] = "grpc-status:5\r\n";
    TEST_ASSERT_EQUAL_UINT(5u + sizeof(ONLY_STATUS) - 1, frame_trailers(5, NULL));
    TEST_ASSERT_EQUAL_MEMORY(ONLY_STATUS, g_buf + 5, sizeof(ONLY_STATUS) - 1);
    TEST_ASSERT_EQUAL_UINT(5u + sizeof(ONLY_STATUS) - 1, frame_trailers(5, ""));
    TEST_ASSERT_EQUAL_MEMORY(ONLY_STATUS, g_buf + 5, sizeof(ONLY_STATUS) - 1);
}

// PROTOCOL-HTTP2.md "Responses": `Status -> "grpc-status" 1*DIGIT`. The values are the gRPC status
// code registry, so each is written as decimal digits with no leading zero and read back exactly.
void test_status_round_trips_the_published_code_values(void)
{
    // The gRPC status codes, by name and number, as the project publishes them.
    static const int32_t CODE[] = {
        0,  // OK
        1,  // CANCELLED
        2,  // UNKNOWN
        3,  // INVALID_ARGUMENT
        4,  // DEADLINE_EXCEEDED
        5,  // NOT_FOUND
        7,  // PERMISSION_DENIED
        12, // UNIMPLEMENTED
        14, // UNAVAILABLE
        16, // UNAUTHENTICATED
    };
    for (size_t i = 0; i < sizeof(CODE) / sizeof(CODE[0]); i++)
    {
        const size_t total = frame_trailers(CODE[i], "why");
        TEST_ASSERT_TRUE(total > 0);
        TEST_ASSERT_TRUE(parse(g_buf, total));
        TEST_ASSERT_TRUE(GrpcWebV.parsed.trailers);

        GrpcWebV.in.data = GrpcWebV.parsed.body;
        GrpcWebV.in.len = GrpcWebV.parsed.body_len;
        GrpcWeb.trailers_status(grpcweb_work);
        TEST_ASSERT_TRUE(GrpcWebV.ok);
        TEST_ASSERT_EQUAL_INT32(CODE[i], GrpcWebV.i32);
    }
    // 1*DIGIT: two digits are written without a leading zero.
    TEST_ASSERT_TRUE(frame_trailers(12, NULL) > 0);
    TEST_ASSERT_EQUAL_MEMORY("grpc-status:12\r\n", g_buf + 5, 16);
}

// Status-Message is Percent-Encoded on the wire (PROTOCOL-HTTP2.md "Responses"), so the read hands
// back the field-value slice as it stands, ending at the field-line's CR.
void test_message_slice_stays_percent_encoded(void)
{
    static const char SECTION[] = "grpc-status:2\r\ngrpc-message:not%20found\r\n";
    GrpcWebV.in.data = (const uint8_t *)SECTION;
    GrpcWebV.in.len = sizeof(SECTION) - 1;
    GrpcWeb.trailers_message(grpcweb_work);
    TEST_ASSERT_TRUE(GrpcWebV.ok);
    TEST_ASSERT_EQUAL_UINT(11u, GrpcWebV.text_len);
    TEST_ASSERT_EQUAL_MEMORY("not%20found", GrpcWebV.text, 11);
    // The slice points into the caller's octets rather than a copy: "grpc-status:2\r\n" is 15
    // octets and "grpc-message:" is 13, so the field-value starts at offset 28.
    TEST_ASSERT_EQUAL_PTR(SECTION + 28, GrpcWebV.text);

    GrpcWeb.trailers_status(grpcweb_work);
    TEST_ASSERT_TRUE(GrpcWebV.ok);
    TEST_ASSERT_EQUAL_INT32(2, GrpcWebV.i32);
}

// A field-name is only a field-name at the start of a field-line (RFC 9112 sec 5), so the same text
// inside another field's value is not read as the Status.
void test_a_key_inside_a_value_is_not_a_field_name(void)
{
    static const char SECTION[] = "grpc-message:see grpc-status:9\r\n";
    GrpcWebV.in.data = (const uint8_t *)SECTION;
    GrpcWebV.in.len = sizeof(SECTION) - 1;
    GrpcWeb.trailers_status(grpcweb_work);
    TEST_ASSERT_FALSE(GrpcWebV.ok);
    TEST_ASSERT_EQUAL_INT32(0, GrpcWebV.i32);

    // A section with no Status at all, and one whose value is not 1*DIGIT.
    static const char NONE[] = "grpc-message:x\r\n";
    GrpcWebV.in.data = (const uint8_t *)NONE;
    GrpcWebV.in.len = sizeof(NONE) - 1;
    GrpcWeb.trailers_status(grpcweb_work);
    TEST_ASSERT_FALSE(GrpcWebV.ok);

    static const char NOT_A_DIGIT[] = "grpc-status:x\r\n";
    GrpcWebV.in.data = (const uint8_t *)NOT_A_DIGIT;
    GrpcWebV.in.len = sizeof(NOT_A_DIGIT) - 1;
    GrpcWeb.trailers_status(grpcweb_work);
    TEST_ASSERT_FALSE(GrpcWebV.ok);

    GrpcWebV.in.data = (const uint8_t *)NOT_A_DIGIT;
    GrpcWebV.in.len = sizeof(NOT_A_DIGIT) - 1;
    GrpcWeb.trailers_message(grpcweb_work);
    TEST_ASSERT_FALSE(GrpcWebV.ok);
    TEST_ASSERT_NULL(GrpcWebV.text);
    TEST_ASSERT_EQUAL_UINT(0u, GrpcWebV.text_len);
}

// A Message of zero octets is a legal frame: the bare 5-octet prefix with Message-Length 0.
void test_empty_message_is_a_bare_prefix(void)
{
    TEST_ASSERT_EQUAL_UINT(5u, frame_message(NULL, 0, PROTO_FALSE));
    TEST_ASSERT_TRUE(GrpcWebV.ok);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_buf[4]);
    TEST_ASSERT_TRUE(parse(g_buf, 5));
    TEST_ASSERT_EQUAL_UINT(0u, GrpcWebV.parsed.body_len);
    TEST_ASSERT_EQUAL_UINT(5u, GrpcWebV.n);
}

// A frame is decodable only once the prefix and the whole Message are buffered: the codec never
// reports a Message it has not yet seen every octet of.
void test_parse_waits_for_the_whole_message(void)
{
    static const uint8_t BODY[4] = {1, 2, 3, 4};
    const size_t total = frame_message(BODY, sizeof(BODY), PROTO_FALSE);
    TEST_ASSERT_EQUAL_UINT(9u, total);
    for (size_t have = 0; have < total; have++)
    {
        TEST_ASSERT_FALSE(parse(g_buf, have));
        TEST_ASSERT_EQUAL_UINT(0u, GrpcWebV.n);
    }
    TEST_ASSERT_TRUE(parse(g_buf, total));
    TEST_ASSERT_FALSE(parse(NULL, total));
}

// A response is zero or more data frames then the trailers frame, and the octet count each parse
// reports is what advances the reader to the next one.
void test_a_stream_walks_frame_by_frame(void)
{
    static const uint8_t A[2] = {0xaa, 0xbb};
    static const uint8_t B[1] = {0xcc};
    uint8_t stream[64];
    size_t n = 0;

    n += frame_message(A, sizeof(A), PROTO_FALSE);
    memcpy(stream, g_buf, n);
    size_t part = frame_message(B, sizeof(B), PROTO_TRUE);
    memcpy(stream + n, g_buf, part);
    n += part;
    part = frame_trailers(0, NULL);
    memcpy(stream + n, g_buf, part);
    n += part;

    size_t off = 0;
    TEST_ASSERT_TRUE(parse(stream + off, n - off));
    TEST_ASSERT_FALSE(GrpcWebV.parsed.trailers);
    TEST_ASSERT_EQUAL_UINT(2u, GrpcWebV.parsed.body_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(A, GrpcWebV.parsed.body, 2);
    off += GrpcWebV.n;

    TEST_ASSERT_TRUE(parse(stream + off, n - off));
    TEST_ASSERT_TRUE(GrpcWebV.parsed.compressed);
    TEST_ASSERT_EQUAL_UINT(1u, GrpcWebV.parsed.body_len);
    off += GrpcWebV.n;

    TEST_ASSERT_TRUE(parse(stream + off, n - off));
    TEST_ASSERT_TRUE(GrpcWebV.parsed.trailers);
    off += GrpcWebV.n;
    TEST_ASSERT_EQUAL_UINT(n, off);
}

// A caller-set frame byte is written through untouched, so a flag combination the codec has no named
// constant for still reaches the wire.
void test_frame_writes_the_given_frame_byte(void)
{
    static const uint8_t BODY[1] = {0x7f};
    GrpcWebV.out.buf = g_buf;
    GrpcWebV.out.cap = sizeof(g_buf);
    GrpcWebV.msg.body = BODY;
    GrpcWebV.msg.body_len = 1;
    GrpcWebV.msg.flags = 0x81;
    GrpcWeb.frame(grpcweb_work);
    TEST_ASSERT_TRUE(GrpcWebV.ok);
    TEST_ASSERT_EQUAL_UINT(6u, GrpcWebV.n);
    TEST_ASSERT_EQUAL_HEX8(0x81, g_buf[0]);
}

// A builder that cannot fit the whole frame writes nothing and says so.
void test_builders_refuse_a_short_buffer(void)
{
    static const uint8_t BODY[4] = {1, 2, 3, 4};
    uint8_t small[8];
    memset(small, 0xAA, sizeof(small));

    GrpcWebV.out.buf = small;
    GrpcWebV.out.cap = 8; // one short of the prefix plus four octets
    GrpcWebV.msg.body = BODY;
    GrpcWebV.msg.body_len = sizeof(BODY);
    GrpcWebV.msg.compressed = PROTO_FALSE;
    GrpcWeb.frame_message(grpcweb_work);
    TEST_ASSERT_FALSE(GrpcWebV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, GrpcWebV.n);
    TEST_ASSERT_EQUAL_HEX8(0xAA, small[0]);

    // A trailers frame that cannot even hold its prefix.
    GrpcWebV.out.buf = small;
    GrpcWebV.out.cap = 4;
    GrpcWebV.trailers.status = 0;
    GrpcWebV.trailers.message = NULL;
    GrpcWeb.frame_trailers(grpcweb_work);
    TEST_ASSERT_FALSE(GrpcWebV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, GrpcWebV.n);

    // Room for the prefix but not the whole grpc-status line.
    GrpcWebV.out.cap = 8;
    GrpcWeb.frame_trailers(grpcweb_work);
    TEST_ASSERT_FALSE(GrpcWebV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, GrpcWebV.n);

    // A body with no octets behind it, and a null destination.
    GrpcWebV.out.buf = g_buf;
    GrpcWebV.out.cap = sizeof(g_buf);
    GrpcWebV.msg.body = NULL;
    GrpcWebV.msg.body_len = 4;
    GrpcWeb.frame_message(grpcweb_work);
    TEST_ASSERT_FALSE(GrpcWebV.ok);

    GrpcWebV.out.buf = NULL;
    GrpcWebV.msg.body = BODY;
    GrpcWeb.frame_message(grpcweb_work);
    TEST_ASSERT_FALSE(GrpcWebV.ok);
}

// A negative Status has no 1*DIGIT spelling, so the trailers frame is refused rather than written
// with a sign the grammar does not allow.
void test_a_negative_status_is_refused(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, frame_trailers(-1, NULL));
    TEST_ASSERT_FALSE(GrpcWebV.ok);
}
