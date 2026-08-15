// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the IEEE 1609 WAVE codec (services/transportation/wave/wave.h).
//
// IEEE 1609.2 and 1609.3 are paid IEEE standards and were not obtainable here, so the expectations
// below are derived from the P-encoding rule the module documents - the count of leading 1 bits in
// the first octet gives the total length, and the remaining bits are the high bits of the value -
// with the derivation written beside each vector, plus PROPERTIES the codec must satisfy whatever
// the spec says.
//
// test_psid_round_trip_over_every_accepted_value is the load-bearing one, and it needs no spec at
// all: any PSID the encoder accepts must decode back to itself. A frame whose PSID does not survive
// that is a frame no peer can route, and this is what caught the 28-bit ceiling being unenforced.

#include "services/transportation/wave/wave.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// The four length classes, at both ends of each.
//
//   0xxxxxxx                            1 octet,  0        .. 0x7F
//   10xxxxxx xxxxxxxx                   2 octets, 0x80     .. 0x3FFF
//   110xxxxx xxxxxxxx xxxxxxxx          3 octets, 0x4000   .. 0x1FFFFF
//   1110xxxx xxxxxxxx xxxxxxxx xxxxxxxx 4 octets, 0x200000 .. 0xFFFFFFF
//
// So 0x3FFF is 10 111111 11111111 = BF FF, and 0x1FFFFF is 110 11111 11111111 11111111 = DF FF FF.
void test_psid_p_encoding_boundaries(void)
{
    struct
    {
        uint32_t psid;
        size_t n;
        uint8_t want[4];
    } static const CASES[] = {
        {0x00000000u, 1, {0x00, 0, 0, 0}},          {0x0000007Fu, 1, {0x7F, 0, 0, 0}},
        {0x00000080u, 2, {0x80, 0x80, 0, 0}},       {0x00003FFFu, 2, {0xBF, 0xFF, 0, 0}},
        {0x00004000u, 3, {0xC0, 0x40, 0x00, 0}},    {0x001FFFFFu, 3, {0xDF, 0xFF, 0xFF, 0}},
        {0x00200000u, 4, {0xE0, 0x20, 0x00, 0x00}}, {0x0FFFFFFFu, 4, {0xEF, 0xFF, 0xFF, 0xFF}},
    };

    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        uint8_t out[4];
        TEST_ASSERT_EQUAL_UINT(CASES[i].n, protocore_wave_encode_psid(CASES[i].psid, out, sizeof(out)));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(CASES[i].want, out, CASES[i].n);
    }
}

// Whatever the encoder accepts, the decoder must return unchanged, consuming exactly what was
// written. An accepted value that does not survive is a PSID no receiver can recover.
void test_psid_round_trip_over_every_accepted_value(void)
{
    static const uint32_t PSID[] = {
        0x00000000u, 0x00000001u, 0x00000020u, 0x0000007Fu, 0x00000080u, 0x000000FFu, 0x00003FFFu, 0x00004000u,
        0x00008002u, 0x00008003u, 0x001FFFFFu, 0x00200000u, 0x08000000u, 0x0FFFFFFFu, 0x10000000u, 0xFFFFFFFFu,
    };

    for (size_t i = 0; i < sizeof(PSID) / sizeof(PSID[0]); i++)
    {
        uint8_t buf[4];
        uint32_t got = 0xA5A5A5A5u;
        size_t n = protocore_wave_encode_psid(PSID[i], buf, sizeof(buf));
        if (n == 0)
        {
            continue; // refused as unrepresentable, which is a legal answer
        }
        TEST_ASSERT_EQUAL_UINT_MESSAGE(n, protocore_wave_decode_psid(buf, n, &got), "decode length");
        TEST_ASSERT_EQUAL_HEX32(PSID[i], got);
    }
}

// A truncated encoding, a first octet in no length class, and null arguments are all refused.
void test_psid_decode_refuses_malformed_input(void)
{
    static const uint8_t TWO[2] = {0x80, 0x80};
    static const uint8_t THREE[3] = {0xC0, 0x40, 0x00};
    static const uint8_t FOUR[4] = {0xE0, 0x20, 0x00, 0x00};
    static const uint8_t NO_CLASS[4] = {0xF0, 0x00, 0x00, 0x00};
    uint32_t psid = 0;

    TEST_ASSERT_EQUAL_UINT(0u, protocore_wave_decode_psid(TWO, 1, &psid));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_wave_decode_psid(THREE, 2, &psid));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_wave_decode_psid(FOUR, 3, &psid));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_wave_decode_psid(NO_CLASS, 4, &psid));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_wave_decode_psid(TWO, 0, &psid));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_wave_decode_psid(NULL, 2, &psid));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_wave_decode_psid(TWO, 2, NULL));
}

