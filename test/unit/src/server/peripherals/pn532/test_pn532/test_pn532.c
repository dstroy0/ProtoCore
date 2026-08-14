// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the NXP PN532 frame codec (server/peripherals/pn532/pn532.h).
//
// Expected bytes come from the PN532 User Manual UM0701-02 rev 02: section 6.2.1.1 for the normal
// information frame and its two checksum relations, 6.2.1.3 / 6.2.1.4 for ACK and NACK, 6.2.1.5 for
// the error frame, and the two worked GetFirmwareVersion frames the manual prints - the command in
// section 6.2.1.6 and the response in section 7.2.9.
//
// test_um0701_getfirmwareversion_frames is the load-bearing case: those two byte strings are
// published verbatim, so reproducing them octet for octet - LEN, LCS and DCS included - is what
// makes this codec trustworthy against a real part rather than against itself.

#include "server/peripherals/pn532/pn532.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// UM0701-02 section 6.2.1.6, "All the following frames are the same for the PN532's point of view
// (GetFirmwareVersion)":  00 FF 02 FE D4 02 2A, with the 0x00 preamble and postamble around it.
//   LEN  = 2    (TFI + PD0)
//   LCS  = 0xFE since section 6.2.1.1 requires lower byte of [LEN + LCS] = 0x00
//   DCS  = 0x2A since lower byte of [TFI + PD0 + DCS] = [0xD4 + 0x02 + DCS] = 0x00
//
// UM0701-02 section 7.2.9 prints the response with preamble and postamble:
//   00 00 FF 06 FA D5 03 32 01 05 07 E9 00
//   LEN = 6, LCS = 0xFA, and 0xD5+0x03+0x32+0x01+0x05+0x07 = 0x118, so DCS = 0xE9.
static const uint8_t CMD[9] = {0x00, 0x00, 0xFF, 0x02, 0xFE, 0xD4, 0x02, 0x2A, 0x00};
static const uint8_t RSP[13] = {0x00, 0x00, 0xFF, 0x06, 0xFA, 0xD5, 0x03, 0x32, 0x01, 0x05, 0x07, 0xE9, 0x00};

