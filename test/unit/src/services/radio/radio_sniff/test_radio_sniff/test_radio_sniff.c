// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the receive-only radio sniffer's pcap framing
// (services/radio/radio_sniff/radio_sniff.h).
//
// test_ieee754_binary32_rss_encoding is the load-bearing case. The IEEE 802.15.4 TAP Link Type
// Specification section 3.2 carries the received signal strength "in dBm as a IEEE-754 floating
// point number", while a radio reports it as an integer, so this module builds the binary32 bit
// pattern by hand. Each expected word below is derived from the IEEE 754 binary32 definition -
// sign, an 8-bit exponent biased by 127, and a 23-bit significand with the leading 1 implicit -
// with the arithmetic written beside it. A wrong bias or a mis-shifted significand yields a
// capture whose RSSI column is nonsense in every dissector that reads it.
//
// The rest of the record is checked against that same TAP specification (section 2.2.1 header,
// section 2.2.2 TLV format, section 3.2 RSS and section 3.4 Channel Assignment) and the libpcap
// savefile record layout, with LINKTYPE_IEEE802_15_4_TAP assigned 283 in the tcpdump registry.

#include "services/radio/radio_sniff/radio_sniff.h"
#include "shared/pcap/pcap.h"
#include <string.h>

#include <unity.h>

static uint8_t radio_sniff_work[16]; // the borrow an entry takes; RadioSniff never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// IEEE 754 binary32: [sign][exponent + 127][significand, leading 1 implicit].
void test_ieee754_binary32_rss_encoding(void)
{
    // 1 = 1.0 * 2^0 -> exponent field 127 = 0x7F, significand 0
    //     0 01111111 000... = 0x3F800000
    RadioSniff.i2f32_args.dbm = 1;
    RadioSniff.i2f32(radio_sniff_work);
    TEST_ASSERT_EQUAL_HEX32(0x3F800000u, RadioSniff.u32);
    RadioSniff.i2f32_args.dbm = -1;
    RadioSniff.i2f32(radio_sniff_work);
    TEST_ASSERT_EQUAL_HEX32(0xBF800000u, RadioSniff.u32); // the same, sign set

    // 2 = 1.0 * 2^1 -> exponent field 128 = 0x80
    //     0 10000000 000... = 0x40000000
    RadioSniff.i2f32_args.dbm = 2;
    RadioSniff.i2f32(radio_sniff_work);
    TEST_ASSERT_EQUAL_HEX32(0x40000000u, RadioSniff.u32);

    // -40 dBm, a typical near-field reading:
    //     40 = 0b101000 = 1.01 * 2^5 -> exponent field 127 + 5 = 132 = 0x84
    //     significand 0.01b -> 0100 0000 0000 0000 0000 000 = 0x200000
    //     1 10000100 01000000000000000000000
    //       = 0x80000000 | (132 << 23) | 0x200000 = 0x80000000 | 0x42000000 | 0x00200000
    RadioSniff.i2f32_args.dbm = -40;
    RadioSniff.i2f32(radio_sniff_work);
    TEST_ASSERT_EQUAL_HEX32(0xC2200000u, RadioSniff.u32);

    // -128 = -(1.0 * 2^7) -> exponent field 134 = 0x86, significand 0
    //        0x80000000 | (134 << 23) = 0x80000000 | 0x43000000
    RadioSniff.i2f32_args.dbm = -128;
    RadioSniff.i2f32(radio_sniff_work);
    TEST_ASSERT_EQUAL_HEX32(0xC3000000u, RadioSniff.u32);

    // 100 = 0b1100100 = 1.100100 * 2^6 -> exponent field 133 = 0x85
    //       significand 100100 followed by zeros = 0x480000
    //       (133 << 23) | 0x480000 = 0x42800000 | 0x00480000
    RadioSniff.i2f32_args.dbm = 100;
    RadioSniff.i2f32(radio_sniff_work);
    TEST_ASSERT_EQUAL_HEX32(0x42C80000u, RadioSniff.u32);

    // Zero has no leading 1 to make implicit; binary32 spells it as an all-zero word.
    RadioSniff.i2f32_args.dbm = 0;
    RadioSniff.i2f32(radio_sniff_work);
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, RadioSniff.u32);
}

