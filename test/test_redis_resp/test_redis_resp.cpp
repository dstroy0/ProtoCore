// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the Redis RESP2 codec (services/iot/redis_resp): the command encoder
// and the cursor reply parser. Pure host tests.

#include "services/iot/redis_resp/redis_resp.h"
#include <math.h>
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// A command encodes as an array of bulk strings.
void test_encode_command()
{
    const char *argv[] = {"SET", "foo", "bar"};
    char buf[64];
    size_t n = pc_resp_encode_command(buf, sizeof(buf), argv, nullptr, 3);
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_EQUAL_STRING("*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n", buf);
    TEST_ASSERT_EQUAL_size_t(n, strlen(buf));
}

// Explicit per-arg lengths allow binary-safe args (embedded NUL).
void test_encode_binary_safe()
{
    const char a0[] = {'S', 'E', 'T'};
    const char a1[] = {'k'};
    const char a2[] = {'a', '\0', 'b'}; // contains a NUL
    const char *argv[] = {a0, a1, a2};
    const size_t lens[] = {3, 1, 3};
    char buf[64];
    size_t n = pc_resp_encode_command(buf, sizeof(buf), argv, lens, 3);
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    const char expect[] = "*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$3\r\na\0b\r\n";
    TEST_ASSERT_EQUAL_size_t(sizeof(expect) - 1, n);
    TEST_ASSERT_EQUAL_MEMORY(expect, buf, n);
}

void test_encode_overflow_fails_closed()
{
    const char *argv[] = {"SET", "foo", "bar"};
    char buf[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_resp_encode_command(buf, sizeof(buf), argv, nullptr, 3));
}

void test_parse_simple_and_error()
{
    RespReply r;
    size_t c;
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)"+OK\r\n", 5, &r, &c));
    TEST_ASSERT_EQUAL(RespType::RESP_SIMPLE, r.type);
    TEST_ASSERT_EQUAL_MEMORY("OK", r.str, 2);
    TEST_ASSERT_EQUAL_size_t(5, c);

    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)"-ERR no\r\n", 9, &r, &c));
    TEST_ASSERT_EQUAL(RespType::RESP_ERROR, r.type);
    TEST_ASSERT_EQUAL_MEMORY("ERR no", r.str, 6);
}

void test_parse_integer()
{
    RespReply r;
    size_t c;
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)":1000\r\n", 7, &r, &c));
    TEST_ASSERT_EQUAL(RespType::RESP_INTEGER, r.type);
    TEST_ASSERT_EQUAL_INT64(1000, r.ival);
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)":-5\r\n", 5, &r, &c));
    TEST_ASSERT_EQUAL_INT64(-5, r.ival);
}

void test_parse_bulk_and_nil()
{
    RespReply r;
    size_t c;
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)"$5\r\nhello\r\n", 11, &r, &c));
    TEST_ASSERT_EQUAL(RespType::RESP_BULK, r.type);
    TEST_ASSERT_EQUAL_size_t(5, r.str_len);
    TEST_ASSERT_EQUAL_MEMORY("hello", r.str, 5);
    TEST_ASSERT_EQUAL_size_t(11, c);

    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)"$-1\r\n", 5, &r, &c)); // nil bulk
    TEST_ASSERT_EQUAL(RespType::RESP_NIL, r.type);
}

// An array is parsed as a header (count); the caller then parses each element
// from the remaining bytes using the reported consumed offset.
void test_parse_array_cursor()
{
    const uint8_t *msg = (const uint8_t *)"*2\r\n$3\r\nfoo\r\n:42\r\n";
    size_t len = strlen((const char *)msg);
    RespReply r;
    size_t c;
    TEST_ASSERT_TRUE(pc_resp_parse(msg, len, &r, &c));
    TEST_ASSERT_EQUAL(RespType::RESP_ARRAY, r.type);
    TEST_ASSERT_EQUAL_INT64(2, r.count);
    size_t off = c;
    TEST_ASSERT_TRUE(pc_resp_parse(msg + off, len - off, &r, &c)); // element 0
    TEST_ASSERT_EQUAL(RespType::RESP_BULK, r.type);
    TEST_ASSERT_EQUAL_MEMORY("foo", r.str, 3);
    off += c;
    TEST_ASSERT_TRUE(pc_resp_parse(msg + off, len - off, &r, &c)); // element 1
    TEST_ASSERT_EQUAL(RespType::RESP_INTEGER, r.type);
    TEST_ASSERT_EQUAL_INT64(42, r.ival);
}

