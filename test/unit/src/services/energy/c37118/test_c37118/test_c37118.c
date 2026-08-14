// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the IEEE C37.118.2 synchrophasor frame codec (services/energy/c37118/c37118.h).
//
// The load-bearing case is test_crc_ccitt_published_check_value. C37.118.2 checks every frame with
// CRC-CCITT (poly 0x1021, init 0xFFFF, unreflected, no final mask), whose catalogued check value -
// the CRC of the nine ASCII octets "123456789" - is 0x29B1. Pinning that one number is what makes
// every CHK assertion below meaningful: a frame whose CHK is merely self-consistent proves nothing
// to a PDC on the other end of the wire, and no wrong polynomial reproduces 0x29B1 by accident.
//
// The frame field positions come from the standard's frame layout: SYNC(2) FRAMESIZE(2) IDCODE(2)
// SOC(4) FRACSEC(4) DATA(n) CHK(2), every multi-octet field in network order, with SYNC byte 0 =
// 0xAA and byte 1 = (type << 4) | version. The STAT bit assignments are Table 6 of C37.118.2-2011.

#include "services/energy/c37118/c37118.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// The catalogued check value for CRC-16/IBM-3740, the parameterization C37.118 calls CRC-CCITT.
void test_crc_ccitt_published_check_value(void)
{
    static const uint8_t CHECK[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX16(0x29B1u, protocore_c37118_crc(CHECK, sizeof(CHECK)));
    // init 0xFFFF, so the empty message is the initial register, not 0.
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, protocore_c37118_crc(CHECK, 0));
}

// A Command frame is the shortest complete frame the standard defines: the 14-octet header, a
// 2-octet DATA word, and the CHK. Every octet position below is read off the frame layout.
void test_command_frame_field_layout(void)
{
    uint8_t buf[64];
    size_t n = protocore_c37118_build_command(buf, sizeof(buf), 0x0007u, 0x11223344u, 0x00A0B0C0u, C37118_CMD_DATA_ON);

    TEST_ASSERT_EQUAL_UINT(18u, n); // C37118_MIN_FRAME (16) + the 2-octet command word
    TEST_ASSERT_EQUAL_HEX8(0xAAu, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x42u, buf[1]); // (C37118_TYPE_CMD << 4) | C37118_VERSION_2011
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[2]); // FRAMESIZE = 18, big-endian
    TEST_ASSERT_EQUAL_HEX8(0x12u, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[4]); // IDCODE
    TEST_ASSERT_EQUAL_HEX8(0x07u, buf[5]);
    TEST_ASSERT_EQUAL_HEX8(0x11u, buf[6]); // SOC
    TEST_ASSERT_EQUAL_HEX8(0x22u, buf[7]);
    TEST_ASSERT_EQUAL_HEX8(0x33u, buf[8]);
    TEST_ASSERT_EQUAL_HEX8(0x44u, buf[9]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[10]); // FRACSEC
    TEST_ASSERT_EQUAL_HEX8(0xA0u, buf[11]);
    TEST_ASSERT_EQUAL_HEX8(0xB0u, buf[12]);
    TEST_ASSERT_EQUAL_HEX8(0xC0u, buf[13]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[14]); // DATA: the command word
    TEST_ASSERT_EQUAL_HEX8(0x02u, buf[15]);

    // CHK covers every octet up to but excluding itself, and is written big-endian.
    uint16_t chk = protocore_c37118_crc(buf, 16);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(chk >> 8), buf[16]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)chk, buf[17]);
}

