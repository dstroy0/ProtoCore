// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the DeviceNet link adaptation (services/fieldbus/devicenet/devicenet.h).
//
// The load-bearing case is test_group_ranges_match_the_identifier_allocation. ODVA's DeviceNet
// CAN Identifier Fields table assigns each message group a contiguous slice of the 11-bit
// identifier space, and publishes the slice boundaries: Group 1 0x000-0x3FF, Group 2 0x400-0x5FF,
// Group 3 0x600-0x7BF, Group 4 0x7C0-0x7EF, and 0x7F0-0x7FF invalid. Those boundaries are what the
// bit packing has to reproduce, so the encoder is checked at the first and last identifier of every
// group and the decoder must hand each one back to the group it came from. An encoder that emits
// outside its own group collides with another group's traffic on a live segment.

#include "services/fieldbus/devicenet/devicenet.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// The published group bases and the field widths behind them.
void test_published_constants(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x000u, DEVICENET_G1_BASE);
    TEST_ASSERT_EQUAL_HEX32(0x400u, DEVICENET_G2_BASE);
    TEST_ASSERT_EQUAL_HEX32(0x600u, DEVICENET_G3_BASE);
    TEST_ASSERT_EQUAL_HEX32(0x7C0u, DEVICENET_G4_BASE);
    TEST_ASSERT_EQUAL_HEX8(0x3Fu, DEVICENET_MAC_MASK); // MAC IDs are 6 bits, 0..63

    TEST_ASSERT_EQUAL_HEX8(0x80u, DEVICENET_HDR_FRAG);
    TEST_ASSERT_EQUAL_HEX8(0x40u, DEVICENET_HDR_XID);
    TEST_ASSERT_EQUAL_HEX8(0x00u, DEVICENET_FRAG_FIRST);
    TEST_ASSERT_EQUAL_HEX8(0x40u, DEVICENET_FRAG_MIDDLE);
    TEST_ASSERT_EQUAL_HEX8(0x80u, DEVICENET_FRAG_LAST);
    TEST_ASSERT_EQUAL_HEX8(0xC0u, DEVICENET_FRAG_ACK);
    TEST_ASSERT_EQUAL_HEX8(0xC0u, DEVICENET_FRAG_TYPE_MASK);
    TEST_ASSERT_EQUAL_HEX8(0x3Fu, DEVICENET_FRAG_COUNT_MASK);
    // The type and count fields partition the octet.
    TEST_ASSERT_EQUAL_HEX8(0xFFu, DEVICENET_FRAG_TYPE_MASK | DEVICENET_FRAG_COUNT_MASK);
    TEST_ASSERT_EQUAL_HEX8(0x00u, DEVICENET_FRAG_TYPE_MASK & DEVICENET_FRAG_COUNT_MASK);

    TEST_ASSERT_EQUAL_UINT8(3u, DEVICENET_G2_EXPLICIT_RESPONSE);
    TEST_ASSERT_EQUAL_UINT8(4u, DEVICENET_G2_UNCONNECTED_EXPLICIT_REQ);
    TEST_ASSERT_EQUAL_UINT8(5u, DEVICENET_G2_POLL_COMMAND);
    TEST_ASSERT_EQUAL_UINT8(7u, DEVICENET_G2_DUP_MAC_CHECK);
}

