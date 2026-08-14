// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the EnOcean ESP3 serial codec (services/radio/enocean/enocean.h).
//
// test_esp3_crc8_published_table is the load-bearing case. The EnOcean Serial Protocol 3
// specification section 3.3 states the generator G(x) = x^8 + x^2 + x^1 + x^0 and prints the
// u8CRC8Table[256] it drives, whose entry at index i is the CRC-8 of the single octet i. That
// parameterization is the CRC catalogue's CRC-8/SMBUS (width 8, poly 0x07, init 0x00, no
// reflection, no final XOR), whose published check value over the ASCII string "123456789" is
// 0xF4. Both anchors are checked here: a wrong polynomial, a reflected variant or a nonzero init
// would pass a build-then-parse round trip and still reject every telegram a real gateway sends.
//
// ESP3 and the ERP1 telegram layout are EnOcean's own specifications, not IETF documents. The
// telegram in test_esp3_published_crc_octets carries its CRC octets derived by hand from the
// spec's own table, with the lookup chain written out.

#include "services/radio/enocean/enocean.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// ESP3 section 3.3 prints u8CRC8Table[256] verbatim. Entry i is the CRC-8 of the one octet i, so
// each pair below is a value the specification publishes.
void test_esp3_crc8_published_table(void)
{
    struct
    {
        uint8_t octet;
        uint8_t crc;
    } static const TABLE[] = {
        {0x00, 0x00}, {0x01, 0x07}, {0x02, 0x0E}, {0x03, 0x09}, {0x04, 0x1C}, {0x07, 0x15},
        {0x08, 0x38}, {0x0F, 0x2D}, {0x10, 0x70}, {0x14, 0x6C}, {0x20, 0xE0}, {0x40, 0xC7},
        {0x55, 0xAC}, {0x7A, 0x61}, {0x80, 0x89}, {0xF6, 0xCC}, {0xFF, 0xF3},
    };
    for (size_t i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); i++)
    {
        const uint8_t one = TABLE[i].octet;
        TEST_ASSERT_EQUAL_HEX8(TABLE[i].crc, protocore_esp3_crc8(&one, 1));
    }

    // The CRC catalogue's published check value for CRC-8/SMBUS: the CRC of the nine ASCII
    // characters "123456789" is 0xF4.
    TEST_ASSERT_EQUAL_HEX8(0xF4, protocore_esp3_crc8((const uint8_t *)"123456789", 9));

    // An empty message leaves the register at its init value, 0x00.
    TEST_ASSERT_EQUAL_HEX8(0x00, protocore_esp3_crc8((const uint8_t *)"", 0));
}

// ESP3 section 1.7: SYNC 0x55, then the 4-octet header (DATA length big-endian, OPTIONAL_DATA
// length, PACKET type), CRC8H over those four, then DATA, OPTIONAL_DATA and CRC8D over both.
//
// The smallest RADIO_ERP1-typed telegram, with its CRC octets taken through the spec's own table
// T (crc starts at 0, crc = T[crc XOR octet] per octet):
//
//   header {0x00, 0x01, 0x00, 0x01}
//     T[0x00 ^ 0x00] = T[0x00] = 0x00
//     T[0x00 ^ 0x01] = T[0x01] = 0x07
//     T[0x07 ^ 0x00] = T[0x07] = 0x15
//     T[0x15 ^ 0x01] = T[0x14] = 0x6C   -> CRC8H
//   data {0x00}
//     T[0x00 ^ 0x00] = T[0x00] = 0x00   -> CRC8D
void test_esp3_published_crc_octets(void)
{
    static const uint8_t DATA[1] = {0x00};
    static const uint8_t WANT[] = {0x55, 0x00, 0x01, 0x00, 0x01, 0x6C, 0x00, 0x00};
    uint8_t out[32];
    const uint16_t n = protocore_esp3_build(ESP3_RADIO_ERP1, DATA, 1, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT16(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));
}

