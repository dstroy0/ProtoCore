// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
    size_t alen = protocore_apdu_build_who_is(apdu, sizeof(apdu), 0, 0, PROTO_FALSE);
    TEST_ASSERT_EQUAL_size_t(2u, alen);

    uint8_t npdu[32];
    size_t nlen = protocore_npdu_build(npdu, sizeof(npdu), PROTO_FALSE, NPDU_PRIO_NORMAL, PROTO_TRUE, 0xFFFFu, NULL, 0,
                                       255, apdu, alen);
    TEST_ASSERT_EQUAL_size_t(8u, nlen);

    uint8_t frame[64];
    size_t flen = protocore_bvlc_build(frame, sizeof(frame), BVLC_FUNC_ORIGINAL_BROADCAST, npdu, nlen);
    TEST_ASSERT_EQUAL_size_t(12u, flen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, frame, 12);
}

// The BVLL length counts the 4-octet header itself, so parsing it back gives the NPDU slice.
void test_bvlc_length_covers_the_whole_bvll(void)
{
    static const uint8_t NPDU[6] = {0x01, 0x00, 0x10, 0x08, 0xAA, 0xBB};
    uint8_t frame[32];
    size_t n = protocore_bvlc_build(frame, sizeof(frame), BVLC_FUNC_ORIGINAL_UNICAST, NPDU, sizeof(NPDU));
    TEST_ASSERT_EQUAL_size_t(10u, n);
    TEST_ASSERT_EQUAL_HEX8(0x00u, frame[2]); // 10 = 0x000A, big-endian
    TEST_ASSERT_EQUAL_HEX8(0x0Au, frame[3]);

    uint8_t function = 0;
    const uint8_t *slice = NULL;
    size_t slice_len = 0;
    TEST_ASSERT_TRUE(protocore_bvlc_parse(frame, n, &function, &slice, &slice_len));
    TEST_ASSERT_EQUAL_HEX8(BVLC_FUNC_ORIGINAL_UNICAST, function);
    TEST_ASSERT_EQUAL_size_t(sizeof(NPDU), slice_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(NPDU, slice, sizeof(NPDU));

    // A datagram longer than the declared BVLL is trimmed to it, not carried whole.
    uint8_t padded[32];
    memcpy(padded, frame, n);
    memset(padded + n, 0x77, 8);
    TEST_ASSERT_TRUE(protocore_bvlc_parse(padded, n + 8, &function, &slice, &slice_len));
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
    TEST_ASSERT_FALSE(protocore_bvlc_parse(frame, sizeof(frame), &function, &slice, &slice_len));
    frame[0] = 0x81;

    frame[3] = 0x03; // a length below the header size cannot be right
    TEST_ASSERT_FALSE(protocore_bvlc_parse(frame, sizeof(frame), &function, &slice, &slice_len));

    frame[3] = 0x40; // a length larger than what arrived
    TEST_ASSERT_FALSE(protocore_bvlc_parse(frame, sizeof(frame), &function, &slice, &slice_len));

    TEST_ASSERT_FALSE(protocore_bvlc_parse(frame, 3, &function, &slice, &slice_len));
    TEST_ASSERT_FALSE(protocore_bvlc_parse(NULL, sizeof(frame), &function, &slice, &slice_len));

    uint8_t out[8];
    TEST_ASSERT_EQUAL_size_t(0u, protocore_bvlc_build(out, 5, BVLC_FUNC_ORIGINAL_UNICAST, frame, 4)); // 4+4 > 5
    TEST_ASSERT_EQUAL_size_t(0u, protocore_bvlc_build(NULL, sizeof(out), BVLC_FUNC_ORIGINAL_UNICAST, frame, 4));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_bvlc_build(out, sizeof(out), BVLC_FUNC_ORIGINAL_UNICAST, NULL, 4));
}