// Each group's first and last identifier, computed from the bit layout:
//   Group 1  0 MsgID(4) MAC(6)  -> lowest (0,0) = 0x000, highest (15,63) = (15<<6)|63 = 0x3FF
//   Group 2  10 MAC(6) MsgID(3) -> lowest (0,0) = 0x400, highest (7,63)  = 0x400|(63<<3)|7 = 0x5FF
//   Group 3  11 MsgID(3) MAC(6) -> lowest (0,0) = 0x600, highest (6,63)  = 0x600|(6<<6)|63 = 0x7BF
//   Group 4  11111 MsgID(6)     -> lowest 0     = 0x7C0, highest 0x2F    = 0x7EF
void test_group_ranges_match_the_identifier_allocation(void)
{
    struct
    {
        DeviceNetGroup group;
        uint8_t msg_id;
        uint8_t mac_id;
        uint32_t id;
    } static const EDGES[] = {
        {DEVICENET_GROUP_1, 0, 0, 0x000u},  {DEVICENET_GROUP_1, 15, 63, 0x3FFu},  {DEVICENET_GROUP_2, 0, 0, 0x400u},
        {DEVICENET_GROUP_2, 7, 63, 0x5FFu}, {DEVICENET_GROUP_3, 0, 0, 0x600u},    {DEVICENET_GROUP_3, 6, 63, 0x7BFu},
        {DEVICENET_GROUP_4, 0, 0, 0x7C0u},  {DEVICENET_GROUP_4, 0x2F, 0, 0x7EFu},
    };
    for (size_t i = 0; i < sizeof(EDGES) / sizeof(EDGES[0]); i++)
    {
        uint32_t id = 0xFFFFFFFFu;
        TEST_ASSERT_TRUE_MESSAGE(protocore_devicenet_encode_id(&id, EDGES[i].group, EDGES[i].msg_id, EDGES[i].mac_id),
                                 "encode refused a legal identifier");
        TEST_ASSERT_EQUAL_HEX32(EDGES[i].id, id);

        DeviceNetId out;
        TEST_ASSERT_TRUE(protocore_devicenet_decode_id(id, &out));
        TEST_ASSERT_EQUAL_INT_MESSAGE(EDGES[i].group, out.group, "identifier decoded into the wrong group");
        TEST_ASSERT_EQUAL_UINT8(EDGES[i].msg_id, out.msg_id);
        if (EDGES[i].group != DEVICENET_GROUP_4) // Group 4 carries no MAC id
        {
            TEST_ASSERT_EQUAL_UINT8(EDGES[i].mac_id, out.mac_id);
        }
    }
}

// Group 3's published range ends at 0x7BF, so message id 7 has no identifier: 0x600 | (7 << 6) is
// 0x7C0, which the allocation gives to Group 4. Encoding it would put the frame in another group's
// range, where a Group 4 receiver would answer it.
void test_group_three_message_id_seven_has_no_identifier(void)
{
    uint32_t id = 0;
    TEST_ASSERT_TRUE(protocore_devicenet_encode_id(&id, DEVICENET_GROUP_3, 6, 63));
    TEST_ASSERT_EQUAL_HEX32(0x7BFu, id); // the top of Group 3

    TEST_ASSERT_FALSE(protocore_devicenet_encode_id(&id, DEVICENET_GROUP_3, 7, 0));
    TEST_ASSERT_FALSE(protocore_devicenet_encode_id(&id, DEVICENET_GROUP_3, 7, 63));
}