// The field offsets a gateway reads: sync, DATA length big-endian, OPTIONAL_DATA length, type,
// CRC8H over the four header octets, then the two payload regions and CRC8D over both of them.
void test_esp3_telegram_field_offsets(void)
{
    static const uint8_t DATA[7] = {0xF6, 0x50, 0x00, 0x29, 0x26, 0x8C, 0x30};
    static const uint8_t OPT[3] = {0x01, 0xFF, 0xFF};
    uint8_t out[64];
    const uint16_t n = protocore_esp3_build(ESP3_RADIO_ERP1, DATA, sizeof(DATA), OPT, sizeof(OPT), out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT16(6 + 7 + 3 + 1, n);

    TEST_ASSERT_EQUAL_HEX8(ESP3_SYNC, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[1]); // DATA length, high octet first
    TEST_ASSERT_EQUAL_HEX8(0x07, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x03, out[3]); // OPTIONAL_DATA length
    TEST_ASSERT_EQUAL_HEX8(0x01, out[4]); // PACKET type RADIO_ERP1
    TEST_ASSERT_EQUAL_HEX8(protocore_esp3_crc8(&out[1], 4), out[5]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, out + 6, sizeof(DATA));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(OPT, out + 6 + sizeof(DATA), sizeof(OPT));
    TEST_ASSERT_EQUAL_HEX8(protocore_esp3_crc8(&out[6], 10), out[16]);
}

// Building a telegram then framing it back yields the same type and the same two regions, and the
// aliased pointers land inside the caller's own octets.
void test_esp3_build_parse_round_trip(void)
{
    static const uint8_t DATA[7] = {0xF6, 0x50, 0x00, 0x29, 0x26, 0x8C, 0x30};
    static const uint8_t OPT[2] = {0x01, 0x02};
    uint8_t wire[64];
    const uint16_t n = protocore_esp3_build(ESP3_RADIO_ERP1, DATA, sizeof(DATA), OPT, sizeof(OPT), wire, sizeof(wire));

    protocore_esp3_packet p;
    memset(&p, 0, sizeof(p));
    TEST_ASSERT_EQUAL_INT((int)n, protocore_esp3_parse(wire, n, &p));
    TEST_ASSERT_EQUAL_HEX8(ESP3_RADIO_ERP1, p.type);
    TEST_ASSERT_EQUAL_UINT16(sizeof(DATA), p.data_len);
    TEST_ASSERT_EQUAL_UINT8(sizeof(OPT), p.opt_len);
    TEST_ASSERT_EQUAL_PTR(wire + 6, p.data);
    TEST_ASSERT_EQUAL_PTR(wire + 6 + sizeof(DATA), p.opt);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, p.data, sizeof(DATA));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(OPT, p.opt, sizeof(OPT));

    // Every ESP3 packet type frames the same way; only the type octet differs.
    static const protocore_esp3_type TYPES[] = {ESP3_RADIO_ERP1,     ESP3_RESPONSE,   ESP3_RADIO_SUB_TEL,
                                                ESP3_EVENT,          ESP3_COMMON_COMMAND, ESP3_SMART_ACK,
                                                ESP3_REMOTE_MAN,     ESP3_RADIO_ERP2};
    for (size_t i = 0; i < sizeof(TYPES) / sizeof(TYPES[0]); i++)
    {
        const uint16_t m = protocore_esp3_build(TYPES[i], DATA, 1, NULL, 0, wire, sizeof(wire));
        TEST_ASSERT_EQUAL_INT((int)m, protocore_esp3_parse(wire, m, &p));
        TEST_ASSERT_EQUAL_HEX8((uint8_t)TYPES[i], (uint8_t)p.type);
    }
}

// A telegram arrives one UART octet at a time, so the frame call reports "not yet" for every
// prefix and the whole length only once the last octet is in.
void test_esp3_parse_waits_for_the_whole_telegram(void)
{
    static const uint8_t DATA[4] = {0xA5, 0x01, 0x02, 0x03};
    uint8_t wire[32];
    protocore_esp3_packet p;
    const uint16_t n = protocore_esp3_build(ESP3_RADIO_ERP1, DATA, sizeof(DATA), NULL, 0, wire, sizeof(wire));

    for (uint16_t k = 1; k < n; k++)
    {
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, protocore_esp3_parse(wire, k, &p), "partial telegram");
    }
    TEST_ASSERT_EQUAL_INT((int)n, protocore_esp3_parse(wire, n, &p));

    // Trailing octets past the telegram are left for the next frame call.
    wire[n] = 0x55;
    TEST_ASSERT_EQUAL_INT((int)n, protocore_esp3_parse(wire, (uint16_t)(n + 1), &p));

    TEST_ASSERT_EQUAL_INT(0, protocore_esp3_parse(NULL, 10, &p));
    TEST_ASSERT_EQUAL_INT(0, protocore_esp3_parse(wire, 0, &p));
}

