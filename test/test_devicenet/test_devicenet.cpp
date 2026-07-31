// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the DeviceNet link-adaptation codec (services/fieldbus/devicenet): the 4-group 11-bit
// CAN identifier encode/decode, the explicit-message header octet, the fragmentation octet,
// single-frame explicit messages, and the fragmentation reassembler. Identifier allocation
// checked against the ODVA DeviceNet spec. Pure host tests.

#include "services/fieldbus/devicenet/devicenet.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_id_group1()
{
    uint32_t id = 0;
    TEST_ASSERT_TRUE(pc_devicenet_encode_id(&id, DeviceNetGroup::DEVICENET_GROUP_1, 0x0A, 0x05)); // msgid 10, mac 5
    TEST_ASSERT_EQUAL_HEX32((0x0Au << 6) | 0x05u, id);
    TEST_ASSERT_TRUE(id < 0x400u);
    DeviceNetId d;
    TEST_ASSERT_TRUE(pc_devicenet_decode_id(id, &d));
    TEST_ASSERT_EQUAL_INT(DeviceNetGroup::DEVICENET_GROUP_1, d.group);
    TEST_ASSERT_EQUAL_UINT8(0x0A, d.msg_id);
    TEST_ASSERT_EQUAL_UINT8(0x05, d.mac_id);
}

void test_id_group2()
{
    uint32_t id = 0;
    // Group 2: 10 MAC(6) MsgID(3). mac 0x21, unconnected explicit request.
    TEST_ASSERT_TRUE(
        pc_devicenet_encode_id(&id, DeviceNetGroup::DEVICENET_GROUP_2, DEVICENET_G2_UNCONNECTED_EXPLICIT_REQ, 0x21));
    TEST_ASSERT_EQUAL_HEX32(0x400u | (0x21u << 3) | 4u, id);
    DeviceNetId d;
    TEST_ASSERT_TRUE(pc_devicenet_decode_id(id, &d));
    TEST_ASSERT_EQUAL_INT(DeviceNetGroup::DEVICENET_GROUP_2, d.group);
    TEST_ASSERT_EQUAL_UINT8(0x21, d.mac_id);
    TEST_ASSERT_EQUAL_UINT8(4, d.msg_id);
}

void test_id_group3_and_4()
{
    uint32_t id = 0;
    TEST_ASSERT_TRUE(pc_devicenet_encode_id(&id, DeviceNetGroup::DEVICENET_GROUP_3, 5, 0x09)); // 11 MsgID(3) MAC(6)
    TEST_ASSERT_EQUAL_HEX32(0x600u | (5u << 6) | 0x09u, id);
    DeviceNetId d;
    TEST_ASSERT_TRUE(pc_devicenet_decode_id(id, &d));
    TEST_ASSERT_EQUAL_INT(DeviceNetGroup::DEVICENET_GROUP_3, d.group);
    TEST_ASSERT_EQUAL_UINT8(5, d.msg_id);
    TEST_ASSERT_EQUAL_UINT8(0x09, d.mac_id);

    TEST_ASSERT_TRUE(pc_devicenet_encode_id(&id, DeviceNetGroup::DEVICENET_GROUP_4, 0x2A, 0)); // 11111 MsgID(6)
    TEST_ASSERT_EQUAL_HEX32(0x7C0u | 0x2Au, id);
    TEST_ASSERT_TRUE(pc_devicenet_decode_id(id, &d));
    TEST_ASSERT_EQUAL_INT(DeviceNetGroup::DEVICENET_GROUP_4, d.group);
    TEST_ASSERT_EQUAL_UINT8(0x2A, d.msg_id);

    // 0x7F0..0x7FF are invalid identifiers.
    TEST_ASSERT_FALSE(pc_devicenet_decode_id(0x7F5, &d));
    // out-of-range arguments fail closed.
    TEST_ASSERT_FALSE(pc_devicenet_encode_id(&id, DeviceNetGroup::DEVICENET_GROUP_1, 16, 0));   // msgid > 4 bits
    TEST_ASSERT_FALSE(pc_devicenet_encode_id(&id, DeviceNetGroup::DEVICENET_GROUP_2, 0, 64));   // mac > 6 bits
    TEST_ASSERT_FALSE(pc_devicenet_encode_id(&id, DeviceNetGroup::DEVICENET_GROUP_4, 0x30, 0)); // msgid > 0x2F
}

