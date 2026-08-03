// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the NATS client protocol codec (services/iot/nats): the CONNECT/PUB/SUB/UNSUB/
// PING/PONG builders and the inbound MSG/INFO/PING/+OK/-ERR parser. Pure host tests.

#include "services/iot/nats/nats.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_build_connect()
{
    char buf[64];
    size_t n = pc_nats_build_connect(buf, sizeof(buf), "{\"verbose\":false}");
    TEST_ASSERT_EQUAL_STRING("CONNECT {\"verbose\":false}\r\n", buf);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
}

void test_build_pub()
{
    char buf[64];
    size_t n = pc_nats_build_pub(buf, sizeof(buf), "foo", NULL, (const uint8_t *)"hello", 5);
    TEST_ASSERT_EQUAL_STRING("PUB foo 5\r\nhello\r\n", buf);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
}

void test_build_pub_with_reply()
{
    char buf[64];
    size_t n = pc_nats_build_pub(buf, sizeof(buf), "req", "_INBOX.1", (const uint8_t *)"hi", 2);
    TEST_ASSERT_EQUAL_STRING("PUB req _INBOX.1 2\r\nhi\r\n", buf);
    TEST_ASSERT_GREATER_THAN(0, (int)n);
}

void test_build_pub_empty_payload()
{
    char buf[32];
    size_t n = pc_nats_build_pub(buf, sizeof(buf), "foo", NULL, NULL, 0);
    TEST_ASSERT_EQUAL_STRING("PUB foo 0\r\n\r\n", buf);
    TEST_ASSERT_GREATER_THAN(0, (int)n);
}

void test_build_sub_and_unsub()
{
    char buf[32];
    TEST_ASSERT_GREATER_THAN(0, (int)pc_nats_build_sub(buf, sizeof(buf), "foo", NULL, "1"));
    TEST_ASSERT_EQUAL_STRING("SUB foo 1\r\n", buf);
    TEST_ASSERT_GREATER_THAN(0, (int)pc_nats_build_sub(buf, sizeof(buf), "foo", "workers", "9"));
    TEST_ASSERT_EQUAL_STRING("SUB foo workers 9\r\n", buf);
    TEST_ASSERT_GREATER_THAN(0, (int)pc_nats_build_unsub(buf, sizeof(buf), "1", 5, PROTO_TRUE));
    TEST_ASSERT_EQUAL_STRING("UNSUB 1 5\r\n", buf);
    TEST_ASSERT_GREATER_THAN(0, (int)pc_nats_build_unsub(buf, sizeof(buf), "1", 0, PROTO_FALSE));
    TEST_ASSERT_EQUAL_STRING("UNSUB 1\r\n", buf);
}

void test_parse_msg()
{
    const char *raw = "MSG foo 1 5\r\nhello\r\nMSG bar 2 3\r\nbye\r\n";
    size_t len = strlen(raw);
    NatsMsg m;
    size_t c;
    TEST_ASSERT_TRUE(pc_nats_parse(raw, len, &m, &c));
    TEST_ASSERT_EQUAL(NATS_MSG, m.type);
    TEST_ASSERT_EQUAL_MEMORY("foo", m.subject, m.subject_len);
    TEST_ASSERT_EQUAL_MEMORY("1", m.sid, m.sid_len);
    TEST_ASSERT_EQUAL_size_t(0, m.reply_len);
    TEST_ASSERT_EQUAL_MEMORY("hello", m.payload, 5);
    // The second message follows at the consumed offset.
    size_t off = c;
    TEST_ASSERT_TRUE(pc_nats_parse(raw + off, len - off, &m, &c));
    TEST_ASSERT_EQUAL_MEMORY("bar", m.subject, 3);
    TEST_ASSERT_EQUAL_MEMORY("bye", m.payload, 3);
}

void test_parse_msg_with_reply()
{
    const char *raw = "MSG foo 1 _INBOX.7 5\r\nhello\r\n";
    NatsMsg m;
    size_t c;
    TEST_ASSERT_TRUE(pc_nats_parse(raw, strlen(raw), &m, &c));
    TEST_ASSERT_EQUAL(NATS_MSG, m.type);
    TEST_ASSERT_EQUAL_MEMORY("_INBOX.7", m.reply, m.reply_len);
    TEST_ASSERT_EQUAL_MEMORY("hello", m.payload, 5);
}

