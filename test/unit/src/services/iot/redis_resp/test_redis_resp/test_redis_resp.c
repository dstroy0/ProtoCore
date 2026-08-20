// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the RESP codec (services/iot/redis_resp/redis_resp.h).
//
// RESP carries no RFC. The governing document is the Redis project's "Redis serialization protocol
// specification", whose per-type sections print the wire encoding of each example. Every expected
// string below is one of those printed examples.
//
// test_sending_commands_to_a_redis_server is load-bearing: the specification's own worked exchange
// says the client sends `LLEN mylist` as `*2\r\n$4\r\nLLEN\r\n$6\r\nmylist\r\n` "as a whole", and an
// encoder that gets a single length prefix wrong sends a command the server cannot parse at all.

#include "services/iot/redis_resp/redis_resp.h"
#include <string.h>

#include <unity.h>

static uint8_t redis_resp_work[16]; // the borrow an entry takes; Resp never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// Unity's double assertions are compiled out in this build, so a Number is compared by hand.
static void assert_near(double want, double got, double eps, const char *what)
{
    const double d = (want > got) ? (want - got) : (got - want);
    TEST_ASSERT_TRUE_MESSAGE(d <= eps, what);
}

static char g_out[256];

static size_t encode(const char *const *argv, size_t argc, const size_t *argv_len)
{
    memset(g_out, 0, sizeof(g_out));
    RespV.command.argv = argv;
    RespV.command.argv_len = argv_len;
    RespV.command.argc = argc;
    RespV.out.buf = g_out;
    RespV.out.cap = sizeof(g_out);
    Resp.encode_command(redis_resp_work);
    return RespV.n;
}

static proto_bool parse(const char *wire, size_t len)
{
    RespV.wire.buf = (const uint8_t *)wire;
    RespV.wire.len = len;
    Resp.parse_reply(redis_resp_work);
    return RespV.ok;
}

static proto_bool parse_z(const char *wire)
{
    return parse(wire, strlen(wire));
}

// "Sending commands to a Redis server": the client sends an array consisting of only bulk strings,
// the command's name first. The specification's own exchange for `LLEN mylist` is
// `*2\r\n$4\r\nLLEN\r\n$6\r\nmylist\r\n`.
void test_sending_commands_to_a_redis_server(void)
{
    static const char *const ARGV[2] = {"LLEN", "mylist"};
    static const char WANT[] = "*2\r\n$4\r\nLLEN\r\n$6\r\nmylist\r\n";
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT) - 1, encode(ARGV, 2, NULL));
    TEST_ASSERT_TRUE(RespV.ok);
    TEST_ASSERT_EQUAL_STRING(WANT, g_out);

    // The server's reply to it, the Integers example `:48293\r\n`.
    TEST_ASSERT_TRUE(parse_z(":48293\r\n"));
    TEST_ASSERT_EQUAL_INT(RESP_INTEGER, RespV.reply.type);
    TEST_ASSERT_EQUAL_INT64(48293, RespV.reply.ival);
    TEST_ASSERT_EQUAL_UINT(8u, RespV.n);
}

// An argument holding a NUL or a CR is still one bulk string: the length prefix is what delimits it,
// so the explicit per-argument lengths carry binary data the NUL-terminated form cannot.
void test_binary_safe_arguments(void)
{
    static const char BINARY[] = {'a', '\0', '\r', '\n', 'b'};
    static const char *const ARGV[3] = {"SET", "k", BINARY};
    static const size_t LENS[3] = {3, 1, sizeof(BINARY)};
    static const char WANT[] = "*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$5\r\na\0\r\nb\r\n";
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT) - 1, encode(ARGV, 3, LENS));
    TEST_ASSERT_TRUE(RespV.ok);
    TEST_ASSERT_EQUAL_MEMORY(WANT, g_out, sizeof(WANT) - 1);

    // A zero-length argument is `$0\r\n\r\n`, the empty bulk string of the specification.
    static const char *const EMPTY[2] = {"ECHO", ""};
    TEST_ASSERT_EQUAL_UINT(20u, encode(EMPTY, 2, NULL));
    TEST_ASSERT_EQUAL_STRING("*2\r\n$4\r\nECHO\r\n$0\r\n\r\n", g_out);
}