// A magnitude wider than the 23-bit significand keeps its exponent and drops the low bits, which
// is what rounding toward zero does; the value must still be the right power of two.
void test_ieee754_binary32_wide_magnitude(void)
{
    // 2^24 = 1.0 * 2^24 -> exponent field 151 = 0x97, significand 0
    //        151 << 23 = 0x4B800000
    RadioSniff.i2f32_args.dbm = 16777216;
    RadioSniff.i2f32(radio_sniff_work);
    TEST_ASSERT_EQUAL_HEX32(0x4B800000u, RadioSniff.u32);

    // 2^30 -> exponent field 157 = 0x9D, 157 << 23 = 0x4E800000
    RadioSniff.i2f32_args.dbm = 1073741824;
    RadioSniff.i2f32(radio_sniff_work);
    TEST_ASSERT_EQUAL_HEX32(0x4E800000u, RadioSniff.u32);

    // -2^31, the most negative int32: magnitude 2^31 -> exponent field 158 = 0x9E,
    // 0x80000000 | (158 << 23) = 0x80000000 | 0x4F000000
    RadioSniff.i2f32_args.dbm = (int32_t)(-2147483647 - 1);
    RadioSniff.i2f32(radio_sniff_work);
    TEST_ASSERT_EQUAL_HEX32(0xCF000000u, RadioSniff.u32);
}

// The capture file declares its frames are TAP-framed 802.15.4: LINKTYPE_IEEE802_15_4_TAP is 283.
void test_pcap_global_header_declares_the_tap_link_type(void)
{
    uint8_t out[32];
    memset(out, 0xAA, sizeof(out));
    RadioSniff.global_header_args.out = out;
    RadioSniff.global_header_args.cap = sizeof(out);
    RadioSniff.global_header(radio_sniff_work);
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_PCAP_GLOBAL_HDR_LEN, RadioSniff.n);

    static const uint8_t WANT[24] = {
        0xd4, 0xc3, 0xb2, 0xa1, // magic 0xa1b2c3d4: microsecond timestamps, writer byte order
        0x02, 0x00, 0x04, 0x00, // version 2.4
        0x00, 0x00, 0x00, 0x00, // thiszone
        0x00, 0x00, 0x00, 0x00, // sigfigs
        0xff, 0xff, 0x00, 0x00, // snaplen 65535
        0x1b, 0x01, 0x00, 0x00, // network 283 = 0x011b
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));
    TEST_ASSERT_EQUAL_INT(283, PROTOCORE_DLT_IEEE802_15_4_TAP);

    RadioSniff.global_header_args.out = out;
    RadioSniff.global_header_args.cap = PROTOCORE_PCAP_GLOBAL_HDR_LEN - 1;
    RadioSniff.global_header(radio_sniff_work);
    TEST_ASSERT_EQUAL_size_t(0, RadioSniff.n);
    RadioSniff.global_header_args.out = NULL;
    RadioSniff.global_header_args.cap = sizeof(out);
    RadioSniff.global_header(radio_sniff_work);
    TEST_ASSERT_EQUAL_size_t(0, RadioSniff.n);
}

// One capture record, octet for octet. TAP section 2.2.1: version, one padding octet, then the
// total length of the header and its TLVs, all little-endian. Section 2.2.2: each TLV is a 16-bit
// type and a 16-bit length, its value padded to a 32-bit boundary. Section 3.2 gives RSS type 1
// with a 4-octet float, section 3.4 Channel Assignment type 3 with a 3-octet value - a 16-bit
// channel number and a channel page - padded to 4. So the whole pseudo-header is 4 + 8 + 8 = 20.
void test_tap_record_layout(void)
{
    static const uint8_t FRAME[5] = {0x41, 0x88, 0x01, 0xCD, 0xAB};
    uint8_t out[64];
    memset(out, 0xEE, sizeof(out));

    RadioSniff.tap_record_args.out = out;
    RadioSniff.tap_record_args.cap = sizeof(out);
    RadioSniff.tap_record_args.frame = FRAME;
    RadioSniff.tap_record_args.flen = sizeof(FRAME);
    RadioSniff.tap_record_args.rssi_dbm = -40;
    RadioSniff.tap_record_args.channel = 15;
    RadioSniff.tap_record_args.ts_sec = 0x01020304u;
    RadioSniff.tap_record_args.ts_usec = 999999u;
    RadioSniff.tap_record(radio_sniff_work);
    const size_t n = RadioSniff.n;
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_PCAP_REC_HDR_LEN + RADIO_SNIFF_TAP_LEN + sizeof(FRAME), n);
    TEST_ASSERT_EQUAL_INT(20, RADIO_SNIFF_TAP_LEN);

    static const uint8_t WANT[16 + 20 + 5] = {
        // pcap record header: seconds, microseconds, captured length, wire length
        0x04,
        0x03,
        0x02,
        0x01, //
        0x3f,
        0x42,
        0x0f,
        0x00, //
        0x19,
        0x00,
        0x00,
        0x00, // caplen = 20 + 5 = 25
        0x19,
        0x00,
        0x00,
        0x00, // origlen, the same: nothing was truncated
        // TAP header: version 0, padding 0, length 20
        0x00,
        0x00,
        0x14,
        0x00, //
        // TLV type 1 (RSS), length 4, value = binary32 of -40 dBm
        0x01,
        0x00,
        0x04,
        0x00, //
        0x00,
        0x00,
        0x20,
        0xc2, //
        // TLV type 3 (Channel Assignment), length 3: channel 15, page 0, then one padding octet
        0x03,
        0x00,
        0x03,
        0x00, //
        0x0f,
        0x00,
        0x00,
        0x00, //
        // the raw 802.15.4 MAC frame
        0x41,
        0x88,
        0x01,
        0xCD,
        0xAB,
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));
    TEST_ASSERT_EQUAL_HEX8(0xEE, out[sizeof(WANT)]); // nothing written past the record
}