// Everything the encoder emits must decode back to the group, message id and MAC id it was given.
void test_identifier_round_trip(void)
{
    for (uint32_t msg = 0; msg <= 15u; msg++)
    {
        for (uint32_t mac = 0; mac <= 63u; mac += 7u)
        {
            uint32_t id;
            DeviceNetId out;
            TEST_ASSERT_TRUE(protocore_devicenet_encode_id(&id, DEVICENET_GROUP_1, (uint8_t)msg, (uint8_t)mac));
            TEST_ASSERT_TRUE(id <= 0x3FFu);
            TEST_ASSERT_TRUE(protocore_devicenet_decode_id(id, &out));
            TEST_ASSERT_EQUAL_INT(DEVICENET_GROUP_1, out.group);
            TEST_ASSERT_EQUAL_UINT8((uint8_t)msg, out.msg_id);
            TEST_ASSERT_EQUAL_UINT8((uint8_t)mac, out.mac_id);
        }
    }
    for (uint32_t msg = 0; msg <= 7u; msg++)
    {
        for (uint32_t mac = 0; mac <= 63u; mac += 7u)
        {
            uint32_t id;
            DeviceNetId out;
            TEST_ASSERT_TRUE(protocore_devicenet_encode_id(&id, DEVICENET_GROUP_2, (uint8_t)msg, (uint8_t)mac));
            TEST_ASSERT_TRUE(id >= 0x400u && id <= 0x5FFu);
            TEST_ASSERT_TRUE(protocore_devicenet_decode_id(id, &out));
            TEST_ASSERT_EQUAL_INT(DEVICENET_GROUP_2, out.group);
            TEST_ASSERT_EQUAL_UINT8((uint8_t)msg, out.msg_id);
            TEST_ASSERT_EQUAL_UINT8((uint8_t)mac, out.mac_id);
        }
    }
    for (uint32_t msg = 0; msg <= 6u; msg++)
    {
        for (uint32_t mac = 0; mac <= 63u; mac += 7u)
        {
            uint32_t id;
            DeviceNetId out;
            TEST_ASSERT_TRUE(protocore_devicenet_encode_id(&id, DEVICENET_GROUP_3, (uint8_t)msg, (uint8_t)mac));
            TEST_ASSERT_TRUE(id >= 0x600u && id <= 0x7BFu);
            TEST_ASSERT_TRUE(protocore_devicenet_decode_id(id, &out));
            TEST_ASSERT_EQUAL_INT(DEVICENET_GROUP_3, out.group);
            TEST_ASSERT_EQUAL_UINT8((uint8_t)msg, out.msg_id);
            TEST_ASSERT_EQUAL_UINT8((uint8_t)mac, out.mac_id);
        }
    }
    for (uint32_t msg = 0; msg <= 0x2Fu; msg++)
    {
        uint32_t id;
        DeviceNetId out;
        TEST_ASSERT_TRUE(protocore_devicenet_encode_id(&id, DEVICENET_GROUP_4, (uint8_t)msg, 0));
        TEST_ASSERT_TRUE(id >= 0x7C0u && id <= 0x7EFu);
        TEST_ASSERT_TRUE(protocore_devicenet_decode_id(id, &out));
        TEST_ASSERT_EQUAL_INT(DEVICENET_GROUP_4, out.group);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)msg, out.msg_id);
    }
}

// The Duplicate MAC ID Check message every node sends at power-up: Group 2, message id 7, carrying
// the node's own MAC id. MAC 63 lands on 0x400 | (63 << 3) | 7 = 0x5FF, the top of Group 2.
void test_duplicate_mac_id_check_identifier(void)
{
    uint32_t id = 0;
    TEST_ASSERT_TRUE(protocore_devicenet_encode_id(&id, DEVICENET_GROUP_2, DEVICENET_G2_DUP_MAC_CHECK, 0));
    TEST_ASSERT_EQUAL_HEX32(0x407u, id);

    TEST_ASSERT_TRUE(protocore_devicenet_encode_id(&id, DEVICENET_GROUP_2, DEVICENET_G2_DUP_MAC_CHECK, 63));
    TEST_ASSERT_EQUAL_HEX32(0x5FFu, id);

    // A master's explicit request to slave MAC 5: 0x400 | (5 << 3) | 4 = 0x42C.
    TEST_ASSERT_TRUE(protocore_devicenet_encode_id(&id, DEVICENET_GROUP_2, DEVICENET_G2_UNCONNECTED_EXPLICIT_REQ, 5));
    TEST_ASSERT_EQUAL_HEX32(0x42Cu, id);
    // The slave's response on message id 3: 0x400 | (5 << 3) | 3 = 0x42B.
    TEST_ASSERT_TRUE(protocore_devicenet_encode_id(&id, DEVICENET_GROUP_2, DEVICENET_G2_EXPLICIT_RESPONSE, 5));
    TEST_ASSERT_EQUAL_HEX32(0x42Bu, id);
}