// "Simple strings": `+OK\r\n`, five bytes. "Simple errors": `-Error message\r\n`, with the printed
// examples `-ERR unknown command 'asdf'` and `-WRONGTYPE Operation against a key holding the wrong
// kind of value`. Both carry the line itself, the CRLF excluded.
void test_simple_strings_and_errors(void)
{
    TEST_ASSERT_TRUE(parse_z("+OK\r\n"));
    TEST_ASSERT_EQUAL_INT(RESP_SIMPLE_STRING, RespV.reply.type);
    TEST_ASSERT_EQUAL_UINT(2u, RespV.reply.str_len);
    TEST_ASSERT_EQUAL_MEMORY("OK", RespV.reply.str, 2);
    TEST_ASSERT_EQUAL_UINT(5u, RespV.n);

    TEST_ASSERT_TRUE(parse_z("-Error message\r\n"));
    TEST_ASSERT_EQUAL_INT(RESP_SIMPLE_ERROR, RespV.reply.type);
    TEST_ASSERT_EQUAL_UINT(13u, RespV.reply.str_len);
    TEST_ASSERT_EQUAL_MEMORY("Error message", RespV.reply.str, 13);

    TEST_ASSERT_TRUE(parse_z("-ERR unknown command 'asdf'\r\n"));
    TEST_ASSERT_EQUAL_INT(RESP_SIMPLE_ERROR, RespV.reply.type);
    TEST_ASSERT_EQUAL_UINT(26u, RespV.reply.str_len);
    TEST_ASSERT_EQUAL_MEMORY("ERR unknown command 'asdf'", RespV.reply.str, 26);

    static const char WRONGTYPE[] = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
    // The message is the line less the leading '-' and the trailing CRLF, so three of the octets the
    // literal holds, plus the NUL sizeof counts.
    TEST_ASSERT_TRUE(parse(WRONGTYPE, sizeof(WRONGTYPE) - 1));
    TEST_ASSERT_EQUAL_UINT(sizeof(WRONGTYPE) - 4, RespV.reply.str_len);
    TEST_ASSERT_EQUAL_MEMORY("WRONGTYPE Operation", RespV.reply.str, 19);
}

// "Integers": `:[<+|->]<value>\r\n`, a signed base-10 64-bit value. `:0\r\n` and `:1000\r\n` are the
// printed examples; the limits are the range the type guarantees.
void test_integers(void)
{
    struct
    {
        const char *wire;
        int64_t value;
    } static const CASES[] = {
        {":0\r\n", 0},
        {":1000\r\n", 1000},
        {":-1\r\n", -1},
        {":9223372036854775807\r\n", INT64_MAX},
        {":-9223372036854775808\r\n", INT64_MIN},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        TEST_ASSERT_TRUE_MESSAGE(parse_z(CASES[i].wire), CASES[i].wire);
        TEST_ASSERT_EQUAL_INT(RESP_INTEGER, RespV.reply.type);
        TEST_ASSERT_EQUAL_INT64_MESSAGE(CASES[i].value, RespV.reply.ival, CASES[i].wire);
        TEST_ASSERT_EQUAL_UINT(strlen(CASES[i].wire), RespV.n);
    }
    // A value that is not a base-10 run is refused rather than read as zero.
    TEST_ASSERT_FALSE(parse_z(":12x\r\n"));
    TEST_ASSERT_FALSE(parse_z(":\r\n"));
}