// The NPCI control octet carries the priority in its low two bits and ORs in the flag bits, so a
// life-safety message expecting a reply with no destination reads 0x04 | 0x03 = 0x07.
void test_npci_control_octet_is_assembled_from_the_bits(void)
{
    static const uint8_t APDU[2] = {0x10, 0x08};
    uint8_t buf[32];

    size_t n = protocore_npdu_build(buf, sizeof(buf), PROTO_TRUE, NPDU_PRIO_LIFE_SAFETY, PROTO_FALSE, 0, NULL, 0, 0,
                                    APDU, sizeof(APDU));
    TEST_ASSERT_EQUAL_size_t(4u, n);
    TEST_ASSERT_EQUAL_HEX8(NPDU_VERSION, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x07u, buf[1]);

    n = protocore_npdu_build(buf, sizeof(buf), PROTO_FALSE, NPDU_PRIO_URGENT, PROTO_FALSE, 0, NULL, 0, 0, APDU,
                             sizeof(APDU));
    TEST_ASSERT_EQUAL_HEX8(0x01u, buf[1]);

    // Only the low two bits of the priority argument reach the octet.
    n = protocore_npdu_build(buf, sizeof(buf), PROTO_FALSE, 0xFCu, PROTO_FALSE, 0, NULL, 0, 0, APDU, sizeof(APDU));
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[1]);
    TEST_ASSERT_EQUAL_size_t(4u, n);
}

// A directed NPDU: DNET, a one-octet DADR, then the hop count, then the APDU.
void test_npdu_with_a_destination_address(void)
{
    static const uint8_t APDU[2] = {0x10, 0x08};
    static const uint8_t DADR[1] = {0x0A};
    uint8_t buf[32];
    size_t n = protocore_npdu_build(buf, sizeof(buf), PROTO_TRUE, NPDU_PRIO_NORMAL, PROTO_TRUE, 0x0005u, DADR, 1, 254,
                                    APDU, sizeof(APDU));
    // version + control + DNET(2) + DLEN(1) + DADR(1) + hop(1) + APDU(2)
    TEST_ASSERT_EQUAL_size_t(9u, n);

    static const uint8_t WANT[9] = {0x01, 0x24, 0x00, 0x05, 0x01, 0x0A, 0xFE, 0x10, 0x08};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 9);

    NpduInfo info;
    TEST_ASSERT_TRUE(protocore_npdu_parse(buf, n, &info));
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
    TEST_ASSERT_TRUE(protocore_npdu_parse(NPDU, sizeof(NPDU), &info));
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
    TEST_ASSERT_FALSE(protocore_npdu_parse(BAD_VERSION, sizeof(BAD_VERSION), &info));

    static const uint8_t SHORT_DEST[4] = {0x01, 0x20, 0x00, 0x05}; // claims a destination, DLEN missing
    TEST_ASSERT_FALSE(protocore_npdu_parse(SHORT_DEST, sizeof(SHORT_DEST), &info));

    static const uint8_t LYING_DLEN[6] = {0x01, 0x20, 0x00, 0x05, 0x10, 0xAA}; // DLEN 16, one octet present
    TEST_ASSERT_FALSE(protocore_npdu_parse(LYING_DLEN, sizeof(LYING_DLEN), &info));

    static const uint8_t NO_HOP[5] = {0x01, 0x20, 0x00, 0x05, 0x00}; // destination present, hop count missing
    TEST_ASSERT_FALSE(protocore_npdu_parse(NO_HOP, sizeof(NO_HOP), &info));

    static const uint8_t SHORT_SRC[6] = {0x01, 0x08, 0x00, 0x03, 0x04, 0xAA}; // SLEN 4, one octet present
    TEST_ASSERT_FALSE(protocore_npdu_parse(SHORT_SRC, sizeof(SHORT_SRC), &info));

    TEST_ASSERT_FALSE(protocore_npdu_parse(BAD_VERSION, 1, &info));
    TEST_ASSERT_FALSE(protocore_npdu_parse(NULL, 4, &info));

    uint8_t out[4];
    static const uint8_t APDU[2] = {0x10, 0x08};
    TEST_ASSERT_EQUAL_size_t(0u, protocore_npdu_build(out, 3, PROTO_FALSE, 0, PROTO_FALSE, 0, NULL, 0, 0, APDU, 2));
    TEST_ASSERT_EQUAL_size_t(
        0u, protocore_npdu_build(out, sizeof(out), PROTO_FALSE, 0, PROTO_FALSE, 0, NULL, 0, 0, NULL, 2));
}

