// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the PROFIBUS-DP FDL telegram codec (services/fieldbus/profibus/profibus.h).
//
// The governing text is EN 50170 volume 2 / IEC 61158-4-3, which PROFIBUS International sells and
// which is not in this tree. Every published value below therefore comes from a secondary source
// that prints the field layout, named in the case that uses it:
//
//   [PBM]  PBMaster wiki, "profibus-fdl" - the FC octet bit table:
//          b7 Res (sender sets 0), b6 Frame Type (1 request / send-request, 0 acknowledgement /
//          response), b5 FCB, b4 FCV when b6 is 1, b3..b0 Function.
//   [FC]   felser.ch PROFIBUS Manual, "Function code" - the request function code numbers:
//          3 SDA_LOW, 4 SDN_LOW, 5 SDA_HIGH, 6 SDN_HIGH, 7 MSRD, 9 Request FDL Status,
//          12 SRD low, 13 SRD high, 14 Request Ident, 15 Request LSAP Status.
//          Corroborated by Beckhoff EL6731 "FDL interface", which prints SRD LOW 0x0C, SRD HIGH 0x0D.
//   [TF]   felser.ch PROFIBUS Manual, "Telegram formats" - SD1 0x10, SD2 0x68, SD3 0xA2, SD4 0xDC,
//          ED 0x16, SC 0xE5; "SC, SD1, SD2, SD3, SD4 and ED have a Hamming distance from each other
//          of 4 (HD=4)"; and the four telegram byte sequences.
//   [CS]   felser.ch PROFIBUS Manual, "Checksum" - the FCS is the "arithmetical sum of DA, SA, and
//          FC without start delimiter (SD) or end delimiter (ED) and disregarding sums carried over".
//   [LI]   felser.ch PROFIBUS Manual, "Length information" - LE/LEr count "DA, SA, FC and the PDU";
//          "The value ranges from 4 to 249, so that no more than 246 bytes can be transferred to the
//          PDU"; "No value <4 is allowed, because a telegram must comprise at least one DA, SA, FC
//          and a data byte"; "The longest telegram therefore comprises 255 bytes overall".
//
// Every golden telegram passes its FC as a literal derived in-comment from [PBM] + [FC], not as the
// module's own macro, and every FCS is added up in the comment beside it.
//
// [LI] forbids LE < 4. The builder refuses a zero-octet data unit and the parser refuses LE < 4,
// so test_sd2_length_field_minimum_is_four passes; it failed against the shipped code until
// 2026-08-18. Corroborating [LI] on the range: embien.com "Profibus Fieldbus Data Link Layer",
// the automation-networks PROFIBUS glossary, and the Gantner IDL 101 manual telegram-format table,
// all printing LE 4..249 over a 1..246 octet PDU.

#include "services/fieldbus/profibus/profibus.h"
#include <string.h>

#include <unity.h>

static uint8_t profibus_work[16]; // the borrow an entry takes; Profibus never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static int popcount8(uint8_t v)
{
    int n = 0;
    for (int b = 0; b < 8; b++)
    {
        n += (v >> b) & 1;
    }
    return n;
}

// Flip each bit of each octet in turn; a telegram with one corrupted bit must never parse. PROFIBUS
// FDL is specified for a Hamming distance of 4 [TF], so a single-bit error is always detectable.
static void every_single_bit_flip_is_refused(const uint8_t *frame, size_t len)
{
    uint8_t f[16];
    PbTelegram t;
    for (size_t i = 0; i < len; i++)
    {
        for (int b = 0; b < 8; b++)
        {
            memcpy(f, frame, len);
            f[i] ^= (uint8_t)(1u << b);
            Profibus.parse_args.frame = f;
            Profibus.parse_args.len = len;
            Profibus.parse_args.out = &t;
            Profibus.parse(profibus_work);
            TEST_ASSERT_FALSE(Profibus.ok);
        }
    }
}

// [TF] prints the delimiters and states the four start delimiters differ from each other in at least
// four bits.
void test_delimiters_and_their_hamming_distance(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x10, PB_SD1);
    TEST_ASSERT_EQUAL_HEX8(0x68, PB_SD2);
    TEST_ASSERT_EQUAL_HEX8(0xA2, PB_SD3);
    TEST_ASSERT_EQUAL_HEX8(0xDC, PB_SD4);
    TEST_ASSERT_EQUAL_HEX8(0x16, PB_ED);

    static const uint8_t SD[4] = {PB_SD1, PB_SD2, PB_SD3, PB_SD4};
    for (int i = 0; i < 4; i++)
    {
        for (int j = i + 1; j < 4; j++)
        {
            TEST_ASSERT_TRUE_MESSAGE(popcount8((uint8_t)(SD[i] ^ SD[j])) >= 4,
                                     "start delimiters closer than a Hamming distance of 4");
        }
    }
}

