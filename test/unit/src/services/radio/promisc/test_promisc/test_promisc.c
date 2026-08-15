// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Wi-Fi promiscuous capture helpers (services/radio/promisc/promisc.h).
//
// test_ieee80211_address_fields_by_ds_bits is the load-bearing case. IEEE 802.11 gives a frame
// three (or four) address fields whose MEANING is decided entirely by the To DS and From DS bits
// of the Frame Control field, per the "Address field contents" table of clause 9.3.2: with both
// clear Address 1 is the destination and Address 3 the BSSID, with From DS set Address 2 becomes
// the BSSID and Address 3 the source, with To DS set Address 1 becomes the BSSID and Address 3 the
// destination, and with both set a fourth address carries the source. A capture that reads the
// addresses positionally attributes every frame on a real network to the wrong station.
//
// The pcap framing is checked against the libpcap savefile layout and the tcpdump link-layer type
// registry, which assigns LINKTYPE_IEEE802_11 the value 105.

#include "services/radio/promisc/promisc.h"
#include "shared/pcap/pcap.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static const uint8_t A1[6] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11};
static const uint8_t A2[6] = {0x22, 0x22, 0x22, 0x22, 0x22, 0x22};
static const uint8_t A3[6] = {0x33, 0x33, 0x33, 0x33, 0x33, 0x33};
static const uint8_t A4[6] = {0x44, 0x44, 0x44, 0x44, 0x44, 0x44};

// A 30-octet MAC header: Frame Control(2), Duration/ID(2), Address 1..3, Sequence Control(2), and
// room for Address 4. Clause 9.2.4.1 puts Protocol Version in bits 0-1 of octet 0, Type in bits
// 2-3, Subtype in bits 4-7; To DS is bit 0 of octet 1 and From DS bit 1.
static void header(uint8_t *f, uint8_t fc0, uint8_t fc1, uint16_t seq_ctrl)
{
    memset(f, 0, 32);
    f[0] = fc0;
    f[1] = fc1;
    f[2] = 0x00; // Duration/ID
    f[3] = 0x00;
    memcpy(f + 4, A1, 6);
    memcpy(f + 10, A2, 6);
    memcpy(f + 16, A3, 6);
    f[22] = (uint8_t)(seq_ctrl & 0xFF); // Sequence Control, least significant octet first
    f[23] = (uint8_t)(seq_ctrl >> 8);
    memcpy(f + 24, A4, 6);
}

// Clause 9.3.2 "Address field contents": the four To DS / From DS combinations.
void test_ieee80211_address_fields_by_ds_bits(void)
{
    uint8_t f[32];
    WifiFrameInfo w;

    // To DS 0, From DS 0 (IBSS or management): A1 = DA, A2 = SA, A3 = BSSID.
    header(f, 0x08, 0x00, 0); // Type 2 (Data), Subtype 0
    TEST_ASSERT_TRUE(wifi_frame_parse(f, 24, &w));
    TEST_ASSERT_EQUAL_PTR(f + 4, w.dst);
    TEST_ASSERT_EQUAL_PTR(f + 10, w.src);
    TEST_ASSERT_EQUAL_PTR(f + 16, w.bssid);

    // To DS 0, From DS 1 (AP to station): A1 = DA, A2 = BSSID, A3 = SA.
    header(f, 0x08, 0x02, 0);
    TEST_ASSERT_TRUE(wifi_frame_parse(f, 24, &w));
    TEST_ASSERT_FALSE(w.to_ds);
    TEST_ASSERT_TRUE(w.from_ds);
    TEST_ASSERT_EQUAL_PTR(f + 4, w.dst);
    TEST_ASSERT_EQUAL_PTR(f + 10, w.bssid);
    TEST_ASSERT_EQUAL_PTR(f + 16, w.src);

    // To DS 1, From DS 0 (station to AP): A1 = BSSID, A2 = SA, A3 = DA.
    header(f, 0x08, 0x01, 0);
    TEST_ASSERT_TRUE(wifi_frame_parse(f, 24, &w));
    TEST_ASSERT_TRUE(w.to_ds);
    TEST_ASSERT_FALSE(w.from_ds);
    TEST_ASSERT_EQUAL_PTR(f + 4, w.bssid);
    TEST_ASSERT_EQUAL_PTR(f + 10, w.src);
    TEST_ASSERT_EQUAL_PTR(f + 16, w.dst);

    // To DS 1, From DS 1 (WDS): A3 = DA, A4 = SA, and there is no single BSSID field.
    header(f, 0x08, 0x03, 0);
    TEST_ASSERT_TRUE(wifi_frame_parse(f, 30, &w));
    TEST_ASSERT_EQUAL_UINT16(30, w.hdr_len);
    TEST_ASSERT_EQUAL_PTR(f + 16, w.dst);
    TEST_ASSERT_EQUAL_PTR(f + 24, w.src);
    TEST_ASSERT_NULL(w.bssid);
}