void test_header_and_frag_octets()
{
    TEST_ASSERT_EQUAL_HEX8(0x80 | 0x21, pc_devicenet_msg_header(true, false, 0x21));
    TEST_ASSERT_EQUAL_HEX8(0xC0 | 0x21, pc_devicenet_msg_header(true, true, 0x21));
    TEST_ASSERT_EQUAL_HEX8(0x21, pc_devicenet_msg_header(false, false, 0x21));
    TEST_ASSERT_EQUAL_HEX8(0x80 | 0x05, pc_devicenet_frag_octet(DEVICENET_FRAG_LAST, 5));
    TEST_ASSERT_EQUAL_HEX8(0x40 | 0x01, pc_devicenet_frag_octet(DEVICENET_FRAG_MIDDLE, 1));
}

void test_build_explicit_single_frame()
{
    const uint8_t cip[3] = {0x0E, 0x20, 0x01}; // a tiny CIP get-attribute-ish body
    CanFrame f;
    TEST_ASSERT_TRUE(pc_devicenet_build_explicit(&f, DeviceNetGroup::DEVICENET_GROUP_2,
                                                 DEVICENET_G2_UNCONNECTED_EXPLICIT_REQ, 0x21, cip, 3));
    TEST_ASSERT_FALSE(f.extended);
    TEST_ASSERT_EQUAL_UINT8(4, f.dlc);       // 1 header + 3 body
    TEST_ASSERT_EQUAL_HEX8(0x21, f.data[0]); // header, not fragmented, mac 0x21
    TEST_ASSERT_EQUAL_MEMORY(cip, f.data + 1, 3);
    // a body that does not fit in one frame is rejected (use fragmentation instead).
    uint8_t big[8] = {0};
    TEST_ASSERT_FALSE(pc_devicenet_build_explicit(&f, DeviceNetGroup::DEVICENET_GROUP_2, 4, 0x21, big, 8));
}

void test_frag_non_fragmented()
{
    // header octet with FRAG clear -> the body is complete in one frame.
    const uint8_t body[4] = {0x21, 0xAA, 0xBB, 0xCC};
    DeviceNetFragRx rx;
    pc_devicenet_frag_reset(&rx);
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_COMPLETE, pc_devicenet_frag_feed(&rx, body, 4));
    TEST_ASSERT_EQUAL_UINT16(3, rx.len);
    TEST_ASSERT_EQUAL_HEX8(0xAA, rx.buf[0]);
}

void test_frag_reassembly_roundtrip()
{
    DeviceNetFragRx rx;
    pc_devicenet_frag_reset(&rx);

    // First fragment (count 0): header(FRAG|mac) + frag(FIRST,0) + 6 data.
    uint8_t f0[8] = {0x80 | 0x21, pc_devicenet_frag_octet(DEVICENET_FRAG_FIRST, 0), 1, 2, 3, 4, 5, 6};
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_STARTED, pc_devicenet_frag_feed(&rx, f0, 8));

    // Middle fragment (count 1): + 6 more data.
    uint8_t f1[8] = {0x80 | 0x21, pc_devicenet_frag_octet(DEVICENET_FRAG_MIDDLE, 1), 7, 8, 9, 10, 11, 12};
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_PROGRESS, pc_devicenet_frag_feed(&rx, f1, 8));

    // Last fragment (count 2): + 2 data.
    uint8_t f2[4] = {0x80 | 0x21, pc_devicenet_frag_octet(DEVICENET_FRAG_LAST, 2), 13, 14};
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_COMPLETE, pc_devicenet_frag_feed(&rx, f2, 4));

    TEST_ASSERT_EQUAL_UINT16(14, rx.len);
    uint8_t expect[14] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    TEST_ASSERT_EQUAL_MEMORY(expect, rx.buf, 14);
}

