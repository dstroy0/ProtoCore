// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the BACnet/IP BVLC + NPDU + APDU codec (services/fieldbus/bacnet/bacnet.h).
//
// The load-bearing case is test_global_broadcast_who_is_datagram. A global-broadcast Who-Is is the
// one BACnet/IP datagram every device on a network must answer, and ASHRAE 135 fixes all twelve of
// its octets: Annex J's BVLL header (0x81, Original-Broadcast-NPDU, a big-endian length covering the
// whole BVLL), Clause 6's NPDU (version 1, NPCI with the destination bit, DNET 0xFFFF with DLEN 0
// for the global network, a hop count), and Clause 21's unconfirmed-request APDU. Building anything
// else means no device replies. Every tag octet below is computed from the Clause 20.2.1.3 tag
// layout - tag number in bits 7..4, class in bit 3, length/value/type in bits 2..0 - rather than
// copied from the encoder.

#include "services/fieldbus/bacnet/bacnet.h"
#include <string.h>

#include <unity.h>

static uint8_t bacnet_work[16]; // the borrow an entry takes; Bacnet never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// Clause 6.2.2 NPCI control bits and Annex J's BVLC constants.
void test_published_constants(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x81u, BVLC_TYPE_BIP);
    TEST_ASSERT_EQUAL_INT(4, BVLC_HEADER_SIZE);
    TEST_ASSERT_EQUAL_HEX8(0x0Au, BVLC_FUNC_ORIGINAL_UNICAST);
    TEST_ASSERT_EQUAL_HEX8(0x0Bu, BVLC_FUNC_ORIGINAL_BROADCAST);
    TEST_ASSERT_EQUAL_HEX8(0x01u, NPDU_VERSION);
    TEST_ASSERT_EQUAL_HEX8(0x80u, NPCI_NETWORK_MSG);
    TEST_ASSERT_EQUAL_HEX8(0x20u, NPCI_DEST_PRESENT);
    TEST_ASSERT_EQUAL_HEX8(0x08u, NPCI_SRC_PRESENT);
    TEST_ASSERT_EQUAL_HEX8(0x04u, NPCI_EXPECTING_REPLY);
    TEST_ASSERT_EQUAL_HEX8(0x03u, NPCI_PRIORITY_MASK);
    // The device / object instance is a 22-bit field, so 4194303 is the largest legal one.
    TEST_ASSERT_EQUAL_HEX32(0x3FFFFFu, BACNET_MAX_INSTANCE);
    TEST_ASSERT_EQUAL_UINT32(4194303u, BACNET_MAX_INSTANCE);
}

// The whole datagram, octet by octet:
//   81            BVLC type, BACnet/IP
//   0B            Original-Broadcast-NPDU
//   00 0C         BVLL length 12, big-endian, counting these four octets
//   01            NPDU version
//   20            NPCI: destination present, priority Normal, no reply expected
//   FF FF         DNET 0xFFFF, the global broadcast network
//   00            DLEN 0, so no DADR follows: broadcast on the remote network
//   FF            hop count 255
//   10            APDU: PDU type 1 (unconfirmed request) in the high nibble, no flags
//   08            service choice 8 = Who-Is
void test_global_broadcast_who_is_datagram(void)
{
    static const uint8_t WANT[12] = {0x81, 0x0B, 0x00, 0x0C, 0x01, 0x20, 0xFF, 0xFF, 0x00, 0xFF, 0x10, 0x08};

    uint8_t apdu[8];
    Bacnet.apdu_build_who_is_args.buf = apdu;
    Bacnet.apdu_build_who_is_args.cap = sizeof(apdu);
    Bacnet.apdu_build_who_is_args.low_limit = 0;
    Bacnet.apdu_build_who_is_args.high_limit = 0;
    Bacnet.apdu_build_who_is_args.has_limits = PROTO_FALSE;
    Bacnet.apdu_build_who_is(bacnet_work);
    size_t alen = Bacnet.n;
    TEST_ASSERT_EQUAL_size_t(2u, alen);

    uint8_t npdu[32];
    Bacnet.npdu_build_args.buf = npdu;
    Bacnet.npdu_build_args.cap = sizeof(npdu);
    Bacnet.npdu_build_args.expecting_reply = PROTO_FALSE;
    Bacnet.npdu_build_args.priority = NPDU_PRIO_NORMAL;
    Bacnet.npdu_build_args.has_dest = PROTO_TRUE;
    Bacnet.npdu_build_args.dnet = 0xFFFFu;
    Bacnet.npdu_build_args.dadr = NULL;
    Bacnet.npdu_build_args.dadr_len = 0;
    Bacnet.npdu_build_args.hop_count = 255;
    Bacnet.npdu_build_args.apdu = apdu;
    Bacnet.npdu_build_args.apdu_len = alen;
    Bacnet.npdu_build(bacnet_work);
    size_t nlen = Bacnet.n;
    TEST_ASSERT_EQUAL_size_t(8u, nlen);

    uint8_t frame[64];
    Bacnet.bvlc_build_args.buf = frame;
    Bacnet.bvlc_build_args.cap = sizeof(frame);
    Bacnet.bvlc_build_args.function = BVLC_FUNC_ORIGINAL_BROADCAST;
    Bacnet.bvlc_build_args.npdu = npdu;
    Bacnet.bvlc_build_args.npdu_len = nlen;
    Bacnet.bvlc_build(bacnet_work);
    size_t flen = Bacnet.n;
    TEST_ASSERT_EQUAL_size_t(12u, flen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, frame, 12);
}

