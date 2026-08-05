// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the IO-Link (SDCI) data-link message codec (services/fieldbus/iolink): the MC / CKT /
// CKS control octets and the SDCI checksum (seed 0x52 + the 8->6 compression of IO-Link spec
// v1.1.4 Annex A.1.6, equation A.1). The checksum is checked against a hand-computed vector
// derived directly from the spec formula, then round-tripped. Pure host tests.

#include "services/fieldbus/iolink/iolink.h"
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_mc_octet()
{
    // read, Page channel, address 0x10 -> 0x80 | (1<<5) | 0x10 = 0xB0.
    uint8_t mc = pc_iol_mc(PROTO_TRUE, IOL_CH_PAGE, 0x10);
    TEST_ASSERT_EQUAL_HEX8(0xB0, mc);
    TEST_ASSERT_TRUE(pc_iol_mc_is_read(mc));
    TEST_ASSERT_EQUAL_UINT8(IOL_CH_PAGE, pc_iol_mc_channel(mc));
    TEST_ASSERT_EQUAL_UINT8(0x10, pc_iol_mc_address(mc));

    uint8_t w = pc_iol_mc(PROTO_FALSE, IOL_CH_ISDU, 0x05);
    TEST_ASSERT_FALSE(pc_iol_mc_is_read(w));
    TEST_ASSERT_EQUAL_UINT8(IOL_CH_ISDU, pc_iol_mc_channel(w));
    TEST_ASSERT_EQUAL_UINT8(0x05, pc_iol_mc_address(w));
}

void test_ckt_cks_octets()
{
    TEST_ASSERT_EQUAL_HEX8((1 << 6) | 0x15, pc_iol_ckt(IOL_MSEQ_TYPE_1, 0x15));
    TEST_ASSERT_EQUAL_HEX8(0x80 | 0x0A, pc_iol_cks(PROTO_TRUE, PROTO_FALSE, 0x0A)); // event flag + checksum
    TEST_ASSERT_EQUAL_HEX8(0x40 | 0x0A, pc_iol_cks(PROTO_FALSE, PROTO_TRUE, 0x0A)); // PD-invalid + checksum
}

// Hand-computed vector from the spec formula: MC=0xB0, CKT(type0)=0x00.
// XOR: 0x52 ^ 0xB0 ^ 0x00 = 0xE2; compress(0xE2) = 0b110101 = 0x35.
void test_checksum_known_vector()
{
    uint8_t msg[2] = {0xB0, 0x00};
    uint8_t check = pc_iol_finalize(msg, 2, 1);
    TEST_ASSERT_EQUAL_HEX8(0x35, check);
    TEST_ASSERT_EQUAL_HEX8(0x35, msg[1]);
    TEST_ASSERT_TRUE(pc_iol_verify(msg, 2, 1));
}

// The type bits of CKT survive finalize, and corruption is caught.
void test_finalize_preserves_type_and_detects_corruption()
{
    uint8_t msg[3] = {pc_iol_mc(PROTO_FALSE, IOL_CH_PROCESS, 0x01), 0xAB, pc_iol_ckt(IOL_MSEQ_TYPE_2, 0)};
    pc_iol_finalize(msg, 3, 2);
    TEST_ASSERT_EQUAL_UINT8(IOL_MSEQ_TYPE_2, (uint8_t)(msg[2] >> 6)); // type 2 preserved
    TEST_ASSERT_TRUE(pc_iol_verify(msg, 3, 2));

    uint8_t bad[3];
    memcpy(bad, msg, 3);
    bad[1] ^= 0x01; // flip a data bit
    TEST_ASSERT_FALSE(pc_iol_verify(bad, 3, 2));
}

// A device reply [PD/OD..., CKS] round-trips, and the status flags survive.
void test_device_reply_cks_roundtrip()
{
    uint8_t reply[2] = {0xAA, pc_iol_cks(PROTO_TRUE, PROTO_FALSE, 0)}; // event flag set, checksum to be filled
    uint8_t cks = pc_iol_finalize(reply, 2, 1);
    TEST_ASSERT_TRUE((cks & IOL_CKS_EVENT) != 0); // event flag preserved
    TEST_ASSERT_TRUE(pc_iol_verify(reply, 2, 1));
    TEST_ASSERT_EQUAL_HEX8(0x8A, cks); // 0x52^0xAA^0x80 = 0x78 -> compress = 0x0A; 0x80|0x0A
}

// pc_iol_finalize and pc_iol_verify reject a null message or an out-of-range check index.
void test_iol_finalize_verify_guards()
{
    uint8_t msg[4] = {0x11, 0x22, 0x33, 0x00};
    TEST_ASSERT_EQUAL_UINT8(0, pc_iol_finalize(NULL, 4, 3)); // null msg
    TEST_ASSERT_EQUAL_UINT8(0, pc_iol_finalize(msg, 4, 4));  // check_idx >= len
    TEST_ASSERT_FALSE(pc_iol_verify(NULL, 4, 3));            // null msg
    TEST_ASSERT_FALSE(pc_iol_verify(msg, 4, 4));             // check_idx >= len
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_mc_octet);
    RUN_TEST(test_ckt_cks_octets);
    RUN_TEST(test_checksum_known_vector);
    RUN_TEST(test_finalize_preserves_type_and_detects_corruption);
    RUN_TEST(test_device_reply_cks_roundtrip);
    RUN_TEST(test_iol_finalize_verify_guards);
    return UNITY_END();
}