// [PBM] splits the FC octet into b7 Res = 0, b6 Frame Type = 1 for a request, b5 FCB, b4 FCV,
// b3..b0 Function. [FC] numbers the functions this module names:
//
//   Request FDL Status = 9  -> b3..b0 = 0x9
//   SRD low            = 12 -> b3..b0 = 0xC
//   SRD high           = 13 -> b3..b0 = 0xD
//
// FCB and FCV are per-transaction state, so only the reserved bit, the frame-type bit and the
// function nibble are fixed.
void test_frame_control_function_codes(void)
{
    static const uint8_t REQUESTS[3] = {PB_FC_REQUEST_FDL_STATUS, PB_FC_SRD_LOW, PB_FC_SRD_HIGH};
    for (size_t i = 0; i < sizeof(REQUESTS); i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x00, (uint8_t)(REQUESTS[i] & 0x80u));
        TEST_ASSERT_EQUAL_HEX8(0x40, (uint8_t)(REQUESTS[i] & 0x40u));
    }
    TEST_ASSERT_EQUAL_HEX8(0x09, (uint8_t)(PB_FC_REQUEST_FDL_STATUS & 0x0Fu));
    TEST_ASSERT_EQUAL_HEX8(0x0C, (uint8_t)(PB_FC_SRD_LOW & 0x0Fu));
    TEST_ASSERT_EQUAL_HEX8(0x0D, (uint8_t)(PB_FC_SRD_HIGH & 0x0Fu));
}

// [CS]: the arithmetical sum of the body octets, carries discarded.
//
//   {0x03, 0x02, 0x49} = 3 + 2 + 73                 = 78  = 0x4E
//   {0xFF, 0xFF, 0x02} = 255 + 255 + 2 = 512, - 256 = 256, - 256 = 0
//   {0x80, 0x80, 0x80, 0x81} = 128 * 3 + 129 = 513, - 256 = 257, - 256 = 1
//   an empty body sums to 0
void test_fcs_is_the_arithmetic_sum_with_carries_discarded(void)
{
    static const uint8_t A[3] = {0x03, 0x02, 0x49};
    Profibus.fcs_args.bytes = A;
    Profibus.fcs_args.len = 3;
    Profibus.fcs(profibus_work);
    TEST_ASSERT_EQUAL_HEX8(0x4E, Profibus.value);

    static const uint8_t B[3] = {0xFF, 0xFF, 0x02};
    Profibus.fcs_args.bytes = B;
    Profibus.fcs_args.len = 3;
    Profibus.fcs(profibus_work);
    TEST_ASSERT_EQUAL_HEX8(0x00, Profibus.value);

    static const uint8_t C[4] = {0x80, 0x80, 0x80, 0x81};
    Profibus.fcs_args.bytes = C;
    Profibus.fcs_args.len = 4;
    Profibus.fcs(profibus_work);
    TEST_ASSERT_EQUAL_HEX8(0x01, Profibus.value);

    Profibus.fcs_args.bytes = A;
    Profibus.fcs_args.len = 0;
    Profibus.fcs(profibus_work);
    TEST_ASSERT_EQUAL_HEX8(0x00, Profibus.value);
}