void test_parse_incomplete_and_malformed()
{
    RespReply r;
    size_t c;
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)"+OK", 3, &r, &c));           // no CRLF yet
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)"$5\r\nhel", 7, &r, &c));     // bulk body incomplete
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)"?bad\r\n", 6, &r, &c));      // unknown type byte
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)":notanum\r\n", 10, &r, &c)); // non-numeric integer
    // A bulk length near SIZE_MAX must fail closed, not wrap the bounds check (32-bit hardening).
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)"$999999999999\r\nhi\r\n", 19, &r, &c));
}

// Every guard sub-condition of pc_resp_encode_command, plus a null arg element and an arg-loop overflow.
void test_encode_guard_subconditions()
{
    char buf[64];
    const char *argv[] = {"GET", "k"};
    size_t alen[] = {3, 1};
    TEST_ASSERT_EQUAL_size_t(0, pc_resp_encode_command(nullptr, sizeof(buf), argv, alen, 2)); // null buf
    TEST_ASSERT_EQUAL_size_t(0, pc_resp_encode_command(buf, 0, argv, alen, 2));               // zero cap
    TEST_ASSERT_EQUAL_size_t(0, pc_resp_encode_command(buf, sizeof(buf), nullptr, alen, 2));  // null args
    TEST_ASSERT_EQUAL_size_t(0, pc_resp_encode_command(buf, sizeof(buf), argv, alen, 0));     // zero argc
    const char *withnull[] = {"GET", nullptr};
    TEST_ASSERT_EQUAL_size_t(0, pc_resp_encode_command(buf, sizeof(buf), withnull, nullptr, 2)); // null arg elem
    TEST_ASSERT_EQUAL_size_t(0, pc_resp_encode_command(buf, 8, argv, alen, 2)); // header fits, arg overflows
}

// Parser guard sub-conditions and the integer / bulk terminator / nil-array edges.
void test_parse_guard_subconditions_and_edges()
{
    RespReply r;
    size_t c;
    TEST_ASSERT_FALSE(pc_resp_parse(nullptr, 5, &r, &c));                           // null buf
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)"+\r", 2, &r, &c));            // len < 3
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)"+OK\r\n", 5, nullptr, &c));   // null out
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)"+OK\r\n", 5, &r, nullptr));   // null consumed
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)":\r\n", 3, &r, &c));          // empty integer
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)":-\r\n", 4, &r, &c));         // '-' then no digit
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)"$5\r\nhelloAB", 11, &r, &c)); // bulk with wrong terminator
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)"*-1\r\n", 5, &r, &c));         // nil array
    TEST_ASSERT_EQUAL_INT(RespType::RESP_NIL, r.type);
}

// RESP3: null (_), boolean (#t/#f).
void test_parse_resp3_null_bool()
{
    RespReply r;
    size_t c;
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)"_\r\n", 3, &r, &c));
    TEST_ASSERT_EQUAL(RespType::RESP_NIL, r.type);
    TEST_ASSERT_EQUAL_size_t(3, c);

    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)"#t\r\n", 4, &r, &c));
    TEST_ASSERT_EQUAL(RespType::RESP_BOOL, r.type);
    TEST_ASSERT_EQUAL_INT64(1, r.ival);
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)"#f\r\n", 4, &r, &c));
    TEST_ASSERT_EQUAL_INT64(0, r.ival);
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)"#x\r\n", 4, &r, &c)); // invalid boolean
}

