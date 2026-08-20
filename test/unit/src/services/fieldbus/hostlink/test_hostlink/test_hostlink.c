// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Omron Host Link (C-mode) frame codec (services/fieldbus/hostlink/hostlink.h).
//
// Omron's communications-commands reference defines the FCS as "the result of an EXCLUSIVE OR
// performed on the data from the beginning of the frame until the end of the text", rendered as two
// hexadecimal characters. test_read_command_frame is the load-bearing case: @00RD00000010 is the
// DM-area read every Host Link host sends first, and its FCS is folded octet by octet in the
// comment below from that definition, so a codec that started the XOR after the '@' or stopped
// before the last text character cannot reproduce the string.

#include "services/fieldbus/hostlink/hostlink.h"
#include <string.h>

#include <unity.h>

static uint8_t hostlink_work[16]; // the borrow an entry takes; Hostlink never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// The FCS folds the frame text. XOR is self-inverse, so a character appearing an even number of
// times cancels; that is what makes the hand derivations below auditable.
void test_fcs_is_a_running_xor(void)
{
    HostlinkV.fcs_args.data = "";
    HostlinkV.fcs_args.len = 0;
    Hostlink.fcs(hostlink_work);
    TEST_ASSERT_EQUAL_HEX8(0x00u, HostlinkV.value);
    HostlinkV.fcs_args.data = "@";
    HostlinkV.fcs_args.len = 1;
    Hostlink.fcs(hostlink_work);
    TEST_ASSERT_EQUAL_HEX8(0x40u, HostlinkV.value); // '@' = 0x40
    HostlinkV.fcs_args.data = "@@";
    HostlinkV.fcs_args.len = 2;
    Hostlink.fcs(hostlink_work);
    TEST_ASSERT_EQUAL_HEX8(0x00u, HostlinkV.value); // a pair cancels
    HostlinkV.fcs_args.data = "@@@";
    HostlinkV.fcs_args.len = 3;
    Hostlink.fcs(hostlink_work);
    TEST_ASSERT_EQUAL_HEX8(0x40u, HostlinkV.value); // an odd count does not
    // '@' ^ '0' ^ '0' = 0x40 ^ 0x30 ^ 0x30 = 0x40
    HostlinkV.fcs_args.data = "@00";
    HostlinkV.fcs_args.len = 3;
    Hostlink.fcs(hostlink_work);
    TEST_ASSERT_EQUAL_HEX8(0x40u, HostlinkV.value);
}

// RD from node 0, beginning word address 0, 10 words. The text is two 4-digit zero-padded decimal
// fields, so the frame body is @00RD00000010 and the FCS folds it:
//   '@'=40 '0'=30 '0'=30 'R'=52 'D'=44 then "00000010"
//   the nine '0' octets leave one 0x30 after pairing, so
//   0x40 ^ 0x52 ^ 0x44 ^ 0x31 ^ 0x30 = 0x12 ^ 0x44 ^ 0x31 ^ 0x30 = 0x56 ^ 0x31 ^ 0x30 = 0x67 ^ 0x30
//   = 0x57 -> "57"
// then the '*' CR terminator.
void test_read_command_frame(void)
{
    char buf[32];
    HostlinkV.build_read_args.buf = buf;
    HostlinkV.build_read_args.cap = sizeof(buf);
    HostlinkV.build_read_args.node = 0;
    HostlinkV.build_read_args.address = 0;
    HostlinkV.build_read_args.count = 10;
    Hostlink.build_read(hostlink_work);
    size_t n = HostlinkV.n;
    TEST_ASSERT_EQUAL_size_t(17u, n);
    TEST_ASSERT_EQUAL_STRING("@00RD0000001057*\r", buf);
    TEST_ASSERT_EQUAL_CHAR('\0', buf[n]); // the frame is usable as a C string
}