// The fragment builder is the sender complement of the reassembler: a built 2-fragment message reassembles.
void test_build_fragment_roundtrip()
{
    CanFrame f0, f1;
    const uint8_t part0[6] = {1, 2, 3, 4, 5, 6};
    const uint8_t part1[2] = {7, 8};
    TEST_ASSERT_TRUE(pc_devicenet_build_fragment(&f0, DeviceNetGroup::DEVICENET_GROUP_2, 0x00, 0x21, false,
                                                 DEVICENET_FRAG_FIRST, 0, part0, 6));
    TEST_ASSERT_TRUE(pc_devicenet_build_fragment(&f1, DeviceNetGroup::DEVICENET_GROUP_2, 0x00, 0x21, false,
                                                 DEVICENET_FRAG_LAST, 1, part1, 2));

    // The frame body is the fragmented header + the fragmentation octet + the data.
    TEST_ASSERT_EQUAL_UINT8(8, f0.dlc); // header + frag octet + 6 data
    TEST_ASSERT_EQUAL_HEX8(0x80 | 0x21, f0.data[0]);
    TEST_ASSERT_EQUAL_HEX8(pc_devicenet_frag_octet(DEVICENET_FRAG_FIRST, 0), f0.data[1]);
    TEST_ASSERT_EQUAL_HEX8(1, f0.data[2]);

    DeviceNetFragRx rx;
    pc_devicenet_frag_reset(&rx);
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_STARTED, pc_devicenet_frag_feed(&rx, f0.data, f0.dlc));
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_COMPLETE, pc_devicenet_frag_feed(&rx, f1.data, f1.dlc));
    const uint8_t expect[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_EQUAL_UINT16(8, rx.len);
    TEST_ASSERT_EQUAL_MEMORY(expect, rx.buf, 8);

    // Guards: data too long (> 6), a null data with a nonzero length, a frag count > 63, and a null out.
    TEST_ASSERT_FALSE(pc_devicenet_build_fragment(&f0, DeviceNetGroup::DEVICENET_GROUP_2, 0, 0x21, false,
                                                  DEVICENET_FRAG_FIRST, 0, part0, 7));
    TEST_ASSERT_FALSE(pc_devicenet_build_fragment(&f0, DeviceNetGroup::DEVICENET_GROUP_2, 0, 0x21, false,
                                                  DEVICENET_FRAG_FIRST, 0, nullptr, 3));
    TEST_ASSERT_FALSE(pc_devicenet_build_fragment(&f0, DeviceNetGroup::DEVICENET_GROUP_2, 0, 0x21, false,
                                                  DEVICENET_FRAG_FIRST, 64, part1, 2));
    TEST_ASSERT_FALSE(pc_devicenet_build_fragment(nullptr, DeviceNetGroup::DEVICENET_GROUP_2, 0, 0x21, false,
                                                  DEVICENET_FRAG_FIRST, 0, part1, 2));
}

void test_frag_out_of_order_errors()
{
    DeviceNetFragRx rx;
    pc_devicenet_frag_reset(&rx);
    uint8_t f0[3] = {0x80 | 0x21, pc_devicenet_frag_octet(DEVICENET_FRAG_FIRST, 0), 0xAA};
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_STARTED, pc_devicenet_frag_feed(&rx, f0, 3));
    // jump straight to a middle count 2 (expected 1) -> error, session reset.
    uint8_t bad[3] = {0x80 | 0x21, pc_devicenet_frag_octet(DEVICENET_FRAG_MIDDLE, 2), 0xBB};
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_ERR, pc_devicenet_frag_feed(&rx, bad, 3));
    TEST_ASSERT_FALSE(rx.active);
}

// Encode/decode/build argument rejections.
void test_id_error_paths()
{
    uint32_t id = 0;
    TEST_ASSERT_FALSE(pc_devicenet_encode_id(&id, DeviceNetGroup::DEVICENET_GROUP_2, 8, 0)); // group 2 msg_id > 7
    TEST_ASSERT_FALSE(pc_devicenet_encode_id(&id, DeviceNetGroup::DEVICENET_GROUP_3, 8, 0)); // group 3 msg_id > 7
    TEST_ASSERT_FALSE(pc_devicenet_encode_id(&id, (DeviceNetGroup)99, 0, 0));                // invalid group
    TEST_ASSERT_FALSE(pc_devicenet_decode_id(0x100, nullptr));                               // null out
    CanFrame f;
    const uint8_t one[1] = {0xAB};
    TEST_ASSERT_FALSE(
        pc_devicenet_build_explicit(&f, DeviceNetGroup::DEVICENET_GROUP_2, 8, 0, one, 1)); // id encode fails
}