// The BVLL length counts the 4-octet header itself, so parsing it back gives the NPDU slice.
void test_bvlc_length_covers_the_whole_bvll(void)
{
    static const uint8_t NPDU[6] = {0x01, 0x00, 0x10, 0x08, 0xAA, 0xBB};
    uint8_t frame[32];
    Bacnet.bvlc_build_args.buf = frame;
    Bacnet.bvlc_build_args.cap = sizeof(frame);
    Bacnet.bvlc_build_args.function = BVLC_FUNC_ORIGINAL_UNICAST;
    Bacnet.bvlc_build_args.npdu = NPDU;
    Bacnet.bvlc_build_args.npdu_len = sizeof(NPDU);
    Bacnet.bvlc_build(bacnet_work);
    size_t n = Bacnet.n;
    TEST_ASSERT_EQUAL_size_t(10u, n);
    TEST_ASSERT_EQUAL_HEX8(0x00u, frame[2]); // 10 = 0x000A, big-endian
    TEST_ASSERT_EQUAL_HEX8(0x0Au, frame[3]);

    uint8_t function = 0;
    const uint8_t *slice = NULL;
    size_t slice_len = 0;
    Bacnet.bvlc_parse_args.buf = frame;
    Bacnet.bvlc_parse_args.len = n;
    Bacnet.bvlc_parse_args.function = &function;
    Bacnet.bvlc_parse_args.npdu = &slice;
    Bacnet.bvlc_parse_args.npdu_len = &slice_len;
    Bacnet.bvlc_parse(bacnet_work);
    TEST_ASSERT_TRUE(Bacnet.ok);
    TEST_ASSERT_EQUAL_HEX8(BVLC_FUNC_ORIGINAL_UNICAST, function);
    TEST_ASSERT_EQUAL_size_t(sizeof(NPDU), slice_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(NPDU, slice, sizeof(NPDU));

    // A datagram longer than the declared BVLL is trimmed to it, not carried whole.
    uint8_t padded[32];
    memcpy(padded, frame, n);
    memset(padded + n, 0x77, 8);
    Bacnet.bvlc_parse_args.buf = padded;
    Bacnet.bvlc_parse_args.len = n + 8;
    Bacnet.bvlc_parse_args.function = &function;
    Bacnet.bvlc_parse_args.npdu = &slice;
    Bacnet.bvlc_parse_args.npdu_len = &slice_len;
    Bacnet.bvlc_parse(bacnet_work);
    TEST_ASSERT_TRUE(Bacnet.ok);
    TEST_ASSERT_EQUAL_size_t(sizeof(NPDU), slice_len);
}

// A BVLL that is not BACnet/IP, or whose length lies, is refused.
void test_bvlc_refusals(void)
{
    uint8_t frame[8] = {0x81, 0x0A, 0x00, 0x08, 0x01, 0x00, 0x10, 0x08};
    uint8_t function;
    const uint8_t *slice;
    size_t slice_len;

    frame[0] = 0x82; // not the BACnet/IP type
    Bacnet.bvlc_parse_args.buf = frame;
    Bacnet.bvlc_parse_args.len = sizeof(frame);
    Bacnet.bvlc_parse_args.function = &function;
    Bacnet.bvlc_parse_args.npdu = &slice;
    Bacnet.bvlc_parse_args.npdu_len = &slice_len;
    Bacnet.bvlc_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok);
    frame[0] = 0x81;

    frame[3] = 0x03; // a length below the header size cannot be right
    Bacnet.bvlc_parse_args.buf = frame;
    Bacnet.bvlc_parse_args.len = sizeof(frame);
    Bacnet.bvlc_parse_args.function = &function;
    Bacnet.bvlc_parse_args.npdu = &slice;
    Bacnet.bvlc_parse_args.npdu_len = &slice_len;
    Bacnet.bvlc_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok);

    frame[3] = 0x40; // a length larger than what arrived
    Bacnet.bvlc_parse_args.buf = frame;
    Bacnet.bvlc_parse_args.len = sizeof(frame);
    Bacnet.bvlc_parse_args.function = &function;
    Bacnet.bvlc_parse_args.npdu = &slice;
    Bacnet.bvlc_parse_args.npdu_len = &slice_len;
    Bacnet.bvlc_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok);

    Bacnet.bvlc_parse_args.buf = frame;
    Bacnet.bvlc_parse_args.len = 3;
    Bacnet.bvlc_parse_args.function = &function;
    Bacnet.bvlc_parse_args.npdu = &slice;
    Bacnet.bvlc_parse_args.npdu_len = &slice_len;
    Bacnet.bvlc_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok);
    Bacnet.bvlc_parse_args.buf = NULL;
    Bacnet.bvlc_parse_args.len = sizeof(frame);
    Bacnet.bvlc_parse_args.function = &function;
    Bacnet.bvlc_parse_args.npdu = &slice;
    Bacnet.bvlc_parse_args.npdu_len = &slice_len;
    Bacnet.bvlc_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok);

    uint8_t out[8];
    Bacnet.bvlc_build_args.buf = out;
    Bacnet.bvlc_build_args.cap = 5;
    Bacnet.bvlc_build_args.function = BVLC_FUNC_ORIGINAL_UNICAST;
    Bacnet.bvlc_build_args.npdu = frame;
    Bacnet.bvlc_build_args.npdu_len = 4;
    Bacnet.bvlc_build(bacnet_work);
    TEST_ASSERT_EQUAL_size_t(0u, Bacnet.n); // 4+4 > 5
    Bacnet.bvlc_build_args.buf = NULL;
    Bacnet.bvlc_build_args.cap = sizeof(out);
    Bacnet.bvlc_build_args.function = BVLC_FUNC_ORIGINAL_UNICAST;
    Bacnet.bvlc_build_args.npdu = frame;
    Bacnet.bvlc_build_args.npdu_len = 4;
    Bacnet.bvlc_build(bacnet_work);
    TEST_ASSERT_EQUAL_size_t(0u, Bacnet.n);
    Bacnet.bvlc_build_args.buf = out;
    Bacnet.bvlc_build_args.cap = sizeof(out);
    Bacnet.bvlc_build_args.function = BVLC_FUNC_ORIGINAL_UNICAST;
    Bacnet.bvlc_build_args.npdu = NULL;
    Bacnet.bvlc_build_args.npdu_len = 4;
    Bacnet.bvlc_build(bacnet_work);
    TEST_ASSERT_EQUAL_size_t(0u, Bacnet.n);
}

