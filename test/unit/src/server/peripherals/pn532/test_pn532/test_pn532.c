// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

static uint8_t pn532_work[16]; // the borrow an entry takes; Pn532 never reads it

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
    Pn532V.build_frame_args.tfi = PN532_TFI_HOST;
    Pn532V.build_frame_args.data = GET_FIRMWARE_VERSION;
    Pn532V.build_frame_args.len = 1;
    Pn532V.build_frame_args.out = out;
    Pn532V.build_frame_args.cap = sizeof(out);
    Pn532.build_frame(pn532_work);
    TEST_ASSERT_EQUAL_UINT16(sizeof(CMD), Pn532V.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CMD, out, sizeof(CMD));

    // the response frame, framed and checked
    uint8_t tfi = 0;
    const uint8_t *pdata = NULL;
    uint8_t pdata_len = 0;
    Pn532V.parse_frame_args.raw = RSP;
    Pn532V.parse_frame_args.len = sizeof(RSP);
    Pn532V.parse_frame_args.tfi = &tfi;
    Pn532V.parse_frame_args.pdata = &pdata;
    Pn532V.parse_frame_args.pdata_len = &pdata_len;
    Pn532.parse_frame(pn532_work);
    TEST_ASSERT_EQUAL_INT((int)sizeof(RSP), Pn532V.n);
    TEST_ASSERT_EQUAL_HEX8(PN532_TFI_PN532, tfi);
    TEST_ASSERT_EQUAL_UINT8(5, pdata_len);

    // section 7.2.2: PD0 is the response code 0x03, then IC 0x32, Ver, Rev, Support.
    static const uint8_t WANT[5] = {0x03, 0x32, 0x01, 0x05, 0x07};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, pdata, sizeof(WANT));

    // and building it back from those five bytes reproduces the published frame.
    Pn532V.build_frame_args.tfi = PN532_TFI_PN532;
    Pn532V.build_frame_args.data = WANT;
    Pn532V.build_frame_args.len = 5;
    Pn532V.build_frame_args.out = out;
    Pn532V.build_frame_args.cap = sizeof(out);
    Pn532.build_frame(pn532_work);
    TEST_ASSERT_EQUAL_UINT16(sizeof(RSP), Pn532V.len);
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
    Pn532V.build_ack_args.out = out;
    Pn532V.build_ack_args.cap = sizeof(out);
    Pn532.build_ack(pn532_work);
    TEST_ASSERT_EQUAL_UINT16(6, Pn532V.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ACK, out, sizeof(ACK));
    Pn532V.is_ack_args.raw = ACK;
    Pn532V.is_ack_args.len = sizeof(ACK);
    Pn532.is_ack(pn532_work);
    TEST_ASSERT_TRUE(Pn532V.ok);

    // five bytes cannot yet be told apart from the head of a longer frame
    Pn532V.is_ack_args.raw = ACK;
    Pn532V.is_ack_args.len = 5;
    Pn532.is_ack(pn532_work);
    TEST_ASSERT_FALSE(Pn532V.ok);
    Pn532V.is_ack_args.raw = NULL;
    Pn532V.is_ack_args.len = 6;
    Pn532.is_ack(pn532_work);
    TEST_ASSERT_FALSE(Pn532V.ok);
    Pn532V.build_ack_args.out = out;
    Pn532V.build_ack_args.cap = 5;
    Pn532.build_ack(pn532_work);
    TEST_ASSERT_EQUAL_UINT16(0, Pn532V.len);
    Pn532V.build_ack_args.out = NULL;
    Pn532V.build_ack_args.cap = 6;
    Pn532.build_ack(pn532_work);
    TEST_ASSERT_EQUAL_UINT16(0, Pn532V.len);
}

