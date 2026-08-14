// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Ethernet POWERLINK basic frame codec (services/fieldbus/powerlink/powerlink.h).
//
// The load-bearing case is test_epsg_message_type_ids: EPSG DS 301 V1.5.1 App. 3.1 is a normative
// constant table assigning SoC 01h, PReq 03h, PRes 04h, SoA 05h and ASnd 06h, and section 4.5's
// Node ID table assigns 240 to C_ADR_MN_DEF_NODE_ID and 255 to C_ADR_BROADCAST. Section 4.6.1.1.1
// fixes the basic frame as MessageType, Destination, Source, Payload - one octet each for the
// first three - so those five numbers plus that order are the whole wire contract a POWERLINK
// managing node has to get right for a controlled node to answer it.

#include "services/fieldbus/powerlink/powerlink.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// EPSG DS 301 V1.5.1 App. 3.1 "POWERLINK Message Type Ids" and section 4.5's Node ID assignment.
void test_epsg_message_type_ids(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x01, EPL_MSG_SOC);
    TEST_ASSERT_EQUAL_HEX8(0x03, EPL_MSG_PREQ);
    TEST_ASSERT_EQUAL_HEX8(0x04, EPL_MSG_PRES);
    TEST_ASSERT_EQUAL_HEX8(0x05, EPL_MSG_SOA);
    TEST_ASSERT_EQUAL_HEX8(0x06, EPL_MSG_ASND);
    TEST_ASSERT_EQUAL_HEX8(240, EPL_NODE_MN);
    TEST_ASSERT_EQUAL_HEX8(255, EPL_NODE_BROADCAST);
}

// EPSG DS 301 section 4.6.1.1.1: the POWERLINK basic frame is MessageType (octet 0), Destination
// (octet 1), Source (octet 2), then the payload. Section 4.6.1.1.2 gives SoC a broadcast
// destination and the MN as source, with no payload the codec carries.
void test_soc_frame(void)
{
    uint8_t out[16];
    size_t n = protocore_epl_soc(EPL_NODE_MN, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(3u, n);
    static const uint8_t WANT[3] = {EPL_MSG_SOC, EPL_NODE_BROADCAST, EPL_NODE_MN};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, 3);

    EplFrame f;
    TEST_ASSERT_TRUE(protocore_epl_parse(out, n, &f));
    TEST_ASSERT_EQUAL_HEX8(EPL_MSG_SOC, f.msg_type);
    TEST_ASSERT_EQUAL_HEX8(EPL_NODE_BROADCAST, f.dest);
    TEST_ASSERT_EQUAL_HEX8(EPL_NODE_MN, f.source);
    TEST_ASSERT_EQUAL_UINT(0u, f.payload_len);
    TEST_ASSERT_NULL(f.payload);
}