// "Bulk strings": `$<length>\r\n<data>\r\n`, so "hello" is `$5\r\nhello\r\n` and the empty string is
// `$0\r\n\r\n`. "Null bulk strings": `$-1\r\n`.
void test_bulk_strings(void)
{
    TEST_ASSERT_TRUE(parse_z("$5\r\nhello\r\n"));
    TEST_ASSERT_EQUAL_INT(RESP_BULK_STRING, RespV.reply.type);
    TEST_ASSERT_EQUAL_UINT(5u, RespV.reply.str_len);
    TEST_ASSERT_EQUAL_MEMORY("hello", RespV.reply.str, 5);
    TEST_ASSERT_EQUAL_UINT(11u, RespV.n);

    TEST_ASSERT_TRUE(parse_z("$0\r\n\r\n"));
    TEST_ASSERT_EQUAL_INT(RESP_BULK_STRING, RespV.reply.type);
    TEST_ASSERT_EQUAL_UINT(0u, RespV.reply.str_len);
    TEST_ASSERT_EQUAL_UINT(6u, RespV.n);

    TEST_ASSERT_TRUE(parse_z("$-1\r\n"));
    TEST_ASSERT_EQUAL_INT(RESP_NULL, RespV.reply.type);
    TEST_ASSERT_EQUAL_UINT(5u, RespV.n);

    // Bulk strings are binary safe: a CRLF inside the body is data, and the length is what ends it.
    static const char EMBEDDED[] = "$4\r\na\r\nb\r\n";
    TEST_ASSERT_TRUE(parse(EMBEDDED, sizeof(EMBEDDED) - 1));
    TEST_ASSERT_EQUAL_INT(RESP_BULK_STRING, RespV.reply.type);
    TEST_ASSERT_EQUAL_UINT(4u, RespV.reply.str_len);
    TEST_ASSERT_EQUAL_MEMORY("a\r\nb", RespV.reply.str, 4);
    TEST_ASSERT_EQUAL_UINT(sizeof(EMBEDDED) - 1, RespV.n);

    // The terminator has to sit where the length puts it, and the whole body has to be buffered.
    TEST_ASSERT_FALSE(parse_z("$5\r\nhelloXX"));
    TEST_ASSERT_FALSE(parse_z("$5\r\nhel\r\n"));
    TEST_ASSERT_FALSE(parse_z("$-2\r\n")); // only -1 is the Null bulk string
}

