// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Sercos IDN and telegram codec (services/fieldbus/sercos/sercos.h).
//
// The IDN half is anchored. Two published documents were obtained in this session and agree on the
// 16-bit IDN bit ranges:
//   Phoenix Contact "sercos System Manual for I/O Devices", UM EN SERCOS SYS, order 8336_en_00,
//   Figure 2-6 "sercos parameter model", Figure 2-7 "IDN structure" and Table 8-1 "Numbering of the
//   IDNs", which read: "Bit 11-0: Data block number", "Bit 14-12: Parameter set (PS)",
//   "Bit 15: S/P parameter (S/P)", with bit 15 = 0 "Standard data (S, normative)" and
//   bit 15 = 1 "Product-specific data (P)", parameter record 0-7 and data block number 0-4095.
//   OPC UA for SERCOS Devices, OPC 30100 v1.2 (OPC Foundation with Sercos International), sec 3.3.5
//   "Sercos Parameter (32-Bit IDN)", which publishes the symbolic notation <S/P>-<PS>-<DBN>.<SI>.<SE>
//   and its field meanings.
// The load-bearing case is test_idn_symbolic_names: every expected word is the sum of the three
// published fields placed at those bit positions, with the addition written into the comment, so
// S-0-0100 is 0x0064 because the S bit is 0, the parameter set is 0 and 100 decimal is 0x064.
//
// The telegram half is NOT anchored, and asserts properties only. The governing document for the
// wire frame is the Sercos III Communication Specification of Sercos International (IEC 61784-2
// CP16 / IEC 61158 Type 19); it is members-only and was not obtained. No field offset, no header
// length and no telegram type number is asserted as a wire value anywhere below. What is asserted
// is round-trip identity, that a frame is a fixed-size header plus its payload, that the cycle
// field carries all 65536 values and the phase octet all 256, that the two defined types are
// distinct, that an undefined type is refused in both directions, and the bounds refusals.
//
// One published layout was found and it does not match this module, which is why the framing
// constants are carried and never asserted: the same Phoenix Contact manual, Figure 3-1 "General
// telegram structure for sercos MDT and AT telegrams" and Table 3-1 "sercos type structure", puts a
// 6-octet sercos header (MST) inside the EtherType 0x88CD frame and encodes MDT versus AT as bit 6
// of its first octet ("6 MDT or AT, 0: MDT, 1: AT"), not as the whole octet. sercos.h documents its
// own reduced 4-octet [type][phase][cycle:2] header instead. A vendor system manual is not the
// governing specification, so nothing here is failed against it; the conflict is recorded, not
// asserted.

#include "services/fieldbus/sercos/sercos.h"
#include <string.h>

#include <unity.h>