void test_build_hpub()
{
    char buf[128];
    const char *hdrs = "NATS/1.0\r\nX: 1\r\n\r\n"; // 18 octets
    size_t n = pc_nats_build_hpub(buf, sizeof(buf), "foo", NULL, hdrs, strlen(hdrs), (const uint8_t *)"hi", 2);
    const char *expect = "HPUB foo 18 20\r\nNATS/1.0\r\nX: 1\r\n\r\nhi\r\n"; // hdr_len 18, total_len 20
    TEST_ASSERT_EQUAL_size_t(strlen(expect), n);
    TEST_ASSERT_EQUAL_MEMORY(expect, buf, n);

    // Guards: null headers and a zero header length fail closed.
    TEST_ASSERT_EQUAL_size_t(0, pc_nats_build_hpub(buf, sizeof(buf), "foo", NULL, NULL, 5, (const uint8_t *)"h", 1));
    TEST_ASSERT_EQUAL_size_t(0, pc_nats_build_hpub(buf, sizeof(buf), "foo", NULL, hdrs, 0, NULL, 0));
}

void test_parse_hmsg()
{
    const char *raw = "HMSG foo 9 18 20\r\nNATS/1.0\r\nX: 1\r\n\r\nhi\r\n";
    NatsMsg m;
    size_t c;
    TEST_ASSERT_TRUE(pc_nats_parse(raw, strlen(raw), &m, &c));
    TEST_ASSERT_EQUAL(NATS_MSG, m.type);
    TEST_ASSERT_EQUAL_MEMORY("foo", m.subject, m.subject_len);
    TEST_ASSERT_EQUAL_MEMORY("9", m.sid, m.sid_len);
    TEST_ASSERT_EQUAL_size_t(18, m.headers_len);
    TEST_ASSERT_EQUAL_MEMORY("NATS/1.0\r\nX: 1\r\n\r\n", m.headers, 18);
    TEST_ASSERT_EQUAL_size_t(2, m.payload_len);
    TEST_ASSERT_EQUAL_MEMORY("hi", m.payload, 2);
    TEST_ASSERT_EQUAL_size_t(strlen(raw), c);

    // With a reply-to token.
    const char *raw2 = "HMSG foo 9 _INBOX.3 18 20\r\nNATS/1.0\r\nX: 1\r\n\r\nhi\r\n";
    TEST_ASSERT_TRUE(pc_nats_parse(raw2, strlen(raw2), &m, &c));
    TEST_ASSERT_EQUAL_MEMORY("_INBOX.3", m.reply, m.reply_len);
    TEST_ASSERT_EQUAL_size_t(18, m.headers_len);
    TEST_ASSERT_EQUAL_MEMORY("hi", m.payload, 2);

    // A header block larger than the total is rejected; a truncated HMSG is not yet a frame.
    TEST_ASSERT_FALSE(pc_nats_parse("HMSG foo 9 30 20\r\n\r\n", 19, &m, &c));
    TEST_ASSERT_FALSE(pc_nats_parse("HMSG foo 9 18 20\r\nNATS", 22, &m, &c));

    // A plain (header-less) MSG leaves headers null.
    TEST_ASSERT_TRUE(pc_nats_parse("MSG foo 1 2\r\nhi\r\n", 17, &m, &c));
    TEST_ASSERT_NULL(m.headers);
    TEST_ASSERT_EQUAL_size_t(0, m.headers_len);
}

void test_parse_control_lines()
{
    NatsMsg m;
    size_t c;
    TEST_ASSERT_TRUE(pc_nats_parse("PING\r\n", 6, &m, &c));
    TEST_ASSERT_EQUAL(NATS_PING, m.type);
    TEST_ASSERT_TRUE(pc_nats_parse("PONG\r\n", 6, &m, &c));
    TEST_ASSERT_EQUAL(NATS_PONG, m.type);
    TEST_ASSERT_TRUE(pc_nats_parse("+OK\r\n", 5, &m, &c));
    TEST_ASSERT_EQUAL(NATS_OK, m.type);
    TEST_ASSERT_TRUE(pc_nats_parse("-ERR 'Unknown Protocol Operation'\r\n", 35, &m, &c));
    TEST_ASSERT_EQUAL(NATS_ERR, m.type);
    TEST_ASSERT_EQUAL_MEMORY("'Unknown Protocol Operation'", m.arg, m.arg_len);
    TEST_ASSERT_TRUE(pc_nats_parse("INFO {\"server_id\":\"x\"}\r\n", 24, &m, &c));
    TEST_ASSERT_EQUAL(NATS_INFO, m.type);
    TEST_ASSERT_EQUAL_MEMORY("{\"server_id\":\"x\"}", m.arg, m.arg_len);

    // -ERR with no argument text at all: the whitespace-skip loop exits because it hit
    // end-of-line, not because it found a non-space byte.
    TEST_ASSERT_TRUE(pc_nats_parse("-ERR\r\n", 6, &m, &c));
    TEST_ASSERT_EQUAL(NATS_ERR, m.type);
    TEST_ASSERT_EQUAL_size_t(0, m.arg_len);
    // -ERR argument preceded by tabs (not spaces) exercises the tab arm of the skip loop.
    TEST_ASSERT_TRUE(pc_nats_parse("-ERR\t\tX\r\n", 9, &m, &c));
    TEST_ASSERT_EQUAL(NATS_ERR, m.type);
    TEST_ASSERT_EQUAL_MEMORY("X", m.arg, m.arg_len);
    TEST_ASSERT_EQUAL_size_t(1, m.arg_len);

    // A verb terminated by a tab instead of a space still matches.
    TEST_ASSERT_TRUE(pc_nats_parse("PING\t\r\n", 7, &m, &c));
    TEST_ASSERT_EQUAL(NATS_PING, m.type);
    // A line whose first 4 bytes match "PING" but isn't followed by a space, a tab, or
    // end-of-line must not be recognized as the PING verb.
    TEST_ASSERT_TRUE(pc_nats_parse("PINGZ\r\n", 7, &m, &c));
    TEST_ASSERT_EQUAL(NATS_UNKNOWN, m.type);
}