// [TF] telegram without data field: SD1 DA SA FC FCS ED, six octets.
//
//   SD1 = 0x10 [TF]
//   DA  = 0x03, SA = 0x02
//   FC  = Request FDL Status: b6 request + function 9 = 0x40 | 0x09 = 0x49 [PBM][FC]
//   FCS = 0x03 + 0x02 + 0x49 = 3 + 2 + 73 = 78 = 0x4E [CS]
//   ED  = 0x16 [TF]
void test_sd1_telegram(void)
{
    uint8_t out[32];
    PbTelegram t;

    Profibus.build_sd1_args.da = 0x03;
    Profibus.build_sd1_args.sa = 0x02;
    Profibus.build_sd1_args.fc = 0x49;
    Profibus.build_sd1_args.out = out;
    Profibus.build_sd1_args.cap = sizeof(out);
    Profibus.build_sd1(profibus_work);
    TEST_ASSERT_EQUAL_UINT(6u, Profibus.n);
    static const uint8_t WANT[6] = {0x10, 0x03, 0x02, 0x49, 0x4E, 0x16};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));

    Profibus.parse_args.frame = WANT;
    Profibus.parse_args.len = sizeof(WANT);
    Profibus.parse_args.out = &t;
    Profibus.parse(profibus_work);
    TEST_ASSERT_TRUE(Profibus.ok);
    TEST_ASSERT_EQUAL_HEX8(PB_SD1, t.sd);
    TEST_ASSERT_EQUAL_HEX8(0x03, t.da);
    TEST_ASSERT_EQUAL_HEX8(0x02, t.sa);
    TEST_ASSERT_EQUAL_HEX8(0x49, t.fc);
    TEST_ASSERT_EQUAL_UINT(0u, t.data_len);
    TEST_ASSERT_NULL(t.data);

    every_single_bit_flip_is_refused(WANT, sizeof(WANT));
}

// [TF] telegram with variable data length: SD2 LE LEr SD2 DA SA FC PDU FCS ED.
//
//   SD2 = 0x68, repeated after LEr [TF]
//   LE  = DA + SA + FC + PDU = 3 + 4 = 7, and LEr repeats it [LI]
//   DA  = 0x05, SA = 0x02
//   FC  = SRD low: b6 request + b5 FCB + function 12 = 0x40 | 0x20 | 0x0C = 0x6C [PBM][FC]
//   PDU = 0x11 0x22 0x33 0x44
//   FCS = 0x05 + 0x02 + 0x6C + 0x11 + 0x22 + 0x33 + 0x44
//       = 5 + 2 + 108 + 17 + 34 + 51 + 68 = 285, - 256 = 29 = 0x1D [CS]
//   ED  = 0x16 [TF]
//   total = 4 header + LE 7 + FCS + ED = 13 octets
void test_sd2_telegram(void)
{
    uint8_t out[32];
    PbTelegram t;
    static const uint8_t PDU[4] = {0x11, 0x22, 0x33, 0x44};

    Profibus.build_sd2_args.da = 0x05;
    Profibus.build_sd2_args.sa = 0x02;
    Profibus.build_sd2_args.fc = 0x6C;
    Profibus.build_sd2_args.data = PDU;
    Profibus.build_sd2_args.data_len = sizeof(PDU);
    Profibus.build_sd2_args.out = out;
    Profibus.build_sd2_args.cap = sizeof(out);
    Profibus.build_sd2(profibus_work);
    TEST_ASSERT_EQUAL_UINT(13u, Profibus.n);
    static const uint8_t WANT[13] = {0x68, 0x07, 0x07, 0x68, 0x05, 0x02, 0x6C, 0x11, 0x22, 0x33, 0x44, 0x1D, 0x16};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));

    Profibus.parse_args.frame = WANT;
    Profibus.parse_args.len = sizeof(WANT);
    Profibus.parse_args.out = &t;
    Profibus.parse(profibus_work);
    TEST_ASSERT_TRUE(Profibus.ok);
    TEST_ASSERT_EQUAL_HEX8(PB_SD2, t.sd);
    TEST_ASSERT_EQUAL_HEX8(0x05, t.da);
    TEST_ASSERT_EQUAL_HEX8(0x02, t.sa);
    TEST_ASSERT_EQUAL_HEX8(0x6C, t.fc);
    TEST_ASSERT_EQUAL_UINT(sizeof(PDU), t.data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PDU, t.data, sizeof(PDU));

    every_single_bit_flip_is_refused(WANT, sizeof(WANT));
}