// Identifiers 0x7F0-0x7FF are outside every group and are refused, as are out-of-range fields.
void test_invalid_identifiers_and_fields(void)
{
    DeviceNetId out;
    TEST_ASSERT_TRUE(protocore_devicenet_decode_id(0x7EFu, &out));
    TEST_ASSERT_FALSE(protocore_devicenet_decode_id(0x7F0u, &out));
    TEST_ASSERT_FALSE(protocore_devicenet_decode_id(0x7FFu, &out));
    TEST_ASSERT_FALSE(protocore_devicenet_decode_id(0x600u, NULL));

    uint32_t id;
    TEST_ASSERT_FALSE(protocore_devicenet_encode_id(NULL, DEVICENET_GROUP_1, 0, 0));
    TEST_ASSERT_FALSE(protocore_devicenet_encode_id(&id, DEVICENET_GROUP_1, 0, 64));    // MAC is 6 bits
    TEST_ASSERT_FALSE(protocore_devicenet_encode_id(&id, DEVICENET_GROUP_1, 16, 0));    // Group 1 msg id is 4 bits
    TEST_ASSERT_FALSE(protocore_devicenet_encode_id(&id, DEVICENET_GROUP_2, 8, 0));     // Group 2 msg id is 3 bits
    TEST_ASSERT_FALSE(protocore_devicenet_encode_id(&id, DEVICENET_GROUP_4, 0x30u, 0)); // Group 4 tops out at 0x2F
    TEST_ASSERT_FALSE(protocore_devicenet_encode_id(&id, (DeviceNetGroup)0, 0, 0));
    TEST_ASSERT_FALSE(protocore_devicenet_encode_id(&id, (DeviceNetGroup)5, 0, 0));
}

// The explicit-message header octet is FRAG | XID | MAC id, with the MAC clipped to its 6 bits.
void test_message_header_octet(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00u, protocore_devicenet_msg_header(PROTO_FALSE, PROTO_FALSE, 0));
    TEST_ASSERT_EQUAL_HEX8(0x05u, protocore_devicenet_msg_header(PROTO_FALSE, PROTO_FALSE, 5));
    TEST_ASSERT_EQUAL_HEX8(0x45u, protocore_devicenet_msg_header(PROTO_FALSE, PROTO_TRUE, 5));
    TEST_ASSERT_EQUAL_HEX8(0x85u, protocore_devicenet_msg_header(PROTO_TRUE, PROTO_FALSE, 5));
    TEST_ASSERT_EQUAL_HEX8(0xC5u, protocore_devicenet_msg_header(PROTO_TRUE, PROTO_TRUE, 5));
    TEST_ASSERT_EQUAL_HEX8(0x3Fu, protocore_devicenet_msg_header(PROTO_FALSE, PROTO_FALSE, 63));
    // A MAC id wider than six bits cannot reach the flag bits.
    TEST_ASSERT_EQUAL_HEX8(0x3Fu, protocore_devicenet_msg_header(PROTO_FALSE, PROTO_FALSE, 0xFF));
}

// The fragmentation octet is type in the top two bits, a modulo-64 count in the low six.
void test_fragmentation_octet(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00u, protocore_devicenet_frag_octet(DEVICENET_FRAG_FIRST, 0));
    TEST_ASSERT_EQUAL_HEX8(0x41u, protocore_devicenet_frag_octet(DEVICENET_FRAG_MIDDLE, 1));
    TEST_ASSERT_EQUAL_HEX8(0x82u, protocore_devicenet_frag_octet(DEVICENET_FRAG_LAST, 2));
    TEST_ASSERT_EQUAL_HEX8(0xFFu, protocore_devicenet_frag_octet(DEVICENET_FRAG_ACK, 63));
    // Neither field can overflow into the other.
    TEST_ASSERT_EQUAL_HEX8(0x80u, protocore_devicenet_frag_octet(DEVICENET_FRAG_LAST, 64));
    TEST_ASSERT_EQUAL_HEX8(0x3Fu, protocore_devicenet_frag_octet(0x3Fu, 63));
}