// Section 6.2.1.4, NACK frame: 00 00 FF FF 00 00. It shares the ACK's first three bytes and asks
// for a retransmission rather than confirming, so reading it as an ACK loses the response.
void test_nack_is_not_an_ack(void)
{
    static const uint8_t NACK[6] = {0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00};
    Pn532V.is_ack_args.raw = NACK;
    Pn532V.is_ack_args.len = sizeof(NACK);
    Pn532.is_ack(pn532_work);
    TEST_ASSERT_FALSE(Pn532V.ok);
    // nor is it an information frame: LEN + LCS is 0xFF, not 0x00.
    Pn532V.parse_frame_args.raw = NACK;
    Pn532V.parse_frame_args.len = sizeof(NACK);
    Pn532V.parse_frame_args.tfi = NULL;
    Pn532V.parse_frame_args.pdata = NULL;
    Pn532V.parse_frame_args.pdata_len = NULL;
    Pn532.parse_frame(pn532_work);
    TEST_ASSERT_EQUAL_INT(-1, Pn532V.n);
}

// An ACK is not an information frame either: LEN 0x00 with LCS 0xFF fails the length checksum.
void test_ack_is_not_an_information_frame(void)
{
    static const uint8_t ACK[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
    Pn532V.parse_frame_args.raw = ACK;
    Pn532V.parse_frame_args.len = sizeof(ACK);
    Pn532V.parse_frame_args.tfi = NULL;
    Pn532V.parse_frame_args.pdata = NULL;
    Pn532V.parse_frame_args.pdata_len = NULL;
    Pn532.parse_frame(pn532_work);
    TEST_ASSERT_EQUAL_INT(-1, Pn532V.n);
}

// Section 6.2.1.5, error frame: 00 00 FF 01 FF 7F 81 00. LEN 1 carries the TFI alone (0x7F, the
// application-level error code) and no PData, and 0x7F + 0x81 = 0x100 satisfies the DCS relation.
void test_um0701_error_frame(void)
{
    static const uint8_t ERR[8] = {0x00, 0x00, 0xFF, 0x01, 0xFF, 0x7F, 0x81, 0x00};
    uint8_t tfi = 0;
    uint8_t pdata_len = 0xFF;
    Pn532V.parse_frame_args.raw = ERR;
    Pn532V.parse_frame_args.len = sizeof(ERR);
    Pn532V.parse_frame_args.tfi = &tfi;
    Pn532V.parse_frame_args.pdata = NULL;
    Pn532V.parse_frame_args.pdata_len = &pdata_len;
    Pn532.parse_frame(pn532_work);
    TEST_ASSERT_EQUAL_INT(8, Pn532V.n);
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
        Pn532V.build_frame_args.tfi = PN532_TFI_HOST;
        Pn532V.build_frame_args.data = payload;
        Pn532V.build_frame_args.len = len;
        Pn532V.build_frame_args.out = frame;
        Pn532V.build_frame_args.cap = sizeof(frame);
        Pn532.build_frame(pn532_work);
        const uint16_t n = Pn532V.len;
        TEST_ASSERT_EQUAL_UINT16((uint16_t)(8 + len), n);

        uint8_t tfi = 0;
        const uint8_t *pdata = NULL;
        uint8_t got = 0xFF;
        Pn532V.parse_frame_args.raw = frame;
        Pn532V.parse_frame_args.len = n;
        Pn532V.parse_frame_args.tfi = &tfi;
        Pn532V.parse_frame_args.pdata = &pdata;
        Pn532V.parse_frame_args.pdata_len = &got;
        Pn532.parse_frame(pn532_work);
        TEST_ASSERT_EQUAL_INT((int)n, Pn532V.n);
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
        Pn532V.parse_frame_args.raw = RSP;
        Pn532V.parse_frame_args.len = n;
        Pn532V.parse_frame_args.tfi = NULL;
        Pn532V.parse_frame_args.pdata = NULL;
        Pn532V.parse_frame_args.pdata_len = NULL;
        Pn532.parse_frame(pn532_work);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, Pn532V.n, "prefix");
    }
    Pn532V.parse_frame_args.raw = NULL;
    Pn532V.parse_frame_args.len = sizeof(RSP);
    Pn532V.parse_frame_args.tfi = NULL;
    Pn532V.parse_frame_args.pdata = NULL;
    Pn532V.parse_frame_args.pdata_len = NULL;
    Pn532.parse_frame(pn532_work);
    TEST_ASSERT_EQUAL_INT(0, Pn532V.n);
}

