// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the declarative frame builder (mmgr/protoframe.h).
//
// This engine is the single place the library turns values into wire bytes, so the ~160 call
// sites that declare a frame spec carry no formatting logic of their own. That is only a good
// trade if the engine itself is pinned hard, which is what this suite is for: every field kind,
// every width, the fail-closed contract at the exact byte boundary, the append rewind, and a
// differential check against the libc printf whose output these frames replace byte for byte.

#include "mmgr/protoframe.h"
#include <stdio.h> // snprintf: the libc reference the frames are diffed against
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// The real HTTP response header frame, declared the way a call site declares one.
static const protocore_field RESP[] = {{PROTOCORE_FK_LIT, 0, 9, "HTTP/1.1 "},
                                PROTOCORE_U32,
                                {PROTOCORE_FK_LIT, 0, 1, " "},
                                PROTOCORE_STR,
                                {PROTOCORE_FK_LIT, 0, 16, "\r\nContent-Type: "},
                                PROTOCORE_STR,
                                {PROTOCORE_FK_LIT, 0, 18, "\r\nContent-Length: "},
                                PROTOCORE_U32,
                                {PROTOCORE_FK_LIT, 0, 2, "\r\n"},
                                PROTOCORE_END};

void test_frame_matches_printf()
{
    char out[256];
    char want[256];
    size_t n = frame.build(out, sizeof(out), RESP,
                           (const protocore_fval[]){PROTOCORE_VU32(200u), PROTOCORE_VSTR("OK"), PROTOCORE_VSTR("text/plain"), PROTOCORE_VU32(21u)}, 4);
    snprintf(want, sizeof(want), "HTTP/1.1 %u %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n", 200u, "OK",
             "text/plain", 21u);
    TEST_ASSERT_EQUAL_STRING(want, out);
    TEST_ASSERT_EQUAL_size_t(strlen(want), n);
}

void test_frame_every_kind()
{
    static const protocore_field ALL[] = {{PROTOCORE_FK_HEX, 8, 0, NULL},
                                   {PROTOCORE_FK_LIT, 0, 1, "|"},
                                   {PROTOCORE_FK_DEC, 3, 0, NULL},
                                   {PROTOCORE_FK_LIT, 0, 1, "|"},
                                   PROTOCORE_I64,
                                   {PROTOCORE_FK_LIT, 0, 1, "|"},
                                   PROTOCORE_U64,
                                   {PROTOCORE_FK_LIT, 0, 1, "|"},
                                   {PROTOCORE_FK_G, 6, 0, NULL},
                                   {PROTOCORE_FK_LIT, 0, 1, "|"},
                                   {PROTOCORE_FK_FIX, 2, 0, NULL},
                                   {PROTOCORE_FK_LIT, 0, 1, "|"},
                                   PROTOCORE_CH,
                                   {PROTOCORE_FK_LIT, 0, 1, "|"},
                                   PROTOCORE_JSON,
                                   {PROTOCORE_FK_LIT, 0, 1, "|"},
                                   PROTOCORE_XML,
                                   {PROTOCORE_FK_LIT, 0, 1, "|"},
                                   {PROTOCORE_FK_OCT, 4, 0, NULL},
                                   PROTOCORE_END};
    char out[160];
    frame.build(out, sizeof(out), ALL,
                (const protocore_fval[]){PROTOCORE_VHEX(0xbeefu), PROTOCORE_VDEC(7u), PROTOCORE_VI64(-5), PROTOCORE_VU64(12u), PROTOCORE_VG(3.14159265),
                                  PROTOCORE_VFIX(2.5), PROTOCORE_VCH('x'), PROTOCORE_VJSON("a\"b"), PROTOCORE_VXML("a<b"), PROTOCORE_VOCT(8u)},
                10);
    TEST_ASSERT_EQUAL_STRING("0000beef|007|-5|12|3.14159|2.50|x|\"a\\\"b\"|a&lt;b|0010", out);
}