// WR to node 0, beginning word address 100, one word 0xABCD. The body is @00WR0100ABCD:
//   0x40 ^ 0x30 ^ 0x30 ^ 0x57 ^ 0x52 ^ 0x30 ^ 0x31 ^ 0x30 ^ 0x30 ^ 0x41 ^ 0x42 ^ 0x43 ^ 0x44
//   the five '0' octets leave one 0x30; 'A'^'B'^'C'^'D' = 0x41^0x42 = 0x03, ^0x43 = 0x40, ^0x44 = 0x04
//   so 0x40 ^ 0x57 ^ 0x52 ^ 0x31 ^ 0x30 ^ 0x04 = 0x17 ^ 0x52 ^ 0x31 ^ 0x30 ^ 0x04
//   = 0x45 ^ 0x31 ^ 0x30 ^ 0x04 = 0x74 ^ 0x30 ^ 0x04 = 0x44 ^ 0x04 = 0x40 -> "40"
void test_write_command_frame(void)
{
    static const uint16_t WORDS[1] = {0xABCD};
    char buf[32];
    HostlinkV.build_write_args.buf = buf;
    HostlinkV.build_write_args.cap = sizeof(buf);
    HostlinkV.build_write_args.node = 0;
    HostlinkV.build_write_args.address = 100;
    HostlinkV.build_write_args.words = WORDS;
    HostlinkV.build_write_args.word_count = 1;
    Hostlink.build_write(hostlink_work);
    size_t n = HostlinkV.n;
    TEST_ASSERT_EQUAL_size_t(17u, n);
    TEST_ASSERT_EQUAL_STRING("@00WR0100ABCD40*\r", buf);
}

// An RD response's text is the 2-character end code then one 4-hex-character value per word.
// Body @00RD00123456789ABC, so end code 00 and the words 1234 5678 9ABC. Its FCS: the four '0'
// octets cancel, leaving
//   0x40 ^ 0x52 ^ 0x44 = 0x12 ^ 0x44 = 0x56
//   ^'1' = 0x67, ^'2' = 0x55, ^'3' = 0x66, ^'4' = 0x52, ^'5' = 0x67, ^'6' = 0x51, ^'7' = 0x66,
//   ^'8' = 0x5E, ^'9' = 0x67, ^'A' = 0x26, ^'B' = 0x64, ^'C' = 0x27
// so the FCS is 0x27 -> "27".
void test_read_response_words(void)
{
    static const char RESP[] = "@00RD00123456789ABC27*\r";
    HostlinkFrame f;
    memset(&f, 0, sizeof(f));
    HostlinkV.parse_args.buf = RESP;
    HostlinkV.parse_args.len = strlen(RESP);
    HostlinkV.parse_args.out = &f;
    Hostlink.parse(hostlink_work);
    TEST_ASSERT_TRUE(HostlinkV.ok);
    TEST_ASSERT_EQUAL_UINT8(0u, f.node);
    TEST_ASSERT_EQUAL_STRING("RD", f.header_code);
    TEST_ASSERT_EQUAL_size_t(14u, f.text_len); // "00" + three 4-character words

    uint8_t code = 0xFF;
    HostlinkV.end_code_args.f = &f;
    HostlinkV.end_code_args.code = &code;
    Hostlink.end_code(hostlink_work);
    TEST_ASSERT_TRUE(HostlinkV.ok);
    TEST_ASSERT_EQUAL_HEX8(0x00u, code); // 00 = normal completion

    uint16_t w = 0;
    HostlinkV.read_word_args.f = &f;
    HostlinkV.read_word_args.index = 0;
    HostlinkV.read_word_args.out = &w;
    Hostlink.read_word(hostlink_work);
    TEST_ASSERT_TRUE(HostlinkV.ok);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, w);
    HostlinkV.read_word_args.f = &f;
    HostlinkV.read_word_args.index = 1;
    HostlinkV.read_word_args.out = &w;
    Hostlink.read_word(hostlink_work);
    TEST_ASSERT_TRUE(HostlinkV.ok);
    TEST_ASSERT_EQUAL_HEX16(0x5678u, w);
    HostlinkV.read_word_args.f = &f;
    HostlinkV.read_word_args.index = 2;
    HostlinkV.read_word_args.out = &w;
    Hostlink.read_word(hostlink_work);
    TEST_ASSERT_TRUE(HostlinkV.ok);
    TEST_ASSERT_EQUAL_HEX16(0x9ABCu, w);

    HostlinkV.read_word_args.f = &f;
    HostlinkV.read_word_args.index = 3;
    HostlinkV.read_word_args.out = &w;
    Hostlink.read_word(hostlink_work);
    TEST_ASSERT_FALSE(HostlinkV.ok); // past the end of the text
    HostlinkV.read_word_args.f = &f;
    HostlinkV.read_word_args.index = 0;
    HostlinkV.read_word_args.out = NULL;
    Hostlink.read_word(hostlink_work);
    TEST_ASSERT_FALSE(HostlinkV.ok);
    HostlinkV.read_word_args.f = NULL;
    HostlinkV.read_word_args.index = 0;
    HostlinkV.read_word_args.out = &w;
    Hostlink.read_word(hostlink_work);
    TEST_ASSERT_FALSE(HostlinkV.ok);
}

