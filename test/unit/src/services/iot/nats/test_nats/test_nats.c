// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the NATS client protocol codec (services/iot/nats/nats.h).
//
// NATS carries no RFC. The governing document is the NATS project's client protocol reference
// ("NATS Protocol", docs.nats.io Reference > Protocols > Client), which prints a syntax line and
// worked examples for each operation. Every expected string below is one of those examples copied
// character for character, with the printed byte counts left as the document states them.
//
// test_published_pub_examples and test_published_hpub_examples are load-bearing: the reference's
// `PUB FOO 11` / `HPUB FOO 22 33` examples pin the #bytes and the #header bytes / #total bytes
// arithmetic, and those counts are what frames one message off a byte stream. A count off by one
// desynchronizes the connection rather than corrupting a single message.

#include "services/iot/nats/nats.h"
#include <string.h>

#include <unity.h>

static uint8_t nats_work[16]; // the borrow an entry takes; Nats never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static char g_out[256];

static void bind_out(void)
{
    memset(g_out, 0, sizeof(g_out));
    NatsV.out.buf = g_out;
    NatsV.out.cap = sizeof(g_out);
}

// Compare the built operation against the reference's example, the reported length included.
static void expect(const char *want)
{
    TEST_ASSERT_TRUE_MESSAGE(NatsV.ok, want);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(strlen(want), NatsV.n, want);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want, g_out, strlen(want), want);
}

static proto_bool parse(const char *wire, size_t len)
{
    NatsV.in.buf = wire;
    NatsV.in.len = len;
    Nats.parse(nats_work);
    return NatsV.ok;
}

// NATS Protocol, PUB: `PUB <subject> [reply-to] <#bytes><CRLF>[payload]<CRLF>`, with the examples
//   PUB FOO 11\r\nHello NATS!\r\n
//   PUB FRONT.DOOR JOKE.22 11\r\nKnock Knock\r\n
//   PUB NOTIFY 0\r\n\r\n
void test_published_pub_examples(void)
{
    bind_out();
    NatsV.publish.subject = "FOO";
    NatsV.publish.reply_to = NULL;
    NatsV.publish.payload = (const uint8_t *)"Hello NATS!";
    NatsV.publish.payload_len = 11;
    Nats.pub(nats_work);
    expect("PUB FOO 11\r\nHello NATS!\r\n");

    bind_out();
    NatsV.publish.subject = "FRONT.DOOR";
    NatsV.publish.reply_to = "JOKE.22";
    NatsV.publish.payload = (const uint8_t *)"Knock Knock";
    NatsV.publish.payload_len = 11;
    Nats.pub(nats_work);
    expect("PUB FRONT.DOOR JOKE.22 11\r\nKnock Knock\r\n");

    bind_out();
    NatsV.publish.subject = "NOTIFY";
    NatsV.publish.reply_to = NULL;
    NatsV.publish.payload = NULL;
    NatsV.publish.payload_len = 0;
    Nats.pub(nats_work);
    expect("PUB NOTIFY 0\r\n\r\n");
}