// A network-layer NSDU sets the top NPCI bit, and the parser must report it rather than handing the
// payload to an application that would read it as an APDU.
void test_network_layer_message_is_flagged(void)
{
    static const uint8_t NPDU[4] = {0x01, 0x80, 0x00, 0x01}; // control 0x80, then a network message
    NpduInfo info;
    TEST_ASSERT_TRUE(protocore_npdu_parse(NPDU, sizeof(NPDU), &info));
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
    size_t n = protocore_apdu_build_who_is(buf, sizeof(buf), 1, 100, PROTO_TRUE);
    static const uint8_t WANT[6] = {0x10, 0x08, 0x09, 0x01, 0x19, 0x64}; // 100 = 0x64
    TEST_ASSERT_EQUAL_size_t(6u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 6);

    // The length field is the octet count, so 4194303 = 0x3FFFFF takes three and the tag reads
    // (1 << 4) | 0x08 | 3 = 0x1B. Zero still takes one octet.
    n = protocore_apdu_build_who_is(buf, sizeof(buf), 0, BACNET_MAX_INSTANCE, PROTO_TRUE);
    static const uint8_t WIDE[8] = {0x10, 0x08, 0x09, 0x00, 0x1B, 0x3F, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_size_t(8u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WIDE, buf, 8);

    // Out-of-range or inverted limits are refused rather than emitted.
    TEST_ASSERT_EQUAL_size_t(0u, protocore_apdu_build_who_is(buf, sizeof(buf), 0, BACNET_MAX_INSTANCE + 1, PROTO_TRUE));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_apdu_build_who_is(buf, sizeof(buf), 100, 1, PROTO_TRUE));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_apdu_build_who_is(buf, 5, 1, 100, PROTO_TRUE));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_apdu_build_who_is(NULL, sizeof(buf), 0, 0, PROTO_FALSE));
}

// I-Am answers Who-Is with four application-tagged values. The object identifier is application
// tag 12 with four value octets, so its tag octet is (12 << 4) | 0 | 4 = 0xC4, and the value is
// (object type << 22) | instance: Device is type 8, so 8 * 2^22 = 0x02000000, plus instance 260
// (0x104) = 0x02000104.
void test_i_am_object_identifier_packs_type_and_instance(void)
{
    uint8_t buf[32];
    size_t n = protocore_apdu_build_i_am(buf, sizeof(buf), 260, 1476, 0, 260);
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
    n = protocore_apdu_build_i_am(buf, sizeof(buf), 0, 50, 3, 7);
    static const uint8_t SMALL[13] = {0x10, 0x00, 0xC4, 0x02, 0x00, 0x00, 0x00, 0x21, 0x32, 0x91, 0x03, 0x21, 0x07};
    TEST_ASSERT_EQUAL_size_t(13u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SMALL, buf, 13);

    TEST_ASSERT_EQUAL_size_t(0u, protocore_apdu_build_i_am(buf, sizeof(buf), BACNET_MAX_INSTANCE + 1, 50, 0, 7));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_apdu_build_i_am(buf, sizeof(buf), 0, 50, 4, 7)); // segmentation is 0..3
    TEST_ASSERT_EQUAL_size_t(0u, protocore_apdu_build_i_am(buf, 12, 0, 50, 3, 7));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_apdu_build_i_am(NULL, sizeof(buf), 0, 50, 0, 7));
}

// ReadProperty is a confirmed request: PDU type 0 in the high nibble, the max-segments/max-APDU
// octet, the invoke id, service choice 12, then the object identifier as context tag 0 (tag octet
// (0 << 4) | 0x08 | 4 = 0x0C) and the property identifier as context tag 1.
void test_read_property_request(void)
{
    uint8_t buf[24];
    size_t n = protocore_apdu_build_read_property(buf, sizeof(buf), 1, 0x05, BACNET_OBJ_ANALOG_INPUT, 5,
                                                  BACNET_PROP_PRESENT_VALUE);
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
    n = protocore_apdu_build_read_property(buf, sizeof(buf), 0x7F, 0x00, BACNET_OBJ_DEVICE, BACNET_MAX_INSTANCE,
                                           BACNET_PROP_OBJECT_NAME);
    static const uint8_t TOP[11] = {0x00, 0x00, 0x7F, 0x0C, 0x0C, 0x02, 0x3F, 0xFF, 0xFF, 0x19, 0x4D}; // 77 = 0x4D
    TEST_ASSERT_EQUAL_size_t(11u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(TOP, buf, 11);

    TEST_ASSERT_EQUAL_size_t(
        0u, protocore_apdu_build_read_property(buf, sizeof(buf), 1, 0, 0, BACNET_MAX_INSTANCE + 1, 85));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_apdu_build_read_property(buf, sizeof(buf), 1, 0, 0x400u, 5, 85));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_apdu_build_read_property(buf, 10, 1, 0, 0, 5, 85));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_apdu_build_read_property(NULL, sizeof(buf), 1, 0, 0, 5, 85));
}

