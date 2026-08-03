// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for IKEv2 NAT traversal (services/security/ikev2/ikev2_natt): the RFC 7296 §2.23 NAT-detection hash
// (cross-checked against a Python hashlib.sha1 reference), the NAT_DETECTION_SOURCE/DESTINATION_IP Notify
// builders + round-trip parse, the peer-NAT / self-NAT detection logic, and the RFC 3948 UDP demux.

#include "services/security/ikev2/ikev2.h"
#include "services/security/ikev2/ikev2_natt.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

static const uint8_t init_spi[8] = {1, 2, 3, 4, 5, 6, 7, 8};
static const uint8_t resp_spi[8] = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18};
static const uint8_t ip4[4] = {203, 0, 113, 5};
static const uint8_t ip6[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};

// SHA-1( init_spi | resp_spi | 203.0.113.5 | port 500 ) from a Python hashlib.sha1 reference.
static const uint8_t kat_hash4[20] = {0x06, 0x3c, 0x89, 0xaa, 0x68, 0xf9, 0x69, 0xa5, 0x66, 0x7d,
                                      0x72, 0x39, 0x99, 0xb5, 0xdd, 0xa5, 0x8e, 0x8b, 0x6c, 0x8e};
// SHA-1( init_spi | resp_spi | 2001:db8::1 | port 4500 ).
static const uint8_t kat_hash6[20] = {0x1e, 0xe2, 0x44, 0x23, 0xbf, 0x8f, 0x59, 0x51, 0x5e, 0x02,
                                      0x65, 0xc6, 0xd0, 0xf0, 0x8b, 0xe3, 0xd0, 0x38, 0xf7, 0xe5};

void test_natd_hash_kat()
{
    uint8_t out[20];
    TEST_ASSERT_EQUAL_size_t(20, pc_ike_natd_hash(init_spi, resp_spi, ip4, 4, 500, out));
    TEST_ASSERT_EQUAL_MEMORY(kat_hash4, out, 20);
    TEST_ASSERT_EQUAL_size_t(20, pc_ike_natd_hash(init_spi, resp_spi, ip6, 16, 4500, out));
    TEST_ASSERT_EQUAL_MEMORY(kat_hash6, out, 20);

    // A different port yields a different hash (the port is bound into the digest).
    TEST_ASSERT_EQUAL_size_t(20, pc_ike_natd_hash(init_spi, resp_spi, ip4, 4, 501, out));
    TEST_ASSERT_FALSE(memcmp(kat_hash4, out, 20) == 0);
    // Bad address length / null out are refused.
    TEST_ASSERT_EQUAL_size_t(0, pc_ike_natd_hash(init_spi, resp_spi, ip4, 6, 500, out));
    TEST_ASSERT_EQUAL_size_t(0, pc_ike_natd_hash(init_spi, resp_spi, ip4, 4, 500, NULL));
}

void test_natd_notify_build_parse()
{
    // A NAT_DETECTION_SOURCE_IP notify carries the 20-byte hash and parses back with the right type.
    uint8_t buf[64];
    size_t n = pc_ike_natd_source_build(buf, sizeof(buf), IKE_PL_NONE, init_spi, resp_spi, ip4, 4, 500);
    TEST_ASSERT_TRUE(n > 4);

    IkeProtocol proto = IKE_PROTO_IKE;
    uint16_t ntype = 0;
    const uint8_t *spi = NULL, *data = NULL;
    uint8_t spi_size = 0xff;
    size_t data_len = 0;
    // pc_ike_notify_parse takes the payload body (after the 4-byte generic header).
    TEST_ASSERT_TRUE(pc_ike_notify_parse(buf + 4, n - 4, &proto, &ntype, &spi, &spi_size, &data, &data_len));
    TEST_ASSERT_EQUAL_UINT16(PC_IKE_N_NAT_DETECTION_SOURCE_IP, ntype);
    TEST_ASSERT_EQUAL(IKE_PROTO_NONE, proto);
    TEST_ASSERT_EQUAL_UINT8(0, spi_size);
    TEST_ASSERT_EQUAL_size_t(20, data_len);
    TEST_ASSERT_EQUAL_MEMORY(kat_hash4, data, 20);

    // The DESTINATION variant differs only in the notify type.
    n = pc_ike_natd_dest_build(buf, sizeof(buf), IKE_PL_NONE, init_spi, resp_spi, ip4, 4, 500);
    TEST_ASSERT_TRUE(pc_ike_notify_parse(buf + 4, n - 4, &proto, &ntype, &spi, &spi_size, &data, &data_len));
    TEST_ASSERT_EQUAL_UINT16(PC_IKE_N_NAT_DETECTION_DESTINATION_IP, ntype);

    // A tiny buffer overflows to 0.
    uint8_t small[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_ike_natd_source_build(small, sizeof(small), IKE_PL_NONE, init_spi,
                                                         resp_spi, ip4, 4, 500));
}