// The reassembler's reject/ignore branches.
void test_frag_reject_paths()
{
    DeviceNetFragRx rx;
    pc_devicenet_frag_reset(&rx);
    const uint8_t body[3] = {0x80 | 0x21, DEVICENET_FRAG_FIRST, 0xAA};

    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_IGNORED,
                          pc_devicenet_frag_feed(nullptr, body, 3)); // null rx
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_IGNORED,
                          pc_devicenet_frag_feed(&rx, nullptr, 3)); // null body
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_IGNORED, pc_devicenet_frag_feed(&rx, body, 0)); // empty

    const uint8_t hdr_only[1] = {0x80 | 0x21}; // FRAG set but no fragmentation octet
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_ERR, pc_devicenet_frag_feed(&rx, hdr_only, 1));

    // A LAST fragment with no active session -> error.
    pc_devicenet_frag_reset(&rx);
    uint8_t last_no_first[3] = {0x80 | 0x21, pc_devicenet_frag_octet(DEVICENET_FRAG_LAST, 0), 0xCC};
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_ERR,
                          pc_devicenet_frag_feed(&rx, last_no_first, sizeof(last_no_first)));

    // An ACK fragment type is flow control, not data -> ignored.
    pc_devicenet_frag_reset(&rx);
    uint8_t ack[3] = {0x80 | 0x21, pc_devicenet_frag_octet(DEVICENET_FRAG_ACK, 0), 0xDD};
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_IGNORED, pc_devicenet_frag_feed(&rx, ack, sizeof(ack)));
}

// Accumulating fragments past PC_DEVICENET_MSG_MAX (256) overflows the reassembly
// buffer, so the middle/last append fails and the session resets.
void test_frag_overflow()
{
    static uint8_t frag[255]; // header + frag octet + 253 data
    memset(frag + 2, 0xEE, 253);
    DeviceNetFragRx rx;

    pc_devicenet_frag_reset(&rx);
    frag[0] = 0x80 | 0x21;
    frag[1] = pc_devicenet_frag_octet(DEVICENET_FRAG_FIRST, 0);
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_STARTED, pc_devicenet_frag_feed(&rx, frag, sizeof(frag)));
    frag[1] = pc_devicenet_frag_octet(DEVICENET_FRAG_MIDDLE, 1); // 253 + 253 = 506 > 256
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_ERR, pc_devicenet_frag_feed(&rx, frag, sizeof(frag)));
    TEST_ASSERT_FALSE(rx.active);

    pc_devicenet_frag_reset(&rx);
    frag[1] = pc_devicenet_frag_octet(DEVICENET_FRAG_FIRST, 0);
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_STARTED, pc_devicenet_frag_feed(&rx, frag, sizeof(frag)));
    frag[1] = pc_devicenet_frag_octet(DEVICENET_FRAG_LAST, 1); // last-fragment append overflow
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_ERR, pc_devicenet_frag_feed(&rx, frag, sizeof(frag)));
}

// Null-pointer arguments to the encoder / reset are rejected without dereferencing.
void test_null_arguments()
{
    // encode_id with a null destination fails closed and writes nothing.
    TEST_ASSERT_FALSE(pc_devicenet_encode_id(nullptr, DeviceNetGroup::DEVICENET_GROUP_1, 0, 0));
    // frag_reset tolerates a null context (no crash, nothing to clear).
    pc_devicenet_frag_reset(nullptr);
    // A null frame destination is rejected before the body is looked at.
    const uint8_t one[1] = {0xAB};
    TEST_ASSERT_FALSE(pc_devicenet_build_explicit(nullptr, DeviceNetGroup::DEVICENET_GROUP_2, 4, 0x21, one, 1));
}

// build_explicit's body arguments: a header-only message (body_len 0) is legal and
// emits a 1-octet frame; a non-zero length with a null body is rejected.
void test_build_explicit_body_arguments()
{
    CanFrame f;
    // body_len 0 with a null body: valid, just the header octet.
    TEST_ASSERT_TRUE(pc_devicenet_build_explicit(&f, DeviceNetGroup::DEVICENET_GROUP_2,
                                                 DEVICENET_G2_UNCONNECTED_EXPLICIT_REQ, 0x21, nullptr, 0));
    TEST_ASSERT_EQUAL_UINT8(1, f.dlc);
    TEST_ASSERT_EQUAL_HEX8(0x21, f.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0, f.data[1]); // the rest of the payload is zeroed
    // body_len set but body null -> rejected.
    TEST_ASSERT_FALSE(pc_devicenet_build_explicit(&f, DeviceNetGroup::DEVICENET_GROUP_2, 4, 0x21, nullptr, 3));
}