// The APDU header parser walks each supported PDU type to its service choice and slices what
// follows. The confirmed-request header is flags + max-resp + invoke id + choice; a simple ACK
// drops the max-resp octet; a complex ACK does too but keeps the invoke id.
void test_apdu_header_parse_per_pdu_type(void)
{
    BacnetApdu a;

    static const uint8_t CONF[6] = {0x02, 0x05, 0x2A, 0x0C, 0xAA, 0xBB}; // SA set, invoke 42
    TEST_ASSERT_TRUE(protocore_apdu_parse(CONF, sizeof(CONF), &a));
    TEST_ASSERT_EQUAL_UINT8(BACNET_PDU_CONFIRMED_REQUEST, a.pdu_type);
    TEST_ASSERT_TRUE(a.sa);
    TEST_ASSERT_FALSE(a.segmented);
    TEST_ASSERT_EQUAL_UINT8(42u, a.invoke_id);
    TEST_ASSERT_EQUAL_UINT8(12u, a.service_choice);
    TEST_ASSERT_EQUAL_size_t(2u, a.service_data_len);
    TEST_ASSERT_EQUAL_HEX8(0xAAu, a.service_data[0]);

    static const uint8_t UNCONF[2] = {0x10, 0x08};
    TEST_ASSERT_TRUE(protocore_apdu_parse(UNCONF, sizeof(UNCONF), &a));
    TEST_ASSERT_EQUAL_UINT8(BACNET_PDU_UNCONFIRMED_REQUEST, a.pdu_type);
    TEST_ASSERT_EQUAL_UINT8(BACNET_SVC_UN_WHO_IS, a.service_choice);
    TEST_ASSERT_EQUAL_size_t(0u, a.service_data_len);
    TEST_ASSERT_NULL(a.service_data);

    static const uint8_t SIMPLE_ACK[3] = {0x20, 0x2A, 0x0F};
    TEST_ASSERT_TRUE(protocore_apdu_parse(SIMPLE_ACK, sizeof(SIMPLE_ACK), &a));
    TEST_ASSERT_EQUAL_UINT8(BACNET_PDU_SIMPLE_ACK, a.pdu_type);
    TEST_ASSERT_EQUAL_UINT8(42u, a.invoke_id);
    TEST_ASSERT_EQUAL_UINT8(15u, a.service_choice);

    static const uint8_t COMPLEX_ACK[4] = {0x30, 0x2A, 0x0C, 0x99};
    TEST_ASSERT_TRUE(protocore_apdu_parse(COMPLEX_ACK, sizeof(COMPLEX_ACK), &a));
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
    TEST_ASSERT_TRUE(protocore_apdu_parse(SEG_REQ, sizeof(SEG_REQ), &a));
    TEST_ASSERT_TRUE(a.segmented);
    TEST_ASSERT_TRUE(a.more_follows);
    TEST_ASSERT_EQUAL_UINT8(42u, a.invoke_id);
    TEST_ASSERT_EQUAL_UINT8(12u, a.service_choice);
    TEST_ASSERT_EQUAL_size_t(2u, a.service_data_len);

    // complex ACK, SEG set: flags, invoke, sequence, window, choice
    static const uint8_t SEG_ACK[5] = {0x38, 0x2A, 0x01, 0x04, 0x0C};
    TEST_ASSERT_TRUE(protocore_apdu_parse(SEG_ACK, sizeof(SEG_ACK), &a));
    TEST_ASSERT_TRUE(a.segmented);
    TEST_ASSERT_EQUAL_UINT8(42u, a.invoke_id);
    TEST_ASSERT_EQUAL_UINT8(12u, a.service_choice);
    TEST_ASSERT_EQUAL_size_t(0u, a.service_data_len);

    TEST_ASSERT_FALSE(protocore_apdu_parse(SEG_REQ, 5, &a)); // the choice octet is missing
    TEST_ASSERT_FALSE(protocore_apdu_parse(SEG_ACK, 4, &a));
}