// A whole explicit message in one frame: the header octet then the CIP request, with FRAG clear.
// The body is a Get_Attribute_Single for the Identity Object's Vendor ID.
void test_single_frame_explicit_message(void)
{
    static const uint8_t CIP[6] = {0x0E, 0x02, 0x20, 0x01, 0x24, 0x01};
    CanFrame f;
    TEST_ASSERT_TRUE(protocore_devicenet_build_explicit(&f, DEVICENET_GROUP_2, DEVICENET_G2_UNCONNECTED_EXPLICIT_REQ, 5,
                                                        CIP, sizeof(CIP)));
    TEST_ASSERT_EQUAL_HEX32(0x42Cu, f.id);
    TEST_ASSERT_FALSE(f.extended);
    TEST_ASSERT_FALSE(f.rtr);
    TEST_ASSERT_EQUAL_UINT8(7u, f.dlc);       // 1 header octet + 6 body octets
    TEST_ASSERT_EQUAL_HEX8(0x05u, f.data[0]); // FRAG clear, XID clear, MAC 5
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CIP, f.data + 1, 6);

    // The header octet plus the body must fit the 8-octet CAN payload.
    static const uint8_t SEVEN[7] = {1, 2, 3, 4, 5, 6, 7};
    TEST_ASSERT_TRUE(protocore_devicenet_build_explicit(&f, DEVICENET_GROUP_2, 4, 5, SEVEN, 7));
    TEST_ASSERT_EQUAL_UINT8(8u, f.dlc);
    static const uint8_t EIGHT[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_FALSE(protocore_devicenet_build_explicit(&f, DEVICENET_GROUP_2, 4, 5, EIGHT, 8));

    TEST_ASSERT_TRUE(protocore_devicenet_build_explicit(&f, DEVICENET_GROUP_2, 4, 5, NULL, 0));
    TEST_ASSERT_EQUAL_UINT8(1u, f.dlc);
    TEST_ASSERT_FALSE(protocore_devicenet_build_explicit(NULL, DEVICENET_GROUP_2, 4, 5, CIP, 6));
    TEST_ASSERT_FALSE(protocore_devicenet_build_explicit(&f, DEVICENET_GROUP_2, 4, 5, NULL, 3));
    TEST_ASSERT_FALSE(protocore_devicenet_build_explicit(&f, DEVICENET_GROUP_2, 8, 5, CIP, 6)); // bad msg id
}