// [TF] telegram with fixed data length: SD3 DA SA FC PDU FCS ED, the PDU exactly 8 octets.
//
//   SD3 = 0xA2 [TF]
//   DA  = 0x05, SA = 0x02
//   FC  = SRD high: b6 request + b5 FCB + b4 FCV + function 13
//       = 0x40 | 0x20 | 0x10 | 0x0D = 0x7D [PBM][FC]
//   PDU = 1 2 3 4 5 6 7 8, which sums to 36 = 0x24
//   FCS = 0x05 + 0x02 + 0x7D + 0x24 = 5 + 2 + 125 + 36 = 168 = 0xA8 [CS]
//   ED  = 0x16 [TF]
//   total = SD3 + DA + SA + FC + 8 + FCS + ED = 14 octets
void test_sd3_telegram(void)
{
    uint8_t out[32];
    PbTelegram t;
    static const uint8_t EIGHT[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    Profibus.build_sd3_args.da = 0x05;
    Profibus.build_sd3_args.sa = 0x02;
    Profibus.build_sd3_args.fc = 0x7D;
    Profibus.build_sd3_args.data = EIGHT;
    Profibus.build_sd3_args.out = out;
    Profibus.build_sd3_args.cap = sizeof(out);
    Profibus.build_sd3(profibus_work);
    TEST_ASSERT_EQUAL_UINT(14u, Profibus.n);
    static const uint8_t WANT[14] = {0xA2, 0x05, 0x02, 0x7D, 1, 2, 3, 4, 5, 6, 7, 8, 0xA8, 0x16};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));

    Profibus.parse_args.frame = WANT;
    Profibus.parse_args.len = sizeof(WANT);
    Profibus.parse_args.out = &t;
    Profibus.parse(profibus_work);
    TEST_ASSERT_TRUE(Profibus.ok);
    TEST_ASSERT_EQUAL_HEX8(PB_SD3, t.sd);
    TEST_ASSERT_EQUAL_HEX8(0x05, t.da);
    TEST_ASSERT_EQUAL_HEX8(0x02, t.sa);
    TEST_ASSERT_EQUAL_HEX8(0x7D, t.fc);
    TEST_ASSERT_EQUAL_UINT(8u, t.data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(EIGHT, t.data, 8);

    every_single_bit_flip_is_refused(WANT, sizeof(WANT));
}