static uint8_t sercos_work[16]; // the borrow an entry takes; Sercos never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// Table 8-1 of UM EN SERCOS SYS numbers the 16-bit IDN as
//   bit 15    S/P parameter, 0 = standard (S), 1 = product-specific (P)
//   bit 14-12 parameter record, 0-7
//   bit 11-0  data block number, 0-4095
// so the word is (S/P << 15) + (PS << 12) + DBN, and each symbolic name below adds up as:
//   S-0-0100  0 + 0      + 0x064  = 0x0064
//   S-0-0001  0 + 0      + 0x001  = 0x0001
//   S-0-0000  0 + 0      + 0x000  = 0x0000
//   P-0-0100  0x8000 + 0 + 0x064  = 0x8064
//   S-1-0100  0 + 0x1000 + 0x064  = 0x1064
//   S-7-4095  0 + 0x7000 + 0x0FFF = 0x7FFF
//   P-7-4095  0x8000 + 0x7000 + 0x0FFF = 0xFFFF
//   P-0-0000  0x8000 + 0 + 0      = 0x8000
void test_idn_symbolic_names(void)
{
    struct
    {
        proto_bool product;
        uint8_t set;
        uint16_t block;
        uint16_t idn;
    } static const CASES[] = {
        {PROTO_FALSE, 0, 100, 0x0064}, {PROTO_FALSE, 0, 1, 0x0001},   {PROTO_FALSE, 0, 0, 0x0000},
        {PROTO_TRUE, 0, 100, 0x8064},  {PROTO_FALSE, 1, 100, 0x1064}, {PROTO_FALSE, 7, 4095, 0x7FFF},
        {PROTO_TRUE, 7, 4095, 0xFFFF}, {PROTO_TRUE, 0, 0, 0x8000},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        Sercos.idn_args.is_product = CASES[i].product;
        Sercos.idn_args.param_set = CASES[i].set;
        Sercos.idn_args.data_block = CASES[i].block;
        Sercos.idn(sercos_work);
        uint16_t got = Sercos.value;
        TEST_ASSERT_EQUAL_HEX16(CASES[i].idn, got);

        proto_bool p = PROTO_FALSE;
        uint8_t s = 0xFF;
        uint16_t b = 0xFFFF;
        Sercos.idn_parse_args.idn = CASES[i].idn;
        Sercos.idn_parse_args.is_product = &p;
        Sercos.idn_parse_args.param_set = &s;
        Sercos.idn_parse_args.data_block = &b;
        Sercos.idn_parse(sercos_work);
        TEST_ASSERT_EQUAL_INT(CASES[i].product ? 1 : 0, p ? 1 : 0);
        TEST_ASSERT_EQUAL_UINT8(CASES[i].set, s);
        TEST_ASSERT_EQUAL_UINT16(CASES[i].block, b);
    }
}

// Each field alone, at the bit range Table 8-1 gives it:
//   S/P at bit 15 alone            -> 1 << 15            = 0x8000
//   parameter set 7 at bits 14-12  -> 7 << 12            = 0x7000
//   data block 4095 at bits 11-0   -> 4095               = 0x0FFF
// The three are disjoint (pairwise AND is zero) and together tile the whole word
// (0x8000 | 0x7000 | 0x0FFF = 0xFFFF), which is what "no field overlaps another" means.
void test_idn_fields_are_disjoint_and_tile_the_word(void)
{
    Sercos.idn_args.is_product = PROTO_TRUE;
    Sercos.idn_args.param_set = 0;
    Sercos.idn_args.data_block = 0;
    Sercos.idn(sercos_work);
    const uint16_t sp = Sercos.value;
    Sercos.idn_args.is_product = PROTO_FALSE;
    Sercos.idn_args.param_set = 7;
    Sercos.idn_args.data_block = 0;
    Sercos.idn(sercos_work);
    const uint16_t ps = Sercos.value;
    Sercos.idn_args.is_product = PROTO_FALSE;
    Sercos.idn_args.param_set = 0;
    Sercos.idn_args.data_block = 4095;
    Sercos.idn(sercos_work);
    const uint16_t dbn = Sercos.value;

    TEST_ASSERT_EQUAL_HEX16(0x8000, sp);
    TEST_ASSERT_EQUAL_HEX16(0x7000, ps);
    TEST_ASSERT_EQUAL_HEX16(0x0FFF, dbn);

    TEST_ASSERT_EQUAL_HEX16(0x0000, sp & ps);
    TEST_ASSERT_EQUAL_HEX16(0x0000, sp & dbn);
    TEST_ASSERT_EQUAL_HEX16(0x0000, ps & dbn);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, (uint16_t)(sp | ps | dbn));
}

// An argument wider than the field Table 8-1 gives it cannot reach a neighboring field: a parameter
// set above 7 leaves the S/P bit and the data block number alone, and a data block number above
// 4095 leaves the S/P bit and the parameter set alone.
void test_an_over_wide_argument_stays_in_its_own_field(void)
{
    for (unsigned set = 8; set < 256; set++)
    {
        Sercos.idn_args.is_product = PROTO_FALSE;
        Sercos.idn_args.param_set = (uint8_t)set;
        Sercos.idn_args.data_block = 0x0ABC;
        Sercos.idn(sercos_work);
        const uint16_t v = Sercos.value;
        TEST_ASSERT_EQUAL_HEX16(0x0000, (uint16_t)(v & 0x8000));
        TEST_ASSERT_EQUAL_HEX16(0x0ABC, (uint16_t)(v & 0x0FFF));
    }
    for (uint32_t block = 0x1000; block <= 0xFFFF; block += 0x111)
    {
        Sercos.idn_args.is_product = PROTO_TRUE;
        Sercos.idn_args.param_set = 5;
        Sercos.idn_args.data_block = (uint16_t)block;
        Sercos.idn(sercos_work);
        const uint16_t v = Sercos.value;
        TEST_ASSERT_EQUAL_HEX16(0x8000, (uint16_t)(v & 0x8000));
        TEST_ASSERT_EQUAL_HEX16(0x5000, (uint16_t)(v & 0x7000));
    }
}