// ESP3 section 1.6: a 0x55 whose header CRC does not check is not a SYNC BYTE, so the decoder
// drops one octet and looks for the next 0x55. Each corruption below reports that, and the
// telegram is still found after the junk is stepped over.
void test_esp3_resynchronizes_on_a_bad_frame(void)
{
    static const uint8_t DATA[4] = {0xA5, 0x01, 0x02, 0x03};
    uint8_t stream[64];
    protocore_esp3_packet p;

    // Not a sync octet at all.
    stream[0] = 0x00;
    TEST_ASSERT_EQUAL_INT(-1, protocore_esp3_parse(stream, 1, &p));

    // A false 0x55 whose four header octets are plausible but whose CRC8H is not the 0x6C those
    // octets require, then the real telegram six octets later.
    stream[0] = 0x55;
    stream[1] = 0x00;
    stream[2] = 0x01;
    stream[3] = 0x00;
    stream[4] = 0x01;
    stream[5] = 0x00;
    const uint16_t n = protocore_esp3_build(ESP3_RADIO_ERP1, DATA, sizeof(DATA), NULL, 0, stream + 6, sizeof(stream) - 6);
    TEST_ASSERT_EQUAL_INT(-1, protocore_esp3_parse(stream, (uint16_t)(6 + n), &p));
    TEST_ASSERT_EQUAL_INT((int)n, protocore_esp3_parse(stream + 6, n, &p));

    // A corrupted data octet fails CRC8D, which is what CRC8D is for.
    uint8_t wire[32];
    const uint16_t m = protocore_esp3_build(ESP3_RADIO_ERP1, DATA, sizeof(DATA), NULL, 0, wire, sizeof(wire));
    wire[7] ^= 0x01;
    TEST_ASSERT_EQUAL_INT(-1, protocore_esp3_parse(wire, m, &p));

    // A DATA length past what this build accepts is implausible, so it resynchronizes rather than
    // waiting forever for octets that will never arrive.
    static const uint8_t TOO_LONG[6] = {0x55, 0x02, 0x01, 0x00, 0x01, 0x00}; // DATA length 513
    TEST_ASSERT_EQUAL_INT(-1, protocore_esp3_parse(TOO_LONG, sizeof(TOO_LONG), &p));
}

// A build writes the whole telegram or nothing.
void test_esp3_build_fails_closed(void)
{
    static const uint8_t DATA[4] = {1, 2, 3, 4};
    uint8_t out[16];
    TEST_ASSERT_EQUAL_UINT16(0, protocore_esp3_build(ESP3_RADIO_ERP1, DATA, 4, NULL, 0, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT16(0, protocore_esp3_build(ESP3_RADIO_ERP1, DATA, 4, NULL, 0, out, 10)); // needs 11
    TEST_ASSERT_EQUAL_UINT16(11, protocore_esp3_build(ESP3_RADIO_ERP1, DATA, 4, NULL, 0, out, 11));
    TEST_ASSERT_EQUAL_UINT16(0, protocore_esp3_build(ESP3_RADIO_ERP1, NULL, PROTOCORE_ENOCEAN_MAX_DATA + 1, NULL, 0, out,
                                                     sizeof(out)));
}

// An ERP1 telegram is RORG, a RORG-specific payload, the 4-octet sender id most significant octet
// first, then the status octet - so the payload is always the length less those six.
void test_erp1_field_layout(void)
{
    // RPS (0xF6, rocker switches) carries one payload octet.
    static const uint8_t RPS[7] = {0xF6, 0x50, 0x00, 0x29, 0x26, 0x8C, 0x30};
    protocore_erp1 e;
    TEST_ASSERT_TRUE(protocore_erp1_parse(RPS, sizeof(RPS), &e));
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_ERP_RORG_RPS, e.rorg);
    TEST_ASSERT_EQUAL_UINT8(1, e.payload_len);
    TEST_ASSERT_EQUAL_PTR(RPS + 1, e.payload);
    TEST_ASSERT_EQUAL_HEX8(0x50, e.payload[0]);
    TEST_ASSERT_EQUAL_HEX32(0x0029268Cu, e.sender_id); // big-endian, octets 2..5
    TEST_ASSERT_EQUAL_HEX8(0x30, e.status);

    // 4BS (0xA5, sensors) carries four.
    static const uint8_t FOURBS[10] = {0xA5, 0x08, 0x28, 0x46, 0x0F, 0x01, 0x82, 0x5D, 0x8B, 0x00};
    TEST_ASSERT_TRUE(protocore_erp1_parse(FOURBS, sizeof(FOURBS), &e));
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_ERP_RORG_4BS, e.rorg);
    TEST_ASSERT_EQUAL_UINT8(4, e.payload_len);
    TEST_ASSERT_EQUAL_HEX32(0x01825D8Bu, e.sender_id);
    TEST_ASSERT_EQUAL_HEX8(0x00, e.status);

    // A telegram with no payload at all is still six octets: RORG, sender id, status.
    static const uint8_t BARE[6] = {0xD2, 0x11, 0x22, 0x33, 0x44, 0x20};
    TEST_ASSERT_TRUE(protocore_erp1_parse(BARE, sizeof(BARE), &e));
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_ERP_RORG_VLD, e.rorg);
    TEST_ASSERT_EQUAL_UINT8(0, e.payload_len);
    TEST_ASSERT_NULL(e.payload);
    TEST_ASSERT_EQUAL_HEX32(0x11223344u, e.sender_id);
}