// The NPCI control octet carries the priority in its low two bits and ORs in the flag bits, so a
// life-safety message expecting a reply with no destination reads 0x04 | 0x03 = 0x07.
void test_npci_control_octet_is_assembled_from_the_bits(void)
{
    static const uint8_t APDU[2] = {0x10, 0x08};
    uint8_t buf[32];

    Bacnet.npdu_build_args.buf = buf;
    Bacnet.npdu_build_args.cap = sizeof(buf);
    Bacnet.npdu_build_args.expecting_reply = PROTO_TRUE;
    Bacnet.npdu_build_args.priority = NPDU_PRIO_LIFE_SAFETY;
    Bacnet.npdu_build_args.has_dest = PROTO_FALSE;
    Bacnet.npdu_build_args.dnet = 0;
    Bacnet.npdu_build_args.dadr = NULL;
    Bacnet.npdu_build_args.dadr_len = 0;
    Bacnet.npdu_build_args.hop_count = 0;
    Bacnet.npdu_build_args.apdu = APDU;
    Bacnet.npdu_build_args.apdu_len = sizeof(APDU);
    Bacnet.npdu_build(bacnet_work);
    size_t n = Bacnet.n;
    TEST_ASSERT_EQUAL_size_t(4u, n);
    TEST_ASSERT_EQUAL_HEX8(NPDU_VERSION, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x07u, buf[1]);

    Bacnet.npdu_build_args.buf = buf;
    Bacnet.npdu_build_args.cap = sizeof(buf);
    Bacnet.npdu_build_args.expecting_reply = PROTO_FALSE;
    Bacnet.npdu_build_args.priority = NPDU_PRIO_URGENT;
    Bacnet.npdu_build_args.has_dest = PROTO_FALSE;
    Bacnet.npdu_build_args.dnet = 0;
    Bacnet.npdu_build_args.dadr = NULL;
    Bacnet.npdu_build_args.dadr_len = 0;
    Bacnet.npdu_build_args.hop_count = 0;
    Bacnet.npdu_build_args.apdu = APDU;
    Bacnet.npdu_build_args.apdu_len = sizeof(APDU);
    Bacnet.npdu_build(bacnet_work);
    n = Bacnet.n;
    TEST_ASSERT_EQUAL_HEX8(0x01u, buf[1]);

    // Only the low two bits of the priority argument reach the octet.
    Bacnet.npdu_build_args.buf = buf;
    Bacnet.npdu_build_args.cap = sizeof(buf);
    Bacnet.npdu_build_args.expecting_reply = PROTO_FALSE;
    Bacnet.npdu_build_args.priority = 0xFCu;
    Bacnet.npdu_build_args.has_dest = PROTO_FALSE;
    Bacnet.npdu_build_args.dnet = 0;
    Bacnet.npdu_build_args.dadr = NULL;
    Bacnet.npdu_build_args.dadr_len = 0;
    Bacnet.npdu_build_args.hop_count = 0;
    Bacnet.npdu_build_args.apdu = APDU;
    Bacnet.npdu_build_args.apdu_len = sizeof(APDU);
    Bacnet.npdu_build(bacnet_work);
    n = Bacnet.n;
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[1]);
    TEST_ASSERT_EQUAL_size_t(4u, n);
}

// A directed NPDU: DNET, a one-octet DADR, then the hop count, then the APDU.
void test_npdu_with_a_destination_address(void)
{
    static const uint8_t APDU[2] = {0x10, 0x08};
    static const uint8_t DADR[1] = {0x0A};
    uint8_t buf[32];
    Bacnet.npdu_build_args.buf = buf;
    Bacnet.npdu_build_args.cap = sizeof(buf);
    Bacnet.npdu_build_args.expecting_reply = PROTO_TRUE;
    Bacnet.npdu_build_args.priority = NPDU_PRIO_NORMAL;
    Bacnet.npdu_build_args.has_dest = PROTO_TRUE;
    Bacnet.npdu_build_args.dnet = 0x0005u;
    Bacnet.npdu_build_args.dadr = DADR;
    Bacnet.npdu_build_args.dadr_len = 1;
    Bacnet.npdu_build_args.hop_count = 254;
    Bacnet.npdu_build_args.apdu = APDU;
    Bacnet.npdu_build_args.apdu_len = sizeof(APDU);
    Bacnet.npdu_build(bacnet_work);
    size_t n = Bacnet.n;
    // version + control + DNET(2) + DLEN(1) + DADR(1) + hop(1) + APDU(2)
    TEST_ASSERT_EQUAL_size_t(9u, n);

    static const uint8_t WANT[9] = {0x01, 0x24, 0x00, 0x05, 0x01, 0x0A, 0xFE, 0x10, 0x08};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 9);

    NpduInfo info;
    Bacnet.npdu_parse_args.buf = buf;
    Bacnet.npdu_parse_args.len = n;
    Bacnet.npdu_parse_args.out = &info;
    Bacnet.npdu_parse(bacnet_work);
    TEST_ASSERT_TRUE(Bacnet.ok);
    TEST_ASSERT_TRUE(info.dest_present);
    TEST_ASSERT_FALSE(info.src_present);
    TEST_ASSERT_FALSE(info.network_message);
    TEST_ASSERT_EQUAL_HEX16(0x0005u, info.dnet);
    TEST_ASSERT_EQUAL_UINT8(254u, info.hop_count);
    TEST_ASSERT_EQUAL_size_t(2u, info.apdu_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(APDU, info.apdu, 2);
}

// Clause 6.2.2 orders the fields DNET/DLEN/DADR, then SNET/SLEN/SADR, then the hop count. A parser
// that reads the hop count before the source fields mistakes SNET's high octet for it and then
// slices the APDU three octets early.
void test_hop_count_follows_the_source_fields(void)
{
    static const uint8_t NPDU[14] = {
        0x01, 0x28,                   // version, control: destination + source present
        0x00, 0x05, 0x01, 0x0A,       // DNET 5, DLEN 1, DADR 0x0A
        0x00, 0x03, 0x02, 0xAA, 0xBB, // SNET 3, SLEN 2, SADR AA BB
        0xFE,                         // hop count
        0x10, 0x08,                   // APDU
    };
    NpduInfo info;
    Bacnet.npdu_parse_args.buf = NPDU;
    Bacnet.npdu_parse_args.len = sizeof(NPDU);
    Bacnet.npdu_parse_args.out = &info;
    Bacnet.npdu_parse(bacnet_work);
    TEST_ASSERT_TRUE(Bacnet.ok);
    TEST_ASSERT_TRUE(info.dest_present);
    TEST_ASSERT_TRUE(info.src_present);
    TEST_ASSERT_EQUAL_HEX16(0x0005u, info.dnet);
    TEST_ASSERT_EQUAL_HEX16(0x0003u, info.snet);
    TEST_ASSERT_EQUAL_UINT8(0xFEu, info.hop_count);
    TEST_ASSERT_EQUAL_size_t(2u, info.apdu_len);
    TEST_ASSERT_EQUAL_HEX8(0x10u, info.apdu[0]);
    TEST_ASSERT_EQUAL_HEX8(0x08u, info.apdu[1]);
}