// Types this header decoder does not cover are refused rather than reported with a garbage choice.
void test_unsupported_pdu_types_and_short_buffers(void)
{
    BacnetApdu a;
    static const uint8_t SEGMENT_ACK[4] = {0x40, 0x00, 0x2A, 0x00};
    static const uint8_t ERROR_PDU[4] = {0x50, 0x2A, 0x0C, 0x00};
    static const uint8_t REJECT[3] = {0x60, 0x2A, 0x01};
    static const uint8_t ABORT[3] = {0x70, 0x2A, 0x04};
    TEST_ASSERT_FALSE(protocore_apdu_parse(SEGMENT_ACK, sizeof(SEGMENT_ACK), &a));
    TEST_ASSERT_FALSE(protocore_apdu_parse(ERROR_PDU, sizeof(ERROR_PDU), &a));
    TEST_ASSERT_FALSE(protocore_apdu_parse(REJECT, sizeof(REJECT), &a));
    TEST_ASSERT_FALSE(protocore_apdu_parse(ABORT, sizeof(ABORT), &a));

    static const uint8_t CONF[4] = {0x00, 0x05, 0x2A, 0x0C};
    TEST_ASSERT_FALSE(protocore_apdu_parse(CONF, 2, &a)); // no invoke id
    TEST_ASSERT_FALSE(protocore_apdu_parse(CONF, 3, &a)); // no service choice
    TEST_ASSERT_TRUE(protocore_apdu_parse(CONF, 4, &a));
    TEST_ASSERT_FALSE(protocore_apdu_parse(CONF, 0, &a));
    TEST_ASSERT_FALSE(protocore_apdu_parse(NULL, 4, &a));
    TEST_ASSERT_FALSE(protocore_apdu_parse(CONF, 4, NULL));
}

// A Who-Is built, framed, sent and taken apart again yields the service choice it started as.
void test_datagram_round_trip(void)
{
    uint8_t apdu[8];
    size_t alen = protocore_apdu_build_who_is(apdu, sizeof(apdu), 100, 200, PROTO_TRUE);
    uint8_t npdu[32];
    size_t nlen = protocore_npdu_build(npdu, sizeof(npdu), PROTO_FALSE, NPDU_PRIO_NORMAL, PROTO_TRUE, 0xFFFFu, NULL, 0,
                                       255, apdu, alen);
    uint8_t frame[64];
    size_t flen = protocore_bvlc_build(frame, sizeof(frame), BVLC_FUNC_ORIGINAL_BROADCAST, npdu, nlen);

    uint8_t function;
    const uint8_t *slice;
    size_t slice_len;
    TEST_ASSERT_TRUE(protocore_bvlc_parse(frame, flen, &function, &slice, &slice_len));
    TEST_ASSERT_EQUAL_HEX8(BVLC_FUNC_ORIGINAL_BROADCAST, function);

    NpduInfo info;
    TEST_ASSERT_TRUE(protocore_npdu_parse(slice, slice_len, &info));
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, info.dnet);
    TEST_ASSERT_EQUAL_size_t(alen, info.apdu_len);

    BacnetApdu a;
    TEST_ASSERT_TRUE(protocore_apdu_parse(info.apdu, info.apdu_len, &a));
    TEST_ASSERT_EQUAL_UINT8(BACNET_PDU_UNCONFIRMED_REQUEST, a.pdu_type);
    TEST_ASSERT_EQUAL_UINT8(BACNET_SVC_UN_WHO_IS, a.service_choice);
    TEST_ASSERT_EQUAL_size_t(4u, a.service_data_len); // the two context-tagged limits
}