// Clause 9.2.4.1.3: Type 0 is Management, 1 Control, 2 Data, 3 Extension, and a Beacon is
// Management Subtype 8 - so a Beacon's first Frame Control octet is 0x80.
void test_ieee80211_frame_control_type_and_subtype(void)
{
    uint8_t f[32];
    WifiFrameInfo w;

    header(f, 0x80, 0x00, 0); // Subtype 8, Type 0
    TEST_ASSERT_TRUE(wifi_frame_parse(f, 24, &w));
    TEST_ASSERT_EQUAL_INT(WIFI_FT_MGMT, w.type);
    TEST_ASSERT_EQUAL_UINT8(8, w.subtype);

    header(f, 0x08, 0x00, 0); // Subtype 0, Type 2
    TEST_ASSERT_TRUE(wifi_frame_parse(f, 24, &w));
    TEST_ASSERT_EQUAL_INT(WIFI_FT_DATA, w.type);
    TEST_ASSERT_EQUAL_UINT8(0, w.subtype);

    header(f, 0xB0, 0x00, 0); // Subtype 11 (Authentication), Type 0
    TEST_ASSERT_TRUE(wifi_frame_parse(f, 24, &w));
    TEST_ASSERT_EQUAL_INT(WIFI_FT_MGMT, w.type);
    TEST_ASSERT_EQUAL_UINT8(11, w.subtype);

    header(f, 0x0C, 0x00, 0); // Type 3 (Extension)
    TEST_ASSERT_TRUE(wifi_frame_parse(f, 24, &w));
    TEST_ASSERT_EQUAL_INT(WIFI_FT_EXT, w.type);

    // The Protocol Version bits are octet 0 bits 0-1 and belong to neither field.
    header(f, 0x83, 0x00, 0);
    TEST_ASSERT_TRUE(wifi_frame_parse(f, 24, &w));
    TEST_ASSERT_EQUAL_INT(WIFI_FT_MGMT, w.type);
    TEST_ASSERT_EQUAL_UINT8(8, w.subtype);
}

// Clause 9.2.4.4: the Sequence Control field is the Fragment Number in bits 0-3 and the 12-bit
// Sequence Number in bits 4-15, the field itself least significant octet first.
void test_ieee80211_sequence_number(void)
{
    uint8_t f[32];
    WifiFrameInfo w;

    // Field value 0x1237: fragment 7, sequence number 0x123.
    header(f, 0x08, 0x00, 0x1237);
    TEST_ASSERT_TRUE(wifi_frame_parse(f, 24, &w));
    TEST_ASSERT_EQUAL_HEX16(0x123, w.seq);

    // The 12-bit field wraps at 4095, so 0xFFF is its largest value.
    header(f, 0x08, 0x00, 0xFFF0);
    TEST_ASSERT_TRUE(wifi_frame_parse(f, 24, &w));
    TEST_ASSERT_EQUAL_HEX16(0xFFF, w.seq);

    header(f, 0x08, 0x00, 0x0000);
    TEST_ASSERT_TRUE(wifi_frame_parse(f, 24, &w));
    TEST_ASSERT_EQUAL_HEX16(0, w.seq);
}