// A version octet other than 1 is refused, and addressing that runs off the buffer with it.
void test_npdu_refusals(void)
{
    NpduInfo info;
    static const uint8_t BAD_VERSION[4] = {0x02, 0x00, 0x10, 0x08};
    Bacnet.npdu_parse_args.buf = BAD_VERSION;
    Bacnet.npdu_parse_args.len = sizeof(BAD_VERSION);
    Bacnet.npdu_parse_args.out = &info;
    Bacnet.npdu_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok);

    static const uint8_t SHORT_DEST[4] = {0x01, 0x20, 0x00, 0x05}; // claims a destination, DLEN missing
    Bacnet.npdu_parse_args.buf = SHORT_DEST;
    Bacnet.npdu_parse_args.len = sizeof(SHORT_DEST);
    Bacnet.npdu_parse_args.out = &info;
    Bacnet.npdu_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok);

    static const uint8_t LYING_DLEN[6] = {0x01, 0x20, 0x00, 0x05, 0x10, 0xAA}; // DLEN 16, one octet present
    Bacnet.npdu_parse_args.buf = LYING_DLEN;
    Bacnet.npdu_parse_args.len = sizeof(LYING_DLEN);
    Bacnet.npdu_parse_args.out = &info;
    Bacnet.npdu_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok);

    static const uint8_t NO_HOP[5] = {0x01, 0x20, 0x00, 0x05, 0x00}; // destination present, hop count missing
    Bacnet.npdu_parse_args.buf = NO_HOP;
    Bacnet.npdu_parse_args.len = sizeof(NO_HOP);
    Bacnet.npdu_parse_args.out = &info;
    Bacnet.npdu_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok);

    static const uint8_t SHORT_SRC[6] = {0x01, 0x08, 0x00, 0x03, 0x04, 0xAA}; // SLEN 4, one octet present
    Bacnet.npdu_parse_args.buf = SHORT_SRC;
    Bacnet.npdu_parse_args.len = sizeof(SHORT_SRC);
    Bacnet.npdu_parse_args.out = &info;
    Bacnet.npdu_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok);

    Bacnet.npdu_parse_args.buf = BAD_VERSION;
    Bacnet.npdu_parse_args.len = 1;
    Bacnet.npdu_parse_args.out = &info;
    Bacnet.npdu_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok);
    Bacnet.npdu_parse_args.buf = NULL;
    Bacnet.npdu_parse_args.len = 4;
    Bacnet.npdu_parse_args.out = &info;
    Bacnet.npdu_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok);

    uint8_t out[4];
    static const uint8_t APDU[2] = {0x10, 0x08};
    Bacnet.npdu_build_args.buf = out;
    Bacnet.npdu_build_args.cap = 3;
    Bacnet.npdu_build_args.expecting_reply = PROTO_FALSE;
    Bacnet.npdu_build_args.priority = 0;
    Bacnet.npdu_build_args.has_dest = PROTO_FALSE;
    Bacnet.npdu_build_args.dnet = 0;
    Bacnet.npdu_build_args.dadr = NULL;
    Bacnet.npdu_build_args.dadr_len = 0;
    Bacnet.npdu_build_args.hop_count = 0;
    Bacnet.npdu_build_args.apdu = APDU;
    Bacnet.npdu_build_args.apdu_len = 2;
    Bacnet.npdu_build(bacnet_work);
    TEST_ASSERT_EQUAL_size_t(0u, Bacnet.n);
    Bacnet.npdu_build_args.buf = out;
    Bacnet.npdu_build_args.cap = sizeof(out);
    Bacnet.npdu_build_args.expecting_reply = PROTO_FALSE;
    Bacnet.npdu_build_args.priority = 0;
    Bacnet.npdu_build_args.has_dest = PROTO_FALSE;
    Bacnet.npdu_build_args.dnet = 0;
    Bacnet.npdu_build_args.dadr = NULL;
    Bacnet.npdu_build_args.dadr_len = 0;
    Bacnet.npdu_build_args.hop_count = 0;
    Bacnet.npdu_build_args.apdu = NULL;
    Bacnet.npdu_build_args.apdu_len = 2;
    Bacnet.npdu_build(bacnet_work);
    TEST_ASSERT_EQUAL_size_t(0u, Bacnet.n);
}

// A network-layer NSDU sets the top NPCI bit, and the parser must report it rather than handing the
// payload to an application that would read it as an APDU.
void test_network_layer_message_is_flagged(void)
{
    static const uint8_t NPDU[4] = {0x01, 0x80, 0x00, 0x01}; // control 0x80, then a network message
    NpduInfo info;
    Bacnet.npdu_parse_args.buf = NPDU;
    Bacnet.npdu_parse_args.len = sizeof(NPDU);
    Bacnet.npdu_parse_args.out = &info;
    Bacnet.npdu_parse(bacnet_work);
    TEST_ASSERT_TRUE(Bacnet.ok);
    TEST_ASSERT_TRUE(info.network_message);
    TEST_ASSERT_FALSE(info.dest_present);
    TEST_ASSERT_EQUAL_size_t(2u, info.apdu_len);
}

