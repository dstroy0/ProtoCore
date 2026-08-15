// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the libpcap savefile headers (shared/pcap/pcap.h).
//
// The file this writes is read by Wireshark and tcpdump, so the octets are the contract. The
// savefile format fixes them: a 24-octet global header opening with magic 0xa1b2c3d4 (that spelling
// is what declares microsecond timestamps AND little-endian field order - the byte-swapped
// 0xd4c3b2a1 is how a reader detects the other endianness), version major 2 minor 4, then a
// 16-octet per-record header of ts_sec / ts_usec / caplen / origlen.
//
// Each field is asserted octet by octet at its own offset rather than by memcmp against a blob, so
// a failure names the field that moved.

#include "shared/pcap/pcap.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Read a little-endian field back out of the buffer, the way a savefile reader would.
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

void test_global_header_is_24_octets(void)
{
    uint8_t out[PROTOCORE_PCAP_GLOBAL_HDR_LEN];
    Pcap.args.out = out;
    Pcap.args.cap = sizeof(out);
    Pcap.args.linktype = PROTOCORE_DLT_ETHERNET;
    Pcap.global_header(Pcap.internal);
    TEST_ASSERT_EQUAL_UINT(24u, Pcap.n);
    TEST_ASSERT_EQUAL_UINT(24u, (unsigned)PROTOCORE_PCAP_GLOBAL_HDR_LEN);
}

// Field by field, at the offsets the format fixes.
void test_global_header_fields(void)
{
    uint8_t out[PROTOCORE_PCAP_GLOBAL_HDR_LEN];
    Pcap.args.out = out;
    Pcap.args.cap = sizeof(out);
    Pcap.args.linktype = PROTOCORE_DLT_IEEE802_11;
    Pcap.global_header(Pcap.internal);

    TEST_ASSERT_EQUAL_HEX32(0xa1b2c3d4u, le32(out + 0)); // usec timestamps, little-endian
    TEST_ASSERT_EQUAL_UINT16(2u, le16(out + 4));         // version major
    TEST_ASSERT_EQUAL_UINT16(4u, le16(out + 6));         // version minor
    TEST_ASSERT_EQUAL_INT32(0, (int32_t)le32(out + 8));  // thiszone: GMT
    TEST_ASSERT_EQUAL_UINT32(0u, le32(out + 12));        // sigfigs
    TEST_ASSERT_EQUAL_UINT32(65535u, le32(out + 16));    // snaplen
    TEST_ASSERT_EQUAL_UINT32((uint32_t)PROTOCORE_DLT_IEEE802_11, le32(out + 20));
}

// The magic must be written as octets D4 C3 B2 A1 in file order. A reader that sees A1 B2 C3 D4
// concludes the file is the other endianness and byte-swaps every field it reads after it.
void test_magic_octet_order_declares_little_endian(void)
{
    uint8_t out[PROTOCORE_PCAP_GLOBAL_HDR_LEN];
    Pcap.args.out = out;
    Pcap.args.cap = sizeof(out);
    Pcap.args.linktype = PROTOCORE_DLT_RAW;
    Pcap.global_header(Pcap.internal);
    TEST_ASSERT_EQUAL_HEX8(0xD4u, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xC3u, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0xB2u, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0xA1u, out[3]);
}

// The link type is what tells the reader how to dissect every frame that follows, so each DLT the
// module names has to reach the file unchanged.
void test_linktype_reaches_the_file(void)
{
    static const uint32_t DLT[] = {
        PROTOCORE_DLT_ETHERNET,           PROTOCORE_DLT_IEEE802_11,       PROTOCORE_DLT_CAN_SOCKETCAN,
        PROTOCORE_DLT_IEEE802_15_4_NOFCS, PROTOCORE_DLT_IEEE802_15_4_TAP, PROTOCORE_DLT_RAW,
    };
    for (size_t i = 0; i < sizeof(DLT) / sizeof(DLT[0]); i++)
    {
        uint8_t out[PROTOCORE_PCAP_GLOBAL_HDR_LEN];
        Pcap.args.out = out;
        Pcap.args.cap = sizeof(out);
        Pcap.args.linktype = DLT[i];
        Pcap.global_header(Pcap.internal);
        TEST_ASSERT_EQUAL_UINT32(DLT[i], le32(out + 20));
    }
}

void test_record_header_is_16_octets(void)
{
    uint8_t out[PROTOCORE_PCAP_REC_HDR_LEN];
    Pcap.args.out = out;
    Pcap.args.cap = sizeof(out);
    Pcap.rec.ts_sec = 1u;
    Pcap.rec.ts_usec = 2u;
    Pcap.rec.caplen = 3u;
    Pcap.rec.origlen = 4u;
    Pcap.record_header(Pcap.internal);
    TEST_ASSERT_EQUAL_UINT(16u, Pcap.n);
    TEST_ASSERT_EQUAL_UINT(16u, (unsigned)PROTOCORE_PCAP_REC_HDR_LEN);
}

// caplen and origlen are separate fields: a frame captured with a snaplen shorter than the wire
// length stores caplen octets but reports the original size, and a dissector reads both.
void test_record_header_fields(void)
{
    uint8_t out[PROTOCORE_PCAP_REC_HDR_LEN];
    Pcap.args.out = out;
    Pcap.args.cap = sizeof(out);
    Pcap.rec.ts_sec = 0x5A5A5A5Au;
    Pcap.rec.ts_usec = 999999u; // the largest a microsecond field carries
    Pcap.rec.caplen = 128u;
    Pcap.rec.origlen = 1514u; // full Ethernet frame, truncated in the capture
    Pcap.record_header(Pcap.internal);

    TEST_ASSERT_EQUAL_HEX32(0x5A5A5A5Au, le32(out + 0));
    TEST_ASSERT_EQUAL_UINT32(999999u, le32(out + 4));
    TEST_ASSERT_EQUAL_UINT32(128u, le32(out + 8));
    TEST_ASSERT_EQUAL_UINT32(1514u, le32(out + 12));
}

// A buffer one octet short of either header writes nothing and reports 0, so a partial header
// never reaches a file a reader would then reject wholesale.
void test_short_buffers_write_nothing(void)
{
    uint8_t small[PROTOCORE_PCAP_GLOBAL_HDR_LEN - 1];
    memset(small, 0xEE, sizeof(small));
    Pcap.args.out = small;
    Pcap.args.cap = sizeof(small);
    Pcap.args.linktype = PROTOCORE_DLT_ETHERNET;
    Pcap.global_header(Pcap.internal);
    TEST_ASSERT_EQUAL_UINT(0u, Pcap.n);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, small[0]);

    uint8_t rsmall[PROTOCORE_PCAP_REC_HDR_LEN - 1];
    memset(rsmall, 0xEE, sizeof(rsmall));
    Pcap.args.out = rsmall;
    Pcap.args.cap = sizeof(rsmall);
    Pcap.rec.ts_sec = 1u;
    Pcap.rec.ts_usec = 1u;
    Pcap.rec.caplen = 1u;
    Pcap.rec.origlen = 1u;
    Pcap.record_header(Pcap.internal);
    TEST_ASSERT_EQUAL_UINT(0u, Pcap.n);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, rsmall[0]);
}