// The captured length a reader trusts is the pseudo-header plus the frame, for any frame length,
// and the record total is that plus the 16-octet pcap record header.
void test_tap_record_lengths_track_the_frame(void)
{
    static uint8_t frame[128];
    uint8_t out[256];
    memset(frame, 0x5A, sizeof(frame));

    for (size_t flen = 1; flen <= sizeof(frame); flen *= 2)
    {
        RadioSniff.tap_record_args.out = out;
        RadioSniff.tap_record_args.cap = sizeof(out);
        RadioSniff.tap_record_args.frame = frame;
        RadioSniff.tap_record_args.flen = flen;
        RadioSniff.tap_record_args.rssi_dbm = -70;
        RadioSniff.tap_record_args.channel = 26;
        RadioSniff.tap_record_args.ts_sec = 7;
        RadioSniff.tap_record_args.ts_usec = 8;
        RadioSniff.tap_record(radio_sniff_work);
        const size_t n = RadioSniff.n;
        TEST_ASSERT_EQUAL_size_t(PROTOCORE_PCAP_REC_HDR_LEN + RADIO_SNIFF_TAP_LEN + flen, n);
        // caplen is a little-endian 32-bit field at offset 8 of the record header.
        const uint32_t caplen =
            (uint32_t)out[8] | ((uint32_t)out[9] << 8) | ((uint32_t)out[10] << 16) | ((uint32_t)out[11] << 24);
        TEST_ASSERT_EQUAL_UINT32((uint32_t)(RADIO_SNIFF_TAP_LEN + flen), caplen);
        // and the TAP length field states the pseudo-header alone, never the frame.
        TEST_ASSERT_EQUAL_HEX8(RADIO_SNIFF_TAP_LEN, out[PROTOCORE_PCAP_REC_HDR_LEN + 2]);
        TEST_ASSERT_EQUAL_HEX8(0x00, out[PROTOCORE_PCAP_REC_HDR_LEN + 3]);
        // the MAC frame follows the pseudo-header
        TEST_ASSERT_EQUAL_HEX8_ARRAY(frame, out + PROTOCORE_PCAP_REC_HDR_LEN + RADIO_SNIFF_TAP_LEN, flen);
    }
}

// The channel a sniffer is tuned to travels in the record, in the 16-bit field TAP section 3.4
// gives it, so both octets carry.
void test_tap_channel_assignment_is_sixteen_bits(void)
{
    static const uint8_t FRAME[1] = {0x00};
    uint8_t out[64];
    const size_t base = PROTOCORE_PCAP_REC_HDR_LEN + 16; // record header, TAP header, RSS TLV, TLV header

    // Channel 26, the top of the 2.4 GHz O-QPSK page 0 range.
    RadioSniff.tap_record_args.out = out;
    RadioSniff.tap_record_args.cap = sizeof(out);
    RadioSniff.tap_record_args.frame = FRAME;
    RadioSniff.tap_record_args.flen = 1;
    RadioSniff.tap_record_args.rssi_dbm = -55;
    RadioSniff.tap_record_args.channel = 26;
    RadioSniff.tap_record_args.ts_sec = 0;
    RadioSniff.tap_record_args.ts_usec = 0;
    RadioSniff.tap_record(radio_sniff_work);
    TEST_ASSERT_TRUE(RadioSniff.n > 0);
    TEST_ASSERT_EQUAL_HEX8(26, out[base]);
    TEST_ASSERT_EQUAL_HEX8(0, out[base + 1]);
    TEST_ASSERT_EQUAL_HEX8(0, out[base + 2]); // channel page 0

    // A channel number past 255 must occupy the high octet rather than wrapping.
    RadioSniff.tap_record_args.out = out;
    RadioSniff.tap_record_args.cap = sizeof(out);
    RadioSniff.tap_record_args.frame = FRAME;
    RadioSniff.tap_record_args.flen = 1;
    RadioSniff.tap_record_args.rssi_dbm = -55;
    RadioSniff.tap_record_args.channel = 0x0123;
    RadioSniff.tap_record_args.ts_sec = 0;
    RadioSniff.tap_record_args.ts_usec = 0;
    RadioSniff.tap_record(radio_sniff_work);
    TEST_ASSERT_TRUE(RadioSniff.n > 0);
    TEST_ASSERT_EQUAL_HEX8(0x23, out[base]);
    TEST_ASSERT_EQUAL_HEX8(0x01, out[base + 1]);
}