void test_frame_widths()
{
    static const protocore_field W[] = {
        {PROTOCORE_FK_HEX, 0, 0, NULL}, {PROTOCORE_FK_LIT, 0, 1, ","}, {PROTOCORE_FK_HEX, 4, 0, NULL}, {PROTOCORE_FK_LIT, 0, 1, ","},
        {PROTOCORE_FK_DEC, 5, 0, NULL}, {PROTOCORE_FK_LIT, 0, 1, ","}, {PROTOCORE_FK_OCT, 0, 0, NULL}, PROTOCORE_END};
    char out[64];
    frame.build(out, sizeof(out), W, (const protocore_fval[]){PROTOCORE_VHEX(0xabu), PROTOCORE_VHEX(0xabu), PROTOCORE_VDEC(42u), PROTOCORE_VOCT(64u)}, 4);
    // width 0 means "no padding", not "zero digits"
    TEST_ASSERT_EQUAL_STRING("ab,00ab,00042,100", out);
}

void test_frame_null_string_is_empty()
{
    static const protocore_field S[] = {{PROTOCORE_FK_LIT, 0, 1, "["}, PROTOCORE_STR, {PROTOCORE_FK_LIT, 0, 1, "]"}, PROTOCORE_END};
    char out[32];
    frame.build(out, sizeof(out), S, (const protocore_fval[]){PROTOCORE_VSTR(NULL)}, 1);
    TEST_ASSERT_EQUAL_STRING("[]", out);
}

void test_frame_literal_only()
{
    static const protocore_field L[] = {{PROTOCORE_FK_LIT, 0, 12, "no args here"}, PROTOCORE_END};
    char out[32];
    TEST_ASSERT_EQUAL_size_t(12, frame.build(out, sizeof(out), L, NULL, 0));
    TEST_ASSERT_EQUAL_STRING("no args here", out);
}

void test_frame_empty_spec()
{
    static const protocore_field E[] = {PROTOCORE_END};
    char out[8];
    out[0] = 'x';
    // an empty frame writes nothing and reports 0, and must still leave a valid C string
    TEST_ASSERT_EQUAL_size_t(0, frame.build(out, sizeof(out), E, NULL, 0));
    TEST_ASSERT_EQUAL_STRING("", out);
}

// The contract that the hand-written conversion kept getting wrong: on overflow the builder
// reports 0 AND leaves a valid empty string, so a caller that ignores the return can never read
// a half-written frame or bytes left over from a previous build.
void test_frame_overflow_fails_closed()
{
    char tiny[8];
    memset(tiny, 'Z', sizeof(tiny));
    TEST_ASSERT_EQUAL_size_t(
        0, frame.build(tiny, sizeof(tiny), RESP,
                       (const protocore_fval[]){PROTOCORE_VU32(200u), PROTOCORE_VSTR("OK"), PROTOCORE_VSTR("text/plain"), PROTOCORE_VU32(21u)}, 4));
    TEST_ASSERT_EQUAL_STRING("", tiny);
}

void test_frame_exact_fit_boundary()
{
    static const protocore_field F[] = {{PROTOCORE_FK_LIT, 0, 4, "abcd"}, PROTOCORE_END};
    char five[5];
    TEST_ASSERT_EQUAL_size_t(4, frame.build(five, sizeof(five), F, NULL, 0)); // 4 bytes + NUL fits exactly
    TEST_ASSERT_EQUAL_STRING("abcd", five);

    char four[4];
    TEST_ASSERT_EQUAL_size_t(0, frame.build(four, sizeof(four), F, NULL, 0)); // one byte short
    TEST_ASSERT_EQUAL_STRING("", four);
}

void test_frame_guards()
{
    char out[8];
    TEST_ASSERT_EQUAL_size_t(
        0, frame.build(NULL, 8, RESP, (const protocore_fval[]){PROTOCORE_VU32(1u), PROTOCORE_VSTR(""), PROTOCORE_VSTR(""), PROTOCORE_VU32(0u)}, 4));
    TEST_ASSERT_EQUAL_size_t(
        0, frame.build(out, 0, RESP, (const protocore_fval[]){PROTOCORE_VU32(1u), PROTOCORE_VSTR(""), PROTOCORE_VSTR(""), PROTOCORE_VU32(0u)}, 4));
    TEST_ASSERT_EQUAL_size_t(0, frame.build(out, sizeof(out), NULL, NULL, 0));
}