// NATS Protocol, HPUB: `HPUB <subject> [reply-to] <#header bytes> <#total bytes><CRLF>[headers]
// <CRLF><CRLF>[payload]<CRLF>`. #header bytes counts the section through its terminating CR LF CR LF
// and #total bytes counts that plus the payload, which is why the examples read 22 33 and 45 56.
//   HPUB FOO 22 33\r\nNATS/1.0\r\nBar: Baz\r\n\r\nHello NATS!\r\n
//   HPUB FRONT.DOOR JOKE.22 45 56\r\nNATS/1.0\r\nBREAKFAST: donut\r\nLUNCH: burger\r\n\r\nKnock Knock\r\n
//   HPUB NOTIFY 22 22\r\nNATS/1.0\r\nBar: Baz\r\n\r\n\r\n
void test_published_hpub_examples(void)
{
    static const char H1[] = "NATS/1.0\r\nBar: Baz\r\n\r\n";
    static const char H2[] = "NATS/1.0\r\nBREAKFAST: donut\r\nLUNCH: burger\r\n\r\n";
    // "NATS/1.0" 8 + CRLF 2 + "Bar: Baz" 8 + CRLF 2 + CRLF 2 = 22, the count the example prints.
    TEST_ASSERT_EQUAL_UINT(22u, sizeof(H1) - 1);
    // 8 + 2 + "BREAKFAST: donut" 16 + 2 + "LUNCH: burger" 13 + 2 + 2 = 45.
    TEST_ASSERT_EQUAL_UINT(45u, sizeof(H2) - 1);

    bind_out();
    NatsV.publish.subject = "FOO";
    NatsV.publish.reply_to = NULL;
    NatsV.publish.payload = (const uint8_t *)"Hello NATS!";
    NatsV.publish.payload_len = 11;
    NatsV.headers.block = H1;
    NatsV.headers.bytes = sizeof(H1) - 1;
    Nats.hpub(nats_work);
    expect("HPUB FOO 22 33\r\nNATS/1.0\r\nBar: Baz\r\n\r\nHello NATS!\r\n");

    bind_out();
    NatsV.publish.subject = "FRONT.DOOR";
    NatsV.publish.reply_to = "JOKE.22";
    NatsV.publish.payload = (const uint8_t *)"Knock Knock";
    NatsV.publish.payload_len = 11;
    NatsV.headers.block = H2;
    NatsV.headers.bytes = sizeof(H2) - 1;
    Nats.hpub(nats_work);
    expect("HPUB FRONT.DOOR JOKE.22 45 56\r\nNATS/1.0\r\nBREAKFAST: donut\r\nLUNCH: burger\r\n\r\nKnock Knock\r\n");

    // A header-only message: #total bytes equals #header bytes.
    bind_out();
    NatsV.publish.subject = "NOTIFY";
    NatsV.publish.reply_to = NULL;
    NatsV.publish.payload = NULL;
    NatsV.publish.payload_len = 0;
    NatsV.headers.block = H1;
    NatsV.headers.bytes = sizeof(H1) - 1;
    Nats.hpub(nats_work);
    expect("HPUB NOTIFY 22 22\r\nNATS/1.0\r\nBar: Baz\r\n\r\n\r\n");
}

// NATS Protocol, SUB: `SUB <subject> [queue group] <sid><CRLF>`, with the examples
//   SUB FOO 1\r\n
//   SUB BAR G1 44\r\n
void test_published_sub_examples(void)
{
    bind_out();
    NatsV.subscription.subject = "FOO";
    NatsV.subscription.queue_group = NULL;
    NatsV.subscription.sid = "1";
    Nats.sub(nats_work);
    expect("SUB FOO 1\r\n");

    bind_out();
    NatsV.subscription.subject = "BAR";
    NatsV.subscription.queue_group = "G1";
    NatsV.subscription.sid = "44";
    Nats.sub(nats_work);
    expect("SUB BAR G1 44\r\n");
}

// NATS Protocol, UNSUB: `UNSUB <sid> [max_msgs]<CRLF>`, with the examples
//   UNSUB 1\r\n
//   UNSUB 1 5\r\n
void test_published_unsub_examples(void)
{
    bind_out();
    NatsV.subscription.sid = "1";
    NatsV.subscription.with_max = PROTO_FALSE;
    NatsV.subscription.max_msgs = 5; // ignored while with_max is clear
    Nats.unsub(nats_work);
    expect("UNSUB 1\r\n");

    bind_out();
    NatsV.subscription.with_max = PROTO_TRUE;
    Nats.unsub(nats_work);
    expect("UNSUB 1 5\r\n");
}

// NATS Protocol, PING/PONG: `PING<CRLF>` and `PONG<CRLF>`, and CONNECT:
// `CONNECT {"option_name":option_value,...}<CRLF>`.
void test_ping_pong_and_connect(void)
{
    bind_out();
    Nats.ping(nats_work);
    expect("PING\r\n");

    bind_out();
    Nats.pong(nats_work);
    expect("PONG\r\n");

    bind_out();
    NatsV.client.options = "{\"verbose\":false,\"pedantic\":false,\"lang\":\"c\"}";
    Nats.connect(nats_work);
    expect("CONNECT {\"verbose\":false,\"pedantic\":false,\"lang\":\"c\"}\r\n");
}