// Decode then re-encode is the identity on every one of the 65536 words, so the three fields lose
// no bit and claim none twice.
void test_idn_round_trip_over_every_word(void)
{
    for (uint32_t v = 0; v <= 0xFFFFu; v++)
    {
        proto_bool p = PROTO_FALSE;
        uint8_t s = 0;
        uint16_t b = 0;
        Sercos.idn_parse_args.idn = (uint16_t)v;
        Sercos.idn_parse_args.is_product = &p;
        Sercos.idn_parse_args.param_set = &s;
        Sercos.idn_parse_args.data_block = &b;
        Sercos.idn_parse(sercos_work);
        Sercos.idn_args.is_product = p;
        Sercos.idn_args.param_set = s;
        Sercos.idn_args.data_block = b;
        Sercos.idn(sercos_work);
        TEST_ASSERT_EQUAL_HEX16((uint16_t)v, Sercos.value);
    }
}

// sercos.h: "Decode a SERCOS IDN into its parts (any out-pointer may be null)". Dropping one output
// must not change the others. 0x9064 is P-1-0100: 0x8000 + 0x1000 + 0x064.
void test_idn_parse_accepts_null_outputs(void)
{
    uint8_t s = 0;
    uint16_t b = 0;
    proto_bool p = PROTO_FALSE;

    Sercos.idn_parse_args.idn = 0x9064;
    Sercos.idn_parse_args.is_product = NULL;
    Sercos.idn_parse_args.param_set = &s;
    Sercos.idn_parse_args.data_block = &b;
    Sercos.idn_parse(sercos_work);
    TEST_ASSERT_EQUAL_UINT8(1, s);
    TEST_ASSERT_EQUAL_UINT16(100, b);

    Sercos.idn_parse_args.idn = 0x9064;
    Sercos.idn_parse_args.is_product = &p;
    Sercos.idn_parse_args.param_set = NULL;
    Sercos.idn_parse_args.data_block = &b;
    Sercos.idn_parse(sercos_work);
    TEST_ASSERT_TRUE(p);
    TEST_ASSERT_EQUAL_UINT16(100, b);

    Sercos.idn_parse_args.idn = 0x9064;
    Sercos.idn_parse_args.is_product = &p;
    Sercos.idn_parse_args.param_set = &s;
    Sercos.idn_parse_args.data_block = NULL;
    Sercos.idn_parse(sercos_work);
    TEST_ASSERT_TRUE(p);
    TEST_ASSERT_EQUAL_UINT8(1, s);

    Sercos.idn_parse_args.idn = 0x9064;
    Sercos.idn_parse_args.is_product = NULL;
    Sercos.idn_parse_args.param_set = NULL;
    Sercos.idn_parse_args.data_block = NULL;
    Sercos.idn_parse(sercos_work);
}

// The header is whatever length an empty telegram is, measured rather than named, and every
// telegram is that header followed by its payload, so the built length grows one for one with the
// payload and the parsed payload length is the built length less the header.
static size_t header_length(void)
{
    uint8_t out[8];
    Sercos.build_args.type = SERCOS_TEL_MDT;
    Sercos.build_args.phase = 0;
    Sercos.build_args.cycle = 0;
    Sercos.build_args.data = NULL;
    Sercos.build_args.data_len = 0;
    Sercos.build_args.out = out;
    Sercos.build_args.cap = sizeof(out);
    Sercos.build(sercos_work);
    return Sercos.n;
}