// A zero-capacity buffer owns no bytes, so not even the NUL may be written. Every appender already
// refused to write into one, which left the terminator in protocore_sb_finish as the single path that
// still would have - a one-byte overwrite past a buffer the caller said had no room at all.
void test_frame_zero_cap_writes_nothing()
{
    char sentinel[2] = {0x7F, 0x7F};
    TEST_ASSERT_EQUAL_size_t(
        0,
        frame.build(sentinel, 0, RESP, (const protocore_fval[]){PROTOCORE_VU32(200u), PROTOCORE_VSTR("OK"), PROTOCORE_VSTR("t"), PROTOCORE_VU32(0u)}, 4));
    TEST_ASSERT_EQUAL_CHAR(0x7F, sentinel[0]);

    static const protocore_field L[] = {{PROTOCORE_FK_LIT, 0, 1, "x"}, PROTOCORE_END};
    TEST_ASSERT_EQUAL_size_t(0, frame.build(sentinel, 0, L, NULL, 0));
    TEST_ASSERT_EQUAL_CHAR(0x7F, sentinel[0]);
}

static const protocore_field HDR[] = {PROTOCORE_STR, {PROTOCORE_FK_LIT, 0, 2, ": "}, PROTOCORE_STR, {PROTOCORE_FK_LIT, 0, 2, "\r\n"}, PROTOCORE_END};

void test_frame_append_accumulates()
{
    char acc[64];
    acc[0] = '\0';
    TEST_ASSERT_EQUAL_size_t(8,
                             frame.append(acc, sizeof(acc), HDR, (const protocore_fval[]){PROTOCORE_VSTR("X-A"), PROTOCORE_VSTR("1")}, 2));
    TEST_ASSERT_EQUAL_size_t(16,
                             frame.append(acc, sizeof(acc), HDR, (const protocore_fval[]){PROTOCORE_VSTR("X-B"), PROTOCORE_VSTR("2")}, 2));
    TEST_ASSERT_EQUAL_STRING("X-A: 1\r\nX-B: 2\r\n", acc);
}

void test_frame_append_rewinds_whole_frame()
{
    // A frame that does not fit must leave the accumulated buffer exactly as it was - a
    // half-written header line is a protocol violation, not a truncation.
    char small[12];
    small[0] = '\0';
    frame.append(small, sizeof(small), HDR, (const protocore_fval[]){PROTOCORE_VSTR("X-A"), PROTOCORE_VSTR("1")}, 2);
    TEST_ASSERT_EQUAL_size_t(
        0, frame.append(small, sizeof(small), HDR, (const protocore_fval[]){PROTOCORE_VSTR("X-VeryLong"), PROTOCORE_VSTR("2")}, 2));
    TEST_ASSERT_EQUAL_STRING("X-A: 1\r\n", small);
}

void test_frame_append_to_full_buffer()
{
    char full[8];
    memset(full, 'a', sizeof(full) - 1);
    full[sizeof(full) - 1] = '\0';
    TEST_ASSERT_EQUAL_size_t(0,
                             frame.append(full, sizeof(full), HDR, (const protocore_fval[]){PROTOCORE_VSTR("X"), PROTOCORE_VSTR("1")}, 2));
    TEST_ASSERT_EQUAL_STRING("aaaaaaa", full); // untouched
}

void test_frame_float_matches_printf()
{
    static const protocore_field G[] = {{PROTOCORE_FK_G, 6, 0, NULL}, PROTOCORE_END};
    static const protocore_field G10[] = {{PROTOCORE_FK_G, 10, 0, NULL}, PROTOCORE_END};
    static const protocore_field F2[] = {{PROTOCORE_FK_FIX, 2, 0, NULL}, PROTOCORE_END};
    static const double vals[] = {0.0, -0.0, 1.0, 2.5, 0.1, 1e-6, 123456.0, 1234567.0, 9.9999995, 1e20};
    char out[64];
    char want[64];
    for (unsigned i = 0; i < sizeof(vals) / sizeof(vals[0]); i++)
    {
        frame.build(out, sizeof(out), G, (const protocore_fval[]){PROTOCORE_VG(vals[i])}, 1);
        snprintf(want, sizeof(want), "%.6g", vals[i]);
        TEST_ASSERT_EQUAL_STRING(want, out);

        frame.build(out, sizeof(out), G10, (const protocore_fval[]){PROTOCORE_VG(vals[i])}, 1);
        snprintf(want, sizeof(want), "%.10g", vals[i]);
        TEST_ASSERT_EQUAL_STRING(want, out);

        // PROTOCORE_FIX is byte-identical to printf only within the 64-bit range it documents.
        if (vals[i] < 18446744073709551616.0)
        {
            frame.build(out, sizeof(out), F2, (const protocore_fval[]){PROTOCORE_VFIX(vals[i])}, 1);
            snprintf(want, sizeof(want), "%.2f", vals[i]);
            TEST_ASSERT_EQUAL_STRING(want, out);
        }
    }
}