// RESP3: double (,) including inf / -inf / nan and an exponent.
void test_parse_resp3_double()
{
    RespReply r;
    size_t c;
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)",3.14\r\n", 7, &r, &c));
    TEST_ASSERT_EQUAL(RespType::RESP_DOUBLE, r.type);
    TEST_ASSERT_EQUAL_MEMORY("3.14", r.str, 4);
    TEST_ASSERT_TRUE(fabs(r.dval - 3.14) < 1e-9);

    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)",inf\r\n", 6, &r, &c));
    TEST_ASSERT_TRUE(isinf(r.dval) && r.dval > 0);
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)",-inf\r\n", 7, &r, &c));
    TEST_ASSERT_TRUE(isinf(r.dval) && r.dval < 0);
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)",nan\r\n", 6, &r, &c));
    TEST_ASSERT_TRUE(isnan(r.dval));
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)",1.5e3\r\n", 8, &r, &c));
    TEST_ASSERT_TRUE(fabs(r.dval - 1500.0) < 1e-6);
}

// RESP3: big number ((), bulk error (!), verbatim string (=).
void test_parse_resp3_bignum_bulkerr_verbatim()
{
    RespReply r;
    size_t c;
    const char *big = "(3492890328409238509324850943850943825024385\r\n";
    const char *digits = "3492890328409238509324850943850943825024385";
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)big, strlen(big), &r, &c));
    TEST_ASSERT_EQUAL(RespType::RESP_BIG_NUMBER, r.type);
    TEST_ASSERT_EQUAL_size_t(strlen(digits), r.str_len);
    TEST_ASSERT_EQUAL_MEMORY(digits, r.str, r.str_len);

    const char *be = "!21\r\nSYNTAX invalid syntax\r\n";
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)be, strlen(be), &r, &c));
    TEST_ASSERT_EQUAL(RespType::RESP_BULK_ERROR, r.type);
    TEST_ASSERT_EQUAL_MEMORY("SYNTAX invalid syntax", r.str, 21);
    TEST_ASSERT_EQUAL_size_t(strlen(be), c);

    const char *vb = "=15\r\ntxt:Some string\r\n";
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)vb, strlen(vb), &r, &c));
    TEST_ASSERT_EQUAL(RespType::RESP_VERBATIM, r.type);
    TEST_ASSERT_EQUAL_MEMORY("txt:Some string", r.str, 15); // format prefix kept
}

// RESP3 aggregates: map (%, reports 2*pairs), set (~), push (>).
void test_parse_resp3_map_set_push()
{
    const uint8_t *m = (const uint8_t *)"%2\r\n$1\r\na\r\n:1\r\n$1\r\nb\r\n:2\r\n";
    size_t len = strlen((const char *)m);
    RespReply r;
    size_t c;
    TEST_ASSERT_TRUE(pc_resp_parse(m, len, &r, &c));
    TEST_ASSERT_EQUAL(RespType::RESP_MAP, r.type);
    TEST_ASSERT_EQUAL_INT64(4, r.count); // 2 pairs -> 4 children
    size_t off = c;
    int64_t seen_vals = 0;
    for (int i = 0; i < 4; i++)
    {
        TEST_ASSERT_TRUE(pc_resp_parse(m + off, len - off, &r, &c));
        if (i % 2 == 1)
        {
            seen_vals += r.ival; // the values are integers 1 and 2
        }
        off += c;
    }
    TEST_ASSERT_EQUAL_size_t(len, off);
    TEST_ASSERT_EQUAL_INT64(3, seen_vals);

    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)"~2\r\n", 4, &r, &c));
    TEST_ASSERT_EQUAL(RespType::RESP_SET, r.type);
    TEST_ASSERT_EQUAL_INT64(2, r.count);
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)">3\r\n", 4, &r, &c));
    TEST_ASSERT_EQUAL(RespType::RESP_PUSH, r.type);
    TEST_ASSERT_EQUAL_INT64(3, r.count);
}

