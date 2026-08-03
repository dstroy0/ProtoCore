// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the CNC RS-232 DNC drip-feed codec (services/machine_tool/dnc): the EIA RS-244
// <-> ISO/ASCII tape-code translation (with an odd-parity + exact-inverse guardrail),
// ISO even parity, block framing, the '%'/leader markers, XON/XOFF flow control, and
// full encode -> decode round-trips for both tape codes. Pure host tests.

#include "services/machine_tool/dnc/dnc.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

static int popcount8(uint8_t v)
{
    int n = 0;
    while (v)
    {
        n++;
        v &= (uint8_t)(v - 1);
    }
    return n;
}

// Guardrail: every EIA code the table emits must have odd parity (odd number of holes),
// the mapping must be an exact inverse over the representable set, and no two ASCII
// characters may collide on the same EIA byte. This catches any transcription slip.
void test_eia_table_odd_parity_and_inverse()
{
    uint8_t seen[256];
    memset(seen, 0, sizeof(seen));
    int representable = 0;
    for (int c = 0; c < 128; c++)
    {
        uint8_t e = pc_dnc_iso_to_eia((char)c);
        if (e == 0xFF)
        {
            continue; // not representable in EIA
        }
        representable++;
        // odd parity across all 8 tracks (EIA characteristic)
        TEST_ASSERT_TRUE_MESSAGE((popcount8(e) & 1) == 1, "EIA code must have odd parity");
        // exact inverse
        TEST_ASSERT_EQUAL_INT(c, (int)(unsigned char)pc_dnc_eia_to_iso(e));
        // distinct
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, seen[e], "two characters collide on one EIA byte");
        seen[e] = 1;
    }
    // digits (10) + A-Z (26) + space + . - + / tab + % = 43
    TEST_ASSERT_EQUAL_INT(43, representable);
}

// Spot-check known EIA RS-244 values (verified against the standard's zone+digit+parity rule).
void test_eia_known_vectors()
{
    TEST_ASSERT_EQUAL_HEX8(0x20, pc_dnc_iso_to_eia('0'));
    TEST_ASSERT_EQUAL_HEX8(0x01, pc_dnc_iso_to_eia('1'));
    TEST_ASSERT_EQUAL_HEX8(0x07, pc_dnc_iso_to_eia('7'));
    TEST_ASSERT_EQUAL_HEX8(0x61, pc_dnc_iso_to_eia('A'));
    TEST_ASSERT_EQUAL_HEX8(0x73, pc_dnc_iso_to_eia('C')); // even hole count -> parity added
    TEST_ASSERT_EQUAL_HEX8(0x26, pc_dnc_iso_to_eia('W')); // numeral 6 + channel 6 (documented)
    TEST_ASSERT_EQUAL_HEX8(0x29, pc_dnc_iso_to_eia('Z'));
    TEST_ASSERT_EQUAL_HEX8(0x10, pc_dnc_iso_to_eia(' '));
    TEST_ASSERT_EQUAL_HEX8(0x6B, pc_dnc_iso_to_eia('.'));
    TEST_ASSERT_EQUAL_HEX8(0x40, pc_dnc_iso_to_eia('-'));
    TEST_ASSERT_EQUAL_HEX8(0x31, pc_dnc_iso_to_eia('/'));
    TEST_ASSERT_EQUAL_HEX8(0x0B, pc_dnc_iso_to_eia('%')); // rewind stop = EIA End-of-Record
    // no lowercase / no ':' '(' ')' in EIA -> fail closed
    TEST_ASSERT_EQUAL_HEX8(0xFF, pc_dnc_iso_to_eia('a'));
    TEST_ASSERT_EQUAL_HEX8(0xFF, pc_dnc_iso_to_eia('('));
    TEST_ASSERT_EQUAL_HEX8(0xFF, pc_dnc_iso_to_eia(':'));
    // unknown EIA byte -> 0
    TEST_ASSERT_EQUAL_INT(0, (int)pc_dnc_eia_to_iso(0x00));
    TEST_ASSERT_EQUAL_INT(0, (int)pc_dnc_eia_to_iso(0x80)); // EOB is special, not a text char
}