// A fragment frame is header octet + fragmentation octet + up to 6 data octets: the two overhead
// octets are what cap the payload at 6 rather than 7.
void test_fragment_frame_layout(void)
{
    static const uint8_t SIX[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    CanFrame f;
    TEST_ASSERT_TRUE(protocore_devicenet_build_fragment(&f, DEVICENET_GROUP_2, 4, 5, PROTO_TRUE, DEVICENET_FRAG_FIRST,
                                                        0, SIX, sizeof(SIX)));
    TEST_ASSERT_EQUAL_HEX32(0x42Cu, f.id);
    TEST_ASSERT_EQUAL_UINT8(8u, f.dlc);
    TEST_ASSERT_EQUAL_HEX8(0xC5u, f.data[0]); // FRAG | XID | MAC 5
    TEST_ASSERT_EQUAL_HEX8(0x00u, f.data[1]); // FIRST, count 0
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SIX, f.data + 2, 6);

    TEST_ASSERT_TRUE(
        protocore_devicenet_build_fragment(&f, DEVICENET_GROUP_2, 4, 5, PROTO_FALSE, DEVICENET_FRAG_LAST, 3, SIX, 2));
    TEST_ASSERT_EQUAL_UINT8(4u, f.dlc);
    TEST_ASSERT_EQUAL_HEX8(0x85u, f.data[0]); // FRAG set, XID clear
    TEST_ASSERT_EQUAL_HEX8(0x83u, f.data[1]); // LAST, count 3

    static const uint8_t SEVEN[7] = {1, 2, 3, 4, 5, 6, 7};
    TEST_ASSERT_FALSE(protocore_devicenet_build_fragment(&f, DEVICENET_GROUP_2, 4, 5, PROTO_FALSE, DEVICENET_FRAG_FIRST,
                                                         0, SEVEN, 7));
    TEST_ASSERT_FALSE(protocore_devicenet_build_fragment(&f, DEVICENET_GROUP_2, 4, 5, PROTO_FALSE, 0x10u, 0, SIX, 6));
    TEST_ASSERT_FALSE(
        protocore_devicenet_build_fragment(&f, DEVICENET_GROUP_2, 4, 5, PROTO_FALSE, DEVICENET_FRAG_FIRST, 64, SIX, 6));
    TEST_ASSERT_FALSE(
        protocore_devicenet_build_fragment(&f, DEVICENET_GROUP_2, 4, 5, PROTO_FALSE, DEVICENET_FRAG_FIRST, 0, NULL, 4));
    TEST_ASSERT_FALSE(protocore_devicenet_build_fragment(NULL, DEVICENET_GROUP_2, 4, 5, PROTO_FALSE,
                                                         DEVICENET_FRAG_FIRST, 0, SIX, 6));
    TEST_ASSERT_FALSE(
        protocore_devicenet_build_fragment(&f, DEVICENET_GROUP_2, 8, 5, PROTO_FALSE, DEVICENET_FRAG_FIRST, 0, SIX, 6));
}

// A message with FRAG clear is complete in one frame, and the reassembled body excludes the header
// octet.
void test_unfragmented_message_completes_immediately(void)
{
    static const uint8_t BODY[5] = {0x05, 0x0E, 0x02, 0x20, 0x01}; // header octet + 4 body octets
    DeviceNetFragRx rx;
    protocore_devicenet_frag_reset(&rx);
    TEST_ASSERT_EQUAL_INT(DEVICENET_FRAG_COMPLETE, protocore_devicenet_frag_feed(&rx, BODY, sizeof(BODY)));
    TEST_ASSERT_EQUAL_UINT16(4u, rx.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(BODY + 1, rx.buf, 4);

    // A header octet alone is a complete, empty message.
    static const uint8_t BARE[1] = {0x05};
    TEST_ASSERT_EQUAL_INT(DEVICENET_FRAG_COMPLETE, protocore_devicenet_frag_feed(&rx, BARE, 1));
    TEST_ASSERT_EQUAL_UINT16(0u, rx.len);

    TEST_ASSERT_EQUAL_INT(DEVICENET_FRAG_IGNORED, protocore_devicenet_frag_feed(&rx, BODY, 0));
    TEST_ASSERT_EQUAL_INT(DEVICENET_FRAG_IGNORED, protocore_devicenet_frag_feed(&rx, NULL, 5));
    TEST_ASSERT_EQUAL_INT(DEVICENET_FRAG_IGNORED, protocore_devicenet_frag_feed(NULL, BODY, 5));
    protocore_devicenet_frag_reset(NULL); // must not fault
}

// A fragmented message: the fragment counts must run 0, 1, 2, ... and the reassembled body is the
// concatenation of the data after each fragmentation octet. What the builder splits, the
// reassembler must rejoin.
void test_fragmented_message_reassembly(void)
{
    static const uint8_t SRC[14] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D};
    CanFrame f;
    DeviceNetFragRx rx;
    protocore_devicenet_frag_reset(&rx);

    TEST_ASSERT_TRUE(
        protocore_devicenet_build_fragment(&f, DEVICENET_GROUP_2, 4, 5, PROTO_FALSE, DEVICENET_FRAG_FIRST, 0, SRC, 6));
    TEST_ASSERT_EQUAL_INT(DEVICENET_FRAG_STARTED, protocore_devicenet_frag_feed(&rx, f.data, f.dlc));
    TEST_ASSERT_TRUE(rx.active);
    TEST_ASSERT_EQUAL_UINT8(1u, rx.next_count);

    TEST_ASSERT_TRUE(protocore_devicenet_build_fragment(&f, DEVICENET_GROUP_2, 4, 5, PROTO_FALSE, DEVICENET_FRAG_MIDDLE,
                                                        1, SRC + 6, 6));
    TEST_ASSERT_EQUAL_INT(DEVICENET_FRAG_PROGRESS, protocore_devicenet_frag_feed(&rx, f.data, f.dlc));
    TEST_ASSERT_EQUAL_UINT8(2u, rx.next_count);

    TEST_ASSERT_TRUE(protocore_devicenet_build_fragment(&f, DEVICENET_GROUP_2, 4, 5, PROTO_FALSE, DEVICENET_FRAG_LAST,
                                                        2, SRC + 12, 2));
    TEST_ASSERT_EQUAL_INT(DEVICENET_FRAG_COMPLETE, protocore_devicenet_frag_feed(&rx, f.data, f.dlc));
    TEST_ASSERT_FALSE(rx.active);
    TEST_ASSERT_EQUAL_UINT16(14u, rx.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SRC, rx.buf, 14);
}