// "Arrays": `*<number-of-elements>\r\n<element-1>...<element-n>`, so `*0\r\n` is empty and
// `*2\r\n$5\r\nhello\r\n$5\r\nworld\r\n` is two bulk strings. The header alone is one parse, and the
// caller walks the elements from the octets behind it. "Null arrays": `*-1\r\n`.
void test_arrays_walk_element_by_element(void)
{
    TEST_ASSERT_TRUE(parse_z("*0\r\n"));
    TEST_ASSERT_EQUAL_INT(RESP_ARRAY, RespV.reply.type);
    TEST_ASSERT_EQUAL_INT64(0, RespV.reply.count);
    TEST_ASSERT_EQUAL_UINT(4u, RespV.n);

    TEST_ASSERT_TRUE(parse_z("*-1\r\n"));
    TEST_ASSERT_EQUAL_INT(RESP_NULL, RespV.reply.type);
    TEST_ASSERT_EQUAL_UINT(5u, RespV.n);
    // The Null array is the length -1 exactly; no other negative count has an encoding.
    TEST_ASSERT_FALSE(parse_z("*-2\r\n"));

    static const char TWO[] = "*2\r\n$5\r\nhello\r\n$5\r\nworld\r\n";
    size_t off = 0;
    TEST_ASSERT_TRUE(parse(TWO + off, sizeof(TWO) - 1 - off));
    TEST_ASSERT_EQUAL_INT(RESP_ARRAY, RespV.reply.type);
    TEST_ASSERT_EQUAL_INT64(2, RespV.reply.count);
    off += RespV.n;
    TEST_ASSERT_TRUE(parse(TWO + off, sizeof(TWO) - 1 - off));
    TEST_ASSERT_EQUAL_MEMORY("hello", RespV.reply.str, 5);
    off += RespV.n;
    TEST_ASSERT_TRUE(parse(TWO + off, sizeof(TWO) - 1 - off));
    TEST_ASSERT_EQUAL_MEMORY("world", RespV.reply.str, 5);
    off += RespV.n;
    TEST_ASSERT_EQUAL_UINT(sizeof(TWO) - 1, off);

    // The specification's array of three integers, `*3\r\n:1\r\n:2\r\n:3\r\n`.
    static const char THREE[] = "*3\r\n:1\r\n:2\r\n:3\r\n";
    off = 0;
    TEST_ASSERT_TRUE(parse(THREE, sizeof(THREE) - 1));
    TEST_ASSERT_EQUAL_INT64(3, RespV.reply.count);
    off += RespV.n;
    for (int64_t want = 1; want <= 3; want++)
    {
        TEST_ASSERT_TRUE(parse(THREE + off, sizeof(THREE) - 1 - off));
        TEST_ASSERT_EQUAL_INT(RESP_INTEGER, RespV.reply.type);
        TEST_ASSERT_EQUAL_INT64(want, RespV.reply.ival);
        off += RespV.n;
    }
    TEST_ASSERT_EQUAL_UINT(sizeof(THREE) - 1, off);

    // "All of the aggregate RESP types support nesting": the specification's nested example, an array
    // of an array of three integers and an array of a simple string and an error.
    static const char NESTED[] = "*2\r\n*3\r\n:1\r\n:2\r\n:3\r\n*2\r\n+Hello\r\n-World\r\n";
    off = 0;
    TEST_ASSERT_TRUE(parse(NESTED, sizeof(NESTED) - 1));
    TEST_ASSERT_EQUAL_INT64(2, RespV.reply.count);
    off += RespV.n;
    TEST_ASSERT_TRUE(parse(NESTED + off, sizeof(NESTED) - 1 - off));
    TEST_ASSERT_EQUAL_INT(RESP_ARRAY, RespV.reply.type);
    TEST_ASSERT_EQUAL_INT64(3, RespV.reply.count);
    off += RespV.n;
    for (int i = 0; i < 3; i++)
    {
        TEST_ASSERT_TRUE(parse(NESTED + off, sizeof(NESTED) - 1 - off));
        off += RespV.n;
    }
    TEST_ASSERT_TRUE(parse(NESTED + off, sizeof(NESTED) - 1 - off));
    TEST_ASSERT_EQUAL_INT(RESP_ARRAY, RespV.reply.type);
    TEST_ASSERT_EQUAL_INT64(2, RespV.reply.count);
    off += RespV.n;
    TEST_ASSERT_TRUE(parse(NESTED + off, sizeof(NESTED) - 1 - off));
    TEST_ASSERT_EQUAL_INT(RESP_SIMPLE_STRING, RespV.reply.type);
    TEST_ASSERT_EQUAL_MEMORY("Hello", RespV.reply.str, 5);
    off += RespV.n;
    TEST_ASSERT_TRUE(parse(NESTED + off, sizeof(NESTED) - 1 - off));
    TEST_ASSERT_EQUAL_INT(RESP_SIMPLE_ERROR, RespV.reply.type);
    TEST_ASSERT_EQUAL_MEMORY("World", RespV.reply.str, 5);
    off += RespV.n;
    TEST_ASSERT_EQUAL_UINT(sizeof(NESTED) - 1, off);

    // "Null elements in arrays": the specification's `*3\r\n$5\r\nhello\r\n$-1\r\n$5\r\nworld\r\n`.
    static const char WITH_NULL[] = "*3\r\n$5\r\nhello\r\n$-1\r\n$5\r\nworld\r\n";
    off = 0;
    TEST_ASSERT_TRUE(parse(WITH_NULL, sizeof(WITH_NULL) - 1));
    off += RespV.n;
    TEST_ASSERT_TRUE(parse(WITH_NULL + off, sizeof(WITH_NULL) - 1 - off));
    TEST_ASSERT_EQUAL_INT(RESP_BULK_STRING, RespV.reply.type);
    off += RespV.n;
    TEST_ASSERT_TRUE(parse(WITH_NULL + off, sizeof(WITH_NULL) - 1 - off));
    TEST_ASSERT_EQUAL_INT(RESP_NULL, RespV.reply.type);
    off += RespV.n;
    TEST_ASSERT_TRUE(parse(WITH_NULL + off, sizeof(WITH_NULL) - 1 - off));
    TEST_ASSERT_EQUAL_MEMORY("world", RespV.reply.str, 5);
    off += RespV.n;
    TEST_ASSERT_EQUAL_UINT(sizeof(WITH_NULL) - 1, off);
}