// Each field of the frame is checked, and a violation is -1: a resynchronizing reader must be able
// to tell "not a frame here" from "not enough bytes yet".
void test_malformed_frames_are_refused(void)
{
    uint8_t bad[sizeof(RSP)];

    memcpy(bad, RSP, sizeof(RSP));
    bad[0] = 0x01; // preamble
    Pn532V.parse_frame_args.raw = bad;
    Pn532V.parse_frame_args.len = sizeof(bad);
    Pn532V.parse_frame_args.tfi = NULL;
    Pn532V.parse_frame_args.pdata = NULL;
    Pn532V.parse_frame_args.pdata_len = NULL;
    Pn532.parse_frame(pn532_work);
    TEST_ASSERT_EQUAL_INT(-1, Pn532V.n);

    memcpy(bad, RSP, sizeof(RSP));
    bad[2] = 0xFE; // start code 00 FF
    Pn532V.parse_frame_args.raw = bad;
    Pn532V.parse_frame_args.len = sizeof(bad);
    Pn532V.parse_frame_args.tfi = NULL;
    Pn532V.parse_frame_args.pdata = NULL;
    Pn532V.parse_frame_args.pdata_len = NULL;
    Pn532.parse_frame(pn532_work);
    TEST_ASSERT_EQUAL_INT(-1, Pn532V.n);

    memcpy(bad, RSP, sizeof(RSP));
    bad[4] = 0xFB; // LCS: 0x06 + 0xFB != 0x00
    Pn532V.parse_frame_args.raw = bad;
    Pn532V.parse_frame_args.len = sizeof(bad);
    Pn532V.parse_frame_args.tfi = NULL;
    Pn532V.parse_frame_args.pdata = NULL;
    Pn532V.parse_frame_args.pdata_len = NULL;
    Pn532.parse_frame(pn532_work);
    TEST_ASSERT_EQUAL_INT(-1, Pn532V.n);

    memcpy(bad, RSP, sizeof(RSP));
    bad[11] = 0xE8; // DCS one off
    Pn532V.parse_frame_args.raw = bad;
    Pn532V.parse_frame_args.len = sizeof(bad);
    Pn532V.parse_frame_args.tfi = NULL;
    Pn532V.parse_frame_args.pdata = NULL;
    Pn532V.parse_frame_args.pdata_len = NULL;
    Pn532.parse_frame(pn532_work);
    TEST_ASSERT_EQUAL_INT(-1, Pn532V.n);

    memcpy(bad, RSP, sizeof(RSP));
    bad[7] ^= 0x01; // a PData byte, which DCS covers
    Pn532V.parse_frame_args.raw = bad;
    Pn532V.parse_frame_args.len = sizeof(bad);
    Pn532V.parse_frame_args.tfi = NULL;
    Pn532V.parse_frame_args.pdata = NULL;
    Pn532V.parse_frame_args.pdata_len = NULL;
    Pn532.parse_frame(pn532_work);
    TEST_ASSERT_EQUAL_INT(-1, Pn532V.n);

    // LEN 0 has no room for the TFI section 6.2.1.1 requires. LCS keeps the relation.
    static const uint8_t EMPTY[8] = {0x00, 0x00, 0xFF, 0x00, 0x00, 0xD5, 0x2B, 0x00};
    Pn532V.parse_frame_args.raw = EMPTY;
    Pn532V.parse_frame_args.len = sizeof(EMPTY);
    Pn532V.parse_frame_args.tfi = NULL;
    Pn532V.parse_frame_args.pdata = NULL;
    Pn532V.parse_frame_args.pdata_len = NULL;
    Pn532.parse_frame(pn532_work);
    TEST_ASSERT_EQUAL_INT(-1, Pn532V.n);
}