// NATS Protocol, MSG: `MSG <subject> <sid> [reply-to] <#bytes><CRLF>[payload]<CRLF>`, with the
// examples
//   MSG FOO.BAR 9 11\r\nHello World\r\n
//   MSG FOO.BAR 9 GREETING.34 11\r\nHello World\r\n
void test_published_msg_examples(void)
{
    static const char WIRE[] = "MSG FOO.BAR 9 11\r\nHello World\r\n";
    TEST_ASSERT_TRUE(parse(WIRE, sizeof(WIRE) - 1));
    TEST_ASSERT_EQUAL_INT(NATS_OP_MSG, NatsV.msg.op);
    TEST_ASSERT_EQUAL_UINT(7u, NatsV.msg.subject_len);
    TEST_ASSERT_EQUAL_MEMORY("FOO.BAR", NatsV.msg.subject, 7);
    TEST_ASSERT_EQUAL_UINT(1u, NatsV.msg.sid_len);
    TEST_ASSERT_EQUAL_MEMORY("9", NatsV.msg.sid, 1);
    TEST_ASSERT_NULL(NatsV.msg.reply_to);
    TEST_ASSERT_EQUAL_UINT(0u, NatsV.msg.reply_to_len);
    TEST_ASSERT_EQUAL_UINT(11u, NatsV.msg.payload_len);
    TEST_ASSERT_EQUAL_MEMORY("Hello World", NatsV.msg.payload, 11);
    TEST_ASSERT_NULL(NatsV.msg.headers);
    TEST_ASSERT_EQUAL_UINT(0u, NatsV.msg.header_bytes);
    TEST_ASSERT_EQUAL_UINT(sizeof(WIRE) - 1, NatsV.consumed);

    static const char WITH_REPLY[] = "MSG FOO.BAR 9 GREETING.34 11\r\nHello World\r\n";
    TEST_ASSERT_TRUE(parse(WITH_REPLY, sizeof(WITH_REPLY) - 1));
    TEST_ASSERT_EQUAL_INT(NATS_OP_MSG, NatsV.msg.op);
    TEST_ASSERT_EQUAL_UINT(11u, NatsV.msg.reply_to_len);
    TEST_ASSERT_EQUAL_MEMORY("GREETING.34", NatsV.msg.reply_to, 11);
    TEST_ASSERT_EQUAL_UINT(11u, NatsV.msg.payload_len);
    TEST_ASSERT_EQUAL_MEMORY("Hello World", NatsV.msg.payload, 11);
    TEST_ASSERT_EQUAL_UINT(sizeof(WITH_REPLY) - 1, NatsV.consumed);
}