// An encode into a buffer one octet short of its length class writes nothing.
void test_psid_encode_bounds(void)
{
    uint8_t out[4];
    TEST_ASSERT_EQUAL_UINT(0u, protocore_wave_encode_psid(0x7Fu, out, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_wave_encode_psid(0x80u, out, 1));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_wave_encode_psid(0x4000u, out, 2));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_wave_encode_psid(0x200000u, out, 3));
    TEST_ASSERT_EQUAL_UINT(4u, protocore_wave_encode_psid(0x200000u, out, 4));
}

// A WSMP data frame is the version octet, the P-encoded PSID, a one-octet length, then the payload.
void test_wsmp_frame_layout(void)
{
    static const uint8_t PL[3] = {0x11, 0x22, 0x33};
    static const uint8_t WANT[6] = {WSMP_VERSION, 0x20, 0x03, 0x11, 0x22, 0x33};
    uint8_t out[16];

    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), protocore_wsmp_build(WAVE_PSID_BSM, PL, sizeof(PL), out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));

    // a three-octet PSID pushes the length octet out by two, and nothing else moves
    static const uint8_t WANT_SPAT[8] = {WSMP_VERSION, 0xC0, 0x80, 0x02, 0x03, 0x11, 0x22, 0x33};
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT_SPAT), protocore_wsmp_build(WAVE_PSID_SPAT, PL, sizeof(PL), out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT_SPAT, out, sizeof(WANT_SPAT));
}

// Every PSID width and every payload length round trips through build then parse.
void test_wsmp_round_trip(void)
{
    static const uint32_t PSID[4] = {WAVE_PSID_BSM, 0x00000080u, WAVE_PSID_SPAT, 0x00200000u};
    uint8_t payload[255];
    uint8_t out[300];
    WsmpFrame f;
    for (size_t i = 0; i < sizeof(payload); i++)
    {
        payload[i] = (uint8_t)(i * 7u);
    }

    for (size_t p = 0; p < 4; p++)
    {
        for (size_t len = 0; len <= sizeof(payload); len += 51)
        {
            size_t n = protocore_wsmp_build(PSID[p], len ? payload : NULL, len, out, sizeof(out));
            TEST_ASSERT_TRUE(n > 0);
            TEST_ASSERT_TRUE(protocore_wsmp_parse(out, n, &f));
            TEST_ASSERT_EQUAL_HEX32(PSID[p], f.psid);
            TEST_ASSERT_EQUAL_UINT(len, f.payload_len);
            if (len)
            {
                TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, f.payload, len);
            }
            else
            {
                TEST_ASSERT_NULL(f.payload);
            }
        }
    }
}

// The version lives in the low nibble of the first octet; anything else is not a WSMP frame.
void test_wsmp_parse_checks_the_version(void)
{
    static const uint8_t PL[2] = {0xAA, 0xBB};
    uint8_t out[16];
    WsmpFrame f;
    size_t n = protocore_wsmp_build(WAVE_PSID_BSM, PL, sizeof(PL), out, sizeof(out));

    TEST_ASSERT_TRUE(protocore_wsmp_parse(out, n, &f));

    // the high nibble is a subtype and is not checked, so only the low nibble may fail it
    out[0] = (uint8_t)(0xF0 | WSMP_VERSION);
    TEST_ASSERT_TRUE(protocore_wsmp_parse(out, n, &f));
    out[0] = (uint8_t)(0xF0 | (WSMP_VERSION + 1));
    TEST_ASSERT_FALSE(protocore_wsmp_parse(out, n, &f));
}