void test_a_telegram_is_a_fixed_header_plus_its_payload(void)
{
    const size_t hdr = header_length();
    TEST_ASSERT_TRUE(hdr > 0);

    static const uint8_t TYPES[2] = {SERCOS_TEL_MDT, SERCOS_TEL_AT};
    uint8_t pdo[32];
    for (size_t i = 0; i < sizeof(pdo); i++)
    {
        pdo[i] = (uint8_t)(i * 11 + 5);
    }

    for (size_t t = 0; t < 2; t++)
    {
        for (size_t len = 0; len <= sizeof(pdo); len++)
        {
            uint8_t out[64];
            Sercos.build_args.type = TYPES[t];
            Sercos.build_args.phase = 0x04;
            Sercos.build_args.cycle = 0xBEEF;
            Sercos.build_args.data = len ? pdo : NULL;
            Sercos.build_args.data_len = len;
            Sercos.build_args.out = out;
            Sercos.build_args.cap = sizeof(out);
            Sercos.build(sercos_work);
            const size_t n = Sercos.n;
            TEST_ASSERT_EQUAL_UINT(hdr + len, n);

            SercosTelegram s;
            Sercos.parse_args.frame = out;
            Sercos.parse_args.len = n;
            Sercos.parse_args.out = &s;
            Sercos.parse(sercos_work);
            TEST_ASSERT_TRUE(Sercos.ok);
            TEST_ASSERT_EQUAL_UINT(len, s.data_len);
        }
    }
}

// Build then parse returns every field the builder was given, for both telegram types and every
// payload length up to 32. sercos.h says the parsed data "points into the input", so the payload
// pointer must land inside the frame just past the header, not in a copy.
void test_telegram_round_trip(void)
{
    const size_t hdr = header_length();
    static const uint8_t TYPES[2] = {SERCOS_TEL_MDT, SERCOS_TEL_AT};
    uint8_t pdo[32];
    for (size_t i = 0; i < sizeof(pdo); i++)
    {
        pdo[i] = (uint8_t)(i * 11 + 5);
    }

    for (size_t t = 0; t < 2; t++)
    {
        for (size_t len = 0; len <= sizeof(pdo); len++)
        {
            uint8_t out[64];
            Sercos.build_args.type = TYPES[t];
            Sercos.build_args.phase = 0x04;
            Sercos.build_args.cycle = 0xBEEF;
            Sercos.build_args.data = len ? pdo : NULL;
            Sercos.build_args.data_len = len;
            Sercos.build_args.out = out;
            Sercos.build_args.cap = sizeof(out);
            Sercos.build(sercos_work);
            const size_t n = Sercos.n;

            SercosTelegram s;
            Sercos.parse_args.frame = out;
            Sercos.parse_args.len = n;
            Sercos.parse_args.out = &s;
            Sercos.parse(sercos_work);
            TEST_ASSERT_TRUE(Sercos.ok);
            TEST_ASSERT_EQUAL_HEX8(TYPES[t], s.type);
            TEST_ASSERT_EQUAL_HEX8(0x04, s.phase);
            TEST_ASSERT_EQUAL_HEX16(0xBEEF, s.cycle);
            TEST_ASSERT_EQUAL_UINT(len, s.data_len);
            if (len)
            {
                TEST_ASSERT_EQUAL_HEX8_ARRAY(pdo, s.data, len);
                TEST_ASSERT_EQUAL_PTR(out + hdr, s.data);
            }
            else
            {
                TEST_ASSERT_NULL(s.data);
            }
        }
    }
}