// Build then parse returns every header field unchanged, for each frame type the SYNC nibble names.
void test_frame_round_trip(void)
{
    static const uint8_t TYPES[] = {C37118_TYPE_DATA, C37118_TYPE_HEADER, C37118_TYPE_CFG1,
                                    C37118_TYPE_CFG2, C37118_TYPE_CMD,    C37118_TYPE_CFG3};
    static const uint8_t PAYLOAD[5] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01};

    for (size_t i = 0; i < sizeof(TYPES) / sizeof(TYPES[0]); i++)
    {
        uint8_t buf[64];
        C37118Frame f;
        size_t n = protocore_c37118_build_frame(buf, sizeof(buf), TYPES[i], C37118_VERSION_2011, 0xBEEFu, 0x5F5E1000u,
                                                0x00FFFFFFu, PAYLOAD, sizeof(PAYLOAD));
        TEST_ASSERT_EQUAL_UINT(16u + sizeof(PAYLOAD), n);
        TEST_ASSERT_TRUE(protocore_c37118_parse_frame(buf, n, &f));
        TEST_ASSERT_EQUAL_UINT8(TYPES[i], f.type);
        TEST_ASSERT_EQUAL_UINT8(C37118_VERSION_2011, f.version);
        TEST_ASSERT_EQUAL_UINT16(n, f.framesize);
        TEST_ASSERT_EQUAL_HEX16(0xBEEFu, f.idcode);
        TEST_ASSERT_EQUAL_HEX32(0x5F5E1000u, f.soc);
        TEST_ASSERT_EQUAL_HEX32(0x00FFFFFFu, f.fracsec);
        TEST_ASSERT_EQUAL_UINT(sizeof(PAYLOAD), f.data_len);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(PAYLOAD, f.data, sizeof(PAYLOAD));
    }

    // The version nibble is carried independently of the type field.
    uint8_t buf[32];
    C37118Frame f;
    size_t n = protocore_c37118_build_frame(buf, sizeof(buf), C37118_TYPE_DATA, C37118_VERSION_2005, 1u, 0u, 0u, NULL, 0);
    TEST_ASSERT_EQUAL_UINT(16u, n);
    TEST_ASSERT_TRUE(protocore_c37118_parse_frame(buf, n, &f));
    TEST_ASSERT_EQUAL_UINT8(C37118_VERSION_2005, f.version);
    TEST_ASSERT_EQUAL_UINT(0u, f.data_len);
}

// Every command code the standard assigns survives the trip; the parse refuses a non-Command frame.
void test_command_word_round_trip(void)
{
    static const uint16_t CMDS[] = {C37118_CMD_DATA_OFF,  C37118_CMD_DATA_ON,   C37118_CMD_SEND_HDR,
                                    C37118_CMD_SEND_CFG1, C37118_CMD_SEND_CFG2, C37118_CMD_SEND_CFG3};
    for (size_t i = 0; i < sizeof(CMDS) / sizeof(CMDS[0]); i++)
    {
        uint8_t buf[32];
        C37118Frame f;
        uint16_t got = 0;
        size_t n = protocore_c37118_build_command(buf, sizeof(buf), 42u, 0u, 0u, CMDS[i]);
        TEST_ASSERT_TRUE(protocore_c37118_parse_frame(buf, n, &f));
        TEST_ASSERT_EQUAL_UINT8(C37118_TYPE_CMD, f.type);
        TEST_ASSERT_TRUE(protocore_c37118_parse_command(&f, &got));
        TEST_ASSERT_EQUAL_HEX16(CMDS[i], got);
    }

    uint8_t data[32];
    C37118Frame df;
    uint16_t cmd = 0;
    size_t n = protocore_c37118_build_frame(data, sizeof(data), C37118_TYPE_DATA, C37118_VERSION_2011, 1u, 0u, 0u,
                                            (const uint8_t *)"\x00\x02", 2);
    TEST_ASSERT_TRUE(protocore_c37118_parse_frame(data, n, &df));
    TEST_ASSERT_FALSE(protocore_c37118_parse_command(&df, &cmd));
}

// A single flipped bit anywhere in the frame fails the CHK, which is the whole point of carrying it.
void test_parse_rejects_a_corrupted_frame(void)
{
    uint8_t buf[32];
    C37118Frame f;
    size_t n = protocore_c37118_build_command(buf, sizeof(buf), 7u, 0x11223344u, 0u, C37118_CMD_DATA_ON);
    TEST_ASSERT_TRUE(protocore_c37118_parse_frame(buf, n, &f));

    for (size_t i = 0; i < n; i++)
    {
        uint8_t saved = buf[i];
        buf[i] = (uint8_t)(saved ^ 0x01u);
        if (i == 2 || i == 3)
        {
            buf[i] = saved; // FRAMESIZE is length, not payload: a flip there is a truncation case
            continue;
        }
        TEST_ASSERT_FALSE_MESSAGE(protocore_c37118_parse_frame(buf, n, &f), "flipped octet parsed as valid");
        buf[i] = saved;
    }
    TEST_ASSERT_TRUE(protocore_c37118_parse_frame(buf, n, &f)); // restored
}

