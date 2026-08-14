// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the raw Layer-2 Ethernet frame codec (services/fieldbus/rawl2/rawl2.h).
//
// Two published tables carry this module. The IEEE Registration Authority's public EtherType
// listing assigns every ethertype the header names, and test_ethertype_registry checks each one
// against it. The frame check sequence is CRC-32/ISO-HDLC, whose published check value - the CRC of
// the ASCII octets "123456789" - is 0xCBF43926 in the CRC catalogue; test_fcs_published_check_value
// is the load-bearing case, because an FCS that is off by a reflection or an init value is a frame
// every switch on the segment drops.

#include "services/fieldbus/rawl2/rawl2.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static const uint8_t DST[ETH_ALEN] = {0x01, 0x0E, 0xCF, 0x00, 0x00, 0x00};
static const uint8_t SRC[ETH_ALEN] = {0x00, 0x1B, 0x1B, 0x11, 0x22, 0x33};

// IEEE Registration Authority public EtherType listing.
void test_ethertype_registry(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0800, ETHERTYPE_IPV4);      // Xerox, IPv4 (RFC 894)
    TEST_ASSERT_EQUAL_HEX16(0x0806, ETHERTYPE_ARP);       // Symbolics, Address Resolution Protocol (RFC 826)
    TEST_ASSERT_EQUAL_HEX16(0x8100, ETH_TPID_8021Q);      // IEEE 802.1 Chair, Customer VLAN Tag (C-TAG)
    TEST_ASSERT_EQUAL_HEX16(0x8892, ETHERTYPE_PROFINET);  // PROFIBUS International
    TEST_ASSERT_EQUAL_HEX16(0x88B8, ETHERTYPE_GOOSE);     // IEC TC57, IEC 61850
    TEST_ASSERT_EQUAL_HEX16(0x88AB, ETHERTYPE_POWERLINK); // B&R Industrial Automation, ETHERNET Powerlink

    TEST_ASSERT_EQUAL_INT(6, ETH_ALEN);        // RFC 894: the Ethernet 48-bit address
    TEST_ASSERT_EQUAL_INT(14, ETH_HDR_LEN);    // dst(6) + src(6) + type(2)
    TEST_ASSERT_EQUAL_INT(18, ETH_VLAN_HDR_LEN); // ... + the 4-octet 802.1Q tag
}

// The Ethernet II header is destination MAC, source MAC, then a big-endian EtherType, and RFC 894
// fixes 0800h as the type of a frame carrying IPv4.
void test_ethernet_ii_header_layout(void)
{
    static const uint8_t PAYLOAD[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t out[64];
    size_t n = protocore_eth_build(DST, SRC, ETHERTYPE_IPV4, PAYLOAD, sizeof(PAYLOAD), out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(ETH_HDR_LEN + sizeof(PAYLOAD), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DST, out, ETH_ALEN);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SRC, out + ETH_ALEN, ETH_ALEN);
    TEST_ASSERT_EQUAL_HEX8(0x08, out[12]); // big-endian on the wire
    TEST_ASSERT_EQUAL_HEX8(0x00, out[13]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, out + ETH_HDR_LEN, sizeof(PAYLOAD));

    EthFrame f;
    TEST_ASSERT_TRUE(protocore_eth_parse(out, n, &f));
    TEST_ASSERT_FALSE(f.vlan);
    TEST_ASSERT_EQUAL_HEX16(ETHERTYPE_IPV4, f.ethertype);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DST, f.dst, ETH_ALEN);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SRC, f.src, ETH_ALEN);
    TEST_ASSERT_EQUAL_UINT(sizeof(PAYLOAD), f.payload_len);
    TEST_ASSERT_EQUAL_PTR(out + ETH_HDR_LEN, f.payload); // the payload points into the frame
}