// The module's own FC macros must build the same telegrams the derived literals above build.
void test_frame_control_macros_build_the_derived_telegrams(void)
{
    uint8_t a[32];
    uint8_t b[32];
    static const uint8_t EIGHT[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    static const uint8_t PDU[4] = {0x11, 0x22, 0x33, 0x44};

    Profibus.build_sd1_args.da = 0x03;
    Profibus.build_sd1_args.sa = 0x02;
    Profibus.build_sd1_args.fc = 0x49;
    Profibus.build_sd1_args.out = a;
    Profibus.build_sd1_args.cap = sizeof(a);
    Profibus.build_sd1(profibus_work);
    TEST_ASSERT_EQUAL_UINT(6u, Profibus.n);
    Profibus.build_sd1_args.da = 0x03;
    Profibus.build_sd1_args.sa = 0x02;
    Profibus.build_sd1_args.fc = PB_FC_REQUEST_FDL_STATUS;
    Profibus.build_sd1_args.out = b;
    Profibus.build_sd1_args.cap = sizeof(b);
    Profibus.build_sd1(profibus_work);
    TEST_ASSERT_EQUAL_UINT(6u, Profibus.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(a, b, 6);

    Profibus.build_sd2_args.da = 0x05;
    Profibus.build_sd2_args.sa = 0x02;
    Profibus.build_sd2_args.fc = 0x6C;
    Profibus.build_sd2_args.data = PDU;
    Profibus.build_sd2_args.data_len = sizeof(PDU);
    Profibus.build_sd2_args.out = a;
    Profibus.build_sd2_args.cap = sizeof(a);
    Profibus.build_sd2(profibus_work);
    TEST_ASSERT_EQUAL_UINT(13u, Profibus.n);
    Profibus.build_sd2_args.da = 0x05;
    Profibus.build_sd2_args.sa = 0x02;
    Profibus.build_sd2_args.fc = PB_FC_SRD_LOW;
    Profibus.build_sd2_args.data = PDU;
    Profibus.build_sd2_args.data_len = sizeof(PDU);
    Profibus.build_sd2_args.out = b;
    Profibus.build_sd2_args.cap = sizeof(b);
    Profibus.build_sd2(profibus_work);
    TEST_ASSERT_EQUAL_UINT(13u, Profibus.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(a, b, 13);

    Profibus.build_sd3_args.da = 0x05;
    Profibus.build_sd3_args.sa = 0x02;
    Profibus.build_sd3_args.fc = 0x7D;
    Profibus.build_sd3_args.data = EIGHT;
    Profibus.build_sd3_args.out = a;
    Profibus.build_sd3_args.cap = sizeof(a);
    Profibus.build_sd3(profibus_work);
    TEST_ASSERT_EQUAL_UINT(14u, Profibus.n);
    Profibus.build_sd3_args.da = 0x05;
    Profibus.build_sd3_args.sa = 0x02;
    Profibus.build_sd3_args.fc = PB_FC_SRD_HIGH;
    Profibus.build_sd3_args.data = EIGHT;
    Profibus.build_sd3_args.out = b;
    Profibus.build_sd3_args.cap = sizeof(b);
    Profibus.build_sd3(profibus_work);
    TEST_ASSERT_EQUAL_UINT(14u, Profibus.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(a, b, 14);
}

// [LI]: LE counts DA + SA + FC + PDU, its range is 4 to 249, the PDU carries at most 246 octets, and
// "The longest telegram therefore comprises 255 bytes overall" - 4 header + 249 + FCS + ED = 255.
// LEr repeats LE. A PDU of 247 would make LE 250, past the range, so no telegram can carry it.
void test_sd2_length_field_across_the_range(void)
{
    static uint8_t data[246];
    for (size_t i = 0; i < sizeof(data); i++)
    {
        data[i] = (uint8_t)(i * 3 + 1);
    }
    uint8_t out[300];

    for (size_t dl = 1; dl <= sizeof(data); dl++)
    {
        Profibus.build_sd2_args.da = 0x03;
        Profibus.build_sd2_args.sa = 0x02;
        Profibus.build_sd2_args.fc = 0x6C;
        Profibus.build_sd2_args.data = data;
        Profibus.build_sd2_args.data_len = dl;
        Profibus.build_sd2_args.out = out;
        Profibus.build_sd2_args.cap = sizeof(out);
        Profibus.build_sd2(profibus_work);
        size_t n = Profibus.n;
        TEST_ASSERT_EQUAL_UINT(9u + dl, n);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(3 + dl), out[1]);
        TEST_ASSERT_EQUAL_HEX8(out[1], out[2]);
        TEST_ASSERT_EQUAL_HEX8(PB_SD2, out[3]);

        PbTelegram t;
        Profibus.parse_args.frame = out;
        Profibus.parse_args.len = n;
        Profibus.parse_args.out = &t;
        Profibus.parse(profibus_work);
        TEST_ASSERT_TRUE(Profibus.ok);
        TEST_ASSERT_EQUAL_UINT(dl, t.data_len);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(data, t.data, dl);
    }

    Profibus.build_sd2_args.da = 0x03;
    Profibus.build_sd2_args.sa = 0x02;
    Profibus.build_sd2_args.fc = 0x6C;
    Profibus.build_sd2_args.data = data;
    Profibus.build_sd2_args.data_len = 246;
    Profibus.build_sd2_args.out = out;
    Profibus.build_sd2_args.cap = sizeof(out);
    Profibus.build_sd2(profibus_work);
    TEST_ASSERT_EQUAL_UINT(255u, Profibus.n);
    Profibus.build_sd2_args.da = 0x03;
    Profibus.build_sd2_args.sa = 0x02;
    Profibus.build_sd2_args.fc = 0x6C;
    Profibus.build_sd2_args.data = data;
    Profibus.build_sd2_args.data_len = 247;
    Profibus.build_sd2_args.out = out;
    Profibus.build_sd2_args.cap = sizeof(out);
    Profibus.build_sd2(profibus_work);
    TEST_ASSERT_EQUAL_UINT(0u, Profibus.n);
}

// [LI]: "No value <4 is allowed, because a telegram must comprise at least one DA, SA, FC and a data
// byte." An SD2 with an empty data unit has LE = 3, so it must neither be built nor accepted; SD1 is
// the format [TF] gives for a telegram without a data field.
void test_sd2_length_field_minimum_is_four(void)
{
    uint8_t out[16];
    Profibus.build_sd2_args.da = 0x7F;
    Profibus.build_sd2_args.sa = 0x02;
    Profibus.build_sd2_args.fc = 0x49;
    Profibus.build_sd2_args.data = NULL;
    Profibus.build_sd2_args.data_len = 0;
    Profibus.build_sd2_args.out = out;
    Profibus.build_sd2_args.cap = sizeof(out);
    Profibus.build_sd2(profibus_work);
    TEST_ASSERT_EQUAL_UINT(0u, Profibus.n);

    // SD2 LE=3 LEr=3 SD2 DA=0x7F SA=0x02 FC=0x49, FCS = 127 + 2 + 73 = 202 = 0xCA, ED.
    static const uint8_t LE_THREE[9] = {0x68, 0x03, 0x03, 0x68, 0x7F, 0x02, 0x49, 0xCA, 0x16};
    PbTelegram t;
    Profibus.parse_args.frame = LE_THREE;
    Profibus.parse_args.len = sizeof(LE_THREE);
    Profibus.parse_args.out = &t;
    Profibus.parse(profibus_work);
    TEST_ASSERT_FALSE(Profibus.ok);
}

// Structural refusals that do not depend on the FCS: LEr must repeat LE, the SD2 octet is repeated
// after LEr, and the parser handles only the three data telegrams. SD4 is the token telegram, whose
// format [TF] is SD4 DA SA with no FC, no FCS and no ED, and 0x11 is not a delimiter at all.
void test_parse_refuses_a_malformed_frame(void)
{
    PbTelegram t;
    uint8_t f[16];
    static const uint8_t SD2[13] = {0x68, 0x07, 0x07, 0x68, 0x05, 0x02, 0x6C, 0x11, 0x22, 0x33, 0x44, 0x1D, 0x16};

    memcpy(f, SD2, sizeof(SD2));
    f[2] = 0x08; // LEr no longer repeats LE
    Profibus.parse_args.frame = f;
    Profibus.parse_args.len = sizeof(SD2);
    Profibus.parse_args.out = &t;
    Profibus.parse(profibus_work);
    TEST_ASSERT_FALSE(Profibus.ok);

    memcpy(f, SD2, sizeof(SD2));
    f[3] = 0x10; // the repeated start delimiter is not SD2
    Profibus.parse_args.frame = f;
    Profibus.parse_args.len = sizeof(SD2);
    Profibus.parse_args.out = &t;
    Profibus.parse(profibus_work);
    TEST_ASSERT_FALSE(Profibus.ok);

    static const uint8_t SD4[6] = {0xDC, 0x03, 0x02, 0x00, 0x00, 0x16};
    Profibus.parse_args.frame = SD4;
    Profibus.parse_args.len = sizeof(SD4);
    Profibus.parse_args.out = &t;
    Profibus.parse(profibus_work);
    TEST_ASSERT_FALSE(Profibus.ok);

    static const uint8_t UNKNOWN[6] = {0x11, 0x03, 0x02, 0x49, 0x4E, 0x16};
    Profibus.parse_args.frame = UNKNOWN;
    Profibus.parse_args.len = sizeof(UNKNOWN);
    Profibus.parse_args.out = &t;
    Profibus.parse(profibus_work);
    TEST_ASSERT_FALSE(Profibus.ok);
}

// A telegram is only a telegram once its end delimiter has arrived, so every prefix is refused.
void test_parse_refuses_a_truncated_telegram(void)
{
    PbTelegram t;
    static const uint8_t SD2[13] = {0x68, 0x07, 0x07, 0x68, 0x05, 0x02, 0x6C, 0x11, 0x22, 0x33, 0x44, 0x1D, 0x16};
    for (size_t n = 0; n < sizeof(SD2); n++)
    {
        Profibus.parse_args.frame = SD2;
        Profibus.parse_args.len = n;
        Profibus.parse_args.out = &t;
        Profibus.parse(profibus_work);
        TEST_ASSERT_FALSE(Profibus.ok);
    }
    static const uint8_t SD3[14] = {0xA2, 0x05, 0x02, 0x7D, 1, 2, 3, 4, 5, 6, 7, 8, 0xA8, 0x16};
    for (size_t n = 0; n < sizeof(SD3); n++)
    {
        Profibus.parse_args.frame = SD3;
        Profibus.parse_args.len = n;
        Profibus.parse_args.out = &t;
        Profibus.parse(profibus_work);
        TEST_ASSERT_FALSE(Profibus.ok);
    }
    Profibus.parse_args.frame = NULL;
    Profibus.parse_args.len = 6;
    Profibus.parse_args.out = &t;
    Profibus.parse(profibus_work);
    TEST_ASSERT_FALSE(Profibus.ok);
    Profibus.parse_args.frame = SD2;
    Profibus.parse_args.len = sizeof(SD2);
    Profibus.parse_args.out = NULL;
    Profibus.parse(profibus_work);
    TEST_ASSERT_FALSE(Profibus.ok);
}

// The three formats [TF] are 6, 9 + PDU and 14 octets long, so one octet short of each writes
// nothing rather than a headless telegram.
void test_builders_refuse_a_short_buffer(void)
{
    uint8_t out[32];
    static const uint8_t EIGHT[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    static const uint8_t PDU[4] = {0x11, 0x22, 0x33, 0x44};

    for (size_t cap = 0; cap < 6; cap++)
    {
        Profibus.build_sd1_args.da = 0x03;
        Profibus.build_sd1_args.sa = 0x02;
        Profibus.build_sd1_args.fc = 0x49;
        Profibus.build_sd1_args.out = out;
        Profibus.build_sd1_args.cap = cap;
        Profibus.build_sd1(profibus_work);
        TEST_ASSERT_EQUAL_UINT(0u, Profibus.n);
    }
    for (size_t cap = 0; cap < 13; cap++)
    {
        Profibus.build_sd2_args.da = 0x05;
        Profibus.build_sd2_args.sa = 0x02;
        Profibus.build_sd2_args.fc = 0x6C;
        Profibus.build_sd2_args.data = PDU;
        Profibus.build_sd2_args.data_len = sizeof(PDU);
        Profibus.build_sd2_args.out = out;
        Profibus.build_sd2_args.cap = cap;
        Profibus.build_sd2(profibus_work);
        TEST_ASSERT_EQUAL_UINT(0u, Profibus.n);
    }
    for (size_t cap = 0; cap < 14; cap++)
    {
        Profibus.build_sd3_args.da = 0x05;
        Profibus.build_sd3_args.sa = 0x02;
        Profibus.build_sd3_args.fc = 0x7D;
        Profibus.build_sd3_args.data = EIGHT;
        Profibus.build_sd3_args.out = out;
        Profibus.build_sd3_args.cap = cap;
        Profibus.build_sd3(profibus_work);
        TEST_ASSERT_EQUAL_UINT(0u, Profibus.n);
    }

    Profibus.build_sd1_args.da = 0x03;
    Profibus.build_sd1_args.sa = 0x02;
    Profibus.build_sd1_args.fc = 0x49;
    Profibus.build_sd1_args.out = NULL;
    Profibus.build_sd1_args.cap = sizeof(out);
    Profibus.build_sd1(profibus_work);
    TEST_ASSERT_EQUAL_UINT(0u, Profibus.n);
    Profibus.build_sd2_args.da = 0x05;
    Profibus.build_sd2_args.sa = 0x02;
    Profibus.build_sd2_args.fc = 0x6C;
    Profibus.build_sd2_args.data = NULL;
    Profibus.build_sd2_args.data_len = 4;
    Profibus.build_sd2_args.out = out;
    Profibus.build_sd2_args.cap = sizeof(out);
    Profibus.build_sd2(profibus_work);
    TEST_ASSERT_EQUAL_UINT(0u, Profibus.n);
    Profibus.build_sd3_args.da = 0x05;
    Profibus.build_sd3_args.sa = 0x02;
    Profibus.build_sd3_args.fc = 0x7D;
    Profibus.build_sd3_args.data = NULL;
    Profibus.build_sd3_args.out = out;
    Profibus.build_sd3_args.cap = sizeof(out);
    Profibus.build_sd3(profibus_work);
    TEST_ASSERT_EQUAL_UINT(0u, Profibus.n);
}

// DA and SA are single octets [TF], so every one of the 256 values must survive a build and parse
// unchanged, whatever FCS it produces.
void test_address_octets_round_trip(void)
{
    uint8_t out[16];
    PbTelegram t;
    for (unsigned da = 0; da < 256; da++)
    {
        Profibus.build_sd1_args.da = (uint8_t)da;
        Profibus.build_sd1_args.sa = (uint8_t)(255 - da);
        Profibus.build_sd1_args.fc = 0x6C;
        Profibus.build_sd1_args.out = out;
        Profibus.build_sd1_args.cap = sizeof(out);
        Profibus.build_sd1(profibus_work);
        size_t n = Profibus.n;
        TEST_ASSERT_EQUAL_UINT(6u, n);
        Profibus.parse_args.frame = out;
        Profibus.parse_args.len = n;
        Profibus.parse_args.out = &t;
        Profibus.parse(profibus_work);
        TEST_ASSERT_TRUE(Profibus.ok);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)da, t.da);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(255 - da), t.sa);
    }
}