void test_parse_incomplete()
{
    NatsMsg m;
    size_t c;
    TEST_ASSERT_FALSE(pc_nats_parse("PING", 4, &m, &c));                // no CRLF yet
    TEST_ASSERT_FALSE(pc_nats_parse("MSG foo 1 5\r\nhel", 16, &m, &c)); // payload short
    // A byte count near SIZE_MAX must fail closed, not wrap the bounds check (32-bit hardening).
    TEST_ASSERT_FALSE(pc_nats_parse("MSG foo 1 999999999999\r\nhi\r\n", 28, &m, &c));
    // The control line's CRLF is the very end of the buffer: there isn't even room for a
    // trailing CRLF after a zero-length payload, so the bounds check must fail closed via
    // the "after_line + 2 > len" arm specifically (not the size-vs-remaining arm above).
    TEST_ASSERT_FALSE(pc_nats_parse("MSG foo 1 5\r\n", 13, &m, &c));
}

void test_build_overflow_fails_closed()
{
    char small[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_nats_build_pub(small, sizeof(small), "foo", NULL, (const uint8_t *)"hello", 5));
}

void test_build_ping_pong()
{
    char buf[16];
    TEST_ASSERT_EQUAL_size_t(6, pc_nats_build_ping(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("PING\r\n", buf);
    TEST_ASSERT_EQUAL_size_t(6, pc_nats_build_pong(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("PONG\r\n", buf);

    // A buffer sized to exactly fit the output leaves no room for a trailing NUL;
    // finish() must skip writing it (rather than overflow) and still return the length.
    char exact[6];
    TEST_ASSERT_EQUAL_size_t(6, pc_nats_build_ping(exact, sizeof(exact)));
    TEST_ASSERT_EQUAL_MEMORY("PING\r\n", exact, 6);
}

void test_build_null_args()
{
    char buf[64];
    TEST_ASSERT_EQUAL_size_t(0, pc_nats_build_connect(NULL, 64, "{}"));
    TEST_ASSERT_EQUAL_size_t(0, pc_nats_build_connect(buf, sizeof(buf), NULL));
    TEST_ASSERT_EQUAL_size_t(0, pc_nats_build_pub(NULL, 64, "s", NULL, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_nats_build_pub(buf, sizeof(buf), NULL, NULL, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_nats_build_pub(buf, sizeof(buf), "s", NULL, NULL, 3)); // len && !payload
    TEST_ASSERT_EQUAL_size_t(0, pc_nats_build_sub(NULL, 64, "s", NULL, "1"));
    TEST_ASSERT_EQUAL_size_t(0, pc_nats_build_sub(buf, sizeof(buf), NULL, NULL, "1"));
    TEST_ASSERT_EQUAL_size_t(0, pc_nats_build_sub(buf, sizeof(buf), "s", NULL, NULL));
    TEST_ASSERT_EQUAL_size_t(0, pc_nats_build_unsub(NULL, 64, "1", 0, PROTO_FALSE));
    TEST_ASSERT_EQUAL_size_t(0, pc_nats_build_unsub(buf, sizeof(buf), NULL, 0, PROTO_FALSE));
}

void test_build_overflow_put_ch()
{
    char buf[16];
    // cap 6: "PUB " fits, "foo" overflows in put_str -> ok=false, then put_ch bails.
    TEST_ASSERT_EQUAL_size_t(0, pc_nats_build_pub(buf, 6, "foo", NULL, (const uint8_t *)"hi", 2));
    // cap 7: "PUB foo" fits exactly, the following put_ch(' ') is the overflow.
    TEST_ASSERT_EQUAL_size_t(0, pc_nats_build_pub(buf, 7, "foo", NULL, (const uint8_t *)"hi", 2));
}

void test_parse_edges()
{
    NatsMsg m;
    size_t c;
    TEST_ASSERT_FALSE(pc_nats_parse(NULL, 5, &m, &c));
    TEST_ASSERT_FALSE(pc_nats_parse("PING\r\n", 6, NULL, &c));
    TEST_ASSERT_FALSE(pc_nats_parse("PING\r\n", 6, &m, NULL));

    // MSG with too few tokens, and with a non-numeric byte count.
    TEST_ASSERT_FALSE(pc_nats_parse("MSG foo\r\n", 9, &m, &c));
    TEST_ASSERT_FALSE(pc_nats_parse("MSG a b xyz\r\n", 13, &m, &c));

    // MSG line with trailing whitespace before the CRLF still parses.
    const char *raw = "MSG a b 3 \r\nXXX\r\n";
    TEST_ASSERT_TRUE(pc_nats_parse(raw, strlen(raw), &m, &c));
    TEST_ASSERT_EQUAL(NATS_MSG, m.type);
    TEST_ASSERT_EQUAL_size_t(3, m.payload_len);

    // An unrecognized verb parses as UNKNOWN (consumes the line).
    TEST_ASSERT_TRUE(pc_nats_parse("ZZZ whatever\r\n", 14, &m, &c));
    TEST_ASSERT_EQUAL(NATS_UNKNOWN, m.type);

    // find_crlf must skip a lone '\r' not immediately followed by '\n' and keep scanning
    // for the real terminator.
    TEST_ASSERT_TRUE(pc_nats_parse("AB\rCD\r\n", 7, &m, &c));
    TEST_ASSERT_EQUAL(NATS_UNKNOWN, m.type);

    // A byte-count token with a character below '0' fails the digit check on its low side
    // (as opposed to the already-covered high side, e.g. 'x').
    TEST_ASSERT_FALSE(pc_nats_parse("MSG a b -5\r\n", 12, &m, &c));

    // More than 4 whitespace-delimited tokens on a MSG line: the tokenizer stops once it
    // has collected 4 (ntok cap reached) even though input remains.
    TEST_ASSERT_TRUE(pc_nats_parse("MSG a b c 5 extra\r\nhello\r\n", 26, &m, &c));
    TEST_ASSERT_EQUAL(NATS_MSG, m.type);
    TEST_ASSERT_EQUAL_MEMORY("a", m.subject, m.subject_len);
    TEST_ASSERT_EQUAL_MEMORY("b", m.sid, m.sid_len);
    TEST_ASSERT_EQUAL_MEMORY("c", m.reply, m.reply_len);
    TEST_ASSERT_EQUAL_MEMORY("hello", m.payload, 5);

    // Tab-delimited MSG tokens exercise the tab arm of both the whitespace-skip loop and
    // the token-scan loop.
    TEST_ASSERT_TRUE(pc_nats_parse("MSG\ta\tb\t5\r\nhello\r\n", 18, &m, &c));
    TEST_ASSERT_EQUAL(NATS_MSG, m.type);
    TEST_ASSERT_EQUAL_MEMORY("a", m.subject, m.subject_len);
    TEST_ASSERT_EQUAL_MEMORY("b", m.sid, m.sid_len);
    TEST_ASSERT_EQUAL_size_t(0, m.reply_len);
    TEST_ASSERT_EQUAL_MEMORY("hello", m.payload, 5);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_build_connect);
    RUN_TEST(test_build_ping_pong);
    RUN_TEST(test_build_null_args);
    RUN_TEST(test_build_overflow_put_ch);
    RUN_TEST(test_parse_edges);
    RUN_TEST(test_build_pub);
    RUN_TEST(test_build_pub_with_reply);
    RUN_TEST(test_build_pub_empty_payload);
    RUN_TEST(test_build_sub_and_unsub);
    RUN_TEST(test_parse_msg);
    RUN_TEST(test_parse_msg_with_reply);
    RUN_TEST(test_build_hpub);
    RUN_TEST(test_parse_hmsg);
    RUN_TEST(test_parse_control_lines);
    RUN_TEST(test_parse_incomplete);
    RUN_TEST(test_build_overflow_fails_closed);
    return UNITY_END();
}