// A frame cut short anywhere, or one whose declared length runs past the octets present, is refused
// rather than handing a caller a payload pointer into whatever follows the buffer.
void test_wsmp_parse_refuses_truncation(void)
{
    static const uint8_t PL[4] = {1, 2, 3, 4};
    uint8_t out[16];
    WsmpFrame f;
    size_t n = protocore_wsmp_build(WAVE_PSID_SPAT, PL, sizeof(PL), out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(9u, n); // version + 3-octet PSID + length + 4 payload

    for (size_t shorter = 0; shorter < n; shorter++)
    {
        TEST_ASSERT_FALSE(protocore_wsmp_parse(out, shorter, &f));
    }
    TEST_ASSERT_FALSE(protocore_wsmp_parse(NULL, n, &f));
    TEST_ASSERT_FALSE(protocore_wsmp_parse(out, n, NULL));

    // a declared length longer than what is present
    out[4] = 5;
    TEST_ASSERT_FALSE(protocore_wsmp_parse(out, n, &f));
}

// The WSM length is one octet, so a payload past 255 is refused rather than wrapping to a short one.
void test_wsmp_payload_length_is_one_octet(void)
{
    uint8_t payload[256];
    uint8_t out[300];
    memset(payload, 0x5A, sizeof(payload));

    TEST_ASSERT_TRUE(protocore_wsmp_build(WAVE_PSID_BSM, payload, 255, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_wsmp_build(WAVE_PSID_BSM, payload, 256, out, sizeof(out)));
}

// A buffer too small for the header or the payload produces nothing.
void test_wsmp_build_bounds(void)
{
    static const uint8_t PL[4] = {1, 2, 3, 4};
    uint8_t out[16];

    TEST_ASSERT_EQUAL_UINT(0u, protocore_wsmp_build(WAVE_PSID_BSM, PL, sizeof(PL), out, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_wsmp_build(WAVE_PSID_BSM, PL, sizeof(PL), out, 6)); // needs 7
    TEST_ASSERT_EQUAL_UINT(7u, protocore_wsmp_build(WAVE_PSID_BSM, PL, sizeof(PL), out, 7));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_wsmp_build(WAVE_PSID_BSM, PL, sizeof(PL), NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_wsmp_build(WAVE_PSID_BSM, NULL, 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(3u, protocore_wsmp_build(WAVE_PSID_BSM, NULL, 0, out, sizeof(out)));
}

// The 1609.2 envelope is protocolVersion then contentType, then the payload it wraps.
void test_1609dot2_envelope(void)
{
    static const uint8_t PL[3] = {0xCA, 0xFE, 0x01};
    static const uint8_t WANT[5] = {WAVE_16092_VERSION, WAVE_16092_SIGNED, 0xCA, 0xFE, 0x01};
    uint8_t out[16];

    TEST_ASSERT_EQUAL_UINT(sizeof(WANT),
                           protocore_wave_1609dot2_wrap(WAVE_16092_SIGNED, PL, sizeof(PL), out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));

    // the unsecured content type differs, so a peer can tell a signed frame from an unsigned one
    TEST_ASSERT_EQUAL_UINT(5u, protocore_wave_1609dot2_wrap(WAVE_16092_UNSECURED, PL, sizeof(PL), out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(WAVE_16092_UNSECURED, out[1]);
    TEST_ASSERT_NOT_EQUAL(WAVE_16092_UNSECURED, WAVE_16092_SIGNED);

    // an empty payload is the bare two-octet header
    TEST_ASSERT_EQUAL_UINT(2u, protocore_wave_1609dot2_wrap(WAVE_16092_UNSECURED, NULL, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(WAVE_16092_VERSION, out[0]);
}

// The envelope refuses a buffer that cannot hold header plus payload, and a null payload above
// length zero.
void test_1609dot2_bounds(void)
{
    static const uint8_t PL[3] = {1, 2, 3};
    uint8_t out[16];

    TEST_ASSERT_EQUAL_UINT(0u, protocore_wave_1609dot2_wrap(WAVE_16092_SIGNED, PL, sizeof(PL), out, 4)); // needs 5
    TEST_ASSERT_EQUAL_UINT(5u, protocore_wave_1609dot2_wrap(WAVE_16092_SIGNED, PL, sizeof(PL), out, 5));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_wave_1609dot2_wrap(WAVE_16092_SIGNED, PL, sizeof(PL), NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_wave_1609dot2_wrap(WAVE_16092_SIGNED, NULL, 3, out, sizeof(out)));
}

// A 1609.2-wrapped payload carried inside a WSMP frame, which is how a J2735 message actually
// reaches the radio: the envelope must come back out of the frame byte for byte.
void test_wsmp_carries_a_1609dot2_envelope(void)
{
    static const uint8_t BSM[4] = {0x01, 0x4B, 0x4B, 0x4B};
    uint8_t secured[16];
    uint8_t frame[32];
    WsmpFrame f;

    size_t s = protocore_wave_1609dot2_wrap(WAVE_16092_SIGNED, BSM, sizeof(BSM), secured, sizeof(secured));
    TEST_ASSERT_EQUAL_UINT(6u, s);

    size_t n = protocore_wsmp_build(WAVE_PSID_BSM, secured, s, frame, sizeof(frame));
    TEST_ASSERT_EQUAL_UINT(3u + s, n);
    TEST_ASSERT_TRUE(protocore_wsmp_parse(frame, n, &f));
    TEST_ASSERT_EQUAL_HEX32(WAVE_PSID_BSM, f.psid);
    TEST_ASSERT_EQUAL_UINT(s, f.payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(secured, f.payload, s);
    TEST_ASSERT_EQUAL_HEX8(WAVE_16092_VERSION, f.payload[0]);
    TEST_ASSERT_EQUAL_HEX8(WAVE_16092_SIGNED, f.payload[1]);
}