// A zero-length argument still gets a well-formed "$0\r\n\r\n" bulk (the length writer's n == 0 path).
void test_encode_zero_length_arg()
{
    const char *argv[] = {"SET", "k", ""};
    const size_t lens[] = {3, 1, 0};
    char buf[64];
    size_t n = pc_resp_encode_command(buf, sizeof(buf), argv, lens, 3);
    TEST_ASSERT_EQUAL_STRING("*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$0\r\n\r\n", buf);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
}

// The encoder's two distinct overflow stages: the leading array header, and an argument body that no
// longer fits once its own "$<len>\r\n" prefix has been written.
void test_encode_overflow_stages()
{
    const char *argv[] = {"GET", "key"};
    const size_t lens[] = {3, 3};
    char buf[64];
    TEST_ASSERT_EQUAL_size_t(0, pc_resp_encode_command(buf, 3, argv, lens, 2));  // "*2\r\n" alone overflows
    TEST_ASSERT_EQUAL_size_t(0, pc_resp_encode_command(buf, 10, argv, lens, 2)); // "$3\r\n" fits, "GET\r\n" does not
}

// RESP3 double: leading sign, fraction-only and integer-only forms, and both exponent spellings.
// A malformed double is still a well-formed reply - the raw text stays authoritative in str and dval
// is simply left at 0.
void test_parse_resp3_double_forms()
{
    RespReply r;
    size_t c;
    struct DblCase
    {
        const char *wire;
        double want;
    };
    const DblCase good[] = {
        {",-2.5\r\n", -2.5},  {",+2.5\r\n", 2.5}, {",1.5e-3\r\n", 0.0015}, {",1.5E2\r\n", 150.0},
        {",1e+2\r\n", 100.0}, {",12\r\n", 12.0},  {",.5\r\n", 0.5},        {",1.\r\n", 1.0},
    };
    for (unsigned i = 0; i < sizeof(good) / sizeof(good[0]); i++)
    {
        TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)good[i].wire, strlen(good[i].wire), &r, &c));
        TEST_ASSERT_EQUAL(RespType::RESP_DOUBLE, r.type);
        TEST_ASSERT_TRUE(fabs(r.dval - good[i].want) < 1e-9);
    }

    const char *malformed[] = {
        ",1e\r\n",    // 'e' with no exponent digits
        ",1.5x\r\n",  // trailing garbage after the mantissa
        ",abc\r\n",   // no digits at all
        ",12a\r\n",   // trailing non-digit, no exponent
        ",1e2x\r\n",  // trailing garbage after the exponent digits
        ",1e2.5\r\n", // '.' ends the exponent digits, then trails
        ",1.-2\r\n",  // '-' ends the fraction digits, then trails
        ",\r\n",      // empty
    };
    for (unsigned i = 0; i < sizeof(malformed) / sizeof(malformed[0]); i++)
    {
        TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)malformed[i], strlen(malformed[i]), &r, &c));
        TEST_ASSERT_EQUAL(RespType::RESP_DOUBLE, r.type);
        TEST_ASSERT_TRUE(fabs(r.dval) < 1e-12);
    }

    // An exponent past the accumulator clamp saturates the double instead of overflowing the int.
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)",1e10000000\r\n", 13, &r, &c));
    TEST_ASSERT_TRUE(isinf(r.dval) && r.dval > 0);
}

// The special forms are matched case-insensitively, and a slice that merely prefixes (or extends)
// one of them is not a match.
void test_parse_double_special_case_insensitive()
{
    RespReply r;
    size_t c;
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)",INF\r\n", 6, &r, &c));
    TEST_ASSERT_TRUE(isinf(r.dval) && r.dval > 0);
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)",+inf\r\n", 7, &r, &c));
    TEST_ASSERT_TRUE(isinf(r.dval) && r.dval > 0);
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)",-INF\r\n", 7, &r, &c));
    TEST_ASSERT_TRUE(isinf(r.dval) && r.dval < 0);
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)",NaN\r\n", 6, &r, &c));
    TEST_ASSERT_TRUE(isnan(r.dval));
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)",inf0\r\n", 7, &r, &c)); // longer than "inf"
    TEST_ASSERT_TRUE(fabs(r.dval) < 1e-12);
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)",in\r\n", 5, &r, &c)); // shorter than "inf"
    TEST_ASSERT_TRUE(fabs(r.dval) < 1e-12);
}

