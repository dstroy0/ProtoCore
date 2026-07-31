// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the GPIB-over-LAN (Prologix-style) command codec (services/instrumentation/gpib): the ++ command
// builders, the data-line escaping (byte-exact against the Prologix manual example), the
// command-vs-data classifier, and the response parsers. Pure host tests.

#include "services/instrumentation/gpib/gpib.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// ── command builders ─────────────────────────────────────────────────────────────────────────

void test_command_generic()
{
    char buf[32];
    TEST_ASSERT_GREATER_THAN(0, (int)pc_gpib_command(buf, sizeof(buf), "mode 1"));
    TEST_ASSERT_EQUAL_STRING("++mode 1\n", buf);
    pc_gpib_command(buf, sizeof(buf), "clr");
    TEST_ASSERT_EQUAL_STRING("++clr\n", buf);
    pc_gpib_command(buf, sizeof(buf), "ver");
    TEST_ASSERT_EQUAL_STRING("++ver\n", buf);
    // overflow fails closed
    char tiny[4];
    TEST_ASSERT_EQUAL_size_t(0, pc_gpib_command(tiny, sizeof(tiny), "read_tmo_ms 500"));
}

void test_addr()
{
    char buf[32];
    pc_gpib_addr(buf, sizeof(buf), 9, 96);
    TEST_ASSERT_EQUAL_STRING("++addr 9 96\n", buf); // primary 9, secondary 0 (96)
    pc_gpib_addr(buf, sizeof(buf), 9, -1);
    TEST_ASSERT_EQUAL_STRING("++addr 9\n", buf);
    TEST_ASSERT_EQUAL_size_t(0, pc_gpib_addr(buf, sizeof(buf), 31, -1)); // pad out of range
}

void test_read()
{
    char buf[32];
    pc_gpib_read(buf, sizeof(buf), GpibRead::UNTIL_TIMEOUT, 0);
    TEST_ASSERT_EQUAL_STRING("++read\n", buf);
    pc_gpib_read(buf, sizeof(buf), GpibRead::UNTIL_EOI, 0);
    TEST_ASSERT_EQUAL_STRING("++read eoi\n", buf);
    pc_gpib_read(buf, sizeof(buf), GpibRead::UNTIL_CHAR, 10);
    TEST_ASSERT_EQUAL_STRING("++read 10\n", buf);
}

void test_spoll_and_eos()
{
    char buf[32];
    pc_gpib_spoll(buf, sizeof(buf), -1, -1);
    TEST_ASSERT_EQUAL_STRING("++spoll\n", buf);
    pc_gpib_spoll(buf, sizeof(buf), 5, -1);
    TEST_ASSERT_EQUAL_STRING("++spoll 5\n", buf);
    pc_gpib_spoll(buf, sizeof(buf), 9, 96);
    TEST_ASSERT_EQUAL_STRING("++spoll 9 96\n", buf);

    // the eos numeric mapping (a common source of bugs): 0=CR+LF, 1=CR, 2=LF, 3=None
    pc_gpib_eos(buf, sizeof(buf), GpibEos::CRLF);
    TEST_ASSERT_EQUAL_STRING("++eos 0\n", buf);
    pc_gpib_eos(buf, sizeof(buf), GpibEos::LF);
    TEST_ASSERT_EQUAL_STRING("++eos 2\n", buf);
    pc_gpib_eos(buf, sizeof(buf), GpibEos::PC_NONE);
    TEST_ASSERT_EQUAL_STRING("++eos 3\n", buf);
}

// ── data escaping (byte-exact Prologix manual example) ────────────────────────────────────────

void test_build_data_escaping()
{
    // Manual §8.1: 00 01 02 13 03 10 04 27 05 43 06 -> escape CR/LF/ESC/'+' with a leading ESC.
    const uint8_t src[] = {0, 1, 2, 13, 3, 10, 4, 27, 5, 43, 6};
    const uint8_t expected[] = {0, 1, 2, 27, 13, 3, 27, 10, 4, 27, 27, 5, 27, 43, 6, '\n'};
    uint8_t buf[32];
    size_t n = pc_gpib_build_data(buf, sizeof(buf), src, sizeof(src));
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), n);
    TEST_ASSERT_EQUAL_MEMORY(expected, buf, sizeof(expected));
}

void test_build_data_plain()
{
    // a plain SCPI command has no special bytes -> passthrough + newline
    const uint8_t idn[] = {'*', 'I', 'D', 'N', '?'};
    uint8_t buf[16];
    size_t n = pc_gpib_build_data(buf, sizeof(buf), idn, sizeof(idn));
    TEST_ASSERT_EQUAL_size_t(6, n);
    TEST_ASSERT_EQUAL_MEMORY("*IDN?\n", buf, 6);
    // a bare '+' in data is escaped (so it is not mistaken for a command prefix)
    const uint8_t plus[] = {'1', '+', '2'};
    n = pc_gpib_build_data(buf, sizeof(buf), plus, sizeof(plus));
    const uint8_t plus_exp[] = {'1', 27, '+', '2', '\n'};
    TEST_ASSERT_EQUAL_size_t(sizeof(plus_exp), n);
    TEST_ASSERT_EQUAL_MEMORY(plus_exp, buf, sizeof(plus_exp));
    // overflow fails closed (2 escaped bytes + newline need 3, buffer is 2)
    uint8_t two[2];
    TEST_ASSERT_EQUAL_size_t(0, pc_gpib_build_data(two, sizeof(two), (const uint8_t *)"\r", 1));
}