// A frame is refused before its CHK is even reached when the SYNC leader, the buffered length, or the
// declared FRAMESIZE cannot describe a complete frame.
void test_parse_rejects_malformed_framing(void)
{
    uint8_t buf[32];
    C37118Frame f;
    size_t n = protocore_c37118_build_command(buf, sizeof(buf), 7u, 0u, 0u, C37118_CMD_DATA_ON);

    TEST_ASSERT_FALSE(protocore_c37118_parse_frame(buf, n - 1, &f)); // not fully buffered
    TEST_ASSERT_FALSE(protocore_c37118_parse_frame(buf, C37118_MIN_FRAME - 1, &f));
    TEST_ASSERT_FALSE(protocore_c37118_parse_frame(NULL, n, &f));
    TEST_ASSERT_FALSE(protocore_c37118_parse_frame(buf, n, NULL));

    uint8_t bad_sync = buf[0];
    buf[0] = 0xAB;
    TEST_ASSERT_FALSE(protocore_c37118_parse_frame(buf, n, &f));
    buf[0] = bad_sync;

    buf[2] = 0x00; // FRAMESIZE below the 16-octet minimum
    buf[3] = 0x0Fu;
    TEST_ASSERT_FALSE(protocore_c37118_parse_frame(buf, n, &f));
    buf[2] = 0xFFu; // FRAMESIZE beyond what is buffered
    buf[3] = 0xFFu;
    TEST_ASSERT_FALSE(protocore_c37118_parse_frame(buf, n, &f));
}

// A buffer that cannot hold the whole frame writes nothing and reports 0.
void test_build_refuses_an_undersized_buffer(void)
{
    uint8_t buf[64];
    TEST_ASSERT_EQUAL_UINT(0u, protocore_c37118_build_frame(buf, C37118_MIN_FRAME - 1, C37118_TYPE_DATA,
                                                            C37118_VERSION_2011, 1u, 0u, 0u, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_c37118_build_command(buf, 17u, 1u, 0u, 0u, C37118_CMD_DATA_ON));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_c37118_build_frame(NULL, sizeof(buf), C37118_TYPE_DATA, C37118_VERSION_2011,
                                                            1u, 0u, 0u, NULL, 0));
    // A payload pointer is required whenever a payload length is given.
    TEST_ASSERT_EQUAL_UINT(0u, protocore_c37118_build_frame(buf, sizeof(buf), C37118_TYPE_DATA, C37118_VERSION_2011, 1u,
                                                            0u, 0u, NULL, 4));
    TEST_ASSERT_EQUAL_UINT(18u, protocore_c37118_build_command(buf, 18u, 1u, 0u, 0u, C37118_CMD_DATA_ON));
}

// --- the DATA frame STAT word (C37.118.2-2011 Table 6) --------------------------------------------

// Build a data frame whose payload is the 16-bit STAT word, then decode it.
static void decode_stat(uint16_t stat, C37118Stat *out)
{
    uint8_t buf[32];
    uint8_t payload[2];
    C37118Frame f;
    payload[0] = (uint8_t)(stat >> 8);
    payload[1] = (uint8_t)stat;
    size_t n = protocore_c37118_build_frame(buf, sizeof(buf), C37118_TYPE_DATA, C37118_VERSION_2011, 1u, 0u, 0u,
                                            payload, sizeof(payload));
    TEST_ASSERT_TRUE(protocore_c37118_parse_frame(buf, n, &f));
    TEST_ASSERT_TRUE(protocore_c37118_decode_stat(&f, out));
    TEST_ASSERT_EQUAL_HEX16(stat, out->raw);
}

// Table 6 assigns bit 15 "data invalid", bit 14 "PMU error", and bit 13 "PMU sync" - so an all-zero
// STAT is the healthy PMU, and each of those three reads inverted from its raw bit.
void test_stat_all_zero_is_a_healthy_pmu(void)
{
    C37118Stat s;
    decode_stat(0x0000u, &s);
    TEST_ASSERT_TRUE(s.data_valid);
    TEST_ASSERT_FALSE(s.pmu_error);
    TEST_ASSERT_TRUE(s.in_sync);
    TEST_ASSERT_FALSE(s.sorted_by_arrival);
    TEST_ASSERT_FALSE(s.trigger);
    TEST_ASSERT_FALSE(s.config_change);
    TEST_ASSERT_FALSE(s.data_modified);
    TEST_ASSERT_EQUAL_UINT8(0u, s.time_quality);
    TEST_ASSERT_EQUAL_UINT8(C37118_UNLOCKED_UNDER_10S, s.unlocked_time);
    TEST_ASSERT_EQUAL_UINT8(C37118_TRIGGER_MANUAL, s.trigger_reason);
}

// Every bit set: the inverted three go false and every field saturates at its width.
void test_stat_all_ones(void)
{
    C37118Stat s;
    decode_stat(0xFFFFu, &s);
    TEST_ASSERT_FALSE(s.data_valid);
    TEST_ASSERT_TRUE(s.pmu_error);
    TEST_ASSERT_FALSE(s.in_sync);
    TEST_ASSERT_TRUE(s.sorted_by_arrival);
    TEST_ASSERT_TRUE(s.trigger);
    TEST_ASSERT_TRUE(s.config_change);
    TEST_ASSERT_TRUE(s.data_modified);
    TEST_ASSERT_EQUAL_UINT8(7u, s.time_quality);       // bits 8-6
    TEST_ASSERT_EQUAL_UINT8(3u, s.unlocked_time);      // bits 5-4
    TEST_ASSERT_EQUAL_UINT8(0x0Fu, s.trigger_reason);  // bits 3-0
}

