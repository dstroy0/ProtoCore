// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the declarative frame builder (shared_primitives/frame.h).
//
// This engine is the single place the library turns values into wire bytes, so the ~160 call
// sites that declare a frame spec carry no formatting logic of their own. That is only a good
// trade if the engine itself is pinned hard, which is what this suite is for: every field kind,
// every width, the fail-closed contract at the exact byte boundary, the append rewind, and a
// differential check against the libc printf whose output these frames replace byte for byte.

#include "shared_primitives/frame.h"
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
static const pc_field RESP[] = {{PC_FK_LIT, 0, 9, "HTTP/1.1 "},
                                PC_U32,
                                {PC_FK_LIT, 0, 1, " "},
                                PC_STR,
                                {PC_FK_LIT, 0, 16, "\r\nContent-Type: "},
                                PC_STR,
                                {PC_FK_LIT, 0, 18, "\r\nContent-Length: "},
                                PC_U32,
                                {PC_FK_LIT, 0, 2, "\r\n"},
                                PC_END};

void test_frame_matches_printf()
{
    char out[256];
    char want[256];
    size_t n = pc_frame_build(out, sizeof(out), RESP, (uint32_t)200, "OK", "text/plain", (uint32_t)21);
    snprintf(want, sizeof(want), "HTTP/1.1 %u %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n", 200u, "OK",
             "text/plain", 21u);
    TEST_ASSERT_EQUAL_STRING(want, out);
    TEST_ASSERT_EQUAL_size_t(strlen(want), n);
}

void test_frame_every_kind()
{
    static const pc_field ALL[] = {{PC_FK_HEX, 8, 0, nullptr},
                                   {PC_FK_LIT, 0, 1, "|"},
                                   {PC_FK_DEC, 3, 0, nullptr},
                                   {PC_FK_LIT, 0, 1, "|"},
                                   PC_I64,
                                   {PC_FK_LIT, 0, 1, "|"},
                                   PC_U64,
                                   {PC_FK_LIT, 0, 1, "|"},
                                   {PC_FK_G, 6, 0, nullptr},
                                   {PC_FK_LIT, 0, 1, "|"},
                                   {PC_FK_FIX, 2, 0, nullptr},
                                   {PC_FK_LIT, 0, 1, "|"},
                                   PC_CH,
                                   {PC_FK_LIT, 0, 1, "|"},
                                   PC_JSON,
                                   {PC_FK_LIT, 0, 1, "|"},
                                   PC_XML,
                                   {PC_FK_LIT, 0, 1, "|"},
                                   {PC_FK_OCT, 4, 0, nullptr},
                                   PC_END};
    char out[160];
    pc_frame_build(out, sizeof(out), ALL, (uint64_t)0xbeef, (uint32_t)7, (int64_t)-5, (uint64_t)12, (double)3.14159265,
                   (double)2.5, 'x', "a\"b", "a<b", (uint64_t)8);
    TEST_ASSERT_EQUAL_STRING("0000beef|007|-5|12|3.14159|2.50|x|\"a\\\"b\"|a&lt;b|0010", out);
}

void test_frame_widths()
{
    static const pc_field W[] = {
        {PC_FK_HEX, 0, 0, nullptr}, {PC_FK_LIT, 0, 1, ","}, {PC_FK_HEX, 4, 0, nullptr}, {PC_FK_LIT, 0, 1, ","},
        {PC_FK_DEC, 5, 0, nullptr}, {PC_FK_LIT, 0, 1, ","}, {PC_FK_OCT, 0, 0, nullptr}, PC_END};
    char out[64];
    pc_frame_build(out, sizeof(out), W, (uint64_t)0xab, (uint64_t)0xab, (uint32_t)42, (uint64_t)64);
    // width 0 means "no padding", not "zero digits"
    TEST_ASSERT_EQUAL_STRING("ab,00ab,00042,100", out);
}

void test_frame_null_string_is_empty()
{
    static const pc_field S[] = {{PC_FK_LIT, 0, 1, "["}, PC_STR, {PC_FK_LIT, 0, 1, "]"}, PC_END};
    char out[32];
    pc_frame_build(out, sizeof(out), S, (const char *)nullptr);
    TEST_ASSERT_EQUAL_STRING("[]", out);
}

void test_frame_literal_only()
{
    static const pc_field L[] = {{PC_FK_LIT, 0, 12, "no args here"}, PC_END};
    char out[32];
    TEST_ASSERT_EQUAL_size_t(12, pc_frame_build(out, sizeof(out), L));
    TEST_ASSERT_EQUAL_STRING("no args here", out);
}

void test_frame_empty_spec()
{
    static const pc_field E[] = {PC_END};
    char out[8];
    out[0] = 'x';
    // an empty frame writes nothing and reports 0, and must still leave a valid C string
    TEST_ASSERT_EQUAL_size_t(0, pc_frame_build(out, sizeof(out), E));
    TEST_ASSERT_EQUAL_STRING("", out);
}

// The contract that the hand-written conversion kept getting wrong: on overflow the builder
// reports 0 AND leaves a valid empty string, so a caller that ignores the return can never read
// a half-written frame or bytes left over from a previous build.
void test_frame_overflow_fails_closed()
{
    char tiny[8];
    memset(tiny, 'Z', sizeof(tiny));
    TEST_ASSERT_EQUAL_size_t(0,
                             pc_frame_build(tiny, sizeof(tiny), RESP, (uint32_t)200, "OK", "text/plain", (uint32_t)21));
    TEST_ASSERT_EQUAL_STRING("", tiny);
}

