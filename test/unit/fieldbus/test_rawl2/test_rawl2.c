// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/rawl2: the Ethernet II / 802.1Q frame codec + the FCS.

#include "services/fieldbus/rawl2/rawl2.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static const uint8_t DST[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
static const uint8_t SRC[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

void test_build_ethernet_ii(void)
{
    uint8_t pl[2] = {0xDE, 0xAD};
    uint8_t out[32];
    size_t n = pc_eth_build(DST, SRC, ETHERTYPE_IPV4, pl, 2, out, sizeof(out));
    const uint8_t expect[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x0A, 0x0B,
                              0x0C, 0x0D, 0x0E, 0x0F, 0x08, 0x00, 0xDE, 0xAD};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, n);
}

void test_build_vlan(void)
{
    uint8_t pl[2] = {0x11, 0x22};
    uint8_t out[32];
    // pcp 3, dei 0, vid 100 -> TCI 0x6064; PROFINET ethertype.
    size_t n = pc_eth_build_vlan(DST, SRC, 3, PROTO_FALSE, 100, ETHERTYPE_PROFINET, pl, 2, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(20, n);
    TEST_ASSERT_EQUAL_HEX8(0x81, out[12]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[13]);
    TEST_ASSERT_EQUAL_HEX8(0x60, out[14]);
    TEST_ASSERT_EQUAL_HEX8(0x64, out[15]);
    TEST_ASSERT_EQUAL_HEX8(0x88, out[16]);
    TEST_ASSERT_EQUAL_HEX8(0x92, out[17]);
}

void test_parse(void)
{
    uint8_t pl[2] = {0xDE, 0xAD};
    uint8_t buf[32];
    size_t n = pc_eth_build(DST, SRC, ETHERTYPE_IPV4, pl, 2, buf, sizeof(buf));
    EthFrame f;
    TEST_ASSERT_TRUE(pc_eth_parse(buf, n, &f));
    TEST_ASSERT_FALSE(f.vlan);
    TEST_ASSERT_EQUAL_UINT16(ETHERTYPE_IPV4, f.ethertype);
    TEST_ASSERT_EQUAL_size_t(2, f.payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pl, f.payload, 2);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SRC, f.src, 6);

    // VLAN roundtrip.
    n = pc_eth_build_vlan(DST, SRC, 5, PROTO_TRUE, 4094, ETHERTYPE_GOOSE, pl, 2, buf, sizeof(buf));
    TEST_ASSERT_TRUE(pc_eth_parse(buf, n, &f));
    TEST_ASSERT_TRUE(f.vlan);
    TEST_ASSERT_EQUAL_UINT8(5, f.pcp);
    TEST_ASSERT_EQUAL_UINT16(4094, f.vid);
    TEST_ASSERT_EQUAL_UINT16(ETHERTYPE_GOOSE, f.ethertype);

    // Too short -> false.
    TEST_ASSERT_FALSE(pc_eth_parse(buf, 10, &f));
}

void test_fcs_check_vector(void)
{
    // The canonical CRC-32 check value: CRC of "123456789" = 0xCBF43926.
    const uint8_t msg[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, pc_eth_fcs(msg, sizeof(msg)));
}

void test_eth_build_parse_guards()
{
    uint8_t out[64];
    uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
    uint8_t pay[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    TEST_ASSERT_EQUAL_size_t(0, pc_eth_build(NULL, mac, 0x0800, pay, sizeof(pay), out, sizeof(out))); // null dst
    TEST_ASSERT_EQUAL_size_t(0, pc_eth_build(mac, mac, 0x0800, pay, sizeof(pay), out, 8));            // cap too small
    TEST_ASSERT_EQUAL_size_t(
        0, pc_eth_build_vlan(NULL, mac, 0, PROTO_FALSE, 100, 0x0800, pay, sizeof(pay), out, sizeof(out))); // null dst
    TEST_ASSERT_EQUAL_size_t(0,
                             pc_eth_build_vlan(mac, mac, 0, PROTO_FALSE, 100, 0x0800, pay, sizeof(pay), out, 8)); // cap
    EthFrame ef;
    uint8_t vlanish[15] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0x81, 0x00, 0x00}; // 802.1Q TPID then truncated
    TEST_ASSERT_FALSE(pc_eth_parse(vlanish, sizeof(vlanish), &ef));
}

void test_eth_build_null_src_out_and_zero_payload(void)
{
    uint8_t out[64];
    uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
    uint8_t pay[4] = {0xAA, 0xBB, 0xCC, 0xDD};

    // pc_eth_build: null src, null out, zero-length payload (skips the copy), and a
    // non-zero payload_len paired with a null payload pointer (each is a distinct guard branch).
    TEST_ASSERT_EQUAL_size_t(0, pc_eth_build(mac, NULL, 0x0800, pay, sizeof(pay), out, sizeof(out))); // null src
    TEST_ASSERT_EQUAL_size_t(0, pc_eth_build(mac, mac, 0x0800, pay, sizeof(pay), NULL, sizeof(out))); // null out
    TEST_ASSERT_EQUAL_size_t(ETH_HDR_LEN,
                             pc_eth_build(mac, mac, 0x0800, NULL, 0, out, sizeof(out)));    // payload_len == 0
    TEST_ASSERT_EQUAL_size_t(0, pc_eth_build(mac, mac, 0x0800, NULL, 4, out, sizeof(out))); // len>0, null payload

    // pc_eth_build_vlan: the same set of guard branches.
    TEST_ASSERT_EQUAL_size_t(
        0, pc_eth_build_vlan(mac, NULL, 0, PROTO_FALSE, 100, 0x0800, pay, sizeof(pay), out, sizeof(out))); // null src
    TEST_ASSERT_EQUAL_size_t(
        0, pc_eth_build_vlan(mac, mac, 0, PROTO_FALSE, 100, 0x0800, pay, sizeof(pay), NULL, sizeof(out))); // null out
    TEST_ASSERT_EQUAL_size_t(ETH_VLAN_HDR_LEN, pc_eth_build_vlan(mac, mac, 0, PROTO_FALSE, 100, 0x0800, NULL, 0, out,
                                                                 sizeof(out))); // payload_len == 0
    TEST_ASSERT_EQUAL_size_t(
        0, pc_eth_build_vlan(mac, mac, 0, PROTO_FALSE, 100, 0x0800, NULL, 4, out, sizeof(out))); // len>0, null payload
}

void test_eth_parse_null_guards(void)
{
    uint8_t buf[32];
    memset(buf, 0, sizeof(buf));
    EthFrame ef;
    TEST_ASSERT_FALSE(pc_eth_parse(NULL, sizeof(buf), &ef)); // null frame
    TEST_ASSERT_FALSE(pc_eth_parse(buf, sizeof(buf), NULL)); // null out
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_build_ethernet_ii);
    RUN_TEST(test_build_vlan);
    RUN_TEST(test_parse);
    RUN_TEST(test_fcs_check_vector);
    RUN_TEST(test_eth_build_parse_guards);
    RUN_TEST(test_eth_build_null_src_out_and_zero_payload);
    RUN_TEST(test_eth_parse_null_guards);
    return UNITY_END();
}