// EPSG DS 301 section 4.6.1.1.3: PReq is unicast from the MN to one CN and carries that CN's
// output process image. Node IDs 1..239 are the regular CNs.
void test_preq_frame(void)
{
    static const uint8_t PDO[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t out[16];
    size_t n = protocore_epl_preq(7, EPL_NODE_MN, PDO, sizeof(PDO), out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(3u + sizeof(PDO), n);
    TEST_ASSERT_EQUAL_HEX8(EPL_MSG_PREQ, out[0]);
    TEST_ASSERT_EQUAL_HEX8(7, out[1]); // unicast to the CN
    TEST_ASSERT_EQUAL_HEX8(EPL_NODE_MN, out[2]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PDO, out + 3, sizeof(PDO));

    EplFrame f;
    TEST_ASSERT_TRUE(protocore_epl_parse(out, n, &f));
    TEST_ASSERT_EQUAL_HEX8(EPL_MSG_PREQ, f.msg_type);
    TEST_ASSERT_EQUAL_HEX8(7, f.dest);
    TEST_ASSERT_EQUAL_UINT(sizeof(PDO), f.payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PDO, f.payload, sizeof(PDO));
    TEST_ASSERT_EQUAL_PTR(out + 3, f.payload); // the payload points into the frame, not a copy
}

// EPSG DS 301 section 4.6.1.1.4: PRes is multicast from the CN, so its destination is the broadcast
// node id and its source is the answering CN.
void test_pres_frame(void)
{
    static const uint8_t PDO[2] = {0x12, 0x34};
    uint8_t out[16];
    size_t n = protocore_epl_pres(7, PDO, sizeof(PDO), out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(5u, n);
    TEST_ASSERT_EQUAL_HEX8(EPL_MSG_PRES, out[0]);
    TEST_ASSERT_EQUAL_HEX8(EPL_NODE_BROADCAST, out[1]);
    TEST_ASSERT_EQUAL_HEX8(7, out[2]);

    EplFrame f;
    TEST_ASSERT_TRUE(protocore_epl_parse(out, n, &f));
    TEST_ASSERT_EQUAL_HEX8(7, f.source);
    TEST_ASSERT_EQUAL_HEX8(EPL_NODE_BROADCAST, f.dest);
}

// EPSG DS 301 section 4.6.1.1.5: SoA is multicast from the MN and opens the asynchronous phase; its
// payload is the SoA field block, which may be absent for a bare invite.
void test_soa_frame(void)
{
    uint8_t out[16];
    size_t n = protocore_epl_soa(EPL_NODE_MN, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(3u, n);
    static const uint8_t BARE[3] = {EPL_MSG_SOA, EPL_NODE_BROADCAST, EPL_NODE_MN};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(BARE, out, 3);

    static const uint8_t FIELDS[3] = {0x05, 0x01, 0x20}; // NMT status, requested service, target
    n = protocore_epl_soa(EPL_NODE_MN, FIELDS, sizeof(FIELDS), out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(6u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(FIELDS, out + 3, sizeof(FIELDS));
}

// EPSG DS 301 section 4.6.1.1.6: ASnd carries the asynchronous service block and may be addressed
// to one node or broadcast.
void test_asnd_frame(void)
{
    static const uint8_t SERVICE[3] = {0x01, 0xAA, 0xBB}; // service id + service data
    uint8_t out[16];

    size_t n = protocore_epl_asnd(1, 240, SERVICE, sizeof(SERVICE), out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(6u, n);
    TEST_ASSERT_EQUAL_HEX8(EPL_MSG_ASND, out[0]);
    TEST_ASSERT_EQUAL_HEX8(1, out[1]);
    TEST_ASSERT_EQUAL_HEX8(240, out[2]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SERVICE, out + 3, sizeof(SERVICE));

    n = protocore_epl_asnd(EPL_NODE_BROADCAST, 5, SERVICE, sizeof(SERVICE), out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(6u, n);
    TEST_ASSERT_EQUAL_HEX8(EPL_NODE_BROADCAST, out[1]);
}

// Whatever a frame carries, parsing what was built returns the same three header octets and the
// same payload octets, for every message type and every payload length up to the buffer.
void test_build_parse_round_trip(void)
{
    static const uint8_t TYPES[5] = {EPL_MSG_SOC, EPL_MSG_PREQ, EPL_MSG_PRES, EPL_MSG_SOA, EPL_MSG_ASND};
    uint8_t payload[32];
    for (size_t i = 0; i < sizeof(payload); i++)
    {
        payload[i] = (uint8_t)(i * 7 + 1);
    }

    for (size_t t = 0; t < sizeof(TYPES); t++)
    {
        for (size_t len = 0; len <= sizeof(payload); len++)
        {
            uint8_t out[64];
            size_t n = protocore_epl_build(TYPES[t], 0x2A, 0xF0, len ? payload : NULL, len, out, sizeof(out));
            TEST_ASSERT_EQUAL_UINT(3u + len, n);

            EplFrame f;
            TEST_ASSERT_TRUE(protocore_epl_parse(out, n, &f));
            TEST_ASSERT_EQUAL_HEX8(TYPES[t], f.msg_type);
            TEST_ASSERT_EQUAL_HEX8(0x2A, f.dest);
            TEST_ASSERT_EQUAL_HEX8(0xF0, f.source);
            TEST_ASSERT_EQUAL_UINT(len, f.payload_len);
            if (len)
            {
                TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, f.payload, len);
            }
        }
    }
}

// App. 3.1 ends with "ID values not listed by the table are reserved", so a frame carrying a
// message type this profile does not define is not an EPL basic frame. 02h and 07h..0Dh sit either
// side of the ids the codec implements.
void test_parse_refuses_an_undefined_message_type(void)
{
    EplFrame f;
    static const uint8_t UNDEFINED[] = {0x00, 0x02, 0x07, 0x08, 0x0D, 0x7F, 0xFF};
    for (size_t i = 0; i < sizeof(UNDEFINED); i++)
    {
        uint8_t frame[4] = {UNDEFINED[i], EPL_NODE_BROADCAST, EPL_NODE_MN, 0x00};
        TEST_ASSERT_FALSE_MESSAGE(protocore_epl_parse(frame, sizeof(frame), &f), "undefined message type accepted");
    }
}

// The three header octets are mandatory, so anything shorter is not a frame.
void test_parse_refuses_a_short_frame(void)
{
    static const uint8_t FRAME[3] = {EPL_MSG_SOC, EPL_NODE_BROADCAST, EPL_NODE_MN};
    EplFrame f;
    for (size_t n = 0; n < 3; n++)
    {
        TEST_ASSERT_FALSE(protocore_epl_parse(FRAME, n, &f));
    }
    TEST_ASSERT_TRUE(protocore_epl_parse(FRAME, 3, &f));
    TEST_ASSERT_FALSE(protocore_epl_parse(NULL, 3, &f));
    TEST_ASSERT_FALSE(protocore_epl_parse(FRAME, 3, NULL));
}

// A builder given less room than the 3-octet header plus the payload writes nothing and reports 0,
// and a nonzero payload length with a null pointer is refused rather than dereferenced.
void test_build_refuses_bad_arguments(void)
{
    static const uint8_t PDO[4] = {1, 2, 3, 4};
    uint8_t out[16];

    for (size_t cap = 0; cap < 3 + sizeof(PDO); cap++)
    {
        TEST_ASSERT_EQUAL_UINT(0u, protocore_epl_build(EPL_MSG_PREQ, 1, 240, PDO, sizeof(PDO), out, cap));
    }
    TEST_ASSERT_EQUAL_UINT(7u, protocore_epl_build(EPL_MSG_PREQ, 1, 240, PDO, sizeof(PDO), out, 7));

    TEST_ASSERT_EQUAL_UINT(0u, protocore_epl_build(EPL_MSG_PREQ, 1, 240, NULL, 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_epl_build(EPL_MSG_SOC, 255, 240, NULL, 0, NULL, sizeof(out)));

    // the convenience builders inherit the same refusal
    TEST_ASSERT_EQUAL_UINT(0u, protocore_epl_soc(EPL_NODE_MN, out, 2));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_epl_pres(7, PDO, sizeof(PDO), out, 6));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_epl_soa(EPL_NODE_MN, PDO, sizeof(PDO), out, 6));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_epl_asnd(1, 240, PDO, sizeof(PDO), out, 6));
}

// One isochronous cycle as EPSG DS 301 section 4.2.4 schedules it: the MN multicasts a SoC, polls
// each CN with a PReq, each CN answers with a PRes carrying its process image, and a SoA closes the
// isochronous phase. Every frame in the sequence parses back to the node it was addressed to.
void test_isochronous_cycle_sequence(void)
{
    uint8_t buf[32];
    EplFrame f;

    TEST_ASSERT_EQUAL_UINT(3u, protocore_epl_soc(EPL_NODE_MN, buf, sizeof(buf)));
    TEST_ASSERT_TRUE(protocore_epl_parse(buf, 3, &f));
    TEST_ASSERT_EQUAL_HEX8(EPL_MSG_SOC, f.msg_type);

    for (uint8_t cn = 1; cn <= 3; cn++)
    {
        uint8_t outputs[2] = {cn, (uint8_t)(cn * 2)};
        size_t n = protocore_epl_preq(cn, EPL_NODE_MN, outputs, sizeof(outputs), buf, sizeof(buf));
        TEST_ASSERT_TRUE(protocore_epl_parse(buf, n, &f));
        TEST_ASSERT_EQUAL_HEX8(EPL_MSG_PREQ, f.msg_type);
        TEST_ASSERT_EQUAL_HEX8(cn, f.dest);
        TEST_ASSERT_EQUAL_HEX8(EPL_NODE_MN, f.source);

        uint8_t inputs[2] = {(uint8_t)(cn + 100), 0};
        n = protocore_epl_pres(cn, inputs, sizeof(inputs), buf, sizeof(buf));
        TEST_ASSERT_TRUE(protocore_epl_parse(buf, n, &f));
        TEST_ASSERT_EQUAL_HEX8(EPL_MSG_PRES, f.msg_type);
        TEST_ASSERT_EQUAL_HEX8(cn, f.source);
        TEST_ASSERT_EQUAL_HEX8(EPL_NODE_BROADCAST, f.dest);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(cn + 100), f.payload[0]);
    }

    TEST_ASSERT_EQUAL_UINT(3u, protocore_epl_soa(EPL_NODE_MN, NULL, 0, buf, sizeof(buf)));
    TEST_ASSERT_TRUE(protocore_epl_parse(buf, 3, &f));
    TEST_ASSERT_EQUAL_HEX8(EPL_MSG_SOA, f.msg_type);
}