// Who-Is with a device-instance range: the limits are context-tagged unsigned integers, minimal
// length. Tag octet = (tag number << 4) | 0x08 for context class | value octet count, so tag 0 with
// one octet is 0x09 and tag 1 with one octet is 0x19.
void test_who_is_with_limits_uses_context_tags(void)
{
    uint8_t buf[16];
    Bacnet.apdu_build_who_is_args.buf = buf;
    Bacnet.apdu_build_who_is_args.cap = sizeof(buf);
    Bacnet.apdu_build_who_is_args.low_limit = 1;
    Bacnet.apdu_build_who_is_args.high_limit = 100;
    Bacnet.apdu_build_who_is_args.has_limits = PROTO_TRUE;
    Bacnet.apdu_build_who_is(bacnet_work);
    size_t n = Bacnet.n;
    static const uint8_t WANT[6] = {0x10, 0x08, 0x09, 0x01, 0x19, 0x64}; // 100 = 0x64
    TEST_ASSERT_EQUAL_size_t(6u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 6);

    // The length field is the octet count, so 4194303 = 0x3FFFFF takes three and the tag reads
    // (1 << 4) | 0x08 | 3 = 0x1B. Zero still takes one octet.
    Bacnet.apdu_build_who_is_args.buf = buf;
    Bacnet.apdu_build_who_is_args.cap = sizeof(buf);
    Bacnet.apdu_build_who_is_args.low_limit = 0;
    Bacnet.apdu_build_who_is_args.high_limit = BACNET_MAX_INSTANCE;
    Bacnet.apdu_build_who_is_args.has_limits = PROTO_TRUE;
    Bacnet.apdu_build_who_is(bacnet_work);
    n = Bacnet.n;
    static const uint8_t WIDE[8] = {0x10, 0x08, 0x09, 0x00, 0x1B, 0x3F, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_size_t(8u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WIDE, buf, 8);

    // Out-of-range or inverted limits are refused rather than emitted.
    Bacnet.apdu_build_who_is_args.buf = buf;
    Bacnet.apdu_build_who_is_args.cap = sizeof(buf);
    Bacnet.apdu_build_who_is_args.low_limit = 0;
    Bacnet.apdu_build_who_is_args.high_limit = BACNET_MAX_INSTANCE + 1;
    Bacnet.apdu_build_who_is_args.has_limits = PROTO_TRUE;
    Bacnet.apdu_build_who_is(bacnet_work);
    TEST_ASSERT_EQUAL_size_t(0u, Bacnet.n);
    Bacnet.apdu_build_who_is_args.buf = buf;
    Bacnet.apdu_build_who_is_args.cap = sizeof(buf);
    Bacnet.apdu_build_who_is_args.low_limit = 100;
    Bacnet.apdu_build_who_is_args.high_limit = 1;
    Bacnet.apdu_build_who_is_args.has_limits = PROTO_TRUE;
    Bacnet.apdu_build_who_is(bacnet_work);
    TEST_ASSERT_EQUAL_size_t(0u, Bacnet.n);
    Bacnet.apdu_build_who_is_args.buf = buf;
    Bacnet.apdu_build_who_is_args.cap = 5;
    Bacnet.apdu_build_who_is_args.low_limit = 1;
    Bacnet.apdu_build_who_is_args.high_limit = 100;
    Bacnet.apdu_build_who_is_args.has_limits = PROTO_TRUE;
    Bacnet.apdu_build_who_is(bacnet_work);
    TEST_ASSERT_EQUAL_size_t(0u, Bacnet.n);
    Bacnet.apdu_build_who_is_args.buf = NULL;
    Bacnet.apdu_build_who_is_args.cap = sizeof(buf);
    Bacnet.apdu_build_who_is_args.low_limit = 0;
    Bacnet.apdu_build_who_is_args.high_limit = 0;
    Bacnet.apdu_build_who_is_args.has_limits = PROTO_FALSE;
    Bacnet.apdu_build_who_is(bacnet_work);
    TEST_ASSERT_EQUAL_size_t(0u, Bacnet.n);
}

// I-Am answers Who-Is with four application-tagged values. The object identifier is application
// tag 12 with four value octets, so its tag octet is (12 << 4) | 0 | 4 = 0xC4, and the value is
// (object type << 22) | instance: Device is type 8, so 8 * 2^22 = 0x02000000, plus instance 260
// (0x104) = 0x02000104.
void test_i_am_object_identifier_packs_type_and_instance(void)
{
    uint8_t buf[32];
    Bacnet.apdu_build_i_am_args.buf = buf;
    Bacnet.apdu_build_i_am_args.cap = sizeof(buf);
    Bacnet.apdu_build_i_am_args.device_instance = 260;
    Bacnet.apdu_build_i_am_args.max_apdu = 1476;
    Bacnet.apdu_build_i_am_args.segmentation = 0;
    Bacnet.apdu_build_i_am_args.vendor_id = 260;
    Bacnet.apdu_build_i_am(bacnet_work);
    size_t n = Bacnet.n;
    static const uint8_t WANT[15] = {
        0x10, 0x00,                   // unconfirmed request, service choice 0 = I-Am
        0xC4, 0x02, 0x00, 0x01, 0x04, // application tag 12, length 4, object id
        0x22, 0x05, 0xC4,             // application tag 2 (unsigned), length 2, 1476 = 0x05C4
        0x91, 0x00,                   // application tag 9 (enumerated), length 1, segmented-both
        0x22, 0x01, 0x04,             // application tag 2, length 2, vendor 260 = 0x0104
    };
    TEST_ASSERT_EQUAL_size_t(15u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 15);

    // A vendor id below 256 needs one value octet, so the tag octet drops to 0x21.
    Bacnet.apdu_build_i_am_args.buf = buf;
    Bacnet.apdu_build_i_am_args.cap = sizeof(buf);
    Bacnet.apdu_build_i_am_args.device_instance = 0;
    Bacnet.apdu_build_i_am_args.max_apdu = 50;
    Bacnet.apdu_build_i_am_args.segmentation = 3;
    Bacnet.apdu_build_i_am_args.vendor_id = 7;
    Bacnet.apdu_build_i_am(bacnet_work);
    n = Bacnet.n;
    static const uint8_t SMALL[13] = {0x10, 0x00, 0xC4, 0x02, 0x00, 0x00, 0x00, 0x21, 0x32, 0x91, 0x03, 0x21, 0x07};
    TEST_ASSERT_EQUAL_size_t(13u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SMALL, buf, 13);

    Bacnet.apdu_build_i_am_args.buf = buf;
    Bacnet.apdu_build_i_am_args.cap = sizeof(buf);
    Bacnet.apdu_build_i_am_args.device_instance = BACNET_MAX_INSTANCE + 1;
    Bacnet.apdu_build_i_am_args.max_apdu = 50;
    Bacnet.apdu_build_i_am_args.segmentation = 0;
    Bacnet.apdu_build_i_am_args.vendor_id = 7;
    Bacnet.apdu_build_i_am(bacnet_work);
    TEST_ASSERT_EQUAL_size_t(0u, Bacnet.n);
    Bacnet.apdu_build_i_am_args.buf = buf;
    Bacnet.apdu_build_i_am_args.cap = sizeof(buf);
    Bacnet.apdu_build_i_am_args.device_instance = 0;
    Bacnet.apdu_build_i_am_args.max_apdu = 50;
    Bacnet.apdu_build_i_am_args.segmentation = 4;
    Bacnet.apdu_build_i_am_args.vendor_id = 7;
    Bacnet.apdu_build_i_am(bacnet_work);
    TEST_ASSERT_EQUAL_size_t(0u, Bacnet.n); // segmentation is 0..3
    Bacnet.apdu_build_i_am_args.buf = buf;
    Bacnet.apdu_build_i_am_args.cap = 12;
    Bacnet.apdu_build_i_am_args.device_instance = 0;
    Bacnet.apdu_build_i_am_args.max_apdu = 50;
    Bacnet.apdu_build_i_am_args.segmentation = 3;
    Bacnet.apdu_build_i_am_args.vendor_id = 7;
    Bacnet.apdu_build_i_am(bacnet_work);
    TEST_ASSERT_EQUAL_size_t(0u, Bacnet.n);
    Bacnet.apdu_build_i_am_args.buf = NULL;
    Bacnet.apdu_build_i_am_args.cap = sizeof(buf);
    Bacnet.apdu_build_i_am_args.device_instance = 0;
    Bacnet.apdu_build_i_am_args.max_apdu = 50;
    Bacnet.apdu_build_i_am_args.segmentation = 0;
    Bacnet.apdu_build_i_am_args.vendor_id = 7;
    Bacnet.apdu_build_i_am(bacnet_work);
    TEST_ASSERT_EQUAL_size_t(0u, Bacnet.n);
}

// ReadProperty is a confirmed request: PDU type 0 in the high nibble, the max-segments/max-APDU
// octet, the invoke id, service choice 12, then the object identifier as context tag 0 (tag octet
// (0 << 4) | 0x08 | 4 = 0x0C) and the property identifier as context tag 1.
void test_read_property_request(void)
{
    uint8_t buf[24];
    Bacnet.apdu_build_read_property_args.buf = buf;
    Bacnet.apdu_build_read_property_args.cap = sizeof(buf);
    Bacnet.apdu_build_read_property_args.invoke_id = 1;
    Bacnet.apdu_build_read_property_args.max_resp = 0x05;
    Bacnet.apdu_build_read_property_args.object_type = BACNET_OBJ_ANALOG_INPUT;
    Bacnet.apdu_build_read_property_args.object_instance = 5;
    Bacnet.apdu_build_read_property_args.property_id = BACNET_PROP_PRESENT_VALUE;
    Bacnet.apdu_build_read_property(bacnet_work);
    size_t n = Bacnet.n;
    static const uint8_t WANT[11] = {
        0x00, 0x05, 0x01, 0x0C,       // confirmed request, max-resp, invoke id 1, service choice 12
        0x0C, 0x00, 0x00, 0x00, 0x05, // context tag 0 length 4: analog-input (type 0), instance 5
        0x19, 0x55,                   // context tag 1 length 1: present-value = 85 = 0x55
    };
    TEST_ASSERT_EQUAL_size_t(11u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 11);
    TEST_ASSERT_EQUAL_INT(85, BACNET_PROP_PRESENT_VALUE);
    TEST_ASSERT_EQUAL_INT(12, BACNET_SVC_CONF_READ_PROPERTY);

    // Device object (type 8) instance 4194303, the top of the 22-bit field: 0x02000000 | 0x3FFFFF.
    Bacnet.apdu_build_read_property_args.buf = buf;
    Bacnet.apdu_build_read_property_args.cap = sizeof(buf);
    Bacnet.apdu_build_read_property_args.invoke_id = 0x7F;
    Bacnet.apdu_build_read_property_args.max_resp = 0x00;
    Bacnet.apdu_build_read_property_args.object_type = BACNET_OBJ_DEVICE;
    Bacnet.apdu_build_read_property_args.object_instance = BACNET_MAX_INSTANCE;
    Bacnet.apdu_build_read_property_args.property_id = BACNET_PROP_OBJECT_NAME;
    Bacnet.apdu_build_read_property(bacnet_work);
    n = Bacnet.n;
    static const uint8_t TOP[11] = {0x00, 0x00, 0x7F, 0x0C, 0x0C, 0x02, 0x3F, 0xFF, 0xFF, 0x19, 0x4D}; // 77 = 0x4D
    TEST_ASSERT_EQUAL_size_t(11u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(TOP, buf, 11);

    Bacnet.apdu_build_read_property_args.buf = buf;
    Bacnet.apdu_build_read_property_args.cap = sizeof(buf);
    Bacnet.apdu_build_read_property_args.invoke_id = 1;
    Bacnet.apdu_build_read_property_args.max_resp = 0;
    Bacnet.apdu_build_read_property_args.object_type = 0;
    Bacnet.apdu_build_read_property_args.object_instance = BACNET_MAX_INSTANCE + 1;
    Bacnet.apdu_build_read_property_args.property_id = 85;
    Bacnet.apdu_build_read_property(bacnet_work);
    TEST_ASSERT_EQUAL_size_t(0u, Bacnet.n);
    Bacnet.apdu_build_read_property_args.buf = buf;
    Bacnet.apdu_build_read_property_args.cap = sizeof(buf);
    Bacnet.apdu_build_read_property_args.invoke_id = 1;
    Bacnet.apdu_build_read_property_args.max_resp = 0;
    Bacnet.apdu_build_read_property_args.object_type = 0x400u;
    Bacnet.apdu_build_read_property_args.object_instance = 5;
    Bacnet.apdu_build_read_property_args.property_id = 85;
    Bacnet.apdu_build_read_property(bacnet_work);
    TEST_ASSERT_EQUAL_size_t(0u, Bacnet.n);
    Bacnet.apdu_build_read_property_args.buf = buf;
    Bacnet.apdu_build_read_property_args.cap = 10;
    Bacnet.apdu_build_read_property_args.invoke_id = 1;
    Bacnet.apdu_build_read_property_args.max_resp = 0;
    Bacnet.apdu_build_read_property_args.object_type = 0;
    Bacnet.apdu_build_read_property_args.object_instance = 5;
    Bacnet.apdu_build_read_property_args.property_id = 85;
    Bacnet.apdu_build_read_property(bacnet_work);
    TEST_ASSERT_EQUAL_size_t(0u, Bacnet.n);
    Bacnet.apdu_build_read_property_args.buf = NULL;
    Bacnet.apdu_build_read_property_args.cap = sizeof(buf);
    Bacnet.apdu_build_read_property_args.invoke_id = 1;
    Bacnet.apdu_build_read_property_args.max_resp = 0;
    Bacnet.apdu_build_read_property_args.object_type = 0;
    Bacnet.apdu_build_read_property_args.object_instance = 5;
    Bacnet.apdu_build_read_property_args.property_id = 85;
    Bacnet.apdu_build_read_property(bacnet_work);
    TEST_ASSERT_EQUAL_size_t(0u, Bacnet.n);
}

// The APDU header parser walks each supported PDU type to its service choice and slices what
// follows. The confirmed-request header is flags + max-resp + invoke id + choice; a simple ACK
// drops the max-resp octet; a complex ACK does too but keeps the invoke id.
void test_apdu_header_parse_per_pdu_type(void)
{
    BacnetApdu a;

    static const uint8_t CONF[6] = {0x02, 0x05, 0x2A, 0x0C, 0xAA, 0xBB}; // SA set, invoke 42
    Bacnet.apdu_parse_args.apdu = CONF;
    Bacnet.apdu_parse_args.len = sizeof(CONF);
    Bacnet.apdu_parse_args.out = &a;
    Bacnet.apdu_parse(bacnet_work);
    TEST_ASSERT_TRUE(Bacnet.ok);
    TEST_ASSERT_EQUAL_UINT8(BACNET_PDU_CONFIRMED_REQUEST, a.pdu_type);
    TEST_ASSERT_TRUE(a.sa);
    TEST_ASSERT_FALSE(a.segmented);
    TEST_ASSERT_EQUAL_UINT8(42u, a.invoke_id);
    TEST_ASSERT_EQUAL_UINT8(12u, a.service_choice);
    TEST_ASSERT_EQUAL_size_t(2u, a.service_data_len);
    TEST_ASSERT_EQUAL_HEX8(0xAAu, a.service_data[0]);

    static const uint8_t UNCONF[2] = {0x10, 0x08};
    Bacnet.apdu_parse_args.apdu = UNCONF;
    Bacnet.apdu_parse_args.len = sizeof(UNCONF);
    Bacnet.apdu_parse_args.out = &a;
    Bacnet.apdu_parse(bacnet_work);
    TEST_ASSERT_TRUE(Bacnet.ok);
    TEST_ASSERT_EQUAL_UINT8(BACNET_PDU_UNCONFIRMED_REQUEST, a.pdu_type);
    TEST_ASSERT_EQUAL_UINT8(BACNET_SVC_UN_WHO_IS, a.service_choice);
    TEST_ASSERT_EQUAL_size_t(0u, a.service_data_len);
    TEST_ASSERT_NULL(a.service_data);

    static const uint8_t SIMPLE_ACK[3] = {0x20, 0x2A, 0x0F};
    Bacnet.apdu_parse_args.apdu = SIMPLE_ACK;
    Bacnet.apdu_parse_args.len = sizeof(SIMPLE_ACK);
    Bacnet.apdu_parse_args.out = &a;
    Bacnet.apdu_parse(bacnet_work);
    TEST_ASSERT_TRUE(Bacnet.ok);
    TEST_ASSERT_EQUAL_UINT8(BACNET_PDU_SIMPLE_ACK, a.pdu_type);
    TEST_ASSERT_EQUAL_UINT8(42u, a.invoke_id);
    TEST_ASSERT_EQUAL_UINT8(15u, a.service_choice);

    static const uint8_t COMPLEX_ACK[4] = {0x30, 0x2A, 0x0C, 0x99};
    Bacnet.apdu_parse_args.apdu = COMPLEX_ACK;
    Bacnet.apdu_parse_args.len = sizeof(COMPLEX_ACK);
    Bacnet.apdu_parse_args.out = &a;
    Bacnet.apdu_parse(bacnet_work);
    TEST_ASSERT_TRUE(Bacnet.ok);
    TEST_ASSERT_EQUAL_UINT8(BACNET_PDU_COMPLEX_ACK, a.pdu_type);
    TEST_ASSERT_EQUAL_UINT8(42u, a.invoke_id);
    TEST_ASSERT_EQUAL_UINT8(12u, a.service_choice);
    TEST_ASSERT_EQUAL_size_t(1u, a.service_data_len);
}

// A segmented PDU carries a sequence number and a proposed window size between the invoke id and the
// service choice. Skipping them reads the sequence number as the service choice.
void test_segmented_pdu_skips_the_sequence_and_window(void)
{
    BacnetApdu a;
    // confirmed request, SEG | MOR set: flags, max-resp, invoke, sequence, window, choice, data
    static const uint8_t SEG_REQ[8] = {0x0C, 0x05, 0x2A, 0x00, 0x04, 0x0C, 0xAA, 0xBB};
    Bacnet.apdu_parse_args.apdu = SEG_REQ;
    Bacnet.apdu_parse_args.len = sizeof(SEG_REQ);
    Bacnet.apdu_parse_args.out = &a;
    Bacnet.apdu_parse(bacnet_work);
    TEST_ASSERT_TRUE(Bacnet.ok);
    TEST_ASSERT_TRUE(a.segmented);
    TEST_ASSERT_TRUE(a.more_follows);
    TEST_ASSERT_EQUAL_UINT8(42u, a.invoke_id);
    TEST_ASSERT_EQUAL_UINT8(12u, a.service_choice);
    TEST_ASSERT_EQUAL_size_t(2u, a.service_data_len);

    // complex ACK, SEG set: flags, invoke, sequence, window, choice
    static const uint8_t SEG_ACK[5] = {0x38, 0x2A, 0x01, 0x04, 0x0C};
    Bacnet.apdu_parse_args.apdu = SEG_ACK;
    Bacnet.apdu_parse_args.len = sizeof(SEG_ACK);
    Bacnet.apdu_parse_args.out = &a;
    Bacnet.apdu_parse(bacnet_work);
    TEST_ASSERT_TRUE(Bacnet.ok);
    TEST_ASSERT_TRUE(a.segmented);
    TEST_ASSERT_EQUAL_UINT8(42u, a.invoke_id);
    TEST_ASSERT_EQUAL_UINT8(12u, a.service_choice);
    TEST_ASSERT_EQUAL_size_t(0u, a.service_data_len);

    Bacnet.apdu_parse_args.apdu = SEG_REQ;
    Bacnet.apdu_parse_args.len = 5;
    Bacnet.apdu_parse_args.out = &a;
    Bacnet.apdu_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok); // the choice octet is missing
    Bacnet.apdu_parse_args.apdu = SEG_ACK;
    Bacnet.apdu_parse_args.len = 4;
    Bacnet.apdu_parse_args.out = &a;
    Bacnet.apdu_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok);
}

