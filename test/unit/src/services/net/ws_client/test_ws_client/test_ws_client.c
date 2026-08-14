// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the WebSocket client codec (services/net/ws_client/ws_client.h).
//
// Two cases carry this module, and both are octet sequences RFC 6455 prints verbatim.
// test_rfc6455_accept_for_the_published_key reproduces the sec 1.3 worked example: the key
// "dGhlIHNhbXBsZSBub25jZQ==" yields "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", and a client that computes any
// other value rejects every conforming server. test_rfc6455_masked_text_frame reproduces the sec
// 5.7 example frame 0x81 0x85 0x37 0xfa 0x21 0x3d 0x7f 0x9f 0x4d 0x51 0x58, which fixes the FIN and
// opcode bits, the MASK bit, the Masking-key placement and the sec 5.3 transformation together.
//
// The extended payload length cases below are the sec 5.7 256-octet and 64 KiB frames.

#include "services/net/ws_client/ws_client.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static char g_accept[PROTOCORE_WS_ACCEPT_CAP];

static const char *accept_for(const char *key, char *out, size_t cap)
{
    WsClient.handshake.key = key;
    WsClient.handshake.accept = out;
    WsClient.handshake.accept_cap = cap;
    WsClient.accept_for_key(WsClient.internal);
    return out;
}

static size_t build_handshake(uint8_t *out, size_t cap, const char *host, const char *resource, const char *key,
                              const char *subprotocol)
{
    WsClient.handshake.host = host;
    WsClient.handshake.resource_name = resource;
    WsClient.handshake.key = key;
    WsClient.handshake.subprotocol = subprotocol;
    WsClient.buf.out = out;
    WsClient.buf.cap = cap;
    WsClient.build_opening_handshake(WsClient.internal);
    return WsClient.n;
}

// The handshake's accept member is both written by the computation and read by the check, so it is
// a writable buffer: a case that supplies its own expected value copies it in here first.
static char g_expect[64];

static proto_bool check_response(const char *response, const char *accept)
{
    WsClient.buf.in = (const uint8_t *)response;
    WsClient.buf.avail = strlen(response);
    if (accept)
    {
        size_t i = 0;
        for (; accept[i] && i + 1 < sizeof(g_expect); i++)
        {
            g_expect[i] = accept[i];
        }
        g_expect[i] = '\0';
    }
    WsClient.handshake.accept = accept ? g_expect : NULL;
    WsClient.check_server_handshake(WsClient.internal);
    return WsClient.ok;
}

static size_t build_frame(uint8_t *out, size_t cap, uint8_t opcode, const uint8_t *payload, size_t len,
                          const uint8_t *mask)
{
    WsClient.buf.out = out;
    WsClient.buf.cap = cap;
    WsClient.frame.opcode = opcode;
    WsClient.frame.payload = payload;
    WsClient.frame.payload_len = len;
    WsClient.frame.masking_key = mask;
    WsClient.build_frame(WsClient.internal);
    return WsClient.n;
}

static proto_bool parse_frame(const uint8_t *in, size_t avail)
{
    WsClient.buf.in = in;
    WsClient.buf.avail = avail;
    WsClient.parse_frame(WsClient.internal);
    return WsClient.ok;
}