// The RESP3 simple types: "Nulls" `_\r\n`, "Booleans" `#<t|f>\r\n`, "Big numbers"
// `([+|-]<number>\r\n` with the printed example, and "Doubles" `,1.23\r\n`, `,10\r\n`, `,inf\r\n`,
// `,-inf\r\n`, `,nan\r\n`.
void test_resp3_simple_types(void)
{
    TEST_ASSERT_TRUE(parse_z("_\r\n"));
    TEST_ASSERT_EQUAL_INT(RESP_NULL, RespV.reply.type);
    TEST_ASSERT_EQUAL_UINT(3u, RespV.n);

    TEST_ASSERT_TRUE(parse_z("#t\r\n"));
    TEST_ASSERT_EQUAL_INT(RESP_BOOLEAN, RespV.reply.type);
    TEST_ASSERT_EQUAL_INT64(1, RespV.reply.ival);
    TEST_ASSERT_TRUE(parse_z("#f\r\n"));
    TEST_ASSERT_EQUAL_INT(RESP_BOOLEAN, RespV.reply.type);
    TEST_ASSERT_EQUAL_INT64(0, RespV.reply.ival);
    // Only 't' and 'f' are Booleans.
    TEST_ASSERT_FALSE(parse_z("#x\r\n"));
    TEST_ASSERT_FALSE(parse_z("#true\r\n"));

    static const char BIG[] = "(3492890328409238509324850943850943825024385\r\n";
    TEST_ASSERT_TRUE(parse(BIG, sizeof(BIG) - 1));
    TEST_ASSERT_EQUAL_INT(RESP_BIG_NUMBER, RespV.reply.type);
    TEST_ASSERT_EQUAL_UINT(43u, RespV.reply.str_len);
    TEST_ASSERT_EQUAL_MEMORY("3492890328409238509324850943850943825024385", RespV.reply.str, 43);

    TEST_ASSERT_TRUE(parse_z(",1.23\r\n"));
    TEST_ASSERT_EQUAL_INT(RESP_DOUBLE, RespV.reply.type);
    TEST_ASSERT_EQUAL_UINT(4u, RespV.reply.str_len);
    TEST_ASSERT_EQUAL_MEMORY("1.23", RespV.reply.str, 4);
    assert_near(1.23, RespV.reply.dval, 1e-12, "Resp.reply.dval");

    TEST_ASSERT_TRUE(parse_z(",10\r\n"));
    TEST_ASSERT_EQUAL_INT(RESP_DOUBLE, RespV.reply.type);
    assert_near(10.0, RespV.reply.dval, 1e-12, "Resp.reply.dval");

    // 1.5e3 is 1500 by the definition of the exponent part.
    TEST_ASSERT_TRUE(parse_z(",1.5e3\r\n"));
    assert_near(1500.0, RespV.reply.dval, 1e-9, "Resp.reply.dval");
    TEST_ASSERT_TRUE(parse_z(",-2.5E-2\r\n"));
    assert_near(-0.025, RespV.reply.dval, 1e-12, "Resp.reply.dval");

    TEST_ASSERT_TRUE(parse_z(",inf\r\n"));
    TEST_ASSERT_TRUE(RespV.reply.dval > 1e308);
    TEST_ASSERT_TRUE(parse_z(",-inf\r\n"));
    TEST_ASSERT_TRUE(RespV.reply.dval < -1e308);
    TEST_ASSERT_TRUE(parse_z(",nan\r\n"));
    TEST_ASSERT_TRUE(RespV.reply.dval != RespV.reply.dval); // NaN is the only value unequal to itself
}

// "Bulk errors": `!<length>\r\n<error>\r\n`, with the printed example `!21\r\nSYNTAX invalid syntax
// \r\n`. "Verbatim strings": `=<length>\r\n<encoding>:<data>\r\n`, with `=15\r\ntxt:Some string\r\n`,
// where the three-byte encoding and its colon are part of the counted length.
void test_bulk_errors_and_verbatim_strings(void)
{
    TEST_ASSERT_TRUE(parse_z("!21\r\nSYNTAX invalid syntax\r\n"));
    TEST_ASSERT_EQUAL_INT(RESP_BULK_ERROR, RespV.reply.type);
    TEST_ASSERT_EQUAL_UINT(21u, RespV.reply.str_len);
    TEST_ASSERT_EQUAL_MEMORY("SYNTAX invalid syntax", RespV.reply.str, 21);
    TEST_ASSERT_EQUAL_UINT(28u, RespV.n);

    TEST_ASSERT_TRUE(parse_z("=15\r\ntxt:Some string\r\n"));
    TEST_ASSERT_EQUAL_INT(RESP_VERBATIM_STRING, RespV.reply.type);
    TEST_ASSERT_EQUAL_UINT(15u, RespV.reply.str_len);
    TEST_ASSERT_EQUAL_MEMORY("txt:Some string", RespV.reply.str, 15);
    // "Exactly three (3) bytes represent the data's encoding", then the colon separator.
    TEST_ASSERT_EQUAL_MEMORY("txt", RespV.reply.str, 3);
    TEST_ASSERT_EQUAL_CHAR(':', RespV.reply.str[3]);
    TEST_ASSERT_EQUAL_UINT(22u, RespV.n);

    // Neither type has a negative-length form: only `$-1` is a Null.
    TEST_ASSERT_FALSE(parse_z("!-1\r\n"));
    TEST_ASSERT_FALSE(parse_z("=-1\r\n"));
}