// The node number, the two header characters and the text all survive build then parse, for every
// node the two BCD-style digits can spell.
void test_build_parse_round_trip(void)
{
    static const char TEXT[] = "0012AB";
    for (uint8_t node = 0; node <= 99; node++)
    {
        char buf[32];
        HostlinkV.build_args.buf = buf;
        HostlinkV.build_args.cap = sizeof(buf);
        HostlinkV.build_args.node = node;
        HostlinkV.build_args.header_code = "RD";
        HostlinkV.build_args.text = TEXT;
        HostlinkV.build_args.text_len = strlen(TEXT);
        Hostlink.build(hostlink_work);
        size_t n = HostlinkV.n;
        TEST_ASSERT_EQUAL_size_t(15u, n);
        TEST_ASSERT_EQUAL_CHAR('0' + (node / 10), buf[1]);
        TEST_ASSERT_EQUAL_CHAR('0' + (node % 10), buf[2]);

        HostlinkFrame f;
        memset(&f, 0, sizeof(f));
        HostlinkV.parse_args.buf = buf;
        HostlinkV.parse_args.len = n;
        HostlinkV.parse_args.out = &f;
        Hostlink.parse(hostlink_work);
        TEST_ASSERT_TRUE(HostlinkV.ok);
        TEST_ASSERT_EQUAL_UINT8(node, f.node);
        TEST_ASSERT_EQUAL_STRING("RD", f.header_code);
        TEST_ASSERT_EQUAL_size_t(strlen(TEXT), f.text_len);
        TEST_ASSERT_EQUAL_MEMORY(TEXT, f.text, strlen(TEXT));
    }
    // A node above 99 has no two-digit spelling.
    char buf[32];
    HostlinkV.build_args.buf = buf;
    HostlinkV.build_args.cap = sizeof(buf);
    HostlinkV.build_args.node = 100;
    HostlinkV.build_args.header_code = "RD";
    HostlinkV.build_args.text = TEXT;
    HostlinkV.build_args.text_len = strlen(TEXT);
    Hostlink.build(hostlink_work);
    TEST_ASSERT_EQUAL_size_t(0u, HostlinkV.n);
}

// The builder renders the FCS in uppercase hex, the spelling Omron's tables show, and the parser
// accepts either case so a peer's lowercase frame is not dropped.
//
// Body "@00RDx": the '0' pair cancels, so 0x40 ^ 0x52 ^ 0x44 = 0x56, ^'x' (0x78) = 0x2E -> "2E".
void test_fcs_rendering_and_acceptance(void)
{
    char buf[32];
    HostlinkV.build_args.buf = buf;
    HostlinkV.build_args.cap = sizeof(buf);
    HostlinkV.build_args.node = 0;
    HostlinkV.build_args.header_code = "RD";
    HostlinkV.build_args.text = "x";
    HostlinkV.build_args.text_len = 1;
    Hostlink.build(hostlink_work);
    size_t n = HostlinkV.n;
    TEST_ASSERT_EQUAL_size_t(10u, n);
    TEST_ASSERT_EQUAL_STRING("@00RDx2E*\r", buf);

    HostlinkFrame f;
    HostlinkV.parse_args.buf = buf;
    HostlinkV.parse_args.len = n;
    HostlinkV.parse_args.out = &f;
    Hostlink.parse(hostlink_work);
    TEST_ASSERT_TRUE(HostlinkV.ok);

    static const char LOWER[] = "@00RDx2e*\r";
    HostlinkV.parse_args.buf = LOWER;
    HostlinkV.parse_args.len = strlen(LOWER);
    HostlinkV.parse_args.out = &f;
    Hostlink.parse(hostlink_work);
    TEST_ASSERT_TRUE(HostlinkV.ok);
    TEST_ASSERT_EQUAL_size_t(1u, f.text_len);
    TEST_ASSERT_EQUAL_CHAR('x', f.text[0]);
}