// A master telegram and a drive telegram must not encode to the same octet, or a receiver could not
// tell which direction a frame came from.
void test_the_two_telegram_types_are_distinct(void)
{
    TEST_ASSERT_NOT_EQUAL_UINT8(SERCOS_TEL_MDT, SERCOS_TEL_AT);

    uint8_t mdt[8];
    uint8_t at[8];
    Sercos.build_args.type = SERCOS_TEL_MDT;
    Sercos.build_args.phase = 0;
    Sercos.build_args.cycle = 0;
    Sercos.build_args.data = NULL;
    Sercos.build_args.data_len = 0;
    Sercos.build_args.out = mdt;
    Sercos.build_args.cap = sizeof(mdt);
    Sercos.build(sercos_work);
    const size_t n = Sercos.n;
    Sercos.build_args.type = SERCOS_TEL_AT;
    Sercos.build_args.phase = 0;
    Sercos.build_args.cycle = 0;
    Sercos.build_args.data = NULL;
    Sercos.build_args.data_len = 0;
    Sercos.build_args.out = at;
    Sercos.build_args.cap = sizeof(at);
    Sercos.build(sercos_work);
    TEST_ASSERT_EQUAL_UINT(n, Sercos.n);
    TEST_ASSERT_TRUE(memcmp(mdt, at, n) != 0);

    SercosTelegram s;
    Sercos.parse_args.frame = mdt;
    Sercos.parse_args.len = n;
    Sercos.parse_args.out = &s;
    Sercos.parse(sercos_work);
    TEST_ASSERT_TRUE(Sercos.ok);
    TEST_ASSERT_EQUAL_HEX8(SERCOS_TEL_MDT, s.type);
    Sercos.parse_args.frame = at;
    Sercos.parse_args.len = n;
    Sercos.parse_args.out = &s;
    Sercos.parse(sercos_work);
    TEST_ASSERT_TRUE(Sercos.ok);
    TEST_ASSERT_EQUAL_HEX8(SERCOS_TEL_AT, s.type);
}

// The cycle count is a 16-bit field, so it must survive every value a 16-bit field can hold,
// including the ones that differ only in the high octet.
void test_cycle_count_carries_every_sixteen_bit_value(void)
{
    uint8_t out[8];
    SercosTelegram s;
    for (uint32_t c = 0; c <= 0xFFFFu; c++)
    {
        Sercos.build_args.type = SERCOS_TEL_MDT;
        Sercos.build_args.phase = 0;
        Sercos.build_args.cycle = (uint16_t)c;
        Sercos.build_args.data = NULL;
        Sercos.build_args.data_len = 0;
        Sercos.build_args.out = out;
        Sercos.build_args.cap = sizeof(out);
        Sercos.build(sercos_work);
        const size_t n = Sercos.n;
        Sercos.parse_args.frame = out;
        Sercos.parse_args.len = n;
        Sercos.parse_args.out = &s;
        Sercos.parse(sercos_work);
        TEST_ASSERT_TRUE(Sercos.ok);
        TEST_ASSERT_EQUAL_HEX16((uint16_t)c, s.cycle);
    }
}

// The phase octet is carried whole: all 256 values come back unchanged.
void test_phase_octet_carries_every_value(void)
{
    uint8_t out[8];
    SercosTelegram s;
    for (unsigned p = 0; p < 256; p++)
    {
        Sercos.build_args.type = SERCOS_TEL_AT;
        Sercos.build_args.phase = (uint8_t)p;
        Sercos.build_args.cycle = 1;
        Sercos.build_args.data = NULL;
        Sercos.build_args.data_len = 0;
        Sercos.build_args.out = out;
        Sercos.build_args.cap = sizeof(out);
        Sercos.build(sercos_work);
        const size_t n = Sercos.n;
        Sercos.parse_args.frame = out;
        Sercos.parse_args.len = n;
        Sercos.parse_args.out = &s;
        Sercos.parse(sercos_work);
        TEST_ASSERT_TRUE(Sercos.ok);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)p, s.phase);
    }
}