// ISO even parity sets bit 7 so the whole byte has an even number of 1 bits.
void test_iso_even_parity()
{
    for (int c = 0; c < 128; c++)
    {
        uint8_t p = pc_dnc_iso_add_parity((uint8_t)c);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)c, (uint8_t)(p & 0x7F)); // low 7 bits unchanged
        TEST_ASSERT_TRUE_MESSAGE((popcount8(p) & 1) == 0, "even parity total");
    }
    TEST_ASSERT_EQUAL_HEX8(0x41, pc_dnc_iso_add_parity('A')); // 0x41 has 2 bits -> even -> bit7 clear
    TEST_ASSERT_EQUAL_HEX8(0xB1, pc_dnc_iso_add_parity('1')); // 0x31 has 3 bits -> odd -> bit7 set
}

// ISO block framing: the characters pass through, terminated by LF (or CR LF).
void test_encode_block_iso()
{
    DncCfg cfg = {DNC_CODE_ISO, PROTO_FALSE, PROTO_FALSE, 0};
    uint8_t out[32];
    size_t n = pc_dnc_encode_block(&cfg, "G01X10", 6, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(7, n);
    TEST_ASSERT_EQUAL_MEMORY("G01X10\n", out, 7);

    cfg.crlf = PROTO_TRUE;
    n = pc_dnc_encode_block(&cfg, "G01X10", 6, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(8, n);
    TEST_ASSERT_EQUAL_MEMORY("G01X10\r\n", out, 8);
}

// EIA block framing: each character is translated, then the 0x80 End-of-Block.
void test_encode_block_eia()
{
    DncCfg cfg = {DNC_CODE_EIA, PROTO_FALSE, PROTO_FALSE, 0};
    uint8_t out[32];
    size_t n = pc_dnc_encode_block(&cfg, "G01", 3, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_HEX8(0x67, out[0]); // G
    TEST_ASSERT_EQUAL_HEX8(0x20, out[1]); // 0
    TEST_ASSERT_EQUAL_HEX8(0x01, out[2]); // 1
    TEST_ASSERT_EQUAL_HEX8(0x80, out[3]); // EOB
}

// A non-representable EIA character or a too-small buffer fails closed (returns 0).
void test_encode_block_fail_closed()
{
    DncCfg cfg = {DNC_CODE_EIA, PROTO_FALSE, PROTO_FALSE, 0};
    uint8_t out[32];
    TEST_ASSERT_EQUAL_size_t(0, pc_dnc_encode_block(&cfg, "g01", 3, out, sizeof(out)));   // lowercase
    TEST_ASSERT_EQUAL_size_t(0, pc_dnc_encode_block(&cfg, "(cmt)", 5, out, sizeof(out))); // '(' not in EIA
    uint8_t tiny[2];
    TEST_ASSERT_EQUAL_size_t(0, pc_dnc_encode_block(&cfg, "G01", 3, tiny, sizeof(tiny))); // overflow
}

// The '%' program marker: ISO '%' + LF, EIA End-of-Record + EOB.
void test_encode_marker()
{
    DncCfg iso = {DNC_CODE_ISO, PROTO_FALSE, PROTO_FALSE, 0};
    uint8_t out[8];
    size_t n = pc_dnc_encode_marker(&iso, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL_MEMORY("%\n", out, 2);

    DncCfg eia = {DNC_CODE_EIA, PROTO_FALSE, PROTO_FALSE, 0};
    n = pc_dnc_encode_marker(&eia, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL_HEX8(0x0B, out[0]); // EOR
    TEST_ASSERT_EQUAL_HEX8(0x80, out[1]); // EOB
}

// Leader is N NUL runout bytes; too-small buffer fails closed.
void test_encode_leader()
{
    DncCfg cfg = {DNC_CODE_ISO, PROTO_FALSE, PROTO_FALSE, 5};
    uint8_t out[8];
    size_t n = pc_dnc_encode_leader(&cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(5, n);
    for (int i = 0; i < 5; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x00, out[i]);
    }
    uint8_t tiny[4];
    TEST_ASSERT_EQUAL_size_t(0, pc_dnc_encode_leader(&cfg, tiny, sizeof(tiny)));
}

// XON/XOFF flow control: XOFF pauses, XON resumes, ordinary bytes are not consumed.
void test_flow_control()
{
    DncFlow f;
    pc_dnc_flow_init(&f);
    TEST_ASSERT_TRUE(pc_dnc_flow_can_send(&f));

    TEST_ASSERT_TRUE(pc_dnc_flow_feed(&f, (uint8_t)DNC_XOFF)); // consumed
    TEST_ASSERT_FALSE(pc_dnc_flow_can_send(&f));               // paused
    TEST_ASSERT_FALSE(pc_dnc_flow_feed(&f, 'G'));              // ordinary byte, not consumed
    TEST_ASSERT_FALSE(pc_dnc_flow_can_send(&f));               // still paused
    TEST_ASSERT_TRUE(pc_dnc_flow_feed(&f, (uint8_t)DNC_XON));  // consumed
    TEST_ASSERT_TRUE(pc_dnc_flow_can_send(&f));                // resumed
}

// Helper: feed a whole buffer through the decoder, collecting lines and the marker sequence.
typedef struct
{
    char lines[8][PC_DNC_LINE_MAX + 1];
    int nlines;
    int starts;
    int ends;
    int overflows;
} DecodeCapture;

static void decode_all(DncCode code, const uint8_t *buf, size_t len, DecodeCapture *cap)
{
    DncDecoder d;
    pc_dnc_decode_init(&d, code);
    memset(cap, 0, sizeof(*cap));
    for (size_t i = 0; i < len; i++)
    {
        DncEvent ev = pc_dnc_decode_feed(&d, buf[i]);
        if (ev == DNC_EV_LINE)
        {
            TEST_ASSERT_EQUAL_size_t(strlen(d.line), d.len); // len matches the delivered line
            if (cap->nlines < 8)
            {
                strcpy(cap->lines[cap->nlines], d.line);
            }
            cap->nlines++;
        }
        else if (ev == DNC_EV_PROG_START)
        {
            cap->starts++;
        }
        else if (ev == DNC_EV_PROG_END)
        {
            cap->ends++;
        }
        else if (ev == DNC_EV_OVERFLOW)
        {
            cap->overflows++;
        }
    }
}

// Full program round-trip: encode leader + %start + blocks + %end + trailer, then decode
// the encoded bytes back into the exact lines and marker sequence. Both tape codes.
void test_roundtrip_program()
{
    const char *prog[] = {"O0001", "G0 X0 Y0", "G1 Z-1. F100", "M30"};
    const int nlines = 4;

    DncCode codes[] = {DNC_CODE_ISO, DNC_CODE_EIA};
    for (int ci = 0; ci < 2; ci++)
    {
        DncCfg cfg = {codes[ci], /*even_parity*/ codes[ci] == DNC_CODE_ISO,
                      /*crlf*/ codes[ci] == DNC_CODE_ISO, 8};
        uint8_t buf[512];
        size_t n = 0;
        n += pc_dnc_encode_leader(&cfg, buf + n, sizeof(buf) - n);
        n += pc_dnc_encode_marker(&cfg, buf + n, sizeof(buf) - n); // program start
        for (int i = 0; i < nlines; i++)
        {
            size_t w = pc_dnc_encode_block(&cfg, prog[i], strlen(prog[i]), buf + n, sizeof(buf) - n);
            TEST_ASSERT_GREATER_THAN_size_t(0, w); // every block is EIA-representable
            n += w;
        }
        n += pc_dnc_encode_marker(&cfg, buf + n, sizeof(buf) - n); // program end
        n += pc_dnc_encode_leader(&cfg, buf + n, sizeof(buf) - n); // trailer

        DecodeCapture cap;
        decode_all(codes[ci], buf, n, &cap);
        TEST_ASSERT_EQUAL_INT(1, cap.starts);
        TEST_ASSERT_EQUAL_INT(1, cap.ends);
        TEST_ASSERT_EQUAL_INT(0, cap.overflows);
        TEST_ASSERT_EQUAL_INT(nlines, cap.nlines);
        for (int i = 0; i < nlines; i++)
        {
            TEST_ASSERT_EQUAL_STRING(prog[i], cap.lines[i]);
        }
    }
}

// A block longer than PC_DNC_LINE_MAX is dropped whole (overflow), and the decoder
// recovers to decode the next block cleanly.
void test_decode_overflow_and_recovery()
{
    DncDecoder d;
    pc_dnc_decode_init(&d, DNC_CODE_ISO);
    int overflow = 0, lines = 0;
    for (int i = 0; i < PC_DNC_LINE_MAX + 50; i++)
    {
        DncEvent ev = pc_dnc_decode_feed(&d, (uint8_t)'X');
        if (ev == DNC_EV_OVERFLOW)
        {
            overflow++;
        }
    }
    DncEvent ev = pc_dnc_decode_feed(&d, (uint8_t)'\n'); // EOB closes the over-long block
    TEST_ASSERT_EQUAL(DNC_EV_OVERFLOW, ev);
    (void)overflow;
    // next block decodes normally
    pc_dnc_decode_feed(&d, 'G');
    pc_dnc_decode_feed(&d, '1');
    ev = pc_dnc_decode_feed(&d, '\n');
    TEST_ASSERT_EQUAL(DNC_EV_LINE, ev);
    TEST_ASSERT_EQUAL_STRING("G1", d.line);
    (void)lines;
}

// Runout (NUL / DEL / CR) is skipped, not decoded as data.
void test_decode_ignores_runout()
{
    const uint8_t stream[] = {
        0x00,
        0x00, // NUL leader
        'G',
        '1', // the block
        '\r',
        '\n', // CR LF end of block
        (uint8_t)DNC_EIA_DEL,
        0x00,
        '\n' // DEL + NUL + a bare LF (empty block)
    };
    DecodeCapture cap;
    decode_all(DNC_CODE_ISO, stream, sizeof(stream), &cap);
    TEST_ASSERT_EQUAL_INT(1, cap.nlines);
    TEST_ASSERT_EQUAL_STRING("G1", cap.lines[0]); // CR, NUL, DEL all skipped
}

// EIA data byte 0x13 is the digit '3', NOT XOFF: it must decode as data (regression for the
// collision between the EIA '3' code and the DC3 flow-control byte).
void test_decode_eia_three_is_not_xoff()
{
    DncDecoder d;
    pc_dnc_decode_init(&d, DNC_CODE_EIA);
    pc_dnc_decode_feed(&d, pc_dnc_iso_to_eia('M'));
    pc_dnc_decode_feed(&d, pc_dnc_iso_to_eia('3')); // 0x13 == DC3, but here it is the digit '3'
    pc_dnc_decode_feed(&d, pc_dnc_iso_to_eia('0'));
    DncEvent ev = pc_dnc_decode_feed(&d, (uint8_t)DNC_EIA_EOB);
    TEST_ASSERT_EQUAL(DNC_EV_LINE, ev);
    TEST_ASSERT_EQUAL_STRING("M30", d.line);
}

// Every End-of-Block / marker overflow path fails closed (never overruns the buffer).
void test_encode_overflow_paths()
{
    uint8_t o[8];
    DncCfg eia = {DNC_CODE_EIA, PROTO_FALSE, PROTO_FALSE, 0};
    TEST_ASSERT_EQUAL_size_t(0, pc_dnc_encode_block(&eia, "G01", 3, o, 3)); // chars fill cap, EOB overflows
    DncCfg iso = {DNC_CODE_ISO, PROTO_FALSE, PROTO_TRUE, 0};                // crlf
    TEST_ASSERT_EQUAL_size_t(0, pc_dnc_encode_block(&iso, "G", 1, o, 1));   // CR overflows
    TEST_ASSERT_EQUAL_size_t(0, pc_dnc_encode_block(&iso, "G", 1, o, 2));   // CR fits, LF overflows
    TEST_ASSERT_EQUAL_size_t(0, pc_dnc_encode_marker(&eia, o, 0));          // EIA EOR has no room
    TEST_ASSERT_EQUAL_size_t(0, pc_dnc_encode_marker(&iso, o, 0));          // ISO '%' has no room
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_eia_table_odd_parity_and_inverse);
    RUN_TEST(test_eia_known_vectors);
    RUN_TEST(test_iso_even_parity);
    RUN_TEST(test_encode_block_iso);
    RUN_TEST(test_encode_block_eia);
    RUN_TEST(test_encode_block_fail_closed);
    RUN_TEST(test_encode_marker);
    RUN_TEST(test_encode_leader);
    RUN_TEST(test_flow_control);
    RUN_TEST(test_roundtrip_program);
    RUN_TEST(test_decode_overflow_and_recovery);
    RUN_TEST(test_decode_ignores_runout);
    RUN_TEST(test_decode_eia_three_is_not_xoff);
    RUN_TEST(test_encode_overflow_paths);
    return UNITY_END();
}