// A record is written whole or not at all: a truncated pcap record desynchronizes every record
// after it, because a reader takes the next record's offset from this one's captured length.
void test_tap_record_fails_closed(void)
{
    static const uint8_t FRAME[4] = {1, 2, 3, 4};
    uint8_t out[64];
    const size_t need = PROTOCORE_PCAP_REC_HDR_LEN + RADIO_SNIFF_TAP_LEN + sizeof(FRAME);

    RadioSniff.tap_record_args.out = out;
    RadioSniff.tap_record_args.cap = need;
    RadioSniff.tap_record_args.frame = FRAME;
    RadioSniff.tap_record_args.flen = sizeof(FRAME);
    RadioSniff.tap_record_args.rssi_dbm = -40;
    RadioSniff.tap_record_args.channel = 11;
    RadioSniff.tap_record_args.ts_sec = 1;
    RadioSniff.tap_record_args.ts_usec = 2;
    RadioSniff.tap_record(radio_sniff_work);
    TEST_ASSERT_EQUAL_size_t(need, RadioSniff.n);
    RadioSniff.tap_record_args.out = out;
    RadioSniff.tap_record_args.cap = need - 1;
    RadioSniff.tap_record_args.frame = FRAME;
    RadioSniff.tap_record_args.flen = sizeof(FRAME);
    RadioSniff.tap_record_args.rssi_dbm = -40;
    RadioSniff.tap_record_args.channel = 11;
    RadioSniff.tap_record_args.ts_sec = 1;
    RadioSniff.tap_record_args.ts_usec = 2;
    RadioSniff.tap_record(radio_sniff_work);
    TEST_ASSERT_EQUAL_size_t(0, RadioSniff.n);
    RadioSniff.tap_record_args.out = NULL;
    RadioSniff.tap_record_args.cap = sizeof(out);
    RadioSniff.tap_record_args.frame = FRAME;
    RadioSniff.tap_record_args.flen = sizeof(FRAME);
    RadioSniff.tap_record_args.rssi_dbm = -40;
    RadioSniff.tap_record_args.channel = 11;
    RadioSniff.tap_record_args.ts_sec = 1;
    RadioSniff.tap_record_args.ts_usec = 2;
    RadioSniff.tap_record(radio_sniff_work);
    TEST_ASSERT_EQUAL_size_t(0, RadioSniff.n);
    RadioSniff.tap_record_args.out = out;
    RadioSniff.tap_record_args.cap = sizeof(out);
    RadioSniff.tap_record_args.frame = NULL;
    RadioSniff.tap_record_args.flen = sizeof(FRAME);
    RadioSniff.tap_record_args.rssi_dbm = -40;
    RadioSniff.tap_record_args.channel = 11;
    RadioSniff.tap_record_args.ts_sec = 1;
    RadioSniff.tap_record_args.ts_usec = 2;
    RadioSniff.tap_record(radio_sniff_work);
    TEST_ASSERT_EQUAL_size_t(0, RadioSniff.n);
    RadioSniff.tap_record_args.out = out;
    RadioSniff.tap_record_args.cap = sizeof(out);
    RadioSniff.tap_record_args.frame = FRAME;
    RadioSniff.tap_record_args.flen = 0;
    RadioSniff.tap_record_args.rssi_dbm = -40;
    RadioSniff.tap_record_args.channel = 11;
    RadioSniff.tap_record_args.ts_sec = 1;
    RadioSniff.tap_record_args.ts_usec = 2;
    RadioSniff.tap_record(radio_sniff_work);
    TEST_ASSERT_EQUAL_size_t(0, RadioSniff.n);
}