// NATS Protocol, HMSG: `HMSG <subject> <sid> [reply-to] <#header bytes> <#total bytes><CRLF>
// [headers]<CRLF><CRLF>[payload]<CRLF>`, with the example
//   HMSG FOO.BAR 9 BAZ.69 34 45\r\nNATS/1.0\r\nFoodGroup: vegetable\r\n\r\nHello World\r\n
// where the header section is 8+2 + 20+2 + 2 = 34 octets and the payload is the 45 - 34 = 11 that
// follow it.
void test_published_hmsg_example(void)
{
    static const char WIRE[] = "HMSG FOO.BAR 9 BAZ.69 34 45\r\nNATS/1.0\r\nFoodGroup: vegetable\r\n\r\nHello World\r\n";
    TEST_ASSERT_TRUE(parse(WIRE, sizeof(WIRE) - 1));
    TEST_ASSERT_EQUAL_INT(NATS_OP_MSG, NatsV.msg.op);
    TEST_ASSERT_EQUAL_MEMORY("FOO.BAR", NatsV.msg.subject, 7);
    TEST_ASSERT_EQUAL_MEMORY("9", NatsV.msg.sid, 1);
    TEST_ASSERT_EQUAL_UINT(6u, NatsV.msg.reply_to_len);
    TEST_ASSERT_EQUAL_MEMORY("BAZ.69", NatsV.msg.reply_to, 6);
    TEST_ASSERT_EQUAL_UINT(34u, NatsV.msg.header_bytes);
    TEST_ASSERT_EQUAL_MEMORY("NATS/1.0\r\nFoodGroup: vegetable\r\n\r\n", NatsV.msg.headers, 34);
    TEST_ASSERT_EQUAL_UINT(11u, NatsV.msg.payload_len);
    TEST_ASSERT_EQUAL_MEMORY("Hello World", NatsV.msg.payload, 11);
    TEST_ASSERT_EQUAL_UINT(sizeof(WIRE) - 1, NatsV.consumed);

    // The same message with the optional reply-to left off.
    static const char NO_REPLY[] = "HMSG FOO.BAR 9 34 45\r\nNATS/1.0\r\nFoodGroup: vegetable\r\n\r\nHello World\r\n";
    TEST_ASSERT_TRUE(parse(NO_REPLY, sizeof(NO_REPLY) - 1));
    TEST_ASSERT_NULL(NatsV.msg.reply_to);
    TEST_ASSERT_EQUAL_UINT(34u, NatsV.msg.header_bytes);
    TEST_ASSERT_EQUAL_UINT(11u, NatsV.msg.payload_len);
}

// The server-to-client operations that carry no payload, plus the argument-bearing INFO and -ERR.
void test_control_line_only_operations(void)
{
    TEST_ASSERT_TRUE(parse("PING\r\n", 6));
    TEST_ASSERT_EQUAL_INT(NATS_OP_PING, NatsV.msg.op);
    TEST_ASSERT_EQUAL_UINT(6u, NatsV.consumed);

    TEST_ASSERT_TRUE(parse("PONG\r\n", 6));
    TEST_ASSERT_EQUAL_INT(NATS_OP_PONG, NatsV.msg.op);

    TEST_ASSERT_TRUE(parse("+OK\r\n", 5));
    TEST_ASSERT_EQUAL_INT(NATS_OP_OK, NatsV.msg.op);
    TEST_ASSERT_EQUAL_UINT(5u, NatsV.consumed);

    // -ERR <error message>: the reference's own answer to an operation the server does not know.
    static const char ERR[] = "-ERR 'Unknown Protocol Operation'\r\n";
    TEST_ASSERT_TRUE(parse(ERR, sizeof(ERR) - 1));
    TEST_ASSERT_EQUAL_INT(NATS_OP_ERR, NatsV.msg.op);
    TEST_ASSERT_EQUAL_UINT(28u, NatsV.msg.arg_len);
    TEST_ASSERT_EQUAL_MEMORY("'Unknown Protocol Operation'", NatsV.msg.arg, 28);

    // INFO {"option_name":option_value,...}
    static const char INFO[] = "INFO {\"server_id\":\"x\",\"version\":\"2.9.0\"}\r\n";
    TEST_ASSERT_TRUE(parse(INFO, sizeof(INFO) - 1));
    TEST_ASSERT_EQUAL_INT(NATS_OP_INFO, NatsV.msg.op);
    TEST_ASSERT_EQUAL_UINT(35u, NatsV.msg.arg_len);
    TEST_ASSERT_EQUAL_MEMORY("{\"server_id\":\"x\",\"version\":\"2.9.0\"}", NatsV.msg.arg, 35);

    // An operation name this decoder does not carry consumes its control line and says so, which is
    // what lets the caller answer with -ERR 'Unknown Protocol Operation'.
    TEST_ASSERT_TRUE(parse("FOOBAR baz\r\n", 12));
    TEST_ASSERT_EQUAL_INT(NATS_OP_UNKNOWN, NatsV.msg.op);
    TEST_ASSERT_EQUAL_UINT(12u, NatsV.consumed);
}