// A single character changed anywhere in the frame text flips at least one FCS bit, so the parser
// refuses it. Delimiters and the FCS field itself are covered by the framing cases.
void test_single_character_corruption_is_refused(void)
{
    char frame[32];
    HostlinkV.build_args.buf = frame;
    HostlinkV.build_args.cap = sizeof(frame);
    HostlinkV.build_args.node = 12;
    HostlinkV.build_args.header_code = "RD";
    HostlinkV.build_args.text = "00001234";
    HostlinkV.build_args.text_len = 8;
    Hostlink.build(hostlink_work);
    size_t n = HostlinkV.n;
    for (size_t i = 0; i < n - 2; i++) // everything up to but not including "*\r"
    {
        char bad[32];
        memcpy(bad, frame, n);
        bad[i] = (char)(bad[i] ^ 0x01);
        HostlinkFrame f;
        HostlinkV.parse_args.buf = bad;
        HostlinkV.parse_args.len = n;
        HostlinkV.parse_args.out = &f;
        Hostlink.parse(hostlink_work);
        TEST_ASSERT_FALSE(HostlinkV.ok);
    }
}

// Framing faults: no '@', no "*CR" terminator, a non-digit node field, a non-hex FCS, and a frame
// shorter than the nine characters the shortest legal frame needs.
void test_parse_rejects_bad_framing(void)
{
    HostlinkFrame f;
    static const char *const BAD[] = {
        "!00RD0000001057*\r",  // no '@'
        "@00RD0000001057*\n",  // no CR
        "@00RD0000001057\r\r", // no '*'
        "@0XRD0000001057*\r",  // node field is not two digits
        "@00RD00000010G7*\r",  // FCS is not hexadecimal
        "@00RD0000001058*\r",  // FCS is hexadecimal but wrong
        "@00RD57*",            // shorter than nine characters
        "",
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        HostlinkV.parse_args.buf = BAD[i];
        HostlinkV.parse_args.len = strlen(BAD[i]);
        HostlinkV.parse_args.out = &f;
        Hostlink.parse(hostlink_work);
        TEST_ASSERT_FALSE_MESSAGE(HostlinkV.ok, BAD[i]);
    }
    HostlinkV.parse_args.buf = NULL;
    HostlinkV.parse_args.len = 17;
    HostlinkV.parse_args.out = &f;
    Hostlink.parse(hostlink_work);
    TEST_ASSERT_FALSE(HostlinkV.ok);
    HostlinkV.parse_args.buf = "@00RD0000001057*\r";
    HostlinkV.parse_args.len = 17;
    HostlinkV.parse_args.out = NULL;
    Hostlink.parse(hostlink_work);
    TEST_ASSERT_FALSE(HostlinkV.ok);
}