// The MAC header is 24 octets, plus 6 for a fourth address, plus 2 for the QoS Control field a
// QoS Data subtype carries (Subtype bit 3 set), plus 4 for the HT Control field the +HTC/Order bit
// of Frame Control adds.
void test_ieee80211_header_length(void)
{
    uint8_t f[32];
    WifiFrameInfo w;

    header(f, 0x08, 0x00, 0); // Data, not QoS
    TEST_ASSERT_TRUE(wifi_frame_parse(f, 24, &w));
    TEST_ASSERT_EQUAL_UINT16(24, w.hdr_len);
    TEST_ASSERT_FALSE(w.is_qos);

    header(f, 0x88, 0x00, 0); // QoS Data: Subtype 8
    TEST_ASSERT_TRUE(wifi_frame_parse(f, 26, &w));
    TEST_ASSERT_TRUE(w.is_qos);
    TEST_ASSERT_EQUAL_UINT16(26, w.hdr_len);

    header(f, 0x88, 0x80, 0); // QoS Data with +HTC set: 24 + 2 + 4
    TEST_ASSERT_TRUE(wifi_frame_parse(f, 30, &w));
    TEST_ASSERT_EQUAL_UINT16(30, w.hdr_len);

    header(f, 0x88, 0x03, 0); // QoS Data, WDS: 24 + 6 + 2
    TEST_ASSERT_TRUE(wifi_frame_parse(f, 32, &w));
    TEST_ASSERT_EQUAL_UINT16(32, w.hdr_len);

    // Management frames carry no QoS Control even at a Subtype with bit 3 set.
    header(f, 0x88 ^ 0x08, 0x00, 0);
    TEST_ASSERT_TRUE(wifi_frame_parse(f, 24, &w));
    TEST_ASSERT_FALSE(w.is_qos);
}

// A Control frame carries only Address 1 (the receiver); the rest of its layout varies by subtype,
// so nothing past octet 10 is read.
void test_ieee80211_control_frame(void)
{
    uint8_t f[32];
    WifiFrameInfo w;
    header(f, 0xD4, 0x00, 0); // Type 1 (Control), Subtype 13 (Acknowledgment)
    TEST_ASSERT_TRUE(wifi_frame_parse(f, 10, &w));
    TEST_ASSERT_EQUAL_INT(WIFI_FT_CTRL, w.type);
    TEST_ASSERT_EQUAL_UINT8(13, w.subtype);
    TEST_ASSERT_EQUAL_UINT16(10, w.hdr_len);
    TEST_ASSERT_EQUAL_PTR(f + 4, w.dst);
    TEST_ASSERT_NULL(w.src);
    TEST_ASSERT_NULL(w.bssid);
}

// Frame Control bit 14 is the Protected Frame bit, which says the Frame Body is encrypted.
void test_ieee80211_protected_frame_bit(void)
{
    uint8_t f[32];
    WifiFrameInfo w;
    header(f, 0x08, 0x40, 0);
    TEST_ASSERT_TRUE(wifi_frame_parse(f, 24, &w));
    TEST_ASSERT_TRUE(w.protected_frame);

    header(f, 0x08, 0x00, 0);
    TEST_ASSERT_TRUE(wifi_frame_parse(f, 24, &w));
    TEST_ASSERT_FALSE(w.protected_frame);
}

// A frame shorter than the header its own bits imply is refused rather than read past.
void test_parse_refuses_a_short_frame(void)
{
    uint8_t f[32];
    WifiFrameInfo w;

    header(f, 0x08, 0x00, 0);
    TEST_ASSERT_FALSE(wifi_frame_parse(f, 9, &w));  // shorter than Frame Control + Duration + A1
    TEST_ASSERT_FALSE(wifi_frame_parse(f, 23, &w)); // a 3-address header needs 24

    header(f, 0x88, 0x00, 0);
    TEST_ASSERT_FALSE(wifi_frame_parse(f, 25, &w)); // QoS Data needs 26

    header(f, 0x08, 0x03, 0);
    TEST_ASSERT_FALSE(wifi_frame_parse(f, 29, &w)); // WDS needs 30

    TEST_ASSERT_FALSE(wifi_frame_parse(NULL, 24, &w));
    TEST_ASSERT_FALSE(wifi_frame_parse(f, 24, NULL));
}

