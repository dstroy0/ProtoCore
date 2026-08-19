// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the CNC DNC drip-feed codec (services/machine_tool/dnc/dnc.h).
//
// The load-bearing case is test_eia_letters_and_digits_are_derived: EIA RS-244 builds a letter or a
// digit from a zone (channels 6 and 7), the BCD digit (channels 1 to 4) and odd parity in channel 5.
// The case recomputes all 35 codes from that construction rather than from the module's table, so a
// single mistyped hole pattern is caught. test_eia_codes_carry_odd_parity then holds the whole
// table, punctuation included, to the parity rule the tape code is defined by, and
// test_iso_parity_is_even holds the ISO/RS-358 side to its own. The framing cases follow
// RS-274 / ISO 6983: a program is bounded by the '%' rewind stop and its blocks end at End-of-Block.

#include "services/machine_tool/dnc/dnc/dnc.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Set bits in v: what the parity rules are stated over.
static unsigned popcount8(uint8_t v)
{
    unsigned n = 0;
    while (v)
    {
        n++;
        v = (uint8_t)(v & (v - 1u));
    }
    return n;
}

// Every ASCII character this tape code represents, so a property holds over the whole table.
static const char DNC_CHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ .-+/\t%";

// EIA RS-244 has odd parity in channel 5 (0x10): every code on the tape carries an odd number of
// punched holes, which is how a reader detects a dropped one.
void test_eia_codes_carry_odd_parity(void)
{
    for (const char *p = DNC_CHARS; *p; p++)
    {
        const uint8_t e = protocore_dnc_iso_to_eia(*p);
        TEST_ASSERT_NOT_EQUAL_UINT8_MESSAGE(0xFF, e, p);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, popcount8(e) & 1u, p);
    }
}

// The construction the code table is built from:
//   digits 1..9  channels 1..4 hold the BCD digit; 0 is channel 6 alone (0x20)
//   A..I         zone channels 6 and 7 (0x60) plus the BCD digits 1..9
//   J..R         zone channel 7 (0x40) plus the BCD digits 1..9
//   S..Z         zone channel 6 (0x20) plus the BCD digits 2..9
// then channel 5 (0x10) is punched when the count so far is even, making every code odd.
static uint8_t eia_expected(uint8_t zone, uint8_t bcd)
{
    const uint8_t base = (uint8_t)(zone | bcd);
    return (popcount8(base) & 1u) ? base : (uint8_t)(base | 0x10u);
}

void test_eia_letters_and_digits_are_derived(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x20, protocore_dnc_iso_to_eia('0')); // digit 0 is channel 6 alone
    for (uint8_t d = 1; d <= 9; d++)
    {
        TEST_ASSERT_EQUAL_HEX8(eia_expected(0x00, d), protocore_dnc_iso_to_eia((char)('0' + d)));
    }
    for (uint8_t d = 1; d <= 9; d++) // A..I
    {
        TEST_ASSERT_EQUAL_HEX8(eia_expected(0x60, d), protocore_dnc_iso_to_eia((char)('A' + d - 1)));
    }
    for (uint8_t d = 1; d <= 9; d++) // J..R
    {
        TEST_ASSERT_EQUAL_HEX8(eia_expected(0x40, d), protocore_dnc_iso_to_eia((char)('J' + d - 1)));
    }
    for (uint8_t d = 2; d <= 9; d++) // S..Z
    {
        TEST_ASSERT_EQUAL_HEX8(eia_expected(0x20, d), protocore_dnc_iso_to_eia((char)('S' + d - 2)));
    }
}

// Translation is one-to-one: every character round-trips, and no two characters share a code, so a
// decoded tape cannot name a different character to the one punched.
void test_eia_translation_is_a_bijection(void)
{
    for (const char *p = DNC_CHARS; *p; p++)
    {
        const uint8_t e = protocore_dnc_iso_to_eia(*p);
        TEST_ASSERT_EQUAL_CHAR_MESSAGE(*p, protocore_dnc_eia_to_iso(e), p);
        for (const char *q = p + 1; *q; q++)
        {
            TEST_ASSERT_NOT_EQUAL_UINT8_MESSAGE(e, protocore_dnc_iso_to_eia(*q), q);
        }
    }
}