// RFC 6455 sec 1.3: "the |Sec-WebSocket-Key| header field had the value 'dGhlIHNhbXBsZSBub25jZQ==',
// the server would concatenate ... '258EAFA5-E914-47DA-95CA-C5AB0DC85B11' ... the base64 encoding
// of [the SHA-1] is 's3pPLMBiTxaQ9kYGzzhZRbK+xOo='."
void test_rfc6455_accept_for_the_published_key(void)
{
    TEST_ASSERT_EQUAL_STRING("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=",
                             accept_for("dGhlIHNhbXBsZSBub25jZQ==", g_accept, sizeof(g_accept)));

    // 28 base64 characters: SHA-1 is 20 octets and RFC 4648 sec 4 encodes 20 octets as 28,
    // the last two of them padding.
    TEST_ASSERT_EQUAL_size_t(28, strlen(g_accept));
    TEST_ASSERT_EQUAL_CHAR('=', g_accept[27]);

    // A different key gives a different accept: the key is what the digest is over.
    char other[PROTOCORE_WS_ACCEPT_CAP];
    (void)accept_for("x3JJHMbDL1EzLkh9GBhXDw==", other, sizeof(other));
    TEST_ASSERT_EQUAL_size_t(28, strlen(other));
    TEST_ASSERT_TRUE(strcmp(other, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != 0);
}

// RFC 6455 sec 4.1: the client's opening handshake is a GET on the /resource name/ with a Host
// field, an Upgrade field of "websocket", a Connection field of "Upgrade", the |Sec-WebSocket-Key|
// and |Sec-WebSocket-Version| 13, ending with the empty line that ends any HTTP message header
// (RFC 9112 sec 2.1).
void test_rfc6455_opening_handshake_fields(void)
{
    uint8_t buf[256];
    const size_t n = build_handshake(buf, sizeof(buf), "example.com", "/chat", "dGhlIHNhbXBsZSBub25jZQ==", NULL);
    TEST_ASSERT_TRUE(n > 0);
    buf[n] = '\0';
    const char *s = (const char *)buf;

    TEST_ASSERT_EQUAL_INT(0, strncmp(s, "GET /chat HTTP/1.1\r\n", 20));
    TEST_ASSERT_NOT_NULL(strstr(s, "\r\nHost: example.com\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(s, "\r\nUpgrade: websocket\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(s, "\r\nConnection: Upgrade\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(s, "\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(s, "\r\nSec-WebSocket-Version: 13\r\n\r\n"));
    // The empty line is the last thing in the request.
    TEST_ASSERT_EQUAL_INT(0, strcmp(s + n - 4, "\r\n\r\n"));
}

// RFC 6455 sec 4.1: |Sec-WebSocket-Protocol| is optional and is sent only when the client requests
// a subprotocol. A pointer to an empty string names none.
void test_subprotocol_is_offered_only_when_named(void)
{
    uint8_t buf[256];
    size_t n = build_handshake(buf, sizeof(buf), "router.example", "/ws", "dGhlIHNhbXBsZSBub25jZQ==", "wamp.2.json");
    TEST_ASSERT_TRUE(n > 0);
    buf[n] = '\0';
    TEST_ASSERT_NOT_NULL(strstr((const char *)buf, "\r\nSec-WebSocket-Protocol: wamp.2.json\r\n"));

    n = build_handshake(buf, sizeof(buf), "example.com", "/chat", "dGhlIHNhbXBsZSBub25jZQ==", NULL);
    buf[n] = '\0';
    TEST_ASSERT_NULL(strstr((const char *)buf, "Sec-WebSocket-Protocol"));

    n = build_handshake(buf, sizeof(buf), "example.com", "/chat", "dGhlIHNhbXBsZSBub25jZQ==", "");
    buf[n] = '\0';
    TEST_ASSERT_NULL(strstr((const char *)buf, "Sec-WebSocket-Protocol"));
}

// RFC 6455 sec 4.1: the client must fail the connection unless the response is 101 and its
// |Sec-WebSocket-Accept| equals the value computed from the key it sent.
void test_rfc6455_server_handshake_is_verified(void)
{
    static const char *const GOOD = "HTTP/1.1 101 Switching Protocols\r\n"
                                    "Upgrade: websocket\r\n"
                                    "Connection: Upgrade\r\n"
                                    "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n";
    TEST_ASSERT_TRUE(check_response(GOOD, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));

    // One character different: same length, so only the comparison itself separates them.
    static const char *const WRONG = "HTTP/1.1 101 Switching Protocols\r\n"
                                     "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOx=\r\n\r\n";
    TEST_ASSERT_FALSE(check_response(WRONG, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));

    // A shorter value is not a prefix match either.
    static const char *const SHORT = "HTTP/1.1 101 Switching Protocols\r\n"
                                     "Sec-WebSocket-Accept: s3pPLMBiTxaQ\r\n\r\n";
    TEST_ASSERT_FALSE(check_response(SHORT, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));

    // Any status other than 101 fails the connection, and so does a 101 with no Accept field.
    TEST_ASSERT_FALSE(check_response("HTTP/1.1 400 Bad Request\r\n\r\n", "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
    TEST_ASSERT_FALSE(check_response("HTTP/1.0 100 Continue\r\n\r\n", "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
    TEST_ASSERT_FALSE(check_response("HTTP/1.1 101 OK\r\nUpgrade: websocket\r\n\r\n", "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));

    // RFC 9110 sec 5.1: field names are case-insensitive, and RFC 9112 sec 5 allows optional
    // whitespace between the colon and the value.
    static const char *const TABBED = "HTTP/1.1 101 Switching Protocols\r\n"
                                      "sec-websocket-accept:\ts3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n";
    TEST_ASSERT_TRUE(check_response(TABBED, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));

    // A longer field name that merely starts with the one being looked for is a different field.
    static const char *const PREFIX = "HTTP/1.1 101 Switching Protocols\r\n"
                                      "Sec-WebSocket-Accept-Extra: bogus\r\n"
                                      "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n";
    TEST_ASSERT_TRUE(check_response(PREFIX, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
}

// RFC 6455 sec 5.7: "A single-frame masked text message ...
//     0x81 0x85 0x37 0xfa 0x21 0x3d 0x7f 0x9f 0x4d 0x51 0x58 (contains 'Hello')"
//
// 0x81 = FIN set, opcode 1 (Text). 0x85 = MASK set, Payload len 5. Then the Masking-key, then
// sec 5.3's transformation, octet i XORed with key octet i modulo 4:
//   'H' 0x48 ^ 0x37 = 0x7f   'e' 0x65 ^ 0xfa = 0x9f   'l' 0x6c ^ 0x21 = 0x4d
//   'l' 0x6c ^ 0x3d = 0x51   'o' 0x6f ^ 0x37 = 0x58
void test_rfc6455_masked_text_frame(void)
{
    static const uint8_t MASK[4] = {0x37, 0xfa, 0x21, 0x3d};
    static const uint8_t WANT[] = {0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58};
    uint8_t buf[32];
    const size_t n = build_frame(buf, sizeof(buf), (uint8_t)WSC_OP_TEXT, (const uint8_t *)"Hello", 5, MASK);
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, sizeof(WANT));
}

// RFC 6455 sec 5.7: "A single-frame unmasked text message ...
//     0x81 0x05 0x48 0x65 0x6c 0x6c 0x6f (contains 'Hello')"
// A server frame carries no Masking-key (sec 5.1), so Payload data starts at octet 2.
void test_rfc6455_parse_unmasked_text_frame(void)
{
    static const uint8_t FRAME[] = {0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f};
    TEST_ASSERT_TRUE(parse_frame(FRAME, sizeof(FRAME)));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)WSC_OP_TEXT, WsClient.frame.opcode);
    TEST_ASSERT_TRUE(WsClient.frame.fin);
    TEST_ASSERT_EQUAL_size_t(2, WsClient.frame.payload_off);
    TEST_ASSERT_EQUAL_size_t(5, WsClient.frame.payload_len);
    TEST_ASSERT_EQUAL_size_t(7, WsClient.frame.consumed);
    TEST_ASSERT_EQUAL_MEMORY("Hello", FRAME + WsClient.frame.payload_off, 5);
}

// RFC 6455 sec 5.7: "A fragmented unmasked text message ...
//     0x01 0x03 0x48 0x65 0x6c (contains 'Hel')
//     0x80 0x02 0x6c 0x6f (contains 'lo')"
// sec 5.4: the first fragment carries the message's opcode with FIN clear, and the last carries
// opcode 0 (Continuation) with FIN set.
void test_rfc6455_fragmented_message(void)
{
    static const uint8_t FIRST[] = {0x01, 0x03, 0x48, 0x65, 0x6c};
    TEST_ASSERT_TRUE(parse_frame(FIRST, sizeof(FIRST)));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)WSC_OP_TEXT, WsClient.frame.opcode);
    TEST_ASSERT_FALSE(WsClient.frame.fin);
    TEST_ASSERT_EQUAL_size_t(3, WsClient.frame.payload_len);
    TEST_ASSERT_EQUAL_size_t(5, WsClient.frame.consumed);

    static const uint8_t LAST[] = {0x80, 0x02, 0x6c, 0x6f};
    TEST_ASSERT_TRUE(parse_frame(LAST, sizeof(LAST)));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)WSC_OP_CONT, WsClient.frame.opcode);
    TEST_ASSERT_TRUE(WsClient.frame.fin);
    TEST_ASSERT_EQUAL_size_t(2, WsClient.frame.payload_len);
    TEST_ASSERT_EQUAL_size_t(4, WsClient.frame.consumed);
}

// RFC 6455 sec 5.7: "Unmasked Ping request and masked Ping response ...
//     0x89 0x05 0x48 0x65 0x6c 0x6c 0x6f
//     0x8a 0x85 0x37 0xfa 0x21 0x3d 0x7f 0x9f 0x4d 0x51 0x58"
// sec 5.5: control frames have opcode 0x8 (Close), 0x9 (Ping) or 0xA (Pong).
void test_rfc6455_control_frames(void)
{
    static const uint8_t PING[] = {0x89, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f};
    TEST_ASSERT_TRUE(parse_frame(PING, sizeof(PING)));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)WSC_OP_PING, WsClient.frame.opcode);
    TEST_ASSERT_TRUE(WsClient.frame.fin);

    // The masked Pong the RFC pairs with it is exactly what this end builds for opcode 0xA.
    static const uint8_t MASK[4] = {0x37, 0xfa, 0x21, 0x3d};
    static const uint8_t PONG[] = {0x8a, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58};
    uint8_t buf[32];
    const size_t n = build_frame(buf, sizeof(buf), (uint8_t)WSC_OP_PONG, (const uint8_t *)"Hello", 5, MASK);
    TEST_ASSERT_EQUAL_size_t(sizeof(PONG), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PONG, buf, sizeof(PONG));

    // A Close frame with no Application data: sec 5.5.1 allows an empty body.
    const size_t c = build_frame(buf, sizeof(buf), (uint8_t)WSC_OP_CLOSE, NULL, 0, MASK);
    TEST_ASSERT_EQUAL_size_t(6, c); // 2 header octets and the 4-octet Masking-key
    TEST_ASSERT_EQUAL_HEX8(0x88, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x80, buf[1]);
}

// RFC 6455 sec 5.2: a Payload len of 0 through 125 is the length itself; 126 means the next two
// octets are a 16-bit length; 127 means the next eight are a 64-bit one, most significant octet
// first. The boundary is what the sizes below straddle.
void test_rfc6455_payload_length_forms(void)
{
    static const uint8_t MASK[4] = {0, 0, 0, 0};
    static uint8_t payload[126];
    uint8_t buf[256];
    memset(payload, 'a', sizeof(payload));

    // 125 octets: the last length that fits the 7-bit field.
    size_t n = build_frame(buf, sizeof(buf), (uint8_t)WSC_OP_BINARY, payload, 125, MASK);
    TEST_ASSERT_EQUAL_size_t(2 + 4 + 125, n);
    TEST_ASSERT_EQUAL_HEX8(0x82, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x80 | 125, buf[1]);

    // 126 octets: the first length that needs the 16-bit form.
    n = build_frame(buf, sizeof(buf), (uint8_t)WSC_OP_BINARY, payload, 126, MASK);
    TEST_ASSERT_EQUAL_size_t(2 + 2 + 4 + 126, n);
    TEST_ASSERT_EQUAL_HEX8(0x80 | 126, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(126, buf[3]);
}

// RFC 6455 sec 5.7: "256 bytes binary message in a single unmasked frame
//     0x82 0x7E 0x0100 [256 bytes of binary data]"
// The client end sets MASK (sec 5.3), so its second octet is 0x80|0x7E; the two length octets are
// the RFC's 0x01 0x00.
static uint8_t g_pl256[256];
void test_rfc6455_256_octet_frame(void)
{
    static const uint8_t MASK[4] = {0, 0, 0, 0};
    static uint8_t out[300];
    memset(g_pl256, 0x5A, sizeof(g_pl256));
    const size_t n = build_frame(out, sizeof(out), (uint8_t)WSC_OP_BINARY, g_pl256, sizeof(g_pl256), MASK);
    TEST_ASSERT_EQUAL_size_t(2 + 2 + 4 + 256, n);
    TEST_ASSERT_EQUAL_HEX8(0x82, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFE, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[3]);

    // The RFC's own unmasked form parses to the same 256 octets, four into the frame.
    static uint8_t server[4 + 256];
    server[0] = 0x82;
    server[1] = 0x7E;
    server[2] = 0x01;
    server[3] = 0x00;
    memset(server + 4, 0x5A, 256);
    TEST_ASSERT_TRUE(parse_frame(server, sizeof(server)));
    TEST_ASSERT_EQUAL_size_t(4, WsClient.frame.payload_off);
    TEST_ASSERT_EQUAL_size_t(256, WsClient.frame.payload_len);
    TEST_ASSERT_EQUAL_size_t(260, WsClient.frame.consumed);
}

// RFC 6455 sec 5.7: "64KiB binary message in a single unmasked frame
//     0x82 0x7F 0x0000000000010000 [65536 bytes of binary data]"
static uint8_t g_pl64k[65536];
static uint8_t g_out64k[65536 + 14];
void test_rfc6455_64kib_frame(void)
{
    static const uint8_t MASK[4] = {1, 2, 3, 4};
    static const uint8_t LEN64[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
    memset(g_pl64k, 0x11, sizeof(g_pl64k));
    const size_t n = build_frame(g_out64k, sizeof(g_out64k), (uint8_t)WSC_OP_BINARY, g_pl64k, sizeof(g_pl64k), MASK);
    TEST_ASSERT_EQUAL_size_t(2 + 8 + 4 + 65536, n);
    TEST_ASSERT_EQUAL_HEX8(0x82, g_out64k[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, g_out64k[1]); // MASK set, Payload len 127
    TEST_ASSERT_EQUAL_HEX8_ARRAY(LEN64, g_out64k + 2, 8);

    // Read back the server's unmasked form of the same message.
    g_out64k[0] = 0x82;
    g_out64k[1] = 0x7F;
    memcpy(g_out64k + 2, LEN64, 8);
    memset(g_out64k + 10, 0x11, 65536);
    TEST_ASSERT_TRUE(parse_frame(g_out64k, 10 + 65536));
    TEST_ASSERT_EQUAL_size_t(10, WsClient.frame.payload_off);
    TEST_ASSERT_EQUAL_size_t(65536, WsClient.frame.payload_len);
    TEST_ASSERT_EQUAL_size_t(65546, WsClient.frame.consumed);
}

// A frame is delivered only once all of it has arrived: an announced length longer than what is
// present is refused, in the short form and in both extended forms.
void test_parse_refuses_an_incomplete_frame(void)
{
    static const uint8_t SHORT[] = {0x81, 0x05, 'h', 'e'}; // says 5 octets, 2 present
    TEST_ASSERT_FALSE(parse_frame(SHORT, sizeof(SHORT)));

    static const uint8_t ONE[] = {0x81};
    TEST_ASSERT_FALSE(parse_frame(ONE, sizeof(ONE))); // no Payload len octet at all
    TEST_ASSERT_FALSE(parse_frame(NULL, 2));

    static const uint8_t NO_LEN16[] = {0x82, 126}; // 16-bit length announced, absent
    TEST_ASSERT_FALSE(parse_frame(NO_LEN16, sizeof(NO_LEN16)));

    static const uint8_t NO_LEN64[] = {0x82, 127}; // 64-bit length announced, absent
    TEST_ASSERT_FALSE(parse_frame(NO_LEN64, sizeof(NO_LEN64)));

    static const uint8_t LEN64_NO_BODY[] = {0x82, 127, 0, 0, 0, 0, 0, 0, 0, 100};
    TEST_ASSERT_FALSE(parse_frame(LEN64_NO_BODY, sizeof(LEN64_NO_BODY)));
}

// RFC 6455 sec 5.2 gives the 64-bit length its most significant bit as 0 and lets it name more
// octets than any buffer on a constrained part holds. Anything above 32 bits is refused rather
// than truncated into a size_t.
void test_parse_refuses_an_oversized_payload_length(void)
{
    static const uint8_t HUGE[] = {0x82, 127, 0x00, 0x00, 0x00, 0x01, 0, 0, 0, 0}; // 2^32
    TEST_ASSERT_FALSE(parse_frame(HUGE, sizeof(HUGE)));
}

// RFC 6455 sec 5.1 says a server does not mask, but a frame that sets MASK still carries a
// 4-octet Masking-key, so Payload data starts four octets later.
void test_parse_stays_aligned_past_a_masking_key(void)
{
    static const uint8_t MASKED[] = {0x81, 0x82, 0, 0, 0, 0, 'a', 'b'};
    TEST_ASSERT_TRUE(parse_frame(MASKED, sizeof(MASKED)));
    TEST_ASSERT_EQUAL_size_t(6, WsClient.frame.payload_off);
    TEST_ASSERT_EQUAL_size_t(2, WsClient.frame.payload_len);
    TEST_ASSERT_EQUAL_size_t(8, WsClient.frame.consumed);
}

// A frame is built whole or not at all: RFC 6455 sec 5.3 makes the Masking-key mandatory on this
// end, and a buffer that cannot hold header plus Payload data yields nothing.
void test_build_frame_fails_closed(void)
{
    static const uint8_t MASK[4] = {1, 2, 3, 4};
    uint8_t buf[16];
    TEST_ASSERT_EQUAL_size_t(0, build_frame(NULL, sizeof(buf), (uint8_t)WSC_OP_TEXT, (const uint8_t *)"x", 1, MASK));
    TEST_ASSERT_EQUAL_size_t(0, build_frame(buf, sizeof(buf), (uint8_t)WSC_OP_TEXT, (const uint8_t *)"x", 1, NULL));
    TEST_ASSERT_EQUAL_size_t(0, build_frame(buf, 4, (uint8_t)WSC_OP_TEXT, (const uint8_t *)"hello", 5, MASK));
    // Exactly enough room is enough: 2 header octets, the key, and the data.
    TEST_ASSERT_EQUAL_size_t(11, build_frame(buf, 11, (uint8_t)WSC_OP_TEXT, (const uint8_t *)"hello", 5, MASK));
}

// The accept computation fails closed rather than leaving a stale or partial value: no key, no
// destination, a destination too small for the 28 characters plus terminator, or a key so long it
// cannot be concatenated with the GUID.
void test_accept_for_key_fails_closed(void)
{
    char out[64];
    out[0] = 'x';
    TEST_ASSERT_EQUAL_STRING("", accept_for(NULL, out, sizeof(out)));

    (void)accept_for("key", NULL, 10); // no destination to write
    (void)accept_for("key", out, 0);   // no room at all

    char small[PROTOCORE_WS_ACCEPT_CAP - 1];
    small[0] = 'x';
    TEST_ASSERT_EQUAL_STRING("", accept_for("dGhlIHNhbXBsZSBub25jZQ==", small, sizeof(small)));

    char long_key[80];
    memset(long_key, 'a', sizeof(long_key) - 1);
    long_key[sizeof(long_key) - 1] = '\0';
    out[0] = 'x';
    TEST_ASSERT_EQUAL_STRING("", accept_for(long_key, out, sizeof(out)));
}

// The handshake needs all four of its terms and a buffer that holds the whole request; anything
// less writes nothing.
void test_build_handshake_fails_closed(void)
{
    uint8_t out[256];
    TEST_ASSERT_EQUAL_size_t(0, build_handshake(NULL, sizeof(out), "h", "/", "k", NULL));
    TEST_ASSERT_EQUAL_size_t(0, build_handshake(out, sizeof(out), NULL, "/", "k", NULL));
    TEST_ASSERT_EQUAL_size_t(0, build_handshake(out, sizeof(out), "h", NULL, "k", NULL));
    TEST_ASSERT_EQUAL_size_t(0, build_handshake(out, sizeof(out), "h", "/", NULL, NULL));
    TEST_ASSERT_EQUAL_size_t(0, build_handshake(out, 10, "host", "/path", "key", NULL));
}

// check_server_handshake refuses what it cannot read: no octets, fewer than a status-line's worth,
// no line ending, or no accept value to compare against.
void test_check_server_handshake_fails_closed(void)
{
    WsClient.buf.in = NULL;
    WsClient.buf.avail = 100;
    WsClient.handshake.accept = g_accept;
    WsClient.check_server_handshake(WsClient.internal);
    TEST_ASSERT_FALSE(WsClient.ok);

    TEST_ASSERT_FALSE(check_response("abcd", "acc"));                   // shorter than "HTTP/1.1 101"
    TEST_ASSERT_FALSE(check_response("HTTP/1.1 101 Switching", "acc")); // no line ending
    TEST_ASSERT_FALSE(check_response("HTTP/1.1 101 x\r\n\r\n", NULL));  // nothing to compare against
    TEST_ASSERT_FALSE(check_response("HTTP/1.1 101 Switching Protocols\r\nSec-WebSocket-Accept:   ", "x"));
}

// This build carries no transport, so every connection call answers no and none of them touches
// the codec's buffers.
void test_transport_reports_no_connection(void)
{
    WsClient.msg.on_message = NULL;
    WsClient.on_message(WsClient.internal);

    WsClient.handshake.host = "example.com";
    WsClient.handshake.port = 80;
    WsClient.handshake.secure = PROTO_FALSE;
    WsClient.handshake.resource_name = "/";
    WsClient.connect(WsClient.internal);
    TEST_ASSERT_FALSE(WsClient.ok);

    WsClient.msg.text = "hi";
    WsClient.send_text(WsClient.internal);
    TEST_ASSERT_FALSE(WsClient.ok);

    WsClient.msg.data = (const uint8_t *)"x";
    WsClient.msg.len = 1;
    WsClient.send_binary(WsClient.internal);
    TEST_ASSERT_FALSE(WsClient.ok);

    WsClient.loop(WsClient.internal);
    TEST_ASSERT_FALSE(WsClient.ok);

    WsClient.connected(WsClient.internal);
    TEST_ASSERT_FALSE(WsClient.ok);

    WsClient.close(WsClient.internal);
}