// ── classifier + parsers ───────────────────────────────────────────────────────────────────────

void test_is_command()
{
    TEST_ASSERT_TRUE(pc_gpib_is_command("++mode 1", 8));
    TEST_ASSERT_TRUE(pc_gpib_is_command("++", 2));
    TEST_ASSERT_FALSE(pc_gpib_is_command("*IDN?", 5)); // data
    TEST_ASSERT_FALSE(pc_gpib_is_command("+X", 2));    // only one +
    TEST_ASSERT_FALSE(pc_gpib_is_command("+", 1));
    TEST_ASSERT_FALSE(pc_gpib_is_command("", 0));
}

void test_parse_decimal()
{
    uint32_t v = 0;
    TEST_ASSERT_TRUE(pc_gpib_parse_decimal("64\r\n", 4, &v)); // a serial-poll status byte
    TEST_ASSERT_EQUAL_UINT32(64, v);
    TEST_ASSERT_TRUE(pc_gpib_parse_decimal(" 255 ", 5, &v));
    TEST_ASSERT_EQUAL_UINT32(255, v);
    TEST_ASSERT_TRUE(pc_gpib_parse_decimal("1", 1, &v)); // an ++srq result
    TEST_ASSERT_EQUAL_UINT32(1, v);
    TEST_ASSERT_FALSE(pc_gpib_parse_decimal("abc", 3, &v));
    TEST_ASSERT_FALSE(pc_gpib_parse_decimal("", 0, &v));
    TEST_ASSERT_FALSE(pc_gpib_parse_decimal("12x", 3, &v));
}

void test_parse_addr()
{
    uint8_t pad = 0;
    int sad = 0;
    TEST_ASSERT_TRUE(pc_gpib_parse_addr("9 96\r\n", 6, &pad, &sad));
    TEST_ASSERT_EQUAL_UINT8(9, pad);
    TEST_ASSERT_EQUAL_INT(96, sad);
    TEST_ASSERT_TRUE(pc_gpib_parse_addr("12", 2, &pad, &sad));
    TEST_ASSERT_EQUAL_UINT8(12, pad);
    TEST_ASSERT_EQUAL_INT(-1, sad);                                // no secondary
    TEST_ASSERT_FALSE(pc_gpib_parse_addr("31", 2, &pad, &sad));    // primary out of range
    TEST_ASSERT_FALSE(pc_gpib_parse_addr("9 200", 5, &pad, &sad)); // secondary out of range
}

void test_parse_version()
{
    const char *resp = "Prologix GPIB-ETHERNET Controller version 1.6.6.0\r\n";
    const char *ver = nullptr;
    size_t vlen = 0;
    TEST_ASSERT_TRUE(pc_gpib_parse_version(resp, strlen(resp), &ver, &vlen));
    TEST_ASSERT_EQUAL_size_t(7, vlen);
    TEST_ASSERT_EQUAL_MEMORY("1.6.6.0", ver, 7);
    // no version token
    TEST_ASSERT_FALSE(pc_gpib_parse_version("AR488 controller", 16, &ver, &vlen));
}

// ── argument guards ──────────────────────────────────────────────────────────────────────────