void test_natd_detection()
{
    // The peer sends NAT_DETECTION_SOURCE_IP over what it believes its own address is (203.0.113.5:500).
    uint8_t src_hash[20];
    pc_ike_natd_hash(init_spi, resp_spi, ip4, 4, 500, src_hash);

    // We observe the packet arriving from exactly that address -> no NAT in front of the peer.
    TEST_ASSERT_FALSE(pc_ike_natd_peer_behind_nat(init_spi, resp_spi, ip4, 4, 500, src_hash));
    // We observe it from a translated address (a NAT rewrote the source) -> the peer is behind NAT.
    const uint8_t natted[4] = {198, 51, 100, 7};
    TEST_ASSERT_TRUE(pc_ike_natd_peer_behind_nat(init_spi, resp_spi, natted, 4, 33000, src_hash));
    // Same address but a translated port still trips detection.
    TEST_ASSERT_TRUE(pc_ike_natd_peer_behind_nat(init_spi, resp_spi, ip4, 4, 33000, src_hash));

    // NAT_DETECTION_DESTINATION_IP: the peer hashes the address it sent to (our public address).
    uint8_t dst_hash[20];
    const uint8_t our_public[4] = {192, 0, 2, 10};
    pc_ike_natd_hash(init_spi, resp_spi, our_public, 4, 4500, dst_hash);
    // If our own local address matches, we are not behind NAT.
    TEST_ASSERT_FALSE(pc_ike_natd_self_behind_nat(init_spi, resp_spi, our_public, 4, 4500, dst_hash));
    // If our local address differs from what the peer sent to, we are behind NAT.
    const uint8_t our_private[4] = {10, 0, 0, 42};
    TEST_ASSERT_TRUE(pc_ike_natd_self_behind_nat(init_spi, resp_spi, our_private, 4, 4500, dst_hash));

    // A null hash never matches (fails safe as "behind NAT").
    TEST_ASSERT_TRUE(pc_ike_natd_peer_behind_nat(init_spi, resp_spi, ip4, 4, 500, NULL));
}

void test_natt_udp_demux()
{
    // A NAT-keepalive is exactly one 0xFF octet.
    const uint8_t keep[1] = {0xFF};
    TEST_ASSERT_TRUE(pc_natt_is_keepalive(keep, 1));
    const uint8_t not_keep[2] = {0xFF, 0x00};
    TEST_ASSERT_FALSE(pc_natt_is_keepalive(not_keep, 2)); // wrong length
    const uint8_t wrong[1] = {0x00};
    TEST_ASSERT_FALSE(pc_natt_is_keepalive(wrong, 1)); // wrong byte
    TEST_ASSERT_FALSE(pc_natt_is_keepalive(NULL, 1));

    // On port 4500 an IKE message carries a 4-octet zero Non-ESP Marker; ESP starts with a nonzero SPI.
    const uint8_t ike_msg[8] = {0, 0, 0, 0, 0x20, 0x22, 0x22, 0x08};
    TEST_ASSERT_TRUE(pc_natt_is_ike(ike_msg, sizeof(ike_msg)));
    const uint8_t esp_pkt[8] = {0x12, 0x34, 0x56, 0x78, 0, 0, 0, 1}; // nonzero SPI
    TEST_ASSERT_FALSE(pc_natt_is_ike(esp_pkt, sizeof(esp_pkt)));
    const uint8_t too_short[3] = {0, 0, 0};
    TEST_ASSERT_FALSE(pc_natt_is_ike(too_short, sizeof(too_short)));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_natd_hash_kat);
    RUN_TEST(test_natd_notify_build_parse);
    RUN_TEST(test_natd_detection);
    RUN_TEST(test_natt_udp_demux);
    return UNITY_END();
}