// Protocol conventions, Field Delimiter: "a space or a tab" delimits fields and "repeated whitespace"
// counts as one delimiter, so these spellings name the same message.
void test_repeated_whitespace_is_one_delimiter(void)
{
    static const char TABS[] = "MSG\tFOO.BAR \t 9   11\r\nHello World\r\n";
    TEST_ASSERT_TRUE(parse(TABS, sizeof(TABS) - 1));
    TEST_ASSERT_EQUAL_INT(NATS_OP_MSG, NatsV.msg.op);
    TEST_ASSERT_EQUAL_UINT(7u, NatsV.msg.subject_len);
    TEST_ASSERT_EQUAL_MEMORY("FOO.BAR", NatsV.msg.subject, 7);
    TEST_ASSERT_EQUAL_UINT(1u, NatsV.msg.sid_len);
    TEST_ASSERT_EQUAL_UINT(11u, NatsV.msg.payload_len);
    TEST_ASSERT_EQUAL_MEMORY("Hello World", NatsV.msg.payload, 11);
}

// A parse reports true only once the whole operation is buffered: the control line for every
// operation, and the payload plus its trailing CR LF for a MSG.
void test_parse_waits_for_the_whole_operation(void)
{
    static const char WIRE[] = "MSG FOO.BAR 9 11\r\nHello World\r\n";
    const size_t total = sizeof(WIRE) - 1;
    for (size_t have = 0; have < total; have++)
    {
        TEST_ASSERT_FALSE_MESSAGE(parse(WIRE, have), WIRE);
        TEST_ASSERT_EQUAL_UINT(0u, NatsV.consumed);
    }
    TEST_ASSERT_TRUE(parse(WIRE, total));
    TEST_ASSERT_EQUAL_UINT(total, NatsV.consumed);

    // A stream of two operations walks one at a time by the octets each reports.
    static const char TWO[] = "PING\r\nMSG a 1 2\r\nhi\r\n";
    size_t off = 0;
    TEST_ASSERT_TRUE(parse(TWO + off, sizeof(TWO) - 1 - off));
    TEST_ASSERT_EQUAL_INT(NATS_OP_PING, NatsV.msg.op);
    off += NatsV.consumed;
    TEST_ASSERT_TRUE(parse(TWO + off, sizeof(TWO) - 1 - off));
    TEST_ASSERT_EQUAL_INT(NATS_OP_MSG, NatsV.msg.op);
    TEST_ASSERT_EQUAL_UINT(2u, NatsV.msg.payload_len);
    TEST_ASSERT_EQUAL_MEMORY("hi", NatsV.msg.payload, 2);
    off += NatsV.consumed;
    TEST_ASSERT_EQUAL_UINT(sizeof(TWO) - 1, off);

    TEST_ASSERT_FALSE(parse(NULL, 8));
}

// A control line whose fields do not fit the grammar is refused rather than half decoded.
void test_malformed_control_lines_are_refused(void)
{
    static const char *const BAD[] = {
        "MSG FOO.BAR 9\r\n",                 // no #bytes field
        "MSG FOO.BAR\r\n",                   // no sid and no #bytes
        "MSG FOO.BAR 9 x\r\n",               // #bytes is not a decimal run
        "MSG FOO.BAR 9 a b 11\r\nHello\r\n", // one field too many
        "HMSG FOO.BAR 9 34\r\n",             // one length field short
        "HMSG FOO.BAR 9 45 34\r\nxx\r\n",    // #header bytes above #total bytes
        "MSG FOO.BAR 9 99\r\nshort\r\n",     // #bytes past what is buffered
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        TEST_ASSERT_FALSE_MESSAGE(parse(BAD[i], strlen(BAD[i])), BAD[i]);
        TEST_ASSERT_EQUAL_UINT(0u, NatsV.consumed);
    }
}

