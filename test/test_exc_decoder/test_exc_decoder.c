// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/exc_decoder: parsing a real ESP32 Guru Meditation panic dump.

#include "server/exc_decoder.h"
#include <stdio.h>
#include <string.h>
#include <unity.h>

static const char *PANIC = "Guru Meditation Error: Core  1 panic'ed (LoadProhibited). Exception was unhandled.\n"
                           "\n"
                           "Core  1 register dump:\n"
                           "PC      : 0x400d1234  PS      : 0x00060730  A0      : 0x800d5678  A1      : 0x3ffb2200\n"
                           "A2      : 0x00000000  A3      : 0x00000001  A4      : 0x00000000  A5      : 0x00000000\n"
                           "EXCVADDR: 0x0000002a  LBEG    : 0x40085d5c  LEND    : 0x40085d6a  LCOUNT  : 0x00000000\n"
                           "\n"
                           "Backtrace: 0x400d1234:0x3ffb2200 0x400d5678:0x3ffb2220 0x400d9abc:0x3ffb2240\n";

void setUp(void)
{
}
void tearDown(void)
{
}

void test_parse_full(void)
{
    ExcInfo info;
    TEST_ASSERT_TRUE(pc_exc_parse(PANIC, &info));
    TEST_ASSERT_EQUAL_INT(1, info.core);
    TEST_ASSERT_EQUAL_STRING("LoadProhibited", info.cause);
    TEST_ASSERT_EQUAL_HEX32(0x400d1234, info.pc);
    TEST_ASSERT_TRUE(info.has_excvaddr);
    TEST_ASSERT_EQUAL_HEX32(0x2a, info.excvaddr);
    TEST_ASSERT_EQUAL_size_t(3, info.frame_count);
    TEST_ASSERT_EQUAL_HEX32(0x400d1234, info.frames[0].pc);
    TEST_ASSERT_EQUAL_HEX32(0x3ffb2200, info.frames[0].sp);
    TEST_ASSERT_EQUAL_HEX32(0x400d9abc, info.frames[2].pc);
}

void test_json(void)
{
    ExcInfo info;
    pc_exc_parse(PANIC, &info);
    char buf[512];
    size_t n = pc_exc_json(&info, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"core\":1"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"cause\":\"LoadProhibited\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"pc\":\"0x400d1234\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"excvaddr\":\"0x0000002a\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"backtrace\":[\"0x400d1234\",\"0x400d5678\",\"0x400d9abc\"]"));
}

void test_backtrace_only_and_corrupted(void)
{
    // No register dump: PC must fall back to the first backtrace frame. Trailing corruption marker.
    const char *bt = "Backtrace: 0x400e1111:0x3ffc0000 0x400e2222:0x3ffc0020 |<-CORRUPTED\n";
    ExcInfo info;
    TEST_ASSERT_TRUE(pc_exc_parse(bt, &info));
    TEST_ASSERT_EQUAL_INT(-1, info.core);
    TEST_ASSERT_EQUAL_STRING("", info.cause);
    TEST_ASSERT_FALSE(info.has_excvaddr);
    TEST_ASSERT_EQUAL_size_t(2, info.frame_count);
    TEST_ASSERT_EQUAL_HEX32(0x400e1111, info.pc); // fell back to frame[0]
}

void test_garbage_returns_false(void)
{
    ExcInfo info;
    TEST_ASSERT_FALSE(pc_exc_parse("nothing to see here\n", &info));
    TEST_ASSERT_FALSE(pc_exc_parse("", &info));
}