// sercos.h: build takes "SERCOS_TEL_MDT or SERCOS_TEL_AT" and parse returns "true if len >= 4 and
// the type is MDT/AT". Every other octet in the type position is refused in both directions, so an
// undefined telegram is neither produced nor accepted.
void test_only_the_two_defined_types_are_accepted(void)
{
    const size_t hdr = header_length();
    uint8_t out[8];
    SercosTelegram s;
    for (unsigned t = 0; t < 256; t++)
    {
        const proto_bool defined = (t == SERCOS_TEL_MDT || t == SERCOS_TEL_AT) ? PROTO_TRUE : PROTO_FALSE;

        Sercos.build_args.type = (uint8_t)t;
        Sercos.build_args.phase = 0;
        Sercos.build_args.cycle = 0;
        Sercos.build_args.data = NULL;
        Sercos.build_args.data_len = 0;
        Sercos.build_args.out = out;
        Sercos.build_args.cap = sizeof(out);
        Sercos.build(sercos_work);
        const size_t n = Sercos.n;
        uint8_t frame[8];
        memset(frame, 0, sizeof(frame));
        frame[0] = (uint8_t)t;

        if (defined)
        {
            TEST_ASSERT_EQUAL_UINT(hdr, n);
            Sercos.parse_args.frame = frame;
            Sercos.parse_args.len = hdr;
            Sercos.parse_args.out = &s;
            Sercos.parse(sercos_work);
            TEST_ASSERT_TRUE(Sercos.ok);
        }
        else
        {
            TEST_ASSERT_EQUAL_UINT(0u, n);
            Sercos.parse_args.frame = frame;
            Sercos.parse_args.len = hdr;
            Sercos.parse_args.out = &s;
            Sercos.parse(sercos_work);
            TEST_ASSERT_FALSE(Sercos.ok);
        }
    }
}

// A frame shorter than the header is not a telegram, a buffer smaller than header plus payload
// cannot hold one, and a null pointer is neither.
void test_bounds_refusals(void)
{
    const size_t hdr = header_length();
    uint8_t out[16];
    SercosTelegram s;
    static const uint8_t PDO[4] = {1, 2, 3, 4};

    uint8_t frame[16];
    memset(frame, 0, sizeof(frame));
    frame[0] = SERCOS_TEL_MDT;
    frame[1] = 0x02;
    for (size_t n = 0; n < hdr; n++)
    {
        Sercos.parse_args.frame = frame;
        Sercos.parse_args.len = n;
        Sercos.parse_args.out = &s;
        Sercos.parse(sercos_work);
        TEST_ASSERT_FALSE(Sercos.ok);
    }
    Sercos.parse_args.frame = frame;
    Sercos.parse_args.len = hdr;
    Sercos.parse_args.out = &s;
    Sercos.parse(sercos_work);
    TEST_ASSERT_TRUE(Sercos.ok);
    Sercos.parse_args.frame = NULL;
    Sercos.parse_args.len = hdr;
    Sercos.parse_args.out = &s;
    Sercos.parse(sercos_work);
    TEST_ASSERT_FALSE(Sercos.ok);
    Sercos.parse_args.frame = frame;
    Sercos.parse_args.len = hdr;
    Sercos.parse_args.out = NULL;
    Sercos.parse(sercos_work);
    TEST_ASSERT_FALSE(Sercos.ok);

    for (size_t cap = 0; cap < hdr + sizeof(PDO); cap++)
    {
        Sercos.build_args.type = SERCOS_TEL_MDT;
        Sercos.build_args.phase = 0;
        Sercos.build_args.cycle = 0;
        Sercos.build_args.data = PDO;
        Sercos.build_args.data_len = sizeof(PDO);
        Sercos.build_args.out = out;
        Sercos.build_args.cap = cap;
        Sercos.build(sercos_work);
        TEST_ASSERT_EQUAL_UINT(0u, Sercos.n);
    }
    Sercos.build_args.type = SERCOS_TEL_MDT;
    Sercos.build_args.phase = 0;
    Sercos.build_args.cycle = 0;
    Sercos.build_args.data = PDO;
    Sercos.build_args.data_len = sizeof(PDO);
    Sercos.build_args.out = out;
    Sercos.build_args.cap = hdr + sizeof(PDO);
    Sercos.build(sercos_work);
    TEST_ASSERT_EQUAL_UINT(hdr + sizeof(PDO), Sercos.n);
    Sercos.build_args.type = SERCOS_TEL_MDT;
    Sercos.build_args.phase = 0;
    Sercos.build_args.cycle = 0;
    Sercos.build_args.data = NULL;
    Sercos.build_args.data_len = 4;
    Sercos.build_args.out = out;
    Sercos.build_args.cap = sizeof(out);
    Sercos.build(sercos_work);
    TEST_ASSERT_EQUAL_UINT(0u, Sercos.n);
    Sercos.build_args.type = SERCOS_TEL_MDT;
    Sercos.build_args.phase = 0;
    Sercos.build_args.cycle = 0;
    Sercos.build_args.data = NULL;
    Sercos.build_args.data_len = 0;
    Sercos.build_args.out = NULL;
    Sercos.build_args.cap = sizeof(out);
    Sercos.build(sercos_work);
    TEST_ASSERT_EQUAL_UINT(0u, Sercos.n);
}