// A declared length past what this build accepts is refused at the header, before the reader waits
// for bytes that will never be framed.
void test_over_length_is_refused(void)
{
    // LEN 20, LCS 0xEC keeps LEN + LCS == 0, so only the length itself is out of range.
    static const uint8_t LONG[8] = {0x00, 0x00, 0xFF, 20, 0xEC, 0xD5, 0x03, 0x00};
    Pn532V.parse_frame_args.raw = LONG;
    Pn532V.parse_frame_args.len = sizeof(LONG);
    Pn532V.parse_frame_args.tfi = NULL;
    Pn532V.parse_frame_args.pdata = NULL;
    Pn532V.parse_frame_args.pdata_len = NULL;
    Pn532.parse_frame(pn532_work);
    TEST_ASSERT_EQUAL_INT(-1, Pn532V.n);

    uint8_t payload[PROTOCORE_PN532_MAX_DATA + 1];
    uint8_t out[64];
    memset(payload, 0x5A, sizeof(payload));
    Pn532V.build_frame_args.tfi = PN532_TFI_HOST;
    Pn532V.build_frame_args.data = payload;
    Pn532V.build_frame_args.len = PROTOCORE_PN532_MAX_DATA + 1;
    Pn532V.build_frame_args.out = out;
    Pn532V.build_frame_args.cap = sizeof(out);
    Pn532.build_frame(pn532_work);
    TEST_ASSERT_EQUAL_UINT16(0, Pn532V.len);
}

// A frame is written whole or not at all, and a null payload with a non-zero length is a caller bug
// rather than a run of whatever the buffer held.
void test_build_refuses_bad_arguments(void)
{
    uint8_t out[32];
    static const uint8_t DATA[2] = {0x02, 0x03};
    out[0] = 0xAA;
    Pn532V.build_frame_args.tfi = PN532_TFI_HOST;
    Pn532V.build_frame_args.data = DATA;
    Pn532V.build_frame_args.len = 2;
    Pn532V.build_frame_args.out = NULL;
    Pn532V.build_frame_args.cap = sizeof(out);
    Pn532.build_frame(pn532_work);
    TEST_ASSERT_EQUAL_UINT16(0, Pn532V.len);
    Pn532V.build_frame_args.tfi = PN532_TFI_HOST;
    Pn532V.build_frame_args.data = DATA;
    Pn532V.build_frame_args.len = 2;
    Pn532V.build_frame_args.out = out;
    Pn532V.build_frame_args.cap = 9;
    Pn532.build_frame(pn532_work);
    TEST_ASSERT_EQUAL_UINT16(0, Pn532V.len);
    TEST_ASSERT_EQUAL_HEX8(0xAA, out[0]); // untouched
    Pn532V.build_frame_args.tfi = PN532_TFI_HOST;
    Pn532V.build_frame_args.data = NULL;
    Pn532V.build_frame_args.len = 2;
    Pn532V.build_frame_args.out = out;
    Pn532V.build_frame_args.cap = sizeof(out);
    Pn532.build_frame(pn532_work);
    TEST_ASSERT_EQUAL_UINT16(0, Pn532V.len);
    // a null payload with zero length is the empty frame, which is legal
    Pn532V.build_frame_args.tfi = PN532_TFI_HOST;
    Pn532V.build_frame_args.data = NULL;
    Pn532V.build_frame_args.len = 0;
    Pn532V.build_frame_args.out = out;
    Pn532V.build_frame_args.cap = sizeof(out);
    Pn532.build_frame(pn532_work);
    TEST_ASSERT_EQUAL_UINT16(8, Pn532V.len);
}