void test_builders_reject_null_buffer_and_zero_cap()
{
    // Every builder needs a destination with room in it; a null command string is refused too.
    char buf[32];
    TEST_ASSERT_EQUAL_size_t(0, pc_gpib_command(nullptr, sizeof(buf), "clr"));
    TEST_ASSERT_EQUAL_size_t(0, pc_gpib_command(buf, 0, "clr"));
    TEST_ASSERT_EQUAL_size_t(0, pc_gpib_command(buf, sizeof(buf), nullptr));
    TEST_ASSERT_EQUAL_size_t(0, pc_gpib_addr(nullptr, sizeof(buf), 9, -1));
    TEST_ASSERT_EQUAL_size_t(0, pc_gpib_addr(buf, 0, 9, -1));
    TEST_ASSERT_EQUAL_size_t(0, pc_gpib_read(nullptr, sizeof(buf), GpibRead::UNTIL_EOI, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_gpib_read(buf, 0, GpibRead::UNTIL_EOI, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_gpib_spoll(nullptr, sizeof(buf), -1, -1));
    TEST_ASSERT_EQUAL_size_t(0, pc_gpib_spoll(buf, 0, -1, -1));
    TEST_ASSERT_EQUAL_size_t(0, pc_gpib_eos(nullptr, sizeof(buf), GpibEos::LF));
    TEST_ASSERT_EQUAL_size_t(0, pc_gpib_eos(buf, 0, GpibEos::LF));
}

void test_build_data_guards_and_empty()
{
    // A null destination / zero capacity / a declared length with no source are refused; a zero
    // length with a null source is the legal "just terminate the line" case.
    uint8_t buf[16];
    const uint8_t src[] = {'A'};
    TEST_ASSERT_EQUAL_size_t(0, pc_gpib_build_data(nullptr, sizeof(buf), src, sizeof(src)));
    TEST_ASSERT_EQUAL_size_t(0, pc_gpib_build_data(buf, 0, src, sizeof(src)));
    TEST_ASSERT_EQUAL_size_t(0, pc_gpib_build_data(buf, sizeof(buf), nullptr, 1));
    size_t n = pc_gpib_build_data(buf, sizeof(buf), nullptr, 0);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_HEX8('\n', buf[0]);
}

void test_is_command_rejects_null()
{
    TEST_ASSERT_FALSE(pc_gpib_is_command(nullptr, 8));
}

// ── parser edges ─────────────────────────────────────────────────────────────────────────────

void test_parse_decimal_edges()
{
    // A null input, a digit below '0', and an optional out pointer.
    uint32_t v = 0;
    TEST_ASSERT_FALSE(pc_gpib_parse_decimal(nullptr, 3, &v));
    TEST_ASSERT_FALSE(pc_gpib_parse_decimal("12-", 3, &v));   // '-' sorts below '0'
    TEST_ASSERT_FALSE(pc_gpib_parse_decimal("   ", 3, &v));   // trims to nothing
    TEST_ASSERT_TRUE(pc_gpib_parse_decimal("7", 1, nullptr)); // out is optional
}

void test_parse_addr_edges()
{
    // Null input, an empty/blank line, a non-numeric primary, and the secondary-address rules.
    uint8_t pad = 0;
    int sad = 0;
    TEST_ASSERT_FALSE(pc_gpib_parse_addr(nullptr, 2, &pad, &sad));
    TEST_ASSERT_FALSE(pc_gpib_parse_addr("  ", 2, &pad, &sad));    // trims to nothing
    TEST_ASSERT_FALSE(pc_gpib_parse_addr("x", 1, &pad, &sad));     // primary is not a number
    TEST_ASSERT_FALSE(pc_gpib_parse_addr("9 -1", 4, &pad, &sad));  // secondary is not a number
    TEST_ASSERT_FALSE(pc_gpib_parse_addr("9 96x", 5, &pad, &sad)); // trailing junk
    TEST_ASSERT_FALSE(pc_gpib_parse_addr("9 50", 4, &pad, &sad));  // secondary below 96
    // both out pointers are optional
    TEST_ASSERT_TRUE(pc_gpib_parse_addr("9", 1, nullptr, &sad));
    TEST_ASSERT_EQUAL_INT(-1, sad);
    TEST_ASSERT_TRUE(pc_gpib_parse_addr("9", 1, &pad, nullptr));
    TEST_ASSERT_EQUAL_UINT8(9, pad);
}

void test_parse_version_edges()
{
    // Null input, a line shorter than the "version " key, a key with nothing after it, and the
    // optional out pointers.
    const char *resp = "controller version 1.6\r\n";
    const char *ver = nullptr;
    size_t vlen = 0;
    TEST_ASSERT_FALSE(pc_gpib_parse_version(nullptr, 10, &ver, &vlen));
    TEST_ASSERT_FALSE(pc_gpib_parse_version("ver", 3, &ver, &vlen));
    TEST_ASSERT_FALSE(pc_gpib_parse_version("version \r\n", 10, &ver, &vlen)); // no version text
    TEST_ASSERT_TRUE(pc_gpib_parse_version(resp, strlen(resp), nullptr, &vlen));
    TEST_ASSERT_EQUAL_size_t(3, vlen);
    TEST_ASSERT_TRUE(pc_gpib_parse_version(resp, strlen(resp), &ver, nullptr));
    TEST_ASSERT_EQUAL_MEMORY("1.6", ver, 3);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_command_generic);
    RUN_TEST(test_addr);
    RUN_TEST(test_read);
    RUN_TEST(test_spoll_and_eos);
    RUN_TEST(test_build_data_escaping);
    RUN_TEST(test_build_data_plain);
    RUN_TEST(test_is_command);
    RUN_TEST(test_parse_decimal);
    RUN_TEST(test_parse_addr);
    RUN_TEST(test_parse_version);
    RUN_TEST(test_builders_reject_null_buffer_and_zero_cap);
    RUN_TEST(test_build_data_guards_and_empty);
    RUN_TEST(test_is_command_rejects_null);
    RUN_TEST(test_parse_decimal_edges);
    RUN_TEST(test_parse_addr_edges);
    RUN_TEST(test_parse_version_edges);
    return UNITY_END();
}