// IEEE 802.1Q: the tag is a TPID of 8100h followed by a TCI whose top three bits are the PCP, next
// bit the DEI and low twelve the VID. The customer EtherType moves four octets later.
void test_8021q_tag_layout(void)
{
    static const uint8_t PAYLOAD[2] = {0xAA, 0xBB};
    uint8_t out[64];
    size_t n = protocore_eth_build_vlan(DST, SRC, 6, PROTO_TRUE, 0x0ABC, ETHERTYPE_PROFINET, PAYLOAD, sizeof(PAYLOAD),
                                        out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(ETH_VLAN_HDR_LEN + sizeof(PAYLOAD), n);
    TEST_ASSERT_EQUAL_HEX8(0x81, out[12]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[13]);
    // PCP 6 = 110b, DEI 1, VID ABCh -> 1101 1010 1011 1100 = DABC
    TEST_ASSERT_EQUAL_HEX8(0xDA, out[14]);
    TEST_ASSERT_EQUAL_HEX8(0xBC, out[15]);
    TEST_ASSERT_EQUAL_HEX8(0x88, out[16]);
    TEST_ASSERT_EQUAL_HEX8(0x92, out[17]);

    EthFrame f;
    TEST_ASSERT_TRUE(protocore_eth_parse(out, n, &f));
    TEST_ASSERT_TRUE(f.vlan);
    TEST_ASSERT_EQUAL_UINT8(6, f.pcp);
    TEST_ASSERT_EQUAL_UINT16(0x0ABC, f.vid);
    TEST_ASSERT_EQUAL_HEX16(ETHERTYPE_PROFINET, f.ethertype);
    TEST_ASSERT_EQUAL_UINT(sizeof(PAYLOAD), f.payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, f.payload, sizeof(PAYLOAD));
}

// The PCP is three bits and the VID twelve, so each field's extremes must survive and neither may
// bleed into the other.
void test_vlan_tci_field_widths(void)
{
    uint8_t out[32];
    EthFrame f;

    for (uint8_t pcp = 0; pcp < 8; pcp++)
    {
        TEST_ASSERT_EQUAL_UINT(ETH_VLAN_HDR_LEN,
                               protocore_eth_build_vlan(DST, SRC, pcp, PROTO_FALSE, 0x0FFF, ETHERTYPE_IPV4, NULL, 0,
                                                        out, sizeof(out)));
        TEST_ASSERT_TRUE(protocore_eth_parse(out, ETH_VLAN_HDR_LEN, &f));
        TEST_ASSERT_EQUAL_UINT8(pcp, f.pcp);
        TEST_ASSERT_EQUAL_UINT16(0x0FFF, f.vid);
    }

    static const uint16_t VIDS[5] = {0, 1, 100, 4094, 4095};
    for (size_t i = 0; i < 5; i++)
    {
        TEST_ASSERT_EQUAL_UINT(ETH_VLAN_HDR_LEN,
                               protocore_eth_build_vlan(DST, SRC, 0, PROTO_FALSE, VIDS[i], ETHERTYPE_IPV4, NULL, 0, out,
                                                        sizeof(out)));
        TEST_ASSERT_TRUE(protocore_eth_parse(out, ETH_VLAN_HDR_LEN, &f));
        TEST_ASSERT_EQUAL_UINT16(VIDS[i], f.vid);
        TEST_ASSERT_EQUAL_UINT8(0, f.pcp);
    }

    // the DEI is bit 12 of the TCI and belongs to neither the PCP nor the VID
    TEST_ASSERT_EQUAL_UINT(ETH_VLAN_HDR_LEN,
                           protocore_eth_build_vlan(DST, SRC, 0, PROTO_TRUE, 0, ETHERTYPE_IPV4, NULL, 0, out,
                                                    sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x10, out[14]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[15]);
}

// A parser that reads the tag as a type would see 8100h; the two framings must stay distinct and
// each must round trip its own way, for every ethertype the raw-L2 protocols use.
void test_tagged_and_untagged_stay_distinct(void)
{
    static const uint16_t TYPES[5] = {ETHERTYPE_IPV4, ETHERTYPE_ARP, ETHERTYPE_PROFINET, ETHERTYPE_GOOSE,
                                      ETHERTYPE_POWERLINK};
    static const uint8_t PAYLOAD[3] = {1, 2, 3};
    uint8_t plain[64], tagged[64];
    EthFrame a, b;

    for (size_t i = 0; i < 5; i++)
    {
        size_t pn = protocore_eth_build(DST, SRC, TYPES[i], PAYLOAD, sizeof(PAYLOAD), plain, sizeof(plain));
        size_t tn = protocore_eth_build_vlan(DST, SRC, 5, PROTO_FALSE, 42, TYPES[i], PAYLOAD, sizeof(PAYLOAD), tagged,
                                             sizeof(tagged));
        TEST_ASSERT_EQUAL_UINT(tn, pn + 4); // the tag is exactly four octets longer

        TEST_ASSERT_TRUE(protocore_eth_parse(plain, pn, &a));
        TEST_ASSERT_TRUE(protocore_eth_parse(tagged, tn, &b));
        TEST_ASSERT_FALSE(a.vlan);
        TEST_ASSERT_TRUE(b.vlan);
        TEST_ASSERT_EQUAL_HEX16(TYPES[i], a.ethertype);
        TEST_ASSERT_EQUAL_HEX16(TYPES[i], b.ethertype);
        TEST_ASSERT_EQUAL_UINT(a.payload_len, b.payload_len);
        TEST_ASSERT_EQUAL_INT(0, memcmp(a.payload, b.payload, a.payload_len));
    }
}

// CRC-32/ISO-HDLC, the algorithm of the IEEE 802.3 frame check sequence, publishes a check value:
// the CRC of the nine ASCII octets "123456789" is 0xCBF43926.
void test_fcs_published_check_value(void)
{
    static const uint8_t CHECK[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, protocore_eth_fcs(CHECK, sizeof(CHECK)));

    // the empty message: init FFFFFFFF with no octets folded in, then the final XOR of FFFFFFFF
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, protocore_eth_fcs(CHECK, 0));

    // a single flipped bit anywhere changes the FCS, which is the whole point of carrying one
    uint8_t flipped[9];
    memcpy(flipped, CHECK, sizeof(CHECK));
    flipped[4] ^= 0x01;
    TEST_ASSERT_TRUE(protocore_eth_fcs(flipped, sizeof(flipped)) != 0xCBF43926u);
}

// A frame shorter than its own header is not a frame; a tagged frame cut inside the tag is not one
// either.
void test_parse_refuses_a_short_frame(void)
{
    uint8_t out[64];
    EthFrame f;

    size_t n = protocore_eth_build(DST, SRC, ETHERTYPE_IPV4, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(ETH_HDR_LEN, n);
    for (size_t i = 0; i < ETH_HDR_LEN; i++)
    {
        TEST_ASSERT_FALSE(protocore_eth_parse(out, i, &f));
    }
    TEST_ASSERT_TRUE(protocore_eth_parse(out, ETH_HDR_LEN, &f));
    TEST_ASSERT_EQUAL_UINT(0u, f.payload_len);

    n = protocore_eth_build_vlan(DST, SRC, 0, PROTO_FALSE, 1, ETHERTYPE_IPV4, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(ETH_VLAN_HDR_LEN, n);
    for (size_t i = ETH_HDR_LEN; i < ETH_VLAN_HDR_LEN; i++)
    {
        TEST_ASSERT_FALSE(protocore_eth_parse(out, i, &f)); // the tag says more is coming
    }
    TEST_ASSERT_TRUE(protocore_eth_parse(out, ETH_VLAN_HDR_LEN, &f));

    TEST_ASSERT_FALSE(protocore_eth_parse(NULL, ETH_HDR_LEN, &f));
    TEST_ASSERT_FALSE(protocore_eth_parse(out, ETH_HDR_LEN, NULL));
}

// A builder given less room than the frame needs writes nothing, and a null address or a nonzero
// payload length with a null pointer is refused rather than dereferenced.
void test_build_refuses_bad_arguments(void)
{
    static const uint8_t PAYLOAD[4] = {1, 2, 3, 4};
    uint8_t out[64];

    for (size_t cap = 0; cap < ETH_HDR_LEN + sizeof(PAYLOAD); cap++)
    {
        TEST_ASSERT_EQUAL_UINT(0u, protocore_eth_build(DST, SRC, ETHERTYPE_IPV4, PAYLOAD, sizeof(PAYLOAD), out, cap));
    }
    for (size_t cap = 0; cap < ETH_VLAN_HDR_LEN + sizeof(PAYLOAD); cap++)
    {
        TEST_ASSERT_EQUAL_UINT(0u, protocore_eth_build_vlan(DST, SRC, 0, PROTO_FALSE, 1, ETHERTYPE_IPV4, PAYLOAD,
                                                            sizeof(PAYLOAD), out, cap));
    }

    TEST_ASSERT_EQUAL_UINT(0u, protocore_eth_build(NULL, SRC, ETHERTYPE_IPV4, NULL, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_eth_build(DST, NULL, ETHERTYPE_IPV4, NULL, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_eth_build(DST, SRC, ETHERTYPE_IPV4, NULL, 0, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_eth_build(DST, SRC, ETHERTYPE_IPV4, NULL, 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u,
                           protocore_eth_build_vlan(NULL, SRC, 0, PROTO_FALSE, 1, ETHERTYPE_IPV4, NULL, 0, out,
                                                    sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u,
                           protocore_eth_build_vlan(DST, SRC, 0, PROTO_FALSE, 1, ETHERTYPE_IPV4, NULL, 4, out,
                                                    sizeof(out)));
}

// A payload of any length up to the buffer comes back byte for byte, tagged or not.
void test_payload_round_trip(void)
{
    uint8_t payload[64];
    for (size_t i = 0; i < sizeof(payload); i++)
    {
        payload[i] = (uint8_t)(i * 5 + 3);
    }
    uint8_t out[128];
    EthFrame f;

    for (size_t len = 0; len <= sizeof(payload); len++)
    {
        size_t n = protocore_eth_build(DST, SRC, ETHERTYPE_GOOSE, len ? payload : NULL, len, out, sizeof(out));
        TEST_ASSERT_EQUAL_UINT(ETH_HDR_LEN + len, n);
        TEST_ASSERT_TRUE(protocore_eth_parse(out, n, &f));
        TEST_ASSERT_EQUAL_UINT(len, f.payload_len);
        if (len)
        {
            TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, f.payload, len);
        }

        n = protocore_eth_build_vlan(DST, SRC, 3, PROTO_FALSE, 7, ETHERTYPE_GOOSE, len ? payload : NULL, len, out,
                                     sizeof(out));
        TEST_ASSERT_EQUAL_UINT(ETH_VLAN_HDR_LEN + len, n);
        TEST_ASSERT_TRUE(protocore_eth_parse(out, n, &f));
        TEST_ASSERT_EQUAL_UINT(len, f.payload_len);
        if (len)
        {
            TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, f.payload, len);
        }
    }
}