// The end-code reader needs two text characters and both must be hexadecimal.
void test_end_code_guards(void)
{
    HostlinkFrame f;
    uint8_t code = 0;

    // "@00RD" alone: the response has no end code at all.
    static const char EMPTY[] = "@00RD56*\r";
    HostlinkV.parse_args.buf = EMPTY;
    HostlinkV.parse_args.len = strlen(EMPTY);
    HostlinkV.parse_args.out = &f;
    Hostlink.parse(hostlink_work);
    TEST_ASSERT_TRUE(HostlinkV.ok);
    TEST_ASSERT_EQUAL_size_t(0u, f.text_len);
    HostlinkV.end_code_args.f = &f;
    HostlinkV.end_code_args.code = &code;
    Hostlink.end_code(hostlink_work);
    TEST_ASSERT_FALSE(HostlinkV.ok);

    // "@00RDzz" - a pair of 'z' cancels, so the FCS is again that of "@00RD" = 0x56.
    static const char NOT_HEX[] = "@00RDzz56*\r";
    HostlinkV.parse_args.buf = NOT_HEX;
    HostlinkV.parse_args.len = strlen(NOT_HEX);
    HostlinkV.parse_args.out = &f;
    Hostlink.parse(hostlink_work);
    TEST_ASSERT_TRUE(HostlinkV.ok);
    HostlinkV.end_code_args.f = &f;
    HostlinkV.end_code_args.code = &code;
    Hostlink.end_code(hostlink_work);
    TEST_ASSERT_FALSE(HostlinkV.ok);

    HostlinkV.end_code_args.f = NULL;
    HostlinkV.end_code_args.code = &code;
    Hostlink.end_code(hostlink_work);
    TEST_ASSERT_FALSE(HostlinkV.ok);

    // The end code is a whole byte, so both nibbles are read: "1A" is 0x1A, not 0x01 or 0x0A.
    char buf[32];
    HostlinkV.build_args.buf = buf;
    HostlinkV.build_args.cap = sizeof(buf);
    HostlinkV.build_args.node = 0;
    HostlinkV.build_args.header_code = "RD";
    HostlinkV.build_args.text = "1A";
    HostlinkV.build_args.text_len = 2;
    Hostlink.build(hostlink_work);
    size_t n = HostlinkV.n;
    HostlinkV.parse_args.buf = buf;
    HostlinkV.parse_args.len = n;
    HostlinkV.parse_args.out = &f;
    Hostlink.parse(hostlink_work);
    TEST_ASSERT_TRUE(HostlinkV.ok);
    HostlinkV.end_code_args.f = &f;
    HostlinkV.end_code_args.code = &code;
    Hostlink.end_code(hostlink_work);
    TEST_ASSERT_TRUE(HostlinkV.ok);
    TEST_ASSERT_EQUAL_HEX8(0x1Au, code);
}