// The libpcap savefile global header: the 0xa1b2c3d4 magic that declares microsecond timestamps
// and the writer's byte order, version 2.4, a zero GMT offset and accuracy, the snapshot length,
// and the link type - LINKTYPE_IEEE802_11 is 105 in the tcpdump registry. Every field is written
// in the writer's own byte order, little-endian here, which is what the magic tells a reader.
void test_pcap_global_header_declares_ieee80211(void)
{
    uint8_t out[32];
    memset(out, 0xAA, sizeof(out));
    Pcap.args.out = out;
    Pcap.args.cap = sizeof(out);
    Pcap.args.linktype = PROTOCORE_DLT_IEEE802_11;
    Pcap.global_header(Pcap.internal);
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_PCAP_GLOBAL_HDR_LEN, Pcap.n);

    static const uint8_t WANT[24] = {
        0xd4, 0xc3, 0xb2, 0xa1, // magic 0xa1b2c3d4, little-endian
        0x02, 0x00, 0x04, 0x00, // version 2.4
        0x00, 0x00, 0x00, 0x00, // thiszone: GMT
        0x00, 0x00, 0x00, 0x00, // sigfigs
        0xff, 0xff, 0x00, 0x00, // snaplen 65535
        105,  0x00, 0x00, 0x00, // network: LINKTYPE_IEEE802_11
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));
    TEST_ASSERT_EQUAL_INT(105, PROTOCORE_DLT_IEEE802_11);
}

// The per-record header: capture time in whole seconds and microseconds, then the octets stored
// and the octets the frame had on the wire - four little-endian 32-bit fields, 16 octets.
void test_pcap_record_header(void)
{
    uint8_t out[24];
    memset(out, 0xAA, sizeof(out));
    Pcap.args.out = out;
    Pcap.args.cap = sizeof(out);
    Pcap.rec.ts_sec = 0x01020304u;
    Pcap.rec.ts_usec = 999999u; // the largest microsecond offset within a second, 0x000F423F
    Pcap.rec.caplen = 60;
    Pcap.rec.origlen = 1500;
    Pcap.record_header(Pcap.internal);
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_PCAP_REC_HDR_LEN, Pcap.n);

    static const uint8_t WANT[16] = {
        0x04, 0x03, 0x02, 0x01, // ts_sec
        0x3f, 0x42, 0x0f, 0x00, // ts_usec 999999
        0x3c, 0x00, 0x00, 0x00, // caplen 60
        0xdc, 0x05, 0x00, 0x00, // origlen 1500
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));
}

// Neither header is written into a region too small to hold it, and neither is written through a
// null one: a short pcap header makes the whole capture file unreadable.
void test_pcap_headers_fail_closed(void)
{
    uint8_t out[24];
    Pcap.args.out = out;
    Pcap.args.cap = PROTOCORE_PCAP_GLOBAL_HDR_LEN - 1;
    Pcap.args.linktype = PROTOCORE_DLT_IEEE802_11;
    Pcap.global_header(Pcap.internal);
    TEST_ASSERT_EQUAL_size_t(0, Pcap.n);

    Pcap.args.out = NULL;
    Pcap.args.cap = sizeof(out);
    Pcap.global_header(Pcap.internal);
    TEST_ASSERT_EQUAL_size_t(0, Pcap.n);

    Pcap.args.out = out;
    Pcap.args.cap = PROTOCORE_PCAP_REC_HDR_LEN - 1;
    Pcap.record_header(Pcap.internal);
    TEST_ASSERT_EQUAL_size_t(0, Pcap.n);
}

// Capture is passive and needs a radio; this build has none, so bring-up answers no and the
// channel and stop calls are inert.
void test_capture_reports_no_radio(void)
{
    TEST_ASSERT_FALSE(protocore_promisc_begin(6, NULL));
    protocore_promisc_set_channel(11);
    protocore_promisc_end();
}
