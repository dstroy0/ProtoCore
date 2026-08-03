// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the Wi-Fi promiscuous capture helpers (services/radio/promisc): the pure 802.11 MAC
// header parser (type/subtype + the to/from-DS src/dst/bssid layouts, QoS, WDS, control frames,
// malformed rejection) and the libpcap header framing. Pure host tests.

#include "services/radio/promisc/promisc.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

static const uint8_t A_DST[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const uint8_t A_AP[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
static const uint8_t A_CLI[6] = {0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb};
static const uint8_t A_SRC[6] = {0xde, 0xad, 0xbe, 0xef, 0x00, 0x01};

// Build a 3-address header into f (24 bytes): fc0, fc1, then a1,a2,a3, seq.
static void hdr3(uint8_t *f, uint8_t fc0, uint8_t fc1, const uint8_t *a1, const uint8_t *a2, const uint8_t *a3,
                 uint16_t seq)
{
    memset(f, 0, 24);
    f[0] = fc0;
    f[1] = fc1;
    memcpy(f + 4, a1, 6);
    memcpy(f + 10, a2, 6);
    memcpy(f + 16, a3, 6);
    uint16_t sc = (uint16_t)(seq << 4);
    f[22] = (uint8_t)sc;
    f[23] = (uint8_t)(sc >> 8);
}

void test_beacon_mgmt()
{
    uint8_t f[64];
    // Mgmt (type 0), Beacon (subtype 8): fc0 = (8<<4)|(0<<2) = 0x80; no DS bits.
    hdr3(f, 0x80, 0x00, A_DST, A_AP, A_AP, 1);
    WifiFrameInfo fi;
    TEST_ASSERT_TRUE(wifi_frame_parse(f, sizeof(f), &fi));
    TEST_ASSERT_EQUAL_UINT8(WIFI_FT_MGMT, fi.type);
    TEST_ASSERT_EQUAL_UINT8(8, fi.subtype);
    TEST_ASSERT_FALSE(fi.to_ds);
    TEST_ASSERT_FALSE(fi.from_ds);
    TEST_ASSERT_EQUAL_UINT16(1, fi.seq);
    TEST_ASSERT_EQUAL_UINT16(24, fi.hdr_len);
    TEST_ASSERT_EQUAL_MEMORY(A_DST, fi.dst, 6);
    TEST_ASSERT_EQUAL_MEMORY(A_AP, fi.src, 6);
    TEST_ASSERT_EQUAL_MEMORY(A_AP, fi.bssid, 6);
}

void test_data_from_ds()
{
    uint8_t f[64];
    // Data (type 2), from the AP: fc0 = (0<<4)|(2<<2) = 0x08; from_ds = 0x02.
    // a1 = DST, a2 = BSSID, a3 = SRC.
    hdr3(f, 0x08, 0x02, A_CLI, A_AP, A_SRC, 7);
    WifiFrameInfo fi;
    TEST_ASSERT_TRUE(wifi_frame_parse(f, sizeof(f), &fi));
    TEST_ASSERT_EQUAL_UINT8(WIFI_FT_DATA, fi.type);
    TEST_ASSERT_FALSE(fi.to_ds);
    TEST_ASSERT_TRUE(fi.from_ds);
    TEST_ASSERT_EQUAL_MEMORY(A_CLI, fi.dst, 6);
    TEST_ASSERT_EQUAL_MEMORY(A_SRC, fi.src, 6);
    TEST_ASSERT_EQUAL_MEMORY(A_AP, fi.bssid, 6);
}

void test_data_to_ds()
{
    uint8_t f[64];
    // Data to the AP: to_ds = 0x01. a1 = BSSID, a2 = SRC, a3 = DST.
    hdr3(f, 0x08, 0x01, A_AP, A_CLI, A_SRC, 0);
    WifiFrameInfo fi;
    TEST_ASSERT_TRUE(wifi_frame_parse(f, sizeof(f), &fi));
    TEST_ASSERT_TRUE(fi.to_ds);
    TEST_ASSERT_FALSE(fi.from_ds);
    TEST_ASSERT_EQUAL_MEMORY(A_AP, fi.bssid, 6);
    TEST_ASSERT_EQUAL_MEMORY(A_CLI, fi.src, 6);
    TEST_ASSERT_EQUAL_MEMORY(A_SRC, fi.dst, 6);
}

void test_qos_data_header_len()
{
    uint8_t f[64];
    // QoS Data subtype 8: fc0 = (8<<4)|(2<<2) = 0x88. Adds a 2-byte QoS Control field.
    hdr3(f, 0x88, 0x00, A_DST, A_AP, A_AP, 3);
    WifiFrameInfo fi;
    TEST_ASSERT_TRUE(wifi_frame_parse(f, sizeof(f), &fi));
    TEST_ASSERT_TRUE(fi.is_qos);
    TEST_ASSERT_EQUAL_UINT16(26, fi.hdr_len);
}

void test_wds_four_address()
{
    uint8_t f[64];
    // WDS: to_ds & from_ds set (fc1 = 0x03). Addr4 at offset 24; DST = a3, SRC = a4.
    hdr3(f, 0x08, 0x03, A_AP, A_CLI, A_DST, 0);
    memcpy(f + 24, A_SRC, 6);
    WifiFrameInfo fi;
    TEST_ASSERT_TRUE(wifi_frame_parse(f, sizeof(f), &fi));
    TEST_ASSERT_TRUE(fi.to_ds);
    TEST_ASSERT_TRUE(fi.from_ds);
    TEST_ASSERT_EQUAL_UINT16(30, fi.hdr_len);
    TEST_ASSERT_EQUAL_MEMORY(A_DST, fi.dst, 6);
    TEST_ASSERT_EQUAL_MEMORY(A_SRC, fi.src, 6);
    TEST_ASSERT_NULL(fi.bssid);
}

void test_control_frame()
{
    // ACK (type 1, subtype 13): fc0 = (13<<4)|(1<<2) = 0xD4. Only Addr1 (RA), 10-byte header.
    uint8_t f[10] = {0xD4, 0x00, 0x00, 0x00, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb};
    WifiFrameInfo fi;
    TEST_ASSERT_TRUE(wifi_frame_parse(f, sizeof(f), &fi));
    TEST_ASSERT_EQUAL_UINT8(WIFI_FT_CTRL, fi.type);
    TEST_ASSERT_EQUAL_UINT16(10, fi.hdr_len);
    TEST_ASSERT_EQUAL_MEMORY(A_CLI, fi.dst, 6);
    TEST_ASSERT_NULL(fi.src);
}

void test_reject_short()
{
    WifiFrameInfo fi;
    uint8_t tiny[8] = {0x80, 0x00};
    TEST_ASSERT_FALSE(wifi_frame_parse(tiny, sizeof(tiny), &fi)); // < 10 bytes
    uint8_t partial[20];
    memset(partial, 0, sizeof(partial));
    partial[0] = 0x80; // mgmt but only 20 bytes (< 24 needed for the 3-address header)
    TEST_ASSERT_FALSE(wifi_frame_parse(partial, sizeof(partial), &fi));
    TEST_ASSERT_FALSE(wifi_frame_parse(NULL, 64, &fi));
}

void test_null_out_pointer()
{
    uint8_t f[64];
    hdr3(f, 0x80, 0x00, A_DST, A_AP, A_AP, 1);
    // frame and len are both valid; only out is null - must still be rejected.
    TEST_ASSERT_FALSE(wifi_frame_parse(f, sizeof(f), NULL));
}

void test_qos_order_bit_ht_control()
{
    uint8_t f[64];
    // QoS Data subtype 8 with the Order bit set: fc0 = 0x88, fc1 = 0x80. hlen = 24 + 2 (QoS) + 4
    // (HT Control) = 30.
    hdr3(f, 0x88, 0x80, A_DST, A_AP, A_AP, 5);
    WifiFrameInfo fi;
    TEST_ASSERT_TRUE(wifi_frame_parse(f, sizeof(f), &fi));
    TEST_ASSERT_TRUE(fi.is_qos);
    TEST_ASSERT_EQUAL_UINT16(30, fi.hdr_len);
}

void test_qos_len_less_than_hlen()
{
    uint8_t f[64];
    // QoS Data subtype 8, no DS/order bits: fc0 = 0x88, fc1 = 0x00. Computed hlen = 24 + 2 = 26,
    // but only 24 bytes are reported captured - shorter than the QoS-extended header.
    hdr3(f, 0x88, 0x00, A_DST, A_AP, A_AP, 3);
    WifiFrameInfo fi;
    TEST_ASSERT_FALSE(wifi_frame_parse(f, 24, &fi));
}

void test_pcap_headers()
{
    uint8_t g[PC_PCAP_GLOBAL_HDR_LEN];
    TEST_ASSERT_EQUAL_UINT(PC_PCAP_GLOBAL_HDR_LEN, pc_pcap_global_header(g, sizeof(g), PC_DLT_IEEE802_11));
    // Magic 0xa1b2c3d4 little-endian, and link type 105 (DLT_IEEE802_11) at offset 20.
    TEST_ASSERT_EQUAL_HEX8(0xd4, g[0]);
    TEST_ASSERT_EQUAL_HEX8(0xc3, g[1]);
    TEST_ASSERT_EQUAL_HEX8(0xb2, g[2]);
    TEST_ASSERT_EQUAL_HEX8(0xa1, g[3]);
    TEST_ASSERT_EQUAL_HEX8(105, g[20]);
    TEST_ASSERT_EQUAL_UINT(0, pc_pcap_global_header(g, 10, PC_DLT_IEEE802_11)); // too small

    uint8_t r[PC_PCAP_REC_HDR_LEN];
    TEST_ASSERT_EQUAL_UINT(PC_PCAP_REC_HDR_LEN, pc_pcap_record_header(r, sizeof(r), 0x11223344, 0x55667788, 100, 120));
    TEST_ASSERT_EQUAL_HEX8(0x44, r[0]); // ts_sec little-endian
    TEST_ASSERT_EQUAL_HEX8(0x11, r[3]);
    TEST_ASSERT_EQUAL_HEX8(100, r[8]);  // caplen
    TEST_ASSERT_EQUAL_HEX8(120, r[12]); // origlen
}

void test_host_stubs_and_short_frame()
{
    TEST_ASSERT_FALSE(pc_promisc_begin(6, NULL)); // host: esp_wifi unavailable
    pc_promisc_set_channel(11);
    pc_promisc_end();
    WifiFrameInfo info;
    uint8_t too_short[2] = {0x08, 0x00};
    TEST_ASSERT_FALSE(wifi_frame_parse(too_short, sizeof(too_short), &info));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_beacon_mgmt);
    RUN_TEST(test_data_from_ds);
    RUN_TEST(test_data_to_ds);
    RUN_TEST(test_qos_data_header_len);
    RUN_TEST(test_wds_four_address);
    RUN_TEST(test_control_frame);
    RUN_TEST(test_reject_short);
    RUN_TEST(test_null_out_pointer);
    RUN_TEST(test_qos_order_bit_ht_control);
    RUN_TEST(test_qos_len_less_than_hlen);
    RUN_TEST(test_pcap_headers);
    RUN_TEST(test_host_stubs_and_short_frame);
    return UNITY_END();
}