// "Maps": `%<number-of-entries>\r\n<key-1><value-1>...`, so the specification's
// `%2\r\n+first\r\n:1\r\n+second\r\n:2\r\n` is two entries and therefore four children.
void test_maps_report_two_children_per_entry(void)
{
    static const char MAP[] = "%2\r\n+first\r\n:1\r\n+second\r\n:2\r\n";
    size_t off = 0;
    TEST_ASSERT_TRUE(parse(MAP, sizeof(MAP) - 1));
    TEST_ASSERT_EQUAL_INT(RESP_MAP, RespV.reply.type);
    TEST_ASSERT_EQUAL_INT64(4, RespV.reply.count);
    off += RespV.n;

    TEST_ASSERT_TRUE(parse(MAP + off, sizeof(MAP) - 1 - off));
    TEST_ASSERT_EQUAL_MEMORY("first", RespV.reply.str, 5);
    off += RespV.n;
    TEST_ASSERT_TRUE(parse(MAP + off, sizeof(MAP) - 1 - off));
    TEST_ASSERT_EQUAL_INT64(1, RespV.reply.ival);
    off += RespV.n;
    TEST_ASSERT_TRUE(parse(MAP + off, sizeof(MAP) - 1 - off));
    TEST_ASSERT_EQUAL_MEMORY("second", RespV.reply.str, 6);
    off += RespV.n;
    TEST_ASSERT_TRUE(parse(MAP + off, sizeof(MAP) - 1 - off));
    TEST_ASSERT_EQUAL_INT64(2, RespV.reply.ival);
    off += RespV.n;
    TEST_ASSERT_EQUAL_UINT(sizeof(MAP) - 1, off);

    TEST_ASSERT_TRUE(parse_z("%0\r\n"));
    TEST_ASSERT_EQUAL_INT(RESP_MAP, RespV.reply.type);
    TEST_ASSERT_EQUAL_INT64(0, RespV.reply.count);
    TEST_ASSERT_FALSE(parse_z("%-1\r\n")); // a Map has no null form
}

// "Sets" (~) and "Pushes" (>) are encoded like Arrays, "differing only in their first byte", so the
// header reports the element count and the children follow.
void test_sets_and_pushes(void)
{
    static const char SET[] = "~3\r\n+orange\r\n+apple\r\n#t\r\n";
    size_t off = 0;
    TEST_ASSERT_TRUE(parse(SET, sizeof(SET) - 1));
    TEST_ASSERT_EQUAL_INT(RESP_SET, RespV.reply.type);
    TEST_ASSERT_EQUAL_INT64(3, RespV.reply.count);
    off += RespV.n;
    TEST_ASSERT_TRUE(parse(SET + off, sizeof(SET) - 1 - off));
    TEST_ASSERT_EQUAL_MEMORY("orange", RespV.reply.str, 6);
    off += RespV.n;
    TEST_ASSERT_TRUE(parse(SET + off, sizeof(SET) - 1 - off));
    TEST_ASSERT_EQUAL_MEMORY("apple", RespV.reply.str, 5);
    off += RespV.n;
    TEST_ASSERT_TRUE(parse(SET + off, sizeof(SET) - 1 - off));
    TEST_ASSERT_EQUAL_INT(RESP_BOOLEAN, RespV.reply.type);
    off += RespV.n;
    TEST_ASSERT_EQUAL_UINT(sizeof(SET) - 1, off);

    static const char PUSH[] = ">4\r\n+pubsub\r\n+message\r\n+somechannel\r\n+this is the message\r\n";
    off = 0;
    TEST_ASSERT_TRUE(parse(PUSH, sizeof(PUSH) - 1));
    TEST_ASSERT_EQUAL_INT(RESP_PUSH, RespV.reply.type);
    TEST_ASSERT_EQUAL_INT64(4, RespV.reply.count);
    off += RespV.n;
    for (int i = 0; i < 4; i++)
    {
        TEST_ASSERT_TRUE(parse(PUSH + off, sizeof(PUSH) - 1 - off));
        TEST_ASSERT_EQUAL_INT(RESP_SIMPLE_STRING, RespV.reply.type);
        off += RespV.n;
    }
    TEST_ASSERT_EQUAL_UINT(sizeof(PUSH) - 1, off);
}