// EIA has no lowercase and no ':' '(' ')'; a character with no hole pattern fails closed rather
// than punching something else. An unknown code decodes to 0, the blank/runout answer.
void test_eia_refuses_characters_it_has_no_code_for(void)
{
    static const char NONE[] = "abcz:()#*";
    for (const char *p = NONE; *p; p++)
    {
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFF, protocore_dnc_iso_to_eia(*p), p);
    }
    TEST_ASSERT_EQUAL_CHAR(0, protocore_dnc_eia_to_iso(0x00)); // blank tape
    TEST_ASSERT_EQUAL_CHAR(0, protocore_dnc_eia_to_iso(0x7F)); // DEL / rubout runout
}

// The '%' rewind stop of RS-274 is EIA End-of-Record, and End-of-Block is channel 8 alone.
void test_eia_special_codes(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x0B, DNC_EIA_EOR);
    TEST_ASSERT_EQUAL_HEX8(0x80, DNC_EIA_EOB);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)DNC_EIA_EOR, protocore_dnc_iso_to_eia('%'));
    TEST_ASSERT_EQUAL_CHAR('%', protocore_dnc_eia_to_iso((uint8_t)DNC_EIA_EOR));
}

// The ISO tape convention (RS-358) is even parity in bit 7: the parity bit is set exactly when the
// seven data bits hold an odd number of ones, and the data bits themselves are untouched.
void test_iso_parity_is_even(void)
{
    for (unsigned c = 0; c < 128u; c++)
    {
        const uint8_t out = protocore_dnc_iso_add_parity((uint8_t)c);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)c, (uint8_t)(out & 0x7Fu));
        TEST_ASSERT_EQUAL_UINT(0u, popcount8(out) & 1u);
    }
    // Bit 7 of the input is ignored, so adding parity twice is the same as adding it once.
    for (unsigned c = 0; c < 256u; c++)
    {
        const uint8_t once = protocore_dnc_iso_add_parity((uint8_t)c);
        TEST_ASSERT_EQUAL_HEX8(once, protocore_dnc_iso_add_parity(once));
    }
}

// XON (DC1) and XOFF (DC3) are the software flow-control pair. XOFF pauses the pump, XON resumes
// it, and any other byte is inbound data the caller keeps.
void test_xon_xoff_flow_state(void)
{
    DncFlow f;
    protocore_dnc_flow_init(&f);
    TEST_ASSERT_TRUE(protocore_dnc_flow_can_send(&f));

    TEST_ASSERT_TRUE(protocore_dnc_flow_feed(&f, (uint8_t)DNC_XOFF));
    TEST_ASSERT_FALSE(protocore_dnc_flow_can_send(&f));

    TEST_ASSERT_FALSE(protocore_dnc_flow_feed(&f, 'G')); // ordinary data, still paused
    TEST_ASSERT_FALSE(protocore_dnc_flow_can_send(&f));

    TEST_ASSERT_TRUE(protocore_dnc_flow_feed(&f, (uint8_t)DNC_XON));
    TEST_ASSERT_TRUE(protocore_dnc_flow_can_send(&f));

    // Repeats are idempotent: two XOFFs still take one XON.
    TEST_ASSERT_TRUE(protocore_dnc_flow_feed(&f, (uint8_t)DNC_XOFF));
    TEST_ASSERT_TRUE(protocore_dnc_flow_feed(&f, (uint8_t)DNC_XOFF));
    TEST_ASSERT_TRUE(protocore_dnc_flow_feed(&f, (uint8_t)DNC_XON));
    TEST_ASSERT_TRUE(protocore_dnc_flow_can_send(&f));

    TEST_ASSERT_EQUAL_HEX8(0x11, DNC_XON);  // DC1
    TEST_ASSERT_EQUAL_HEX8(0x13, DNC_XOFF); // DC3
}