// A buffer that cannot hold the frame plus its NUL terminator yields 0, never a truncated frame.
void test_builders_refuse_a_short_buffer(void)
{
    char buf[32];
    static const uint16_t WORDS[2] = {0x0001, 0x0002};

    HostlinkV.build_read_args.buf = buf;
    HostlinkV.build_read_args.cap = 17;
    HostlinkV.build_read_args.node = 0;
    HostlinkV.build_read_args.address = 0;
    HostlinkV.build_read_args.count = 10;
    Hostlink.build_read(hostlink_work);
    TEST_ASSERT_EQUAL_size_t(0u, HostlinkV.n); // 17 frame + NUL needs 18
    HostlinkV.build_read_args.buf = buf;
    HostlinkV.build_read_args.cap = 18;
    HostlinkV.build_read_args.node = 0;
    HostlinkV.build_read_args.address = 0;
    HostlinkV.build_read_args.count = 10;
    Hostlink.build_read(hostlink_work);
    TEST_ASSERT_EQUAL_size_t(17u, HostlinkV.n);

    HostlinkV.build_read_args.buf = buf;
    HostlinkV.build_read_args.cap = sizeof(buf);
    HostlinkV.build_read_args.node = 0;
    HostlinkV.build_read_args.address = 10000;
    HostlinkV.build_read_args.count = 1;
    Hostlink.build_read(hostlink_work);
    TEST_ASSERT_EQUAL_size_t(0u, HostlinkV.n); // address > 9999
    HostlinkV.build_read_args.buf = buf;
    HostlinkV.build_read_args.cap = sizeof(buf);
    HostlinkV.build_read_args.node = 0;
    HostlinkV.build_read_args.address = 0;
    HostlinkV.build_read_args.count = 0;
    Hostlink.build_read(hostlink_work);
    TEST_ASSERT_EQUAL_size_t(0u, HostlinkV.n); // zero words
    HostlinkV.build_read_args.buf = buf;
    HostlinkV.build_read_args.cap = sizeof(buf);
    HostlinkV.build_read_args.node = 0;
    HostlinkV.build_read_args.address = 0;
    HostlinkV.build_read_args.count = 10000;
    Hostlink.build_read(hostlink_work);
    TEST_ASSERT_EQUAL_size_t(0u, HostlinkV.n); // count > 9999

    HostlinkV.build_write_args.buf = buf;
    HostlinkV.build_write_args.cap = 21;
    HostlinkV.build_write_args.node = 0;
    HostlinkV.build_write_args.address = 100;
    HostlinkV.build_write_args.words = WORDS;
    HostlinkV.build_write_args.word_count = 2;
    Hostlink.build_write(hostlink_work);
    TEST_ASSERT_EQUAL_size_t(0u, HostlinkV.n); // 21 frame + NUL needs 22
    HostlinkV.build_write_args.buf = buf;
    HostlinkV.build_write_args.cap = 22;
    HostlinkV.build_write_args.node = 0;
    HostlinkV.build_write_args.address = 100;
    HostlinkV.build_write_args.words = WORDS;
    HostlinkV.build_write_args.word_count = 2;
    Hostlink.build_write(hostlink_work);
    TEST_ASSERT_EQUAL_size_t(21u, HostlinkV.n);
    HostlinkV.build_write_args.buf = buf;
    HostlinkV.build_write_args.cap = sizeof(buf);
    HostlinkV.build_write_args.node = 0;
    HostlinkV.build_write_args.address = 100;
    HostlinkV.build_write_args.words = WORDS;
    HostlinkV.build_write_args.word_count = 0;
    Hostlink.build_write(hostlink_work);
    TEST_ASSERT_EQUAL_size_t(0u, HostlinkV.n);
    HostlinkV.build_write_args.buf = buf;
    HostlinkV.build_write_args.cap = sizeof(buf);
    HostlinkV.build_write_args.node = 0;
    HostlinkV.build_write_args.address = 100;
    HostlinkV.build_write_args.words = NULL;
    HostlinkV.build_write_args.word_count = 1;
    Hostlink.build_write(hostlink_work);
    TEST_ASSERT_EQUAL_size_t(0u, HostlinkV.n);
    HostlinkV.build_write_args.buf = NULL;
    HostlinkV.build_write_args.cap = sizeof(buf);
    HostlinkV.build_write_args.node = 0;
    HostlinkV.build_write_args.address = 100;
    HostlinkV.build_write_args.words = WORDS;
    HostlinkV.build_write_args.word_count = 1;
    Hostlink.build_write(hostlink_work);
    TEST_ASSERT_EQUAL_size_t(0u, HostlinkV.n);
    HostlinkV.build_write_args.buf = buf;
    HostlinkV.build_write_args.cap = sizeof(buf);
    HostlinkV.build_write_args.node = 100;
    HostlinkV.build_write_args.address = 100;
    HostlinkV.build_write_args.words = WORDS;
    HostlinkV.build_write_args.word_count = 1;
    Hostlink.build_write(hostlink_work);
    TEST_ASSERT_EQUAL_size_t(0u, HostlinkV.n);

    HostlinkV.build_args.buf = NULL;
    HostlinkV.build_args.cap = sizeof(buf);
    HostlinkV.build_args.node = 0;
    HostlinkV.build_args.header_code = "RD";
    HostlinkV.build_args.text = "";
    HostlinkV.build_args.text_len = 0;
    Hostlink.build(hostlink_work);
    TEST_ASSERT_EQUAL_size_t(0u, HostlinkV.n);
    HostlinkV.build_args.buf = buf;
    HostlinkV.build_args.cap = sizeof(buf);
    HostlinkV.build_args.node = 0;
    HostlinkV.build_args.header_code = NULL;
    HostlinkV.build_args.text = "";
    HostlinkV.build_args.text_len = 0;
    Hostlink.build(hostlink_work);
    TEST_ASSERT_EQUAL_size_t(0u, HostlinkV.n);
    HostlinkV.build_args.buf = buf;
    HostlinkV.build_args.cap = sizeof(buf);
    HostlinkV.build_args.node = 0;
    HostlinkV.build_args.header_code = "R";
    HostlinkV.build_args.text = "";
    HostlinkV.build_args.text_len = 0;
    Hostlink.build(hostlink_work);
    TEST_ASSERT_EQUAL_size_t(0u, HostlinkV.n); // one header character
    HostlinkV.build_args.buf = buf;
    HostlinkV.build_args.cap = sizeof(buf);
    HostlinkV.build_args.node = 0;
    HostlinkV.build_args.header_code = "RD";
    HostlinkV.build_args.text = NULL;
    HostlinkV.build_args.text_len = 4;
    Hostlink.build(hostlink_work);
    TEST_ASSERT_EQUAL_size_t(0u, HostlinkV.n);
}