// Types this header decoder does not cover are refused rather than reported with a garbage choice.
void test_unsupported_pdu_types_and_short_buffers(void)
{
    BacnetApdu a;
    static const uint8_t SEGMENT_ACK[4] = {0x40, 0x00, 0x2A, 0x00};
    static const uint8_t ERROR_PDU[4] = {0x50, 0x2A, 0x0C, 0x00};
    static const uint8_t REJECT[3] = {0x60, 0x2A, 0x01};
    static const uint8_t ABORT[3] = {0x70, 0x2A, 0x04};
    Bacnet.apdu_parse_args.apdu = SEGMENT_ACK;
    Bacnet.apdu_parse_args.len = sizeof(SEGMENT_ACK);
    Bacnet.apdu_parse_args.out = &a;
    Bacnet.apdu_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok);
    Bacnet.apdu_parse_args.apdu = ERROR_PDU;
    Bacnet.apdu_parse_args.len = sizeof(ERROR_PDU);
    Bacnet.apdu_parse_args.out = &a;
    Bacnet.apdu_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok);
    Bacnet.apdu_parse_args.apdu = REJECT;
    Bacnet.apdu_parse_args.len = sizeof(REJECT);
    Bacnet.apdu_parse_args.out = &a;
    Bacnet.apdu_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok);
    Bacnet.apdu_parse_args.apdu = ABORT;
    Bacnet.apdu_parse_args.len = sizeof(ABORT);
    Bacnet.apdu_parse_args.out = &a;
    Bacnet.apdu_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok);

    static const uint8_t CONF[4] = {0x00, 0x05, 0x2A, 0x0C};
    Bacnet.apdu_parse_args.apdu = CONF;
    Bacnet.apdu_parse_args.len = 2;
    Bacnet.apdu_parse_args.out = &a;
    Bacnet.apdu_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok); // no invoke id
    Bacnet.apdu_parse_args.apdu = CONF;
    Bacnet.apdu_parse_args.len = 3;
    Bacnet.apdu_parse_args.out = &a;
    Bacnet.apdu_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok); // no service choice
    Bacnet.apdu_parse_args.apdu = CONF;
    Bacnet.apdu_parse_args.len = 4;
    Bacnet.apdu_parse_args.out = &a;
    Bacnet.apdu_parse(bacnet_work);
    TEST_ASSERT_TRUE(Bacnet.ok);
    Bacnet.apdu_parse_args.apdu = CONF;
    Bacnet.apdu_parse_args.len = 0;
    Bacnet.apdu_parse_args.out = &a;
    Bacnet.apdu_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok);
    Bacnet.apdu_parse_args.apdu = NULL;
    Bacnet.apdu_parse_args.len = 4;
    Bacnet.apdu_parse_args.out = &a;
    Bacnet.apdu_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok);
    Bacnet.apdu_parse_args.apdu = CONF;
    Bacnet.apdu_parse_args.len = 4;
    Bacnet.apdu_parse_args.out = NULL;
    Bacnet.apdu_parse(bacnet_work);
    TEST_ASSERT_FALSE(Bacnet.ok);
}