// The Attributes type (|) is the one first byte in the table this parser does not carry, and any
// first byte outside the table is refused rather than guessed at.
void test_unknown_first_bytes_are_refused(void)
{
    TEST_ASSERT_FALSE(parse_z("|1\r\n+key-popularity\r\n"));
    static const char *const BAD[] = {"?1\r\n", "@x\r\n", "1\r\n", " \r\n", "\r\n\r\n"};
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        TEST_ASSERT_FALSE_MESSAGE(parse_z(BAD[i]), BAD[i]);
        TEST_ASSERT_EQUAL_UINT(0u, RespV.n);
    }
}

// A parse reports true only once the whole value is buffered, so a value split across two reads is
// waited for rather than half decoded.
void test_parse_waits_for_the_whole_value(void)
{
    static const char WIRE[] = "$5\r\nhello\r\n";
    const size_t total = sizeof(WIRE) - 1;
    for (size_t have = 0; have < total; have++)
    {
        TEST_ASSERT_FALSE_MESSAGE(parse(WIRE, have), WIRE);
        TEST_ASSERT_EQUAL_UINT(0u, RespV.n);
    }
    TEST_ASSERT_TRUE(parse(WIRE, total));
    TEST_ASSERT_EQUAL_UINT(total, RespV.n);

    // A header line with no CRLF yet is not a value either.
    TEST_ASSERT_FALSE(parse_z("+PARTIAL"));
    TEST_ASSERT_FALSE(parse(NULL, 8));
}

// An encode that cannot fit the whole command reports 0 octets rather than a truncated array, which
// a server would read as a different command.
void test_encode_fails_closed(void)
{
    static const char *const ARGV[2] = {"LLEN", "mylist"};
    char small[16];
    RespV.command.argv = ARGV;
    RespV.command.argv_len = NULL;
    RespV.command.argc = 2;
    RespV.out.buf = small;
    RespV.out.cap = sizeof(small); // the command needs 26 plus a NUL
    Resp.encode_command(redis_resp_work);
    TEST_ASSERT_FALSE(RespV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, RespV.n);

    // An argument array with a hole in it is refused.
    static const char *const HOLE[2] = {"LLEN", NULL};
    TEST_ASSERT_EQUAL_UINT(0u, encode(HOLE, 2, NULL));
    TEST_ASSERT_FALSE(RespV.ok);

    // No arguments at all is not a command.
    TEST_ASSERT_EQUAL_UINT(0u, encode(ARGV, 0, NULL));
    TEST_ASSERT_FALSE(RespV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, encode(NULL, 2, NULL));
    TEST_ASSERT_FALSE(RespV.ok);

    RespV.command.argv = ARGV;
    RespV.command.argc = 2;
    RespV.out.buf = NULL;
    RespV.out.cap = sizeof(g_out);
    Resp.encode_command(redis_resp_work);
    TEST_ASSERT_FALSE(RespV.ok);
    RespV.out.buf = g_out;
    RespV.out.cap = 0;
    Resp.encode_command(redis_resp_work);
    TEST_ASSERT_FALSE(RespV.ok);
}

// A command of many arguments keeps the array count and every length prefix in step, which is what
// pipelining several commands into one write depends on.
void test_multi_argument_command(void)
{
    static const char *const ARGV[5] = {"HSET", "myhash", "field1", "value1", "x"};
    static const char WANT[] = "*5\r\n$4\r\nHSET\r\n$6\r\nmyhash\r\n$6\r\nfield1\r\n$6\r\nvalue1\r\n$1\r\nx\r\n";
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT) - 1, encode(ARGV, 5, NULL));
    TEST_ASSERT_EQUAL_STRING(WANT, g_out);

    // The encoded command reads back as an Array header of five bulk strings.
    size_t off = 0;
    TEST_ASSERT_TRUE(parse(g_out + off, sizeof(WANT) - 1 - off));
    TEST_ASSERT_EQUAL_INT(RESP_ARRAY, RespV.reply.type);
    TEST_ASSERT_EQUAL_INT64(5, RespV.reply.count);
    off += RespV.n;
    for (size_t i = 0; i < 5; i++)
    {
        TEST_ASSERT_TRUE(parse(g_out + off, sizeof(WANT) - 1 - off));
        TEST_ASSERT_EQUAL_INT(RESP_BULK_STRING, RespV.reply.type);
        TEST_ASSERT_EQUAL_UINT(strlen(ARGV[i]), RespV.reply.str_len);
        TEST_ASSERT_EQUAL_MEMORY(ARGV[i], RespV.reply.str, strlen(ARGV[i]));
        off += RespV.n;
    }
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT) - 1, off);
}
