// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the IO-Link (SDCI) data-link message codec (services/fieldbus/iolink/iolink.h).
//
// The IO-Link Interface and System Specification, Annex A.1.6, states: a seed of 0x52 is XORed with
// the first octet of the message, every octet is XOR-processed, the check octet is included with
// its checksum bits set to "0", and the 8-bit result is compressed to 6 bits by equations (A.1):
//   D56 = D78 xor D58 xor D38 xor D18      D26 = D58 xor D48
//   D46 = D68 xor D48 xor D28 xor D08      D16 = D38 xor D28
//   D36 = D78 xor D68                      D06 = D18 xor D08
// test_type0_read_checksum is the load-bearing case: it folds a TYPE_0 master read message by those
// equations in the comment and asserts the octet that falls out, so a codec with the wrong seed,
// the wrong span or a transposed compression bit cannot reproduce it.

#include "services/fieldbus/iolink/iolink.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Figure A.1: the MC octet is R/W in bit 7, communication channel in bits 6..5, address in bits
// 4..0. Table A.1 assigns the channels, Table A.2 the direction.
void test_mc_octet_fields(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x80u, IOL_MC_READ); // bit 7 set = read access
    TEST_ASSERT_EQUAL_HEX8(0x00u, IOL_MC_WRITE);
    TEST_ASSERT_EQUAL_UINT8(0u, IOL_CH_PROCESS);
    TEST_ASSERT_EQUAL_UINT8(1u, IOL_CH_PAGE);
    TEST_ASSERT_EQUAL_UINT8(2u, IOL_CH_DIAGNOSIS);
    TEST_ASSERT_EQUAL_UINT8(3u, IOL_CH_ISDU);

    // read, Page channel, address 0: 1 000 0 0000 -> 0x80 | (1 << 5) = 0xA0
    TEST_ASSERT_EQUAL_HEX8(0xA0u, protocore_iol_mc(PROTO_TRUE, IOL_CH_PAGE, 0));
    // write, ISDU channel, address 0x1F: (3 << 5) | 0x1F = 0x60 | 0x1F = 0x7F
    TEST_ASSERT_EQUAL_HEX8(0x7Fu, protocore_iol_mc(PROTO_FALSE, IOL_CH_ISDU, 0x1F));
    // the address field is five bits, so bit 5 of a wider argument never leaks into the channel
    TEST_ASSERT_EQUAL_HEX8(0x00u, protocore_iol_mc(PROTO_FALSE, IOL_CH_PROCESS, 0x20));

    // Every field comes back out of every octet.
    for (unsigned v = 0; v < 256u; v++)
    {
        uint8_t mc = (uint8_t)v;
        TEST_ASSERT_EQUAL_HEX8(mc, protocore_iol_mc(protocore_iol_mc_is_read(mc), protocore_iol_mc_channel(mc),
                                                    protocore_iol_mc_address(mc)));
    }
}

// Figure A.2: the CKT octet carries the M-sequence type in bits 7..6 and the checksum in bits 5..0.
// Table A.3 defines types 0, 1 and 2; 3 is reserved.
void test_ckt_octet_fields(void)
{
    TEST_ASSERT_EQUAL_HEX8(0xC0u, IOL_CHECK_HIGH_MASK);
    TEST_ASSERT_EQUAL_HEX8(0x3Fu, IOL_CHECK_SUM_MASK);
    TEST_ASSERT_EQUAL_UINT8(0u, IOL_MSEQ_TYPE_0);
    TEST_ASSERT_EQUAL_UINT8(1u, IOL_MSEQ_TYPE_1);
    TEST_ASSERT_EQUAL_UINT8(2u, IOL_MSEQ_TYPE_2);

    TEST_ASSERT_EQUAL_HEX8(0x00u, protocore_iol_ckt(IOL_MSEQ_TYPE_0, 0));
    TEST_ASSERT_EQUAL_HEX8(0x40u, protocore_iol_ckt(IOL_MSEQ_TYPE_1, 0));
    TEST_ASSERT_EQUAL_HEX8(0x80u, protocore_iol_ckt(IOL_MSEQ_TYPE_2, 0));
    TEST_ASSERT_EQUAL_HEX8(0x7Fu, protocore_iol_ckt(IOL_MSEQ_TYPE_1, 0x3F));
    // The checksum argument is six bits: a wider one never overwrites the type.
    TEST_ASSERT_EQUAL_HEX8(0x40u, protocore_iol_ckt(IOL_MSEQ_TYPE_1, 0xC0));
}