// Length-prefixed body rejects: an unparsable length, a negative length on the RESP3 forms (only
// $-1 is a nil sentinel), a body that runs past the buffer, and a broken trailing terminator.
void test_parse_bulk_body_rejects()
{
    RespReply r;
    size_t c;
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)"$xx\r\nhi\r\n", 9, &r, &c));   // length not a number
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)"!-1\r\n", 5, &r, &c));         // bulk error has no nil form
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)"=-3\r\nabc\r\n", 10, &r, &c)); // verbatim has no nil form
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)"$0\r\n", 4, &r, &c));          // no room for the body CRLF
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)"$5\r\nhello\rX", 11, &r, &c)); // '\r' not followed by '\n'

    // A zero-length bulk string is valid and consumes its own trailing CRLF.
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)"$0\r\n\r\n", 6, &r, &c));
    TEST_ASSERT_EQUAL(RespType::RESP_BULK, r.type);
    TEST_ASSERT_EQUAL_size_t(0, r.str_len);
    TEST_ASSERT_EQUAL_size_t(6, c);
}

// Aggregate (* ~ >), map (%) and boolean (#) header rejects.
void test_parse_aggregate_and_scalar_rejects()
{
    RespReply r;
    size_t c;
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)"*xy\r\n", 5, &r, &c)); // array count not a number
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)"~-1\r\n", 5, &r, &c)); // only *-1 is the nil sentinel
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)">-2\r\n", 5, &r, &c));
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)"%-1\r\n", 5, &r, &c)); // negative map pair count
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)"%ab\r\n", 5, &r, &c)); // map count not a number
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)"#tt\r\n", 5, &r, &c)); // boolean is exactly one octet
}

// The line scanner must not stop on a bare '\r', and the integer scanner rejects an octet below '0'
// as well as one above '9'.
void test_parse_line_scan_and_integer_octets()
{
    RespReply r;
    size_t c;
    TEST_ASSERT_TRUE(pc_resp_parse((const uint8_t *)"+A\rB\r\n", 6, &r, &c)); // a bare '\r' is payload
    TEST_ASSERT_EQUAL(RespType::RESP_SIMPLE, r.type);
    TEST_ASSERT_EQUAL_size_t(3, r.str_len);
    TEST_ASSERT_EQUAL_MEMORY("A\rB", r.str, 3);
    TEST_ASSERT_EQUAL_size_t(6, c);
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)": 1\r\n", 5, &r, &c));  // ' ' is below '0'
    TEST_ASSERT_FALSE(pc_resp_parse((const uint8_t *)":1a2\r\n", 6, &r, &c)); // 'a' is above '9'
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_encode_command);
    RUN_TEST(test_encode_binary_safe);
    RUN_TEST(test_encode_overflow_fails_closed);
    RUN_TEST(test_parse_simple_and_error);
    RUN_TEST(test_parse_integer);
    RUN_TEST(test_parse_bulk_and_nil);
    RUN_TEST(test_parse_array_cursor);
    RUN_TEST(test_parse_incomplete_and_malformed);
    RUN_TEST(test_encode_guard_subconditions);
    RUN_TEST(test_parse_guard_subconditions_and_edges);
    RUN_TEST(test_parse_resp3_null_bool);
    RUN_TEST(test_parse_resp3_double);
    RUN_TEST(test_parse_resp3_bignum_bulkerr_verbatim);
    RUN_TEST(test_parse_resp3_map_set_push);
    RUN_TEST(test_encode_zero_length_arg);
    RUN_TEST(test_encode_overflow_stages);
    RUN_TEST(test_parse_resp3_double_forms);
    RUN_TEST(test_parse_double_special_case_insensitive);
    RUN_TEST(test_parse_bulk_body_rejects);
    RUN_TEST(test_parse_aggregate_and_scalar_rejects);
    RUN_TEST(test_parse_line_scan_and_integer_octets);
    return UNITY_END();
}