// A Who-Is built, framed, sent and taken apart again yields the service choice it started as.
void test_datagram_round_trip(void)
{
    uint8_t apdu[8];
    Bacnet.apdu_build_who_is_args.buf = apdu;
    Bacnet.apdu_build_who_is_args.cap = sizeof(apdu);
    Bacnet.apdu_build_who_is_args.low_limit = 100;
    Bacnet.apdu_build_who_is_args.high_limit = 200;
    Bacnet.apdu_build_who_is_args.has_limits = PROTO_TRUE;
    Bacnet.apdu_build_who_is(bacnet_work);
    size_t alen = Bacnet.n;
    uint8_t npdu[32];
    Bacnet.npdu_build_args.buf = npdu;
    Bacnet.npdu_build_args.cap = sizeof(npdu);
    Bacnet.npdu_build_args.expecting_reply = PROTO_FALSE;
    Bacnet.npdu_build_args.priority = NPDU_PRIO_NORMAL;
    Bacnet.npdu_build_args.has_dest = PROTO_TRUE;
    Bacnet.npdu_build_args.dnet = 0xFFFFu;
    Bacnet.npdu_build_args.dadr = NULL;
    Bacnet.npdu_build_args.dadr_len = 0;
    Bacnet.npdu_build_args.hop_count = 255;
    Bacnet.npdu_build_args.apdu = apdu;
    Bacnet.npdu_build_args.apdu_len = alen;
    Bacnet.npdu_build(bacnet_work);
    size_t nlen = Bacnet.n;
    uint8_t frame[64];
    Bacnet.bvlc_build_args.buf = frame;
    Bacnet.bvlc_build_args.cap = sizeof(frame);
    Bacnet.bvlc_build_args.function = BVLC_FUNC_ORIGINAL_BROADCAST;
    Bacnet.bvlc_build_args.npdu = npdu;
    Bacnet.bvlc_build_args.npdu_len = nlen;
    Bacnet.bvlc_build(bacnet_work);
    size_t flen = Bacnet.n;

    uint8_t function;
    const uint8_t *slice;
    size_t slice_len;
    Bacnet.bvlc_parse_args.buf = frame;
    Bacnet.bvlc_parse_args.len = flen;
    Bacnet.bvlc_parse_args.function = &function;
    Bacnet.bvlc_parse_args.npdu = &slice;
    Bacnet.bvlc_parse_args.npdu_len = &slice_len;
    Bacnet.bvlc_parse(bacnet_work);
    TEST_ASSERT_TRUE(Bacnet.ok);
    TEST_ASSERT_EQUAL_HEX8(BVLC_FUNC_ORIGINAL_BROADCAST, function);

    NpduInfo info;
    Bacnet.npdu_parse_args.buf = slice;
    Bacnet.npdu_parse_args.len = slice_len;
    Bacnet.npdu_parse_args.out = &info;
    Bacnet.npdu_parse(bacnet_work);
    TEST_ASSERT_TRUE(Bacnet.ok);
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, info.dnet);
    TEST_ASSERT_EQUAL_size_t(alen, info.apdu_len);

    BacnetApdu a;
    Bacnet.apdu_parse_args.apdu = info.apdu;
    Bacnet.apdu_parse_args.len = info.apdu_len;
    Bacnet.apdu_parse_args.out = &a;
    Bacnet.apdu_parse(bacnet_work);
    TEST_ASSERT_TRUE(Bacnet.ok);
    TEST_ASSERT_EQUAL_UINT8(BACNET_PDU_UNCONFIRMED_REQUEST, a.pdu_type);
    TEST_ASSERT_EQUAL_UINT8(BACNET_SVC_UN_WHO_IS, a.service_choice);
    TEST_ASSERT_EQUAL_size_t(4u, a.service_data_len); // the two context-tagged limits
}