void test_um0701_getfirmwareversion_frames(void)
{
    // the command frame, built from its command code alone
    static const uint8_t GET_FIRMWARE_VERSION[1] = {0x02};
    uint8_t out[32];
    TEST_ASSERT_EQUAL_UINT16(sizeof(CMD),
                             protocore_pn532_build_frame(PN532_TFI_HOST, GET_FIRMWARE_VERSION, 1, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CMD, out, sizeof(CMD));

    // the response frame, framed and checked
    uint8_t tfi = 0;
    const uint8_t *pdata = NULL;
    uint8_t pdata_len = 0;
    TEST_ASSERT_EQUAL_INT((int)sizeof(RSP), protocore_pn532_parse_frame(RSP, sizeof(RSP), &tfi, &pdata, &pdata_len));
    TEST_ASSERT_EQUAL_HEX8(PN532_TFI_PN532, tfi);
    TEST_ASSERT_EQUAL_UINT8(5, pdata_len);

    // section 7.2.2: PD0 is the response code 0x03, then IC 0x32, Ver, Rev, Support.
    static const uint8_t WANT[5] = {0x03, 0x32, 0x01, 0x05, 0x07};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, pdata, sizeof(WANT));

    // and building it back from those five bytes reproduces the published frame.
    TEST_ASSERT_EQUAL_UINT16(sizeof(RSP), protocore_pn532_build_frame(PN532_TFI_PN532, WANT, 5, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(RSP, out, sizeof(RSP));
}

// Section 6.2.1.1: TFI is D4h host -> PN532 and D5h PN532 -> host.
void test_frame_identifiers(void)
{
    TEST_ASSERT_EQUAL_HEX8(0xD4, PN532_TFI_HOST);
    TEST_ASSERT_EQUAL_HEX8(0xD5, PN532_TFI_PN532);
}

// Section 6.2.1.3, ACK frame: 00 00 FF 00 FF 00.
void test_ack_frame(void)
{
    static const uint8_t ACK[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
    uint8_t out[6];
    TEST_ASSERT_EQUAL_UINT16(6, protocore_pn532_build_ack(out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ACK, out, sizeof(ACK));
    TEST_ASSERT_TRUE(protocore_pn532_is_ack(ACK, sizeof(ACK)));

    // five bytes cannot yet be told apart from the head of a longer frame
    TEST_ASSERT_FALSE(protocore_pn532_is_ack(ACK, 5));
    TEST_ASSERT_FALSE(protocore_pn532_is_ack(NULL, 6));
    TEST_ASSERT_EQUAL_UINT16(0, protocore_pn532_build_ack(out, 5));
    TEST_ASSERT_EQUAL_UINT16(0, protocore_pn532_build_ack(NULL, 6));
}

// Section 6.2.1.4, NACK frame: 00 00 FF FF 00 00. It shares the ACK's first three bytes and asks
// for a retransmission rather than confirming, so reading it as an ACK loses the response.
void test_nack_is_not_an_ack(void)
{
    static const uint8_t NACK[6] = {0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00};
    TEST_ASSERT_FALSE(protocore_pn532_is_ack(NACK, sizeof(NACK)));
    // nor is it an information frame: LEN + LCS is 0xFF, not 0x00.
    TEST_ASSERT_EQUAL_INT(-1, protocore_pn532_parse_frame(NACK, sizeof(NACK), NULL, NULL, NULL));
}

// An ACK is not an information frame either: LEN 0x00 with LCS 0xFF fails the length checksum.
void test_ack_is_not_an_information_frame(void)
{
    static const uint8_t ACK[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
    TEST_ASSERT_EQUAL_INT(-1, protocore_pn532_parse_frame(ACK, sizeof(ACK), NULL, NULL, NULL));
}

// Section 6.2.1.5, error frame: 00 00 FF 01 FF 7F 81 00. LEN 1 carries the TFI alone (0x7F, the
// application-level error code) and no PData, and 0x7F + 0x81 = 0x100 satisfies the DCS relation.
void test_um0701_error_frame(void)
{
    static const uint8_t ERR[8] = {0x00, 0x00, 0xFF, 0x01, 0xFF, 0x7F, 0x81, 0x00};
    uint8_t tfi = 0;
    uint8_t pdata_len = 0xFF;
    TEST_ASSERT_EQUAL_INT(8, protocore_pn532_parse_frame(ERR, sizeof(ERR), &tfi, NULL, &pdata_len));
    TEST_ASSERT_EQUAL_HEX8(0x7F, tfi);
    TEST_ASSERT_EQUAL_UINT8(0, pdata_len);
}

// Build then parse returns the payload unchanged, at every length the build accepts.
void test_round_trip(void)
{
    uint8_t payload[PROTOCORE_PN532_MAX_DATA];
    for (uint8_t i = 0; i < sizeof(payload); i++)
    {
        payload[i] = (uint8_t)(0x10 + i);
    }
    for (uint8_t len = 0; len <= PROTOCORE_PN532_MAX_DATA; len++)
    {
        uint8_t frame[8 + PROTOCORE_PN532_MAX_DATA];
        const uint16_t n = protocore_pn532_build_frame(PN532_TFI_HOST, payload, len, frame, sizeof(frame));
        TEST_ASSERT_EQUAL_UINT16((uint16_t)(8 + len), n);

        uint8_t tfi = 0;
        const uint8_t *pdata = NULL;
        uint8_t got = 0xFF;
        TEST_ASSERT_EQUAL_INT((int)n, protocore_pn532_parse_frame(frame, n, &tfi, &pdata, &got));
        TEST_ASSERT_EQUAL_HEX8(PN532_TFI_HOST, tfi);
        TEST_ASSERT_EQUAL_UINT8(len, got);
        if (len)
        {
            TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, pdata, len);
        }
    }
}

// A frame that has not fully arrived is 0 - ask for more bytes - and never -1, which would make a
// stream reader drop a frame that was merely split across two reads.
void test_incomplete_frame_asks_for_more(void)
{
    for (uint16_t n = 0; n < (uint16_t)sizeof(RSP); n++)
    {
        // index 0..1 of a partial frame is still a legal preamble, so every prefix is "more needed"
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, protocore_pn532_parse_frame(RSP, n, NULL, NULL, NULL), "prefix");
    }
    TEST_ASSERT_EQUAL_INT(0, protocore_pn532_parse_frame(NULL, sizeof(RSP), NULL, NULL, NULL));
}

// Each field of the frame is checked, and a violation is -1: a resynchronizing reader must be able
// to tell "not a frame here" from "not enough bytes yet".
void test_malformed_frames_are_refused(void)
{
    uint8_t bad[sizeof(RSP)];

    memcpy(bad, RSP, sizeof(RSP));
    bad[0] = 0x01; // preamble
    TEST_ASSERT_EQUAL_INT(-1, protocore_pn532_parse_frame(bad, sizeof(bad), NULL, NULL, NULL));

    memcpy(bad, RSP, sizeof(RSP));
    bad[2] = 0xFE; // start code 00 FF
    TEST_ASSERT_EQUAL_INT(-1, protocore_pn532_parse_frame(bad, sizeof(bad), NULL, NULL, NULL));

    memcpy(bad, RSP, sizeof(RSP));
    bad[4] = 0xFB; // LCS: 0x06 + 0xFB != 0x00
    TEST_ASSERT_EQUAL_INT(-1, protocore_pn532_parse_frame(bad, sizeof(bad), NULL, NULL, NULL));

    memcpy(bad, RSP, sizeof(RSP));
    bad[11] = 0xE8; // DCS one off
    TEST_ASSERT_EQUAL_INT(-1, protocore_pn532_parse_frame(bad, sizeof(bad), NULL, NULL, NULL));

    memcpy(bad, RSP, sizeof(RSP));
    bad[7] ^= 0x01; // a PData byte, which DCS covers
    TEST_ASSERT_EQUAL_INT(-1, protocore_pn532_parse_frame(bad, sizeof(bad), NULL, NULL, NULL));

    // LEN 0 has no room for the TFI section 6.2.1.1 requires. LCS keeps the relation.
    static const uint8_t EMPTY[8] = {0x00, 0x00, 0xFF, 0x00, 0x00, 0xD5, 0x2B, 0x00};
    TEST_ASSERT_EQUAL_INT(-1, protocore_pn532_parse_frame(EMPTY, sizeof(EMPTY), NULL, NULL, NULL));
}

// A declared length past what this build accepts is refused at the header, before the reader waits
// for bytes that will never be framed.
void test_over_length_is_refused(void)
{
    // LEN 20, LCS 0xEC keeps LEN + LCS == 0, so only the length itself is out of range.
    static const uint8_t LONG[8] = {0x00, 0x00, 0xFF, 20, 0xEC, 0xD5, 0x03, 0x00};
    TEST_ASSERT_EQUAL_INT(-1, protocore_pn532_parse_frame(LONG, sizeof(LONG), NULL, NULL, NULL));

    uint8_t payload[PROTOCORE_PN532_MAX_DATA + 1];
    uint8_t out[64];
    memset(payload, 0x5A, sizeof(payload));
    TEST_ASSERT_EQUAL_UINT16(
        0, protocore_pn532_build_frame(PN532_TFI_HOST, payload, PROTOCORE_PN532_MAX_DATA + 1, out, sizeof(out)));
}

// A frame is written whole or not at all, and a null payload with a non-zero length is a caller bug
// rather than a run of whatever the buffer held.
void test_build_refuses_bad_arguments(void)
{
    uint8_t out[32];
    static const uint8_t DATA[2] = {0x02, 0x03};
    out[0] = 0xAA;
    TEST_ASSERT_EQUAL_UINT16(0, protocore_pn532_build_frame(PN532_TFI_HOST, DATA, 2, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT16(0, protocore_pn532_build_frame(PN532_TFI_HOST, DATA, 2, out, 9));
    TEST_ASSERT_EQUAL_HEX8(0xAA, out[0]); // untouched
    TEST_ASSERT_EQUAL_UINT16(0, protocore_pn532_build_frame(PN532_TFI_HOST, NULL, 2, out, sizeof(out)));
    // a null payload with zero length is the empty frame, which is legal
    TEST_ASSERT_EQUAL_UINT16(8, protocore_pn532_build_frame(PN532_TFI_HOST, NULL, 0, out, sizeof(out)));
}