// Past 2^64 the integer part will not go through uint64: casting 1e20 wrapped and produced
// "0.00" - a twenty-digit value rendered as zero. The documented fallback is the
// significant-digit form, and this pins it so the failure can never come back as a small number.
void test_frame_fixed_huge_falls_back()
{
    static const protocore_field F2[] = {{PROTOCORE_FK_FIX, 2, 0, NULL}, PROTOCORE_END};
    char out[64];
    frame.build(out, sizeof(out), F2, (const protocore_fval[]){PROTOCORE_VFIX(1e20)}, 1);
    TEST_ASSERT_EQUAL_STRING("1e+20", out);
    frame.build(out, sizeof(out), F2, (const protocore_fval[]){PROTOCORE_VFIX(1e300)}, 1);
    TEST_ASSERT_EQUAL_STRING("1e+300", out);
}

void test_frame_unknown_opcode_refuses()
{
    // A spec built against a newer engine must not silently emit a frame missing a field.
    static const protocore_field BAD[] = {{PROTOCORE_FK_LIT, 0, 1, "a"}, {200, 0, 0, NULL}, PROTOCORE_END};
    char out[32];
    TEST_ASSERT_EQUAL_size_t(0, frame.build(out, sizeof(out), BAD, (const protocore_fval[]){{200, {.u32 = 0}}}, 1));
    TEST_ASSERT_EQUAL_STRING("", out);
}

// The tag the caller wrote is compared against the spec's before the value is read, so a spec and
// its arguments that disagree produce nothing rather than a frame built from the wrong member.
void test_frame_value_kind_must_match_spec()
{
    static const protocore_field N[] = {PROTOCORE_U32, PROTOCORE_END};
    char out[32];
    out[0] = 'x';
    TEST_ASSERT_EQUAL_size_t(0, frame.build(out, sizeof(out), N, (const protocore_fval[]){PROTOCORE_VSTR("7")}, 1));
    TEST_ASSERT_EQUAL_STRING("", out);
}

// Arity is a parameter now, so both directions of a miscount are refusals rather than a read past
// the end or a silently dropped field.
void test_frame_arity_must_match_spec()
{
    static const protocore_field TWO[] = {PROTOCORE_U32, {PROTOCORE_FK_LIT, 0, 1, "-"}, PROTOCORE_U32, PROTOCORE_END};
    char out[32];

    TEST_ASSERT_EQUAL_size_t(0, frame.build(out, sizeof(out), TWO, (const protocore_fval[]){PROTOCORE_VU32(1u)}, 1));
    TEST_ASSERT_EQUAL_STRING("", out);

    TEST_ASSERT_EQUAL_size_t(
        0, frame.build(out, sizeof(out), TWO, (const protocore_fval[]){PROTOCORE_VU32(1u), PROTOCORE_VU32(2u), PROTOCORE_VU32(3u)}, 3));
    TEST_ASSERT_EQUAL_STRING("", out);

    TEST_ASSERT_EQUAL_size_t(3, frame.build(out, sizeof(out), TWO, (const protocore_fval[]){PROTOCORE_VU32(1u), PROTOCORE_VU32(2u)}, 2));
    TEST_ASSERT_EQUAL_STRING("1-2", out);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_frame_matches_printf);
    RUN_TEST(test_frame_every_kind);
    RUN_TEST(test_frame_widths);
    RUN_TEST(test_frame_null_string_is_empty);
    RUN_TEST(test_frame_literal_only);
    RUN_TEST(test_frame_empty_spec);
    RUN_TEST(test_frame_overflow_fails_closed);
    RUN_TEST(test_frame_exact_fit_boundary);
    RUN_TEST(test_frame_guards);
    RUN_TEST(test_frame_zero_cap_writes_nothing);
    RUN_TEST(test_frame_append_accumulates);
    RUN_TEST(test_frame_append_rewinds_whole_frame);
    RUN_TEST(test_frame_append_to_full_buffer);
    RUN_TEST(test_frame_float_matches_printf);
    RUN_TEST(test_frame_fixed_huge_falls_back);
    RUN_TEST(test_frame_value_kind_must_match_spec);
    RUN_TEST(test_frame_arity_must_match_spec);
    RUN_TEST(test_frame_unknown_opcode_refuses);
    return UNITY_END();
}
