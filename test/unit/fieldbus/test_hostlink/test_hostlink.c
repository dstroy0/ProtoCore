// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the Omron Host Link (C-mode) frame codec (services/fieldbus/hostlink): the FCS,
// the command builder, and the FCS-validating parser. Pure host tests.

#include "services/fieldbus/hostlink/hostlink.h"
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// FCS of "@00RD00000010" is 0x57 (XOR of all ASCII values).
void test_fcs_vector()
{
    const char *body = "@00RD00000010";
    TEST_ASSERT_EQUAL_HEX8(0x57, pc_hostlink_fcs(body, strlen(body)));
}

// The full DM-read command frame for node 0: @00RD00000010 57 *CR.
void test_build_dm_read()
{
    char buf[32];
    size_t n = pc_hostlink_build(buf, sizeof(buf), 0, "RD", "00000010", 8);
    TEST_ASSERT_EQUAL_STRING("@00RD00000010"
                             "57"
                             "*\r",
                             buf);
    TEST_ASSERT_EQUAL_size_t(17, n);
}

void test_build_read_and_extract()
{
    char buf[64];
    // RD command: node 0, DM word 100 (-> "0100"), read 2 words (-> "0002").
    size_t n = pc_hostlink_build_read(buf, sizeof(buf), 0, 100, 2);
    TEST_ASSERT_TRUE(n > 0);
    HostlinkFrame f;
    TEST_ASSERT_TRUE(pc_hostlink_parse(buf, n, &f));
    TEST_ASSERT_EQUAL_STRING("RD", f.header_code);
    TEST_ASSERT_EQUAL_size_t(8, f.text_len);
    TEST_ASSERT_EQUAL_MEMORY("01000002", f.text, 8);

    // A simulated RD response: end code 00 (normal) + two word values 0x1234, 0x5678.
    char resp[64];
    size_t rn = pc_hostlink_build(resp, sizeof(resp), 0, "RD", "0012345678", 10);
    HostlinkFrame rf;
    TEST_ASSERT_TRUE(pc_hostlink_parse(resp, rn, &rf));
    uint8_t ec;
    TEST_ASSERT_TRUE(pc_hostlink_end_code(&rf, &ec));
    TEST_ASSERT_EQUAL_UINT8(0, ec);
    uint16_t w;
    TEST_ASSERT_TRUE(pc_hostlink_read_word(&rf, 0, &w));
    TEST_ASSERT_EQUAL_HEX16(0x1234, w);
    TEST_ASSERT_TRUE(pc_hostlink_read_word(&rf, 1, &w));
    TEST_ASSERT_EQUAL_HEX16(0x5678, w);
    TEST_ASSERT_FALSE(pc_hostlink_read_word(&rf, 2, &w));  // past the last word
    TEST_ASSERT_FALSE(pc_hostlink_read_word(NULL, 0, &w)); // null frame

    // A non-hex value character is rejected.
    char bad[64];
    size_t bn = pc_hostlink_build(bad, sizeof(bad), 0, "RD", "00123G", 6);
    HostlinkFrame bf;
    TEST_ASSERT_TRUE(pc_hostlink_parse(bad, bn, &bf));
    TEST_ASSERT_FALSE(pc_hostlink_read_word(&bf, 0, &w));

    // Builder guards: an out-of-range address / count, a zero count, and a too-small buffer fail closed.
    TEST_ASSERT_EQUAL_size_t(0, pc_hostlink_build_read(buf, sizeof(buf), 0, 10000, 1));
    TEST_ASSERT_EQUAL_size_t(0, pc_hostlink_build_read(buf, sizeof(buf), 0, 100, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_hostlink_build_read(buf, 4, 0, 100, 2));
}

// WR (DM-area write): node 0, DM word 100, two words 0x1234 / 0x5678.
void test_build_write()
{
    char buf[64];
    uint16_t words[2] = {0x1234, 0x5678};
    size_t n = pc_hostlink_build_write(buf, sizeof(buf), 0, 100, words, 2);
    TEST_ASSERT_EQUAL_STRING("@00WR0100123456784C*\r", buf);
    TEST_ASSERT_EQUAL_size_t(21, n);

    // Round-trips through the FCS-validating parser: header "WR", text = address + the two words.
    HostlinkFrame f;
    TEST_ASSERT_TRUE(pc_hostlink_parse(buf, n, &f));
    TEST_ASSERT_EQUAL_STRING("WR", f.header_code);
    TEST_ASSERT_EQUAL_size_t(12, f.text_len);
    TEST_ASSERT_EQUAL_MEMORY("010012345678", f.text, 12);

    // A simulated WR response carries only the 2-char end code (00 = normal).
    char resp[32];
    size_t rn = pc_hostlink_build(resp, sizeof(resp), 0, "WR", "00", 2);
    HostlinkFrame rf;
    TEST_ASSERT_TRUE(pc_hostlink_parse(resp, rn, &rf));
    uint8_t ec = 0xFF;
    TEST_ASSERT_TRUE(pc_hostlink_end_code(&rf, &ec));
    TEST_ASSERT_EQUAL_UINT8(0, ec);

    // Guards: out-of-range address, zero word_count, null words, node > 99, and a too-small buffer fail closed.
    TEST_ASSERT_EQUAL_size_t(0, pc_hostlink_build_write(buf, sizeof(buf), 0, 10000, words, 1));
    TEST_ASSERT_EQUAL_size_t(0, pc_hostlink_build_write(buf, sizeof(buf), 0, 100, words, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_hostlink_build_write(buf, sizeof(buf), 0, 100, NULL, 1));
    TEST_ASSERT_EQUAL_size_t(0, pc_hostlink_build_write(buf, sizeof(buf), 100, 100, words, 1));
    TEST_ASSERT_EQUAL_size_t(0, pc_hostlink_build_write(buf, 8, 0, 100, words, 2));
}

// The node number renders as two digits.
void test_build_node_digits()
{
    char buf[32];
    size_t n = pc_hostlink_build(buf, sizeof(buf), 13, "RD", "0000", 4);
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_EQUAL_HEX8('@', buf[0]);
    TEST_ASSERT_EQUAL_HEX8('1', buf[1]);
    TEST_ASSERT_EQUAL_HEX8('3', buf[2]);
    TEST_ASSERT_EQUAL_HEX8('R', buf[3]);
    TEST_ASSERT_EQUAL_HEX8('D', buf[4]);
}

void test_round_trip()
{
    char buf[32];
    size_t n = pc_hostlink_build(buf, sizeof(buf), 5, "RD", "00000010", 8);

    HostlinkFrame f;
    TEST_ASSERT_TRUE(pc_hostlink_parse(buf, n, &f));
    TEST_ASSERT_EQUAL_UINT8(5, f.node);
    TEST_ASSERT_EQUAL_STRING("RD", f.header_code);
    TEST_ASSERT_EQUAL_size_t(8, f.text_len);
    TEST_ASSERT_EQUAL_MEMORY("00000010", f.text, 8);
}

// A response: @00RD00 <data> FCS *CR - the text starts with the end code "00".
void test_parse_response_end_code()
{
    char buf[64];
    // Build a "response-shaped" frame: header RD, text = end code "00" + 4 data digits.
    size_t n = pc_hostlink_build(buf, sizeof(buf), 0, "RD", "001234", 6);
    HostlinkFrame f;
    TEST_ASSERT_TRUE(pc_hostlink_parse(buf, n, &f));
    uint8_t code;
    TEST_ASSERT_TRUE(pc_hostlink_end_code(&f, &code));
    TEST_ASSERT_EQUAL_HEX8(0x00, code); // normal completion
    TEST_ASSERT_EQUAL_MEMORY("001234", f.text, 6);
}

void test_parse_rejects_bad()
{
    char buf[32];
    size_t n = pc_hostlink_build(buf, sizeof(buf), 0, "RD", "00000010", 8);

    HostlinkFrame f;
    // Corrupt a text char -> FCS no longer matches.
    char corrupt[32];
    memcpy(corrupt, buf, n);
    corrupt[6] ^= 0x01;
    TEST_ASSERT_FALSE(pc_hostlink_parse(corrupt, n, &f));

    // Missing terminator.
    memcpy(corrupt, buf, n);
    corrupt[n - 1] = 'X';
    TEST_ASSERT_FALSE(pc_hostlink_parse(corrupt, n, &f));

    // Not a Host Link frame.
    TEST_ASSERT_FALSE(pc_hostlink_parse("hello", 5, &f));
    // Too short.
    TEST_ASSERT_FALSE(pc_hostlink_parse("@00*\r", 5, &f));
}

void test_build_overflow_fails_closed()
{
    char small[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_hostlink_build(small, sizeof(small), 0, "RD", "00000010", 8));
    // A one-character header code is rejected.
    char buf[32];
    TEST_ASSERT_EQUAL_size_t(0, pc_hostlink_build(buf, sizeof(buf), 0, "R", "0", 1));
}

// Builder/parser guards plus the hex-digit decoder's letter and invalid branches.
void test_guards_and_hex()
{
    char buf[32];
    // build guards
    TEST_ASSERT_EQUAL_size_t(0, pc_hostlink_build(NULL, sizeof(buf), 0, "RD", "0", 1));  // null buf
    TEST_ASSERT_EQUAL_size_t(0, pc_hostlink_build(buf, sizeof(buf), 0, NULL, "0", 1));   // null header code
    TEST_ASSERT_EQUAL_size_t(0, pc_hostlink_build(buf, sizeof(buf), 100, "RD", "0", 1)); // node > 99
    TEST_ASSERT_EQUAL_size_t(0, pc_hostlink_build(buf, sizeof(buf), 0, "RD", NULL, 4));  // text_len but null text

    // parse: a non-digit node, and FCS characters that are not hex.
    HostlinkFrame f;
    TEST_ASSERT_FALSE(pc_hostlink_parse("@A0RDFF*\r", 9, &f)); // node field not a digit
    TEST_ASSERT_FALSE(pc_hostlink_parse("@00RDGG*\r", 9, &f)); // FCS chars not hex digits

    // end code: null, too-short text, then upper/lower/invalid hex through hex_val.
    uint8_t code = 0;
    TEST_ASSERT_FALSE(pc_hostlink_end_code(NULL, &code));
    HostlinkFrame g;
    g.text = "X";
    g.text_len = 1;
    TEST_ASSERT_FALSE(pc_hostlink_end_code(&g, &code)); // text_len < 2
    g.text = "AB";
    g.text_len = 2;
    TEST_ASSERT_TRUE(pc_hostlink_end_code(&g, &code)); // uppercase A-F
    TEST_ASSERT_EQUAL_HEX8(0xAB, code);
    g.text = "cd";
    g.text_len = 2;
    TEST_ASSERT_TRUE(pc_hostlink_end_code(&g, &code)); // lowercase a-f
    TEST_ASSERT_EQUAL_HEX8(0xCD, code);
    g.text = "G!";
    g.text_len = 2;
    TEST_ASSERT_FALSE(pc_hostlink_end_code(&g, &code)); // non-hex character
}

// hex_digit()'s '>= 10' branch (letter nibble): FCS of "@00RDX" is 0x0E, so the low
// nibble renders as 'E'.
void test_build_fcs_hex_letter()
{
    char buf[32];
    size_t n = pc_hostlink_build(buf, sizeof(buf), 0, "RD", "X", 1);
    TEST_ASSERT_EQUAL_STRING("@00RDX0E*\r", buf);
    TEST_ASSERT_EQUAL_size_t(10, n);
}

// hex_val()'s lowercase branch: a character >= 'a' but > 'f' must fail the range check.
void test_hex_val_lowercase_out_of_range()
{
    HostlinkFrame g;
    g.text = "z0";
    g.text_len = 2;
    uint8_t code = 0;
    TEST_ASSERT_FALSE(pc_hostlink_end_code(&g, &code));
}

// text_len == 0 is a valid, empty-text frame: exercises the guard's "text_len" atom
// being false (short-circuiting away "!text") and the build's "if (text_len)" false path.
void test_build_zero_length_text()
{
    char buf[32];
    size_t n = pc_hostlink_build(buf, sizeof(buf), 0, "RD", NULL, 0);
    TEST_ASSERT_EQUAL_STRING("@00RD56*\r", buf);
    TEST_ASSERT_EQUAL_size_t(9, n);
}

// An empty header code string (header_code[0] == '\0') is rejected.
void test_build_empty_header_code()
{
    char buf[32];
    TEST_ASSERT_EQUAL_size_t(0, pc_hostlink_build(buf, sizeof(buf), 0, "", "0", 1));
}

// pc_hostlink_parse() null-pointer guards for buf and out.
void test_parse_null_pointers()
{
    HostlinkFrame f;
    TEST_ASSERT_FALSE(pc_hostlink_parse(NULL, 9, &f));
    TEST_ASSERT_FALSE(pc_hostlink_parse("@00RDGG*\r", 9, NULL));
}

// The byte before '\r' must be '*'; a well-formed '@'...'\r' frame with that byte
// disturbed must still be rejected.
void test_parse_bad_star_position()
{
    char buf[32];
    size_t n = pc_hostlink_build(buf, sizeof(buf), 0, "RD", "00000010", 8);
    char corrupt[32];
    memcpy(corrupt, buf, n);
    corrupt[n - 2] = 'Z'; // was '*'
    HostlinkFrame f;
    TEST_ASSERT_FALSE(pc_hostlink_parse(corrupt, n, &f));
}

// The buf[0] != '@' guard's true branch, at a length that clears the len < 9 floor
// (the "hello" case in test_parse_rejects_bad is too short to reach this check at all).
void test_parse_bad_start_char()
{
    HostlinkFrame f;
    TEST_ASSERT_FALSE(pc_hostlink_parse("X00RDFF*\r", 9, &f));
}

// Node-field bounds checks: each digit position rejected on both the low and high side.
void test_parse_node_field_bounds()
{
    HostlinkFrame f;
    TEST_ASSERT_FALSE(pc_hostlink_parse("@/0RDFF*\r", 9, &f)); // buf[1] < '0'
    TEST_ASSERT_FALSE(pc_hostlink_parse("@0/RDFF*\r", 9, &f)); // buf[2] < '0'
    TEST_ASSERT_FALSE(pc_hostlink_parse("@0ARDFF*\r", 9, &f)); // buf[2] > '9'
}

// FCS validation: high nibble char is valid hex but the low nibble char is not.
void test_parse_fcs_low_nibble_invalid()
{
    HostlinkFrame f;
    TEST_ASSERT_FALSE(pc_hostlink_parse("@00RD0G*\r", 9, &f));
}

// pc_hostlink_end_code(): high nibble valid, low nibble invalid.
void test_end_code_low_nibble_invalid()
{
    HostlinkFrame g;
    g.text = "0G";
    g.text_len = 2;
    uint8_t code = 0;
    TEST_ASSERT_FALSE(pc_hostlink_end_code(&g, &code));
}

// pc_hostlink_end_code() with a valid frame and a null `code` output pointer still
// reports success without writing through the pointer.
void test_end_code_null_code_output()
{
    HostlinkFrame g;
    g.text = "AB";
    g.text_len = 2;
    TEST_ASSERT_TRUE(pc_hostlink_end_code(&g, NULL));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_fcs_vector);
    RUN_TEST(test_build_dm_read);
    RUN_TEST(test_build_read_and_extract);
    RUN_TEST(test_build_write);
    RUN_TEST(test_build_node_digits);
    RUN_TEST(test_round_trip);
    RUN_TEST(test_parse_response_end_code);
    RUN_TEST(test_parse_rejects_bad);
    RUN_TEST(test_build_overflow_fails_closed);
    RUN_TEST(test_guards_and_hex);
    RUN_TEST(test_build_fcs_hex_letter);
    RUN_TEST(test_hex_val_lowercase_out_of_range);
    RUN_TEST(test_build_zero_length_text);
    RUN_TEST(test_build_empty_header_code);
    RUN_TEST(test_parse_null_pointers);
    RUN_TEST(test_parse_bad_star_position);
    RUN_TEST(test_parse_bad_start_char);
    RUN_TEST(test_parse_node_field_bounds);
    RUN_TEST(test_parse_fcs_low_nibble_invalid);
    RUN_TEST(test_end_code_low_nibble_invalid);
    RUN_TEST(test_end_code_null_code_output);
    return UNITY_END();
}