// A builder that cannot fit the whole operation reports 0 octets rather than half an operation, and
// an absent argument it needs is refused.
void test_builders_fail_closed(void)
{
    char small[8];
    NatsV.out.buf = small;
    NatsV.out.cap = sizeof(small);
    NatsV.publish.subject = "FOO";
    NatsV.publish.reply_to = NULL;
    NatsV.publish.payload = (const uint8_t *)"Hello NATS!";
    NatsV.publish.payload_len = 11;
    Nats.pub(nats_work);
    TEST_ASSERT_FALSE(NatsV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, NatsV.n);

    bind_out();
    NatsV.publish.subject = NULL;
    Nats.pub(nats_work);
    TEST_ASSERT_FALSE(NatsV.ok);

    bind_out();
    NatsV.publish.subject = "FOO";
    NatsV.publish.payload = NULL;
    NatsV.publish.payload_len = 4; // octets promised but not lent
    Nats.pub(nats_work);
    TEST_ASSERT_FALSE(NatsV.ok);

    bind_out();
    NatsV.out.buf = NULL;
    Nats.ping(nats_work);
    TEST_ASSERT_FALSE(NatsV.ok);

    bind_out();
    NatsV.client.options = NULL;
    Nats.connect(nats_work);
    TEST_ASSERT_FALSE(NatsV.ok);

    bind_out();
    NatsV.subscription.subject = "FOO";
    NatsV.subscription.queue_group = NULL;
    NatsV.subscription.sid = NULL;
    Nats.sub(nats_work);
    TEST_ASSERT_FALSE(NatsV.ok);

    // An HPUB with no header section is not an HPUB.
    bind_out();
    NatsV.publish.subject = "FOO";
    NatsV.publish.payload = NULL;
    NatsV.publish.payload_len = 0;
    NatsV.headers.block = NULL;
    NatsV.headers.bytes = 22;
    Nats.hpub(nats_work);
    TEST_ASSERT_FALSE(NatsV.ok);
    NatsV.headers.block = "NATS/1.0\r\n\r\n";
    NatsV.headers.bytes = 0;
    Nats.hpub(nats_work);
    TEST_ASSERT_FALSE(NatsV.ok);
}

// #bytes is written as decimal digits with no padding, so a payload past nine octets is a multi-digit
// field the parser reads back as the same count.
void test_byte_counts_render_and_read_as_decimal(void)
{
    static uint8_t payload[1234];
    static char wire[2048];
    for (size_t i = 0; i < sizeof(payload); i++)
    {
        payload[i] = (uint8_t)('a' + (i % 26));
    }

    NatsV.out.buf = wire;
    NatsV.out.cap = sizeof(wire);
    NatsV.publish.subject = "FOO";
    NatsV.publish.reply_to = NULL;
    NatsV.publish.payload = payload;
    NatsV.publish.payload_len = sizeof(payload);
    Nats.pub(nats_work);
    TEST_ASSERT_TRUE(NatsV.ok);
    // "PUB FOO 1234\r\n" is 14 octets ahead of the payload, and the trailing CR LF is 2 behind it.
    TEST_ASSERT_EQUAL_UINT(14u + 1234u + 2u, NatsV.n);
    TEST_ASSERT_EQUAL_MEMORY("PUB FOO 1234\r\n", wire, 14);
    TEST_ASSERT_EQUAL_MEMORY(payload, wire + 14, sizeof(payload));
    TEST_ASSERT_EQUAL_MEMORY("\r\n", wire + 14 + sizeof(payload), 2);

    // The same count read back off a MSG carrying it.
    static char msg[2048];
    memcpy(msg, "MSG FOO 9 1234\r\n", 16);
    memcpy(msg + 16, payload, sizeof(payload));
    memcpy(msg + 16 + sizeof(payload), "\r\n", 2);
    TEST_ASSERT_TRUE(parse(msg, 16 + sizeof(payload) + 2));
    TEST_ASSERT_EQUAL_UINT(1234u, NatsV.msg.payload_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, NatsV.msg.payload, sizeof(payload));
    TEST_ASSERT_EQUAL_UINT(16u + 1234u + 2u, NatsV.consumed);
}