void test_frame_exact_fit_boundary()
{
    static const pc_field F[] = {{PC_FK_LIT, 0, 4, "abcd"}, PC_END};
    char five[5];
    TEST_ASSERT_EQUAL_size_t(4, pc_frame_build(five, sizeof(five), F)); // 4 bytes + NUL fits exactly
    TEST_ASSERT_EQUAL_STRING("abcd", five);

    char four[4];
    TEST_ASSERT_EQUAL_size_t(0, pc_frame_build(four, sizeof(four), F)); // one byte short
    TEST_ASSERT_EQUAL_STRING("", four);
}

void test_frame_guards()
{
    char out[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_frame_build(nullptr, 8, RESP, (uint32_t)1, "", "", (uint32_t)0));
    TEST_ASSERT_EQUAL_size_t(0, pc_frame_build(out, 0, RESP, (uint32_t)1, "", "", (uint32_t)0));
    TEST_ASSERT_EQUAL_size_t(0, pc_frame_build(out, sizeof(out), nullptr));
}

// A zero-capacity buffer owns no bytes, so not even the NUL may be written. Every appender already
// refused to write into one, which left the terminator in pc_sb_finish as the single path that
// still would have - a one-byte overwrite past a buffer the caller said had no room at all.
void test_frame_zero_cap_writes_nothing()
{
    char sentinel[2] = {0x7F, 0x7F};
    TEST_ASSERT_EQUAL_size_t(0, pc_frame_build(sentinel, 0, RESP, (uint32_t)200, "OK", "t", (uint32_t)0));
    TEST_ASSERT_EQUAL_CHAR(0x7F, sentinel[0]);

    static const pc_field L[] = {{PC_FK_LIT, 0, 1, "x"}, PC_END};
    TEST_ASSERT_EQUAL_size_t(0, pc_frame_build(sentinel, 0, L));
    TEST_ASSERT_EQUAL_CHAR(0x7F, sentinel[0]);
}

static const pc_field HDR[] = {PC_STR, {PC_FK_LIT, 0, 2, ": "}, PC_STR, {PC_FK_LIT, 0, 2, "\r\n"}, PC_END};

void test_frame_append_accumulates()
{
    char acc[64];
    acc[0] = '\0';
    TEST_ASSERT_EQUAL_size_t(8, pc_frame_append(acc, sizeof(acc), HDR, "X-A", "1"));
    TEST_ASSERT_EQUAL_size_t(16, pc_frame_append(acc, sizeof(acc), HDR, "X-B", "2"));
    TEST_ASSERT_EQUAL_STRING("X-A: 1\r\nX-B: 2\r\n", acc);
}

void test_frame_append_rewinds_whole_frame()
{
    // A frame that does not fit must leave the accumulated buffer exactly as it was - a
    // half-written header line is a protocol violation, not a truncation.
    char small[12];
    small[0] = '\0';
    pc_frame_append(small, sizeof(small), HDR, "X-A", "1");
    TEST_ASSERT_EQUAL_size_t(0, pc_frame_append(small, sizeof(small), HDR, "X-VeryLong", "2"));
    TEST_ASSERT_EQUAL_STRING("X-A: 1\r\n", small);
}

void test_frame_append_to_full_buffer()
{
    char full[8];
    memset(full, 'a', sizeof(full) - 1);
    full[sizeof(full) - 1] = '\0';
    TEST_ASSERT_EQUAL_size_t(0, pc_frame_append(full, sizeof(full), HDR, "X", "1"));
    TEST_ASSERT_EQUAL_STRING("aaaaaaa", full); // untouched
}

void test_frame_float_matches_printf()
{
    static const pc_field G[] = {{PC_FK_G, 6, 0, nullptr}, PC_END};
    static const pc_field G10[] = {{PC_FK_G, 10, 0, nullptr}, PC_END};
    static const pc_field F2[] = {{PC_FK_FIX, 2, 0, nullptr}, PC_END};
    static const double vals[] = {0.0, -0.0, 1.0, 2.5, 0.1, 1e-6, 123456.0, 1234567.0, 9.9999995, 1e20};
    char out[64];
    char want[64];
    for (unsigned i = 0; i < sizeof(vals) / sizeof(vals[0]); i++)
    {
        pc_frame_build(out, sizeof(out), G, vals[i]);
        snprintf(want, sizeof(want), "%.6g", vals[i]);
        TEST_ASSERT_EQUAL_STRING(want, out);

        pc_frame_build(out, sizeof(out), G10, vals[i]);
        snprintf(want, sizeof(want), "%.10g", vals[i]);
        TEST_ASSERT_EQUAL_STRING(want, out);

        // PC_FIX is byte-identical to printf only within the 64-bit range it documents.
        if (vals[i] < 18446744073709551616.0)
        {
            pc_frame_build(out, sizeof(out), F2, vals[i]);
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
    static const pc_field F2[] = {{PC_FK_FIX, 2, 0, nullptr}, PC_END};
    char out[64];
    pc_frame_build(out, sizeof(out), F2, 1e20);
    TEST_ASSERT_EQUAL_STRING("1e+20", out);
    pc_frame_build(out, sizeof(out), F2, 1e300);
    TEST_ASSERT_EQUAL_STRING("1e+300", out);
}

void test_frame_unknown_opcode_refuses()
{
    // A spec built against a newer engine must not silently emit a frame missing a field.
    static const pc_field BAD[] = {{PC_FK_LIT, 0, 1, "a"}, {200, 0, 0, nullptr}, PC_END};
    char out[32];
    TEST_ASSERT_EQUAL_size_t(0, pc_frame_build(out, sizeof(out), BAD));
    TEST_ASSERT_EQUAL_STRING("", out);
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
    RUN_TEST(test_frame_unknown_opcode_refuses);
    return UNITY_END();
}