// A count that skips, a middle or last fragment with no first one, and an acknowledge fragment are
// each handled without letting the wrong octets into the reassembled body.
void test_reassembly_refusals(void)
{
    static const uint8_t FIRST[8] = {0x85, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    static const uint8_t MIDDLE_1[8] = {0x85, 0x41, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC};
    static const uint8_t MIDDLE_2[4] = {0x85, 0x42, 0xDD, 0xEE};
    static const uint8_t LAST_2[4] = {0x85, 0x82, 0xDD, 0xEE};
    DeviceNetFragRx rx;

    // A middle fragment with no first one is an error.
    protocore_devicenet_frag_reset(&rx);
    TEST_ASSERT_EQUAL_INT(DEVICENET_FRAG_ERR, protocore_devicenet_frag_feed(&rx, MIDDLE_1, sizeof(MIDDLE_1)));
    TEST_ASSERT_EQUAL_UINT16(0u, rx.len);

    // A last fragment with no first one is an error too.
    protocore_devicenet_frag_reset(&rx);
    TEST_ASSERT_EQUAL_INT(DEVICENET_FRAG_ERR, protocore_devicenet_frag_feed(&rx, LAST_2, sizeof(LAST_2)));

    // A skipped count aborts the message rather than splicing the wrong octets in.
    protocore_devicenet_frag_reset(&rx);
    TEST_ASSERT_EQUAL_INT(DEVICENET_FRAG_STARTED, protocore_devicenet_frag_feed(&rx, FIRST, sizeof(FIRST)));
    TEST_ASSERT_EQUAL_INT(DEVICENET_FRAG_ERR, protocore_devicenet_frag_feed(&rx, MIDDLE_2, sizeof(MIDDLE_2)));
    TEST_ASSERT_FALSE(rx.active);
    TEST_ASSERT_EQUAL_UINT16(0u, rx.len);

    // FRAG set with no fragmentation octet at all.
    static const uint8_t STUB[1] = {0x85};
    protocore_devicenet_frag_reset(&rx);
    TEST_ASSERT_EQUAL_INT(DEVICENET_FRAG_ERR, protocore_devicenet_frag_feed(&rx, STUB, sizeof(STUB)));

    // An acknowledge fragment is flow control and contributes no data.
    static const uint8_t ACK[2] = {0x85, 0xC1};
    protocore_devicenet_frag_reset(&rx);
    TEST_ASSERT_EQUAL_INT(DEVICENET_FRAG_STARTED, protocore_devicenet_frag_feed(&rx, FIRST, sizeof(FIRST)));
    TEST_ASSERT_EQUAL_INT(DEVICENET_FRAG_IGNORED, protocore_devicenet_frag_feed(&rx, ACK, sizeof(ACK)));
    TEST_ASSERT_EQUAL_UINT16(6u, rx.len); // unchanged by the acknowledge
    TEST_ASSERT_TRUE(rx.active);
    TEST_ASSERT_EQUAL_INT(DEVICENET_FRAG_PROGRESS, protocore_devicenet_frag_feed(&rx, MIDDLE_1, sizeof(MIDDLE_1)));
    TEST_ASSERT_EQUAL_UINT16(12u, rx.len);
}

// A new first fragment restarts the message: whatever the previous, abandoned one had accumulated
// is discarded rather than prefixed onto the new body.
void test_a_new_first_fragment_restarts_the_message(void)
{
    static const uint8_t FIRST[8] = {0x85, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    static const uint8_t FIRST_AGAIN[4] = {0x85, 0x00, 0xAA, 0xBB};
    static const uint8_t LAST_1[4] = {0x85, 0x81, 0xCC, 0xDD};
    DeviceNetFragRx rx;

    protocore_devicenet_frag_reset(&rx);
    TEST_ASSERT_EQUAL_INT(DEVICENET_FRAG_STARTED, protocore_devicenet_frag_feed(&rx, FIRST, sizeof(FIRST)));
    TEST_ASSERT_EQUAL_UINT16(6u, rx.len);

    TEST_ASSERT_EQUAL_INT(DEVICENET_FRAG_STARTED, protocore_devicenet_frag_feed(&rx, FIRST_AGAIN, sizeof(FIRST_AGAIN)));
    TEST_ASSERT_EQUAL_UINT16(2u, rx.len);
    TEST_ASSERT_EQUAL_HEX8(0xAAu, rx.buf[0]);

    TEST_ASSERT_EQUAL_INT(DEVICENET_FRAG_COMPLETE, protocore_devicenet_frag_feed(&rx, LAST_1, sizeof(LAST_1)));
    static const uint8_t WANT[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    TEST_ASSERT_EQUAL_UINT16(4u, rx.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, rx.buf, 4);
}

// The count is modulo 64, so a long message wraps 63 -> 0 rather than stalling.
void test_fragment_count_wraps_at_sixty_four(void)
{
    static const uint8_t FIRST_63[4] = {0x85, 0x3F, 0x11, 0x22}; // FIRST with count 63
    static const uint8_t MIDDLE_0[4] = {0x85, 0x40, 0x33, 0x44}; // MIDDLE with count 0
    static const uint8_t LAST_1[4] = {0x85, 0x81, 0x55, 0x66};
    DeviceNetFragRx rx;
    protocore_devicenet_frag_reset(&rx);

    TEST_ASSERT_EQUAL_INT(DEVICENET_FRAG_STARTED, protocore_devicenet_frag_feed(&rx, FIRST_63, sizeof(FIRST_63)));
    TEST_ASSERT_EQUAL_UINT8(0u, rx.next_count); // (63 + 1) mod 64
    TEST_ASSERT_EQUAL_INT(DEVICENET_FRAG_PROGRESS, protocore_devicenet_frag_feed(&rx, MIDDLE_0, sizeof(MIDDLE_0)));
    TEST_ASSERT_EQUAL_UINT8(1u, rx.next_count);
    TEST_ASSERT_EQUAL_INT(DEVICENET_FRAG_COMPLETE, protocore_devicenet_frag_feed(&rx, LAST_1, sizeof(LAST_1)));
    static const uint8_t WANT[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    TEST_ASSERT_EQUAL_UINT16(6u, rx.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, rx.buf, 6);
}