// Build then decode returns every field unchanged, for each payload width the common RORGs use.
void test_erp1_round_trip(void)
{
    static const uint8_t PAYLOAD[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    static const uint8_t RORGS[] = {PROTOCORE_ERP_RORG_RPS, PROTOCORE_ERP_RORG_1BS, PROTOCORE_ERP_RORG_4BS,
                                    PROTOCORE_ERP_RORG_VLD, PROTOCORE_ERP_RORG_MSC, PROTOCORE_ERP_RORG_ADT,
                                    PROTOCORE_ERP_RORG_UTE};
    for (size_t i = 0; i < sizeof(RORGS); i++)
    {
        for (uint8_t plen = 0; plen <= 4; plen++)
        {
            uint8_t out[16];
            const uint16_t n = protocore_erp1_build(out, sizeof(out), RORGS[i], PAYLOAD, plen, 0xFEDCBA98u, 0x30);
            TEST_ASSERT_EQUAL_UINT16((uint16_t)(1 + plen + 5), n);

            protocore_erp1 e;
            TEST_ASSERT_TRUE(protocore_erp1_parse(out, n, &e));
            TEST_ASSERT_EQUAL_HEX8(RORGS[i], e.rorg);
            TEST_ASSERT_EQUAL_UINT8(plen, e.payload_len);
            TEST_ASSERT_EQUAL_HEX32(0xFEDCBA98u, e.sender_id);
            TEST_ASSERT_EQUAL_HEX8(0x30, e.status);
            if (plen)
            {
                TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, e.payload, plen);
            }
        }
    }
}

// Fewer than the six fixed octets is not an ERP1 telegram: the sender id would be read out of
// whatever follows the buffer.
void test_erp1_fails_closed(void)
{
    static const uint8_t FIVE[5] = {0xF6, 0x00, 0x29, 0x26, 0x8C};
    protocore_erp1 e;
    TEST_ASSERT_FALSE(protocore_erp1_parse(FIVE, sizeof(FIVE), &e));
    TEST_ASSERT_FALSE(protocore_erp1_parse(NULL, 6, &e));
    TEST_ASSERT_FALSE(protocore_erp1_parse(FIVE, 6, NULL));

    static const uint8_t PAYLOAD[4] = {1, 2, 3, 4};
    uint8_t out[16];
    TEST_ASSERT_EQUAL_UINT16(0, protocore_erp1_build(NULL, sizeof(out), 0xF6, PAYLOAD, 4, 1, 0));
    TEST_ASSERT_EQUAL_UINT16(0, protocore_erp1_build(out, sizeof(out), 0xF6, NULL, 4, 1, 0)); // length, no payload
    TEST_ASSERT_EQUAL_UINT16(0, protocore_erp1_build(out, 9, 0xF6, PAYLOAD, 4, 1, 0));        // needs 10
    TEST_ASSERT_EQUAL_UINT16(10, protocore_erp1_build(out, 10, 0xF6, PAYLOAD, 4, 1, 0));
}

// An ERP1 telegram is the DATA field of a RADIO_ERP1 packet, so the two layers nest: build the
// radio telegram, wrap it, frame it back out and decode it again.
void test_erp1_inside_an_esp3_packet(void)
{
    static const uint8_t PAYLOAD[1] = {0x50};
    uint8_t erp1[16];
    const uint16_t elen = protocore_erp1_build(erp1, sizeof(erp1), PROTOCORE_ERP_RORG_RPS, PAYLOAD, 1, 0x0029268Cu, 0x30);

    uint8_t wire[64];
    const uint16_t wlen = protocore_esp3_build(ESP3_RADIO_ERP1, erp1, elen, NULL, 0, wire, sizeof(wire));

    protocore_esp3_packet p;
    TEST_ASSERT_EQUAL_INT((int)wlen, protocore_esp3_parse(wire, wlen, &p));
    TEST_ASSERT_EQUAL_HEX8(ESP3_RADIO_ERP1, p.type);

    protocore_erp1 e;
    TEST_ASSERT_TRUE(protocore_erp1_parse(p.data, p.data_len, &e));
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_ERP_RORG_RPS, e.rorg);
    TEST_ASSERT_EQUAL_HEX8(0x50, e.payload[0]);
    TEST_ASSERT_EQUAL_HEX32(0x0029268Cu, e.sender_id);
    TEST_ASSERT_EQUAL_HEX8(0x30, e.status);
}