// Figure A.3: the CKS octet carries the Event flag in bit 7 (Table A.6), the PD status in bit 6
// (Table A.5, 1 = Process Data invalid) and the checksum in bits 5..0.
void test_cks_octet_fields(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x80u, IOL_CKS_EVENT);
    TEST_ASSERT_EQUAL_HEX8(0x40u, IOL_CKS_PD_INVALID);

    TEST_ASSERT_EQUAL_HEX8(0x00u, protocore_iol_cks(PROTO_FALSE, PROTO_FALSE, 0));
    TEST_ASSERT_EQUAL_HEX8(0x80u, protocore_iol_cks(PROTO_TRUE, PROTO_FALSE, 0));
    TEST_ASSERT_EQUAL_HEX8(0x40u, protocore_iol_cks(PROTO_FALSE, PROTO_TRUE, 0));
    TEST_ASSERT_EQUAL_HEX8(0xC0u, protocore_iol_cks(PROTO_TRUE, PROTO_TRUE, 0));
    TEST_ASSERT_EQUAL_HEX8(0xFFu, protocore_iol_cks(PROTO_TRUE, PROTO_TRUE, 0x3F));
}

// A TYPE_0 master read message is MC then CKT (Figure A.5). Reading address 0 of the Page channel:
//   MC  = read | Page << 5 | 0        = 0xA0
//   CKT = type 0 << 6 | checksum 0    = 0x00
// Checksum8 = seed 0x52 xor MC xor CKT = 0x52 ^ 0xA0 = 0xF2 = 1111 0010, so
//   D78..D08 = 1 1 1 1 0 0 1 0
//   D56 = D78^D58^D38^D18 = 1^1^0^1 = 1
//   D46 = D68^D48^D28^D08 = 1^1^0^0 = 0
//   D36 = D78^D68 = 1^1 = 0
//   D26 = D58^D48 = 1^1 = 0
//   D16 = D38^D28 = 0^0 = 0
//   D06 = D18^D08 = 1^0 = 1
// Checksum6 = 100001b = 0x21, so the finalized CKT is 0x00 | 0x21 = 0x21.
void test_type0_read_checksum(void)
{
    uint8_t msg[2];
    msg[0] = protocore_iol_mc(PROTO_TRUE, IOL_CH_PAGE, 0);
    msg[1] = protocore_iol_ckt(IOL_MSEQ_TYPE_0, 0);
    TEST_ASSERT_EQUAL_HEX8(0xA0u, msg[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, msg[1]);

    TEST_ASSERT_EQUAL_HEX8(0x21u, protocore_iol_checksum6(msg, sizeof(msg)));
    TEST_ASSERT_EQUAL_HEX8(0x21u, protocore_iol_finalize(msg, sizeof(msg), 1));
    TEST_ASSERT_EQUAL_HEX8(0x21u, msg[1]);
    TEST_ASSERT_TRUE(protocore_iol_verify(msg, sizeof(msg), 1));
}

// A TYPE_0 master write message is MC, CKT, then one octet of On-request Data.
//   MC  = write | ISDU << 5 | 0x05 = 0x65
//   CKT = type 1 << 6 | 0          = 0x40
//   OD  = 0xAA
// Checksum8 = 0x52 ^ 0x65 ^ 0x40 ^ 0xAA = 0x37 ^ 0x40 ^ 0xAA = 0x77 ^ 0xAA = 0xDD = 1101 1101, so
//   D56 = 1^0^1^0 = 0   D46 = 1^1^1^1 = 0   D36 = 1^1 = 0
//   D26 = 0^1 = 1       D16 = 1^1 = 0       D06 = 0^1 = 1
// Checksum6 = 000101b = 0x05, so the finalized CKT is 0x40 | 0x05 = 0x45.
void test_type0_write_checksum(void)
{
    uint8_t msg[3];
    msg[0] = protocore_iol_mc(PROTO_FALSE, IOL_CH_ISDU, 0x05);
    msg[1] = protocore_iol_ckt(IOL_MSEQ_TYPE_1, 0);
    msg[2] = 0xAA;
    TEST_ASSERT_EQUAL_HEX8(0x65u, msg[0]);

    TEST_ASSERT_EQUAL_HEX8(0x45u, protocore_iol_finalize(msg, sizeof(msg), 1));
    TEST_ASSERT_TRUE(protocore_iol_verify(msg, sizeof(msg), 1));
}

// The Device reply carries its checksum in the CKS octet, so the same procedure runs with the
// check octet at the end. A TYPE_0 read reply is OD then CKS, here with an Event pending:
//   OD  = 0x5A, CKS = Event | PD valid | 0 = 0x80
// Checksum8 = 0x52 ^ 0x5A ^ 0x80 = 0x08 ^ 0x80 = 0x88 = 1000 1000, so
//   D56 = 1^0^1^0 = 0   D46 = 0^0^0^0 = 0   D36 = 1^0 = 1
//   D26 = 0^0 = 0       D16 = 1^0 = 1       D06 = 0^0 = 0
// Checksum6 = 001010b = 0x0A, so the finalized CKS is 0x80 | 0x0A = 0x8A.
void test_device_reply_checksum(void)
{
    uint8_t msg[2];
    msg[0] = 0x5A;
    msg[1] = protocore_iol_cks(PROTO_TRUE, PROTO_FALSE, 0);
    TEST_ASSERT_EQUAL_HEX8(0x80u, msg[1]);

    TEST_ASSERT_EQUAL_HEX8(0x8Au, protocore_iol_finalize(msg, sizeof(msg), 1));
    TEST_ASSERT_TRUE(protocore_iol_verify(msg, sizeof(msg), 1));
    // Finalizing preserved the Event flag rather than overwriting the whole octet.
    TEST_ASSERT_EQUAL_HEX8(IOL_CKS_EVENT, (uint8_t)(msg[1] & IOL_CHECK_HIGH_MASK));
}

// Finalize then verify holds for every check-octet position and every set of type / status bits,
// because the checksum is computed with those bits present and the checksum bits masked to zero.
void test_finalize_then_verify(void)
{
    static const uint8_t HIGH[4] = {0x00, 0x40, 0x80, 0xC0};
    for (size_t len = 1; len <= 6; len++)
    {
        for (size_t idx = 0; idx < len; idx++)
        {
            for (size_t h = 0; h < sizeof(HIGH) / sizeof(HIGH[0]); h++)
            {
                uint8_t msg[6];
                for (size_t i = 0; i < len; i++)
                {
                    msg[i] = (uint8_t)(0x11u * (i + 1u));
                }
                msg[idx] = HIGH[h];
                (void)protocore_iol_finalize(msg, len, idx);
                TEST_ASSERT_TRUE(protocore_iol_verify(msg, len, idx));
                TEST_ASSERT_EQUAL_HEX8(HIGH[h], (uint8_t)(msg[idx] & IOL_CHECK_HIGH_MASK));
            }
        }
    }
}

// Equations (A.1) pair every input bit with two distinct output bits: flipping D78 moves D56 and
// D36, D68 moves D46 and D36, and so on down to D08 moving D46 and D06. So a single-bit change
// anywhere in the message always changes Checksum 6, and every one of them is refused.
void test_single_bit_corruption_is_refused(void)
{
    uint8_t good[5];
    good[0] = protocore_iol_mc(PROTO_TRUE, IOL_CH_PROCESS, 0x03);
    good[1] = protocore_iol_ckt(IOL_MSEQ_TYPE_2, 0);
    good[2] = 0x12;
    good[3] = 0x34;
    good[4] = 0x56;
    (void)protocore_iol_finalize(good, sizeof(good), 1);
    TEST_ASSERT_TRUE(protocore_iol_verify(good, sizeof(good), 1));

    for (size_t i = 0; i < sizeof(good); i++)
    {
        for (int bit = 0; bit < 8; bit++)
        {
            uint8_t bad[5];
            memcpy(bad, good, sizeof(good));
            bad[i] ^= (uint8_t)(1u << bit);
            TEST_ASSERT_FALSE(protocore_iol_verify(bad, sizeof(bad), 1));
        }
    }
}

// A check index outside the message is refused rather than read or written out of bounds.
void test_bounds_are_refused(void)
{
    uint8_t msg[2] = {0xA0, 0x00};
    TEST_ASSERT_EQUAL_HEX8(0x00u, protocore_iol_finalize(msg, 2, 2));
    TEST_ASSERT_EQUAL_HEX8(0x00u, protocore_iol_finalize(NULL, 2, 0));
    TEST_ASSERT_FALSE(protocore_iol_verify(msg, 2, 2));
    TEST_ASSERT_FALSE(protocore_iol_verify(NULL, 2, 0));
    // The message was not touched by the refused calls.
    TEST_ASSERT_EQUAL_HEX8(0xA0u, msg[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, msg[1]);
}