void test_json_omits_core_when_absent_and_overflow(void)
{
    const char *bt = "Backtrace: 0x400e1111:0x3ffc0000\n";
    ExcInfo info;
    pc_exc_parse(bt, &info);
    char buf[128];
    size_t n = pc_exc_json(&info, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NULL(strstr(buf, "\"core\"")); // omitted when core == -1
    TEST_ASSERT_NULL(strstr(buf, "excvaddr")); // omitted when absent

    char tiny[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_exc_json(&info, tiny, sizeof(tiny)));
}

void test_upper_hex_and_json_overflow()
{
    ExcInfo info;
    // Uppercase hex addresses exercise the A-F branch of the nibble parser.
    const char *up = "Guru Meditation Error: Core  0 panic'ed (StoreProhibited).\n"
                     "Backtrace: 0x400D1234:0x3FFB2200 0x400DABCD:0x3FFB2220\n";
    TEST_ASSERT_TRUE(pc_exc_parse(up, &info));
    TEST_ASSERT_EQUAL_HEX32(0x400D1234, info.frames[0].pc);
    TEST_ASSERT_EQUAL_HEX32(0x400DABCD, info.frames[1].pc);
    char buf[256];
    TEST_ASSERT_TRUE(pc_exc_json(&info, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_size_t(0, pc_exc_json(&info, buf, 8)); // tiny cap fails closed
}

// Null-pointer guards on parse + json; a register dump whose "PC" is the very first line
// (no leading newline anchor); a backtrace whose stack-pointer field is malformed (the
// frame is rejected and parsing stops); and JSON escaping of a cause containing a quote
// and a backslash.
void test_exc_edge_guards(void)
{
    ExcInfo info;
    TEST_ASSERT_FALSE(pc_exc_parse(NULL, &info)); // null text
    TEST_ASSERT_FALSE(pc_exc_parse("x", NULL));   // null out
    char buf[128];
    TEST_ASSERT_EQUAL_size_t(0, pc_exc_json(NULL, buf, sizeof(buf))); // null info
    TEST_ASSERT_EQUAL_size_t(0, pc_exc_json(&info, NULL, 128));       // null out
    TEST_ASSERT_EQUAL_size_t(0, pc_exc_json(&info, buf, 0));             // zero cap

    // "PC" on the very first line (strncmp anchor, not the "\nPC" search).
    TEST_ASSERT_TRUE(pc_exc_parse("PC      : 0x400dfeed  PS : 0x1\n", &info));
    TEST_ASSERT_EQUAL_HEX32(0x400dfeed, info.pc);

    // Backtrace with a malformed stack pointer ("0xZZ" has no hex digits): the frame is
    // rejected, parsing stops, and no frames are recorded.
    TEST_ASSERT_TRUE(pc_exc_parse("Core 0 panic'ed (BadSP). Backtrace: 0x400d1000:0xZZ\n", &info));
    TEST_ASSERT_EQUAL_size_t(0, info.frame_count);

    // A cause containing a quote and a backslash is JSON-escaped.
    TEST_ASSERT_TRUE(pc_exc_parse("Guru Meditation Error: Core 0 panic'ed (Weird\"Cause\\x).\n"
                                  "Backtrace: 0x400d1234:0x3ffb0000\n",
                                  &info));
    size_t n = pc_exc_json(&info, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "Weird\\\"Cause\\\\x")); // escaped quote + backslash
}

void test_hex_literal_rejections(void)
{
    // parse_hex refuses anything that is not "0x"/"0X" + at least one hex digit, and stops at 8 digits.
    ExcInfo info;
    TEST_ASSERT_FALSE(pc_exc_parse("PC      : zz\n", &info)); // first char is not '0'
    TEST_ASSERT_EQUAL_HEX32(0, info.pc);
    TEST_ASSERT_FALSE(pc_exc_parse("PC      : 01234\n", &info)); // '0' but no x/X marker
    TEST_ASSERT_EQUAL_HEX32(0, info.pc);
    TEST_ASSERT_FALSE(pc_exc_parse("PC      : 0x\n", &info)); // marker but zero digits
    TEST_ASSERT_EQUAL_HEX32(0, info.pc);

    // "0X" (capital marker) is accepted just like "0x".
    TEST_ASSERT_TRUE(pc_exc_parse("PC      : 0X400dbeef\n", &info));
    TEST_ASSERT_EQUAL_HEX32(0x400dbeef, info.pc);

    // A ninth hex digit is not consumed: the value stops at 8 nibbles.
    TEST_ASSERT_TRUE(pc_exc_parse("PC      : 0x123456789\n", &info));
    TEST_ASSERT_EQUAL_HEX32(0x12345678, info.pc);

    // A letter above 'f'/'F' is not a hex digit and terminates the run.
    TEST_ASSERT_TRUE(pc_exc_parse("PC      : 0x1z\n", &info));
    TEST_ASSERT_EQUAL_HEX32(0x1, info.pc);
}

void test_field_without_colon_or_value(void)
{
    // A recognized field name with no ':' after it, and one whose value will not parse, are both ignored.
    ExcInfo info;
    TEST_ASSERT_FALSE(pc_exc_parse("EXCVADDR 0x2a\n", &info)); // no colon anywhere after EXCVADDR
    TEST_ASSERT_FALSE(info.has_excvaddr);
    TEST_ASSERT_FALSE(pc_exc_parse("EXCVADDR: zz\n", &info)); // colon, unparsable value
    TEST_ASSERT_FALSE(info.has_excvaddr);
    TEST_ASSERT_FALSE(pc_exc_parse("dump\nPC 400d1234\n", &info)); // PC line with no colon
    TEST_ASSERT_EQUAL_HEX32(0, info.pc);

    // A tab (not just a space) is skipped between the colon and the value.
    TEST_ASSERT_FALSE(pc_exc_parse("EXCVADDR:\t0x0000002a\n", &info));
    TEST_ASSERT_TRUE(info.has_excvaddr);
    TEST_ASSERT_EQUAL_HEX32(0x2a, info.excvaddr);
}

void test_core_field_variants(void)
{
    // "Core " followed by a non-digit leaves core at -1; a digit run ends at the first non-digit.
    ExcInfo info;
    TEST_ASSERT_FALSE(pc_exc_parse("Core \nregister dump:\n", &info)); // char below '0'
    TEST_ASSERT_EQUAL_INT(-1, info.core);
    TEST_ASSERT_FALSE(pc_exc_parse("Core xx\n", &info)); // char above '9'
    TEST_ASSERT_EQUAL_INT(-1, info.core);

    // ':' terminates the digit run (it is above '9').
    TEST_ASSERT_TRUE(pc_exc_parse("Core 1: panic'ed (Abort).\n", &info));
    TEST_ASSERT_EQUAL_INT(1, info.core);

    // An absurd core number is clamped instead of overflowing.
    TEST_ASSERT_TRUE(pc_exc_parse("Core 1234567 panic'ed (Big).\n", &info));
    TEST_ASSERT_EQUAL_INT(123456, info.core);
}

void test_multi_digit_core_in_json(void)
{
    // A two-digit core exercises the multi-iteration decimal emitter.
    ExcInfo info;
    TEST_ASSERT_TRUE(pc_exc_parse("Guru Meditation Error: Core  12 panic'ed (IllegalInstruction).\n", &info));
    TEST_ASSERT_EQUAL_INT(12, info.core);
    char buf[256];
    TEST_ASSERT_TRUE(pc_exc_json(&info, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"core\":12,"));
}

void test_cause_truncation(void)
{
    // The cause is bounded by the field width, and an unterminated cause stops at end-of-string.
    ExcInfo info;
    char text[128];
    snprintf(text, sizeof(text), "panic'ed (%s).\n", "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    TEST_ASSERT_TRUE(pc_exc_parse(text, &info));
    TEST_ASSERT_EQUAL_size_t(sizeof(info.cause) - 1, strlen(info.cause));
    TEST_ASSERT_EQUAL_STRING("ABCDEFGHIJKLMNOPQRSTUVWXYZ01234", info.cause);

    TEST_ASSERT_TRUE(pc_exc_parse("Core 0 panic'ed (Trunc", &info)); // no ')' before NUL
    TEST_ASSERT_EQUAL_STRING("Trunc", info.cause);
}

void test_backtrace_frame_cap_and_separator(void)
{
    // The frame list stops at PC_EXC_MAX_FRAMES even when more pairs follow.
    char text[2048];
    int n = snprintf(text, sizeof(text), "Backtrace:");
    for (int i = 0; i < PC_EXC_MAX_FRAMES + 3; i++)
    {
        n += snprintf(text + n, sizeof(text) - (size_t)n, " 0x400d%04x:0x3ffb%04x", i, i);
    }
    ExcInfo info;
    TEST_ASSERT_TRUE(pc_exc_parse(text, &info));
    TEST_ASSERT_EQUAL_size_t(PC_EXC_MAX_FRAMES, info.frame_count);
    TEST_ASSERT_EQUAL_HEX32(0x400d0000, info.frames[0].pc);
    TEST_ASSERT_EQUAL_HEX32(0x400d0000 + PC_EXC_MAX_FRAMES - 1, info.frames[PC_EXC_MAX_FRAMES - 1].pc);

    // A pc not followed by ':' is not a frame: the scan stops before recording anything.
    TEST_ASSERT_FALSE(pc_exc_parse("Backtrace: 0x400d1234 0x3ffb2200\n", &info));
    TEST_ASSERT_EQUAL_size_t(0, info.frame_count);
}

void test_parse_true_on_zero_pc_frame(void)
{
    // A single frame whose pc is 0 still counts as a successful parse (frame_count carries it).
    ExcInfo info;
    TEST_ASSERT_TRUE(pc_exc_parse("Backtrace: 0x00000000:0x3ffb2200\n", &info));
    TEST_ASSERT_EQUAL_STRING("", info.cause);
    TEST_ASSERT_EQUAL_HEX32(0, info.pc);
    TEST_ASSERT_EQUAL_size_t(1, info.frame_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_exc_edge_guards);
    RUN_TEST(test_parse_full);
    RUN_TEST(test_json);
    RUN_TEST(test_backtrace_only_and_corrupted);
    RUN_TEST(test_garbage_returns_false);
    RUN_TEST(test_json_omits_core_when_absent_and_overflow);
    RUN_TEST(test_upper_hex_and_json_overflow);
    RUN_TEST(test_hex_literal_rejections);
    RUN_TEST(test_field_without_colon_or_value);
    RUN_TEST(test_core_field_variants);
    RUN_TEST(test_multi_digit_core_in_json);
    RUN_TEST(test_cause_truncation);
    RUN_TEST(test_backtrace_frame_cap_and_separator);
    RUN_TEST(test_parse_true_on_zero_pc_frame);
    return UNITY_END();
}