// One cycle of the exchange sercos.h describes: the master sends a setpoint in an MDT and the drive
// answers with an actual value in an AT. The two IDNs name the parameters carried, and add up from
// Table 8-1 as S-0-0047 = 0 + 0 + 0x02F = 0x002F and S-0-0051 = 0 + 0 + 0x033 = 0x0033.
void test_mdt_at_exchange(void)
{
    uint8_t buf[32];
    SercosTelegram s;

    Sercos.idn_args.is_product = PROTO_FALSE;
    Sercos.idn_args.param_set = 0;
    Sercos.idn_args.data_block = 47;
    Sercos.idn(sercos_work);
    TEST_ASSERT_EQUAL_HEX16(0x002F, Sercos.value);
    Sercos.idn_args.is_product = PROTO_FALSE;
    Sercos.idn_args.param_set = 0;
    Sercos.idn_args.data_block = 51;
    Sercos.idn(sercos_work);
    TEST_ASSERT_EQUAL_HEX16(0x0033, Sercos.value);

    static const uint8_t SETPOINT[4] = {0x10, 0x27, 0x00, 0x00};
    Sercos.build_args.type = SERCOS_TEL_MDT;
    Sercos.build_args.phase = 4;
    Sercos.build_args.cycle = 1;
    Sercos.build_args.data = SETPOINT;
    Sercos.build_args.data_len = sizeof(SETPOINT);
    Sercos.build_args.out = buf;
    Sercos.build_args.cap = sizeof(buf);
    Sercos.build(sercos_work);
    size_t n = Sercos.n;
    Sercos.parse_args.frame = buf;
    Sercos.parse_args.len = n;
    Sercos.parse_args.out = &s;
    Sercos.parse(sercos_work);
    TEST_ASSERT_TRUE(Sercos.ok);
    TEST_ASSERT_EQUAL_HEX8(SERCOS_TEL_MDT, s.type);
    TEST_ASSERT_EQUAL_HEX8(4, s.phase);
    TEST_ASSERT_EQUAL_HEX16(1, s.cycle);
    TEST_ASSERT_EQUAL_UINT(sizeof(SETPOINT), s.data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SETPOINT, s.data, sizeof(SETPOINT));

    static const uint8_t FEEDBACK[4] = {0x0F, 0x27, 0x00, 0x00};
    Sercos.build_args.type = SERCOS_TEL_AT;
    Sercos.build_args.phase = 4;
    Sercos.build_args.cycle = 1;
    Sercos.build_args.data = FEEDBACK;
    Sercos.build_args.data_len = sizeof(FEEDBACK);
    Sercos.build_args.out = buf;
    Sercos.build_args.cap = sizeof(buf);
    Sercos.build(sercos_work);
    n = Sercos.n;
    Sercos.parse_args.frame = buf;
    Sercos.parse_args.len = n;
    Sercos.parse_args.out = &s;
    Sercos.parse(sercos_work);
    TEST_ASSERT_TRUE(Sercos.ok);
    TEST_ASSERT_EQUAL_HEX8(SERCOS_TEL_AT, s.type);
    TEST_ASSERT_EQUAL_HEX16(1, s.cycle);
    TEST_ASSERT_EQUAL_UINT(sizeof(FEEDBACK), s.data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(FEEDBACK, s.data, sizeof(FEEDBACK));
}