// An ISO block is the line's 7-bit characters followed by End-of-Block, which is LF; the CR the
// crlf option adds precedes it.
void test_iso_block_framing(void)
{
    static const char LINE[] = "G01X10.5";
    uint8_t out[32];

    DncCfg cfg = {DNC_CODE_ISO, PROTO_FALSE, PROTO_FALSE, 0};
    size_t n = protocore_dnc_encode_block(&cfg, LINE, sizeof(LINE) - 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(sizeof(LINE) - 1 + 1, n);
    TEST_ASSERT_EQUAL_MEMORY(LINE, out, sizeof(LINE) - 1);
    TEST_ASSERT_EQUAL_HEX8(0x0A, out[n - 1]);

    cfg.crlf = PROTO_TRUE;
    n = protocore_dnc_encode_block(&cfg, LINE, sizeof(LINE) - 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(sizeof(LINE) - 1 + 2, n);
    TEST_ASSERT_EQUAL_HEX8(0x0D, out[n - 2]);
    TEST_ASSERT_EQUAL_HEX8(0x0A, out[n - 1]);
}

// With the parity option every emitted octet, the End-of-Block included, carries even parity.
void test_iso_block_carries_parity(void)
{
    static const char LINE[] = "N10G01";
    uint8_t out[32];
    DncCfg cfg = {DNC_CODE_ISO, PROTO_TRUE, PROTO_TRUE, 0};
    const size_t n = protocore_dnc_encode_block(&cfg, LINE, sizeof(LINE) - 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(sizeof(LINE) - 1 + 2, n);
    for (size_t i = 0; i < n; i++)
    {
        TEST_ASSERT_EQUAL_UINT(0u, popcount8(out[i]) & 1u);
        TEST_ASSERT_EQUAL_HEX8(protocore_dnc_iso_add_parity(out[i]), out[i]);
    }
    // CR is 0x0D, three bits, so its parity bit is set; LF is 0x0A, two bits, so it is not.
    TEST_ASSERT_EQUAL_HEX8(0x8D, out[n - 2]);
    TEST_ASSERT_EQUAL_HEX8(0x0A, out[n - 1]);
}

// An EIA block is the translated characters followed by End-of-Block 0x80.
void test_eia_block_framing(void)
{
    static const char LINE[] = "G01";
    uint8_t out[32];
    DncCfg cfg = {DNC_CODE_EIA, PROTO_FALSE, PROTO_FALSE, 0};
    const size_t n = protocore_dnc_encode_block(&cfg, LINE, sizeof(LINE) - 1, out, sizeof(out));

    TEST_ASSERT_EQUAL_UINT(4u, n);
    TEST_ASSERT_EQUAL_HEX8(protocore_dnc_iso_to_eia('G'), out[0]);
    TEST_ASSERT_EQUAL_HEX8(protocore_dnc_iso_to_eia('0'), out[1]);
    TEST_ASSERT_EQUAL_HEX8(protocore_dnc_iso_to_eia('1'), out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x80, out[3]);
}

// A block holding a character EIA has no code for emits nothing: a partial block punched on tape
// would be read as a different, valid one.
void test_eia_block_fails_closed(void)
{
    uint8_t out[32];
    DncCfg cfg = {DNC_CODE_EIA, PROTO_FALSE, PROTO_FALSE, 0};
    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnc_encode_block(&cfg, "G01(comment)", 12, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnc_encode_block(&cfg, "g01", 3, out, sizeof(out)));
}

// A buffer that cannot hold the line and its End-of-Block writes nothing.
void test_block_refuses_a_short_buffer(void)
{
    uint8_t out[4];
    DncCfg iso = {DNC_CODE_ISO, PROTO_FALSE, PROTO_FALSE, 0};
    TEST_ASSERT_EQUAL_UINT(4u, protocore_dnc_encode_block(&iso, "G01", 3, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnc_encode_block(&iso, "G01", 3, out, 3)); // no room for the LF
    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnc_encode_block(&iso, "G01", 3, out, 0));

    DncCfg eia = {DNC_CODE_EIA, PROTO_FALSE, PROTO_FALSE, 0};
    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnc_encode_block(&eia, "G01", 3, out, 3));
}

// The rewind stop that opens and closes a program: '%' in ISO, End-of-Record in EIA, each followed
// by an End-of-Block. Start and end are the same octets.
void test_program_marker(void)
{
    uint8_t out[8];

    DncCfg iso = {DNC_CODE_ISO, PROTO_FALSE, PROTO_FALSE, 0};
    TEST_ASSERT_EQUAL_UINT(2u, protocore_dnc_encode_marker(&iso, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8('%', out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x0A, out[1]);

    iso.even_parity = PROTO_TRUE; // '%' is 0x25, three bits, so its parity bit is set
    TEST_ASSERT_EQUAL_UINT(2u, protocore_dnc_encode_marker(&iso, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0xA5, out[0]);

    DncCfg eia = {DNC_CODE_EIA, PROTO_FALSE, PROTO_FALSE, 0};
    TEST_ASSERT_EQUAL_UINT(2u, protocore_dnc_encode_marker(&eia, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x0B, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x80, out[1]);

    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnc_encode_marker(&eia, out, 1));
}

// The leader is runout: leader_len NUL octets, which the reader skips until the first '%'.
void test_leader_runout(void)
{
    uint8_t out[16];
    DncCfg cfg = {DNC_CODE_ISO, PROTO_FALSE, PROTO_FALSE, 8};
    memset(out, 0xAA, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(8u, protocore_dnc_encode_leader(&cfg, out, sizeof(out)));
    for (size_t i = 0; i < 8; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x00, out[i]);
    }
    TEST_ASSERT_EQUAL_HEX8(0xAA, out[8]); // nothing past the leader was touched

    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnc_encode_leader(&cfg, out, 7));

    cfg.leader_len = 0;
    TEST_ASSERT_EQUAL_UINT(0u, protocore_dnc_encode_leader(&cfg, out, sizeof(out)));
}

// The decoder reports the two '%' markers as program start and program end, and hands back each
// block between them once its End-of-Block arrives.
void test_decoder_reports_markers_and_lines(void)
{
    static const char STREAM[] = "%\nN10G01\nN20X5\n%\n";
    DncDecoder d;
    protocore_dnc_decode_init(&d, DNC_CODE_ISO);

    int starts = 0, ends = 0, lines = 0;
    char first[PROTOCORE_DNC_LINE_MAX + 1] = {0};
    char second[PROTOCORE_DNC_LINE_MAX + 1] = {0};
    for (size_t i = 0; i < sizeof(STREAM) - 1; i++)
    {
        const DncEvent ev = protocore_dnc_decode_feed(&d, (uint8_t)STREAM[i]);
        if (ev == DNC_EV_PROG_START)
        {
            starts++;
        }
        else if (ev == DNC_EV_PROG_END)
        {
            ends++;
        }
        else if (ev == DNC_EV_LINE)
        {
            lines++;
            memcpy(lines == 1 ? first : second, d.line, (size_t)d.len + 1u);
        }
    }
    TEST_ASSERT_EQUAL_INT(1, starts);
    TEST_ASSERT_EQUAL_INT(1, ends);
    TEST_ASSERT_EQUAL_INT(2, lines);
    TEST_ASSERT_EQUAL_STRING("N10G01", first);
    TEST_ASSERT_EQUAL_STRING("N20X5", second);
}

// Leader NULs, CR of a CR LF pair, and DEL runout are skipped, so they never join a block.
void test_decoder_skips_runout(void)
{
    static const uint8_t STREAM[] = {0x00, 0x00, 0x7F, 'G', '0', '1', 0x0D, 0x0A};
    DncDecoder d;
    protocore_dnc_decode_init(&d, DNC_CODE_ISO);

    DncEvent last = DNC_EV_NONE;
    for (size_t i = 0; i < sizeof(STREAM); i++)
    {
        last = protocore_dnc_decode_feed(&d, STREAM[i]);
    }
    TEST_ASSERT_EQUAL_INT(DNC_EV_LINE, last);
    TEST_ASSERT_EQUAL_STRING("G01", d.line);
    TEST_ASSERT_EQUAL_UINT16(3u, d.len);
}

// An End-of-Block with nothing before it is a blank block, not an empty line: a leader or a stray
// CR LF must not deliver one.
void test_decoder_ignores_a_blank_block(void)
{
    DncDecoder d;
    protocore_dnc_decode_init(&d, DNC_CODE_ISO);
    TEST_ASSERT_EQUAL_INT(DNC_EV_NONE, protocore_dnc_decode_feed(&d, '\n'));
    TEST_ASSERT_EQUAL_INT(DNC_EV_NONE, protocore_dnc_decode_feed(&d, '\n'));
}

// A block longer than the decoder's line buffer is dropped whole and reported once, at its
// End-of-Block; the next block decodes normally.
void test_decoder_drops_an_overlong_block(void)
{
    DncDecoder d;
    protocore_dnc_decode_init(&d, DNC_CODE_ISO);

    for (int i = 0; i < PROTOCORE_DNC_LINE_MAX + 10; i++)
    {
        TEST_ASSERT_EQUAL_INT(DNC_EV_NONE, protocore_dnc_decode_feed(&d, 'X'));
    }
    TEST_ASSERT_EQUAL_INT(DNC_EV_OVERFLOW, protocore_dnc_decode_feed(&d, '\n'));

    TEST_ASSERT_EQUAL_INT(DNC_EV_NONE, protocore_dnc_decode_feed(&d, 'G'));
    TEST_ASSERT_EQUAL_INT(DNC_EV_LINE, protocore_dnc_decode_feed(&d, '\n'));
    TEST_ASSERT_EQUAL_STRING("G", d.line);
}

// A block exactly the buffer's length still fits.
void test_decoder_accepts_a_full_length_block(void)
{
    DncDecoder d;
    protocore_dnc_decode_init(&d, DNC_CODE_ISO);
    for (int i = 0; i < PROTOCORE_DNC_LINE_MAX; i++)
    {
        TEST_ASSERT_EQUAL_INT(DNC_EV_NONE, protocore_dnc_decode_feed(&d, 'X'));
    }
    TEST_ASSERT_EQUAL_INT(DNC_EV_LINE, protocore_dnc_decode_feed(&d, '\n'));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)PROTOCORE_DNC_LINE_MAX, d.len);
}

// The decoder strips the ISO parity bit, so a parity-punched tape decodes to the same characters an
// unpunched one does.
void test_decoder_strips_iso_parity(void)
{
    static const char LINE[] = "N10G01X5.5";
    uint8_t tape[32];
    DncCfg cfg = {DNC_CODE_ISO, PROTO_TRUE, PROTO_TRUE, 0};
    const size_t n = protocore_dnc_encode_block(&cfg, LINE, sizeof(LINE) - 1, tape, sizeof(tape));
    TEST_ASSERT_TRUE(n > 0);

    DncDecoder d;
    protocore_dnc_decode_init(&d, DNC_CODE_ISO);
    DncEvent last = DNC_EV_NONE;
    for (size_t i = 0; i < n; i++)
    {
        last = protocore_dnc_decode_feed(&d, tape[i]);
    }
    TEST_ASSERT_EQUAL_INT(DNC_EV_LINE, last);
    TEST_ASSERT_EQUAL_STRING(LINE, d.line);
}

// A whole program, encoded and decoded back, in either tape code: the markers bound it and the
// blocks come back as the lines that went in.
void test_program_round_trip(void)
{
    static const char *const PROGRAM[] = {"O1234", "N10G21G90", "N20G01X10.5Y-3.25F200", "N30M30"};
    static const DncCode CODES[] = {DNC_CODE_ISO, DNC_CODE_EIA};

    for (size_t c = 0; c < 2; c++)
    {
        uint8_t tape[512];
        size_t n = 0;
        DncCfg cfg = {CODES[c], (proto_bool)(CODES[c] == DNC_CODE_ISO), PROTO_FALSE, 4};

        n += protocore_dnc_encode_leader(&cfg, tape + n, sizeof(tape) - n);
        n += protocore_dnc_encode_marker(&cfg, tape + n, sizeof(tape) - n);
        for (size_t i = 0; i < 4; i++)
        {
            const size_t w =
                protocore_dnc_encode_block(&cfg, PROGRAM[i], strlen(PROGRAM[i]), tape + n, sizeof(tape) - n);
            TEST_ASSERT_TRUE_MESSAGE(w > 0, PROGRAM[i]);
            n += w;
        }
        n += protocore_dnc_encode_marker(&cfg, tape + n, sizeof(tape) - n);

        DncDecoder d;
        protocore_dnc_decode_init(&d, CODES[c]);
        size_t got = 0;
        int starts = 0, ends = 0;
        for (size_t i = 0; i < n; i++)
        {
            const DncEvent ev = protocore_dnc_decode_feed(&d, tape[i]);
            if (ev == DNC_EV_LINE)
            {
                TEST_ASSERT_TRUE(got < 4);
                TEST_ASSERT_EQUAL_STRING(PROGRAM[got], d.line);
                got++;
            }
            else if (ev == DNC_EV_PROG_START)
            {
                starts++;
            }
            else if (ev == DNC_EV_PROG_END)
            {
                ends++;
            }
        }
        TEST_ASSERT_EQUAL_UINT(4u, got);
        TEST_ASSERT_EQUAL_INT(1, starts);
        TEST_ASSERT_EQUAL_INT(1, ends);
    }
}

// A marker discards whatever partial block preceded it: the rewind stop stands alone.
void test_marker_discards_a_partial_block(void)
{
    DncDecoder d;
    protocore_dnc_decode_init(&d, DNC_CODE_ISO);
    (void)protocore_dnc_decode_feed(&d, 'G');
    (void)protocore_dnc_decode_feed(&d, '0');
    TEST_ASSERT_EQUAL_INT(DNC_EV_PROG_START, protocore_dnc_decode_feed(&d, '%'));
    TEST_ASSERT_EQUAL_UINT16(0u, d.len);

    TEST_ASSERT_EQUAL_INT(DNC_EV_NONE, protocore_dnc_decode_feed(&d, 'X'));
    TEST_ASSERT_EQUAL_INT(DNC_EV_LINE, protocore_dnc_decode_feed(&d, '\n'));
    TEST_ASSERT_EQUAL_STRING("X", d.line);
}

// In the forward program stream 0x13 is the EIA code for '3', not DC3: flow control rides the
// reverse channel, so the decoder must not swallow it.
void test_eia_decoder_does_not_filter_flow_bytes(void)
{
    DncDecoder d;
    protocore_dnc_decode_init(&d, DNC_CODE_EIA);
    TEST_ASSERT_EQUAL_HEX8(0x13, protocore_dnc_iso_to_eia('3'));

    (void)protocore_dnc_decode_feed(&d, 0x13); // '3'
    TEST_ASSERT_EQUAL_INT(DNC_EV_LINE, protocore_dnc_decode_feed(&d, (uint8_t)DNC_EIA_EOB));
    TEST_ASSERT_EQUAL_STRING("3", d.line);
}