// A non-fragmented frame carrying only the header octet is a complete, empty message.
void test_frag_non_fragmented_header_only()
{
    const uint8_t body[1] = {0x21}; // FRAG clear, no payload octets
    DeviceNetFragRx rx;
    pc_devicenet_frag_reset(&rx);
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_COMPLETE, pc_devicenet_frag_feed(&rx, body, 1));
    TEST_ASSERT_EQUAL_UINT16(0, rx.len);
    TEST_ASSERT_FALSE(rx.active);
}

// Fragments that carry no data octets past the fragmentation octet still advance the
// session: FIRST/MIDDLE/LAST with body_len 2 are accepted and leave the buffer empty.
void test_frag_empty_data_fragments()
{
    DeviceNetFragRx rx;
    pc_devicenet_frag_reset(&rx);
    uint8_t f[2] = {0x80 | 0x21, pc_devicenet_frag_octet(DEVICENET_FRAG_FIRST, 0)};
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_STARTED, pc_devicenet_frag_feed(&rx, f, 2));
    TEST_ASSERT_EQUAL_UINT16(0, rx.len);
    f[1] = pc_devicenet_frag_octet(DEVICENET_FRAG_MIDDLE, 1);
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_PROGRESS, pc_devicenet_frag_feed(&rx, f, 2));
    TEST_ASSERT_EQUAL_UINT16(0, rx.len);
    f[1] = pc_devicenet_frag_octet(DEVICENET_FRAG_LAST, 2);
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_COMPLETE, pc_devicenet_frag_feed(&rx, f, 2));
    TEST_ASSERT_EQUAL_UINT16(0, rx.len);
    TEST_ASSERT_FALSE(rx.active); // LAST closed the session
}

// The two remaining sequencing rejections: a MIDDLE with no session open, and a LAST
// whose count does not match the expected one.
void test_frag_sequence_rejects()
{
    DeviceNetFragRx rx;
    pc_devicenet_frag_reset(&rx);
    // MIDDLE with no FIRST -> error (no session to continue).
    uint8_t mid[3] = {0x80 | 0x21, pc_devicenet_frag_octet(DEVICENET_FRAG_MIDDLE, 1), 0xAA};
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_ERR, pc_devicenet_frag_feed(&rx, mid, 3));
    TEST_ASSERT_FALSE(rx.active);

    // FIRST (count 0, expects 1) then LAST with count 5 -> error, session reset.
    pc_devicenet_frag_reset(&rx);
    uint8_t first[3] = {0x80 | 0x21, pc_devicenet_frag_octet(DEVICENET_FRAG_FIRST, 0), 0xBB};
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_STARTED, pc_devicenet_frag_feed(&rx, first, 3));
    uint8_t last[3] = {0x80 | 0x21, pc_devicenet_frag_octet(DEVICENET_FRAG_LAST, 5), 0xCC};
    TEST_ASSERT_EQUAL_INT(DeviceNetFragResult::DEVICENET_FRAG_ERR, pc_devicenet_frag_feed(&rx, last, 3));
    TEST_ASSERT_FALSE(rx.active);
    TEST_ASSERT_EQUAL_UINT16(0, rx.len);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_id_group1);
    RUN_TEST(test_id_group2);
    RUN_TEST(test_id_group3_and_4);
    RUN_TEST(test_header_and_frag_octets);
    RUN_TEST(test_build_explicit_single_frame);
    RUN_TEST(test_frag_non_fragmented);
    RUN_TEST(test_frag_reassembly_roundtrip);
    RUN_TEST(test_build_fragment_roundtrip);
    RUN_TEST(test_frag_out_of_order_errors);
    RUN_TEST(test_id_error_paths);
    RUN_TEST(test_frag_reject_paths);
    RUN_TEST(test_frag_overflow);
    RUN_TEST(test_null_arguments);
    RUN_TEST(test_build_explicit_body_arguments);
    RUN_TEST(test_frag_non_fragmented_header_only);
    RUN_TEST(test_frag_empty_data_fragments);
    RUN_TEST(test_frag_sequence_rejects);
    return UNITY_END();
}