// One bit at a time: each flag moves alone, so no two share a mask.
void test_stat_flags_are_independent(void)
{
    C37118Stat s;

    decode_stat(0x8000u, &s);
    TEST_ASSERT_FALSE(s.data_valid);
    TEST_ASSERT_FALSE(s.pmu_error);
    TEST_ASSERT_TRUE(s.in_sync);

    decode_stat(0x4000u, &s);
    TEST_ASSERT_TRUE(s.data_valid);
    TEST_ASSERT_TRUE(s.pmu_error);

    decode_stat(0x2000u, &s);
    TEST_ASSERT_FALSE(s.in_sync);
    TEST_ASSERT_TRUE(s.data_valid);

    decode_stat(0x1000u, &s);
    TEST_ASSERT_TRUE(s.sorted_by_arrival);

    decode_stat(0x0800u, &s);
    TEST_ASSERT_TRUE(s.trigger);
    TEST_ASSERT_FALSE(s.config_change);

    decode_stat(0x0400u, &s);
    TEST_ASSERT_TRUE(s.config_change);
    TEST_ASSERT_FALSE(s.trigger);

    decode_stat(0x0200u, &s);
    TEST_ASSERT_TRUE(s.data_modified);
}

// The three multi-bit fields, each read at its own shift: time quality at bit 6, unlocked time at
// bit 4, trigger reason in the low nibble.
void test_stat_multi_bit_fields(void)
{
    C37118Stat s;

    decode_stat((uint16_t)(5u << 6), &s);
    TEST_ASSERT_EQUAL_UINT8(5u, s.time_quality);
    TEST_ASSERT_EQUAL_UINT8(0u, s.unlocked_time);
    TEST_ASSERT_EQUAL_UINT8(0u, s.trigger_reason);

    decode_stat((uint16_t)(C37118_UNLOCKED_100_1000S << 4), &s);
    TEST_ASSERT_EQUAL_UINT8(C37118_UNLOCKED_100_1000S, s.unlocked_time);
    TEST_ASSERT_EQUAL_UINT8(0u, s.time_quality);
    TEST_ASSERT_EQUAL_UINT8(0u, s.trigger_reason);

    decode_stat((uint16_t)C37118_TRIGGER_DFDT, &s);
    TEST_ASSERT_EQUAL_UINT8(C37118_TRIGGER_DFDT, s.trigger_reason);
    TEST_ASSERT_EQUAL_UINT8(0u, s.unlocked_time);

    // All three at once, still separate: quality 3, unlocked over 1000 s, frequency trigger.
    decode_stat((uint16_t)((3u << 6) | (C37118_UNLOCKED_OVER_1000S << 4) | C37118_TRIGGER_FREQ), &s);
    TEST_ASSERT_EQUAL_UINT8(3u, s.time_quality);
    TEST_ASSERT_EQUAL_UINT8(C37118_UNLOCKED_OVER_1000S, s.unlocked_time);
    TEST_ASSERT_EQUAL_UINT8(C37118_TRIGGER_FREQ, s.trigger_reason);
}

// STAT only exists in a data frame, and only when the payload actually carries the word.
void test_stat_is_refused_outside_a_data_frame(void)
{
    uint8_t buf[32];
    C37118Frame f;
    C37118Stat s;

    size_t n = protocore_c37118_build_command(buf, sizeof(buf), 1u, 0u, 0u, C37118_CMD_DATA_ON);
    TEST_ASSERT_TRUE(protocore_c37118_parse_frame(buf, n, &f));
    TEST_ASSERT_FALSE(protocore_c37118_decode_stat(&f, &s));

    static const uint8_t ONE[1] = {0x55};
    n = protocore_c37118_build_frame(buf, sizeof(buf), C37118_TYPE_DATA, C37118_VERSION_2011, 1u, 0u, 0u, ONE, 1);
    TEST_ASSERT_TRUE(protocore_c37118_parse_frame(buf, n, &f));
    TEST_ASSERT_FALSE(protocore_c37118_decode_stat(&f, &s)); // a 1-octet payload is not a STAT word
    TEST_ASSERT_FALSE(protocore_c37118_decode_stat(NULL, &s));
    TEST_ASSERT_FALSE(protocore_c37118_decode_stat(&f, NULL));
}
