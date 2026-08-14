// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the DMX512 + RDM codec (server/peripherals/dmx/dmx.h).
//
// The governing document is ANSI E1.20-2010 (RDM over DMX512). Table 6-2 fixes the packet format,
// 6.2.3 defines the Message Length as "the number of slots in the RDM Packet including the START
// Code and excluding the Checksum", 6.2.11 defines the Checksum as the modulo-0x10000 additive sum
// of that same span, Tables 7-1 and 7-2 give the DISC_UNIQUE_BRANCH response encoding and decoding,
// 10.5.1 gives the 0x13-octet DEVICE_INFO block, and Appendix A gives every constant.
//
// test_e120_table_6_6_checksum_example is the load-bearing case: E1.20 Table 6-6 prints a whole
// packet slot by slot together with its checksum, 0x066A. Reproducing those 27 octets exactly is
// what makes the builder trustworthy, because a wrong field offset, a wrong endianness, or a
// checksum over the wrong span all move that number.

#include "server/peripherals/dmx/dmx.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// ANSI E1.20-2010 Table 6-6, "Checksum Usage Example", slot for slot:
//   0 START Code 0xCC   1 Sub START Code 0x01   2 Message Length 0x19
//   3-8   Destination UID 0x123456789abc
//   9-14  Source UID      0xcba987654321
//   15 TN 0x00   16 Port ID 0x01   17 Message Count 0x00   18-19 Sub-Device 0x0000
//   20 Command Class 0x20 (GET_COMMAND)   21-22 PID 0x0030 (STATUS_MESSAGES)
//   23 PDL 0x01   24 PD 0x04 (STATUS_ERROR)   25-26 Checksum 0x066A
void test_e120_table_6_6_checksum_example(void)
{
    static const uint8_t WANT[27] = {0xCC, 0x01, 0x19, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC,
                                     0xCB, 0xA9, 0x87, 0x65, 0x43, 0x21, 0x00, 0x01, 0x00,
                                     0x00, 0x00, 0x20, 0x00, 0x30, 0x01, 0x04, 0x06, 0x6A};
    static const uint8_t PD[1] = {0x04};

    RdmPacket p;
    memset(&p, 0, sizeof(p));
    p.dest_uid = 0x123456789ABCULL;
    p.src_uid = 0xCBA987654321ULL;
    p.tn = 0x00u;
    p.port_id = 0x01u;
    p.msg_count = 0x00u;
    p.sub_device = 0x0000u;
    p.cc = RDM_CC_GET;
    p.pid = 0x0030u;

    uint8_t buf[32];
    size_t n = protocore_rdm_build(buf, sizeof(buf), &p, PD, 1u);
    TEST_ASSERT_EQUAL_size_t(27u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 27u);

    // 6.2.11: the checksum is the additive sum of slots 0 through 24.
    TEST_ASSERT_EQUAL_HEX16(0x066Au, protocore_rdm_checksum(WANT, 25u));

    // The same octets parse back to the same fields, checksum and all.
    RdmPacket g;
    size_t consumed = 0u;
    TEST_ASSERT_TRUE(protocore_rdm_parse(WANT, sizeof(WANT), &g, &consumed));
    TEST_ASSERT_EQUAL_size_t(27u, consumed);
    TEST_ASSERT_EQUAL_HEX64(0x123456789ABCULL, g.dest_uid);
    TEST_ASSERT_EQUAL_HEX64(0xCBA987654321ULL, g.src_uid);
    TEST_ASSERT_EQUAL_HEX8(0x00u, g.tn);
    TEST_ASSERT_EQUAL_HEX8(0x01u, g.port_id);
    TEST_ASSERT_EQUAL_HEX8(0x00u, g.msg_count);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, g.sub_device);
    TEST_ASSERT_EQUAL_HEX8(0x20u, g.cc);
    TEST_ASSERT_EQUAL_HEX16(0x0030u, g.pid);
    TEST_ASSERT_EQUAL_UINT8(1u, g.pdl);
    TEST_ASSERT_EQUAL_HEX8(0x04u, g.pdata[0]);
}

// Appendix A: START Codes, Table A-1 Command Class Defines, Table A-2 Response Type Defines, and
// the Parameter ID values from Table A-3.
void test_e120_appendix_a_constants(void)
{
    TEST_ASSERT_EQUAL_HEX8(0xCCu, RDM_SC);     // SC_RDM
    TEST_ASSERT_EQUAL_HEX8(0x01u, RDM_SUB_SC); // SC_SUB_MESSAGE

    TEST_ASSERT_EQUAL_HEX8(0x10u, RDM_CC_DISCOVERY);
    TEST_ASSERT_EQUAL_HEX8(0x11u, RDM_CC_DISCOVERY_RESPONSE);
    TEST_ASSERT_EQUAL_HEX8(0x20u, RDM_CC_GET);
    TEST_ASSERT_EQUAL_HEX8(0x21u, RDM_CC_GET_RESPONSE);
    TEST_ASSERT_EQUAL_HEX8(0x30u, RDM_CC_SET);
    TEST_ASSERT_EQUAL_HEX8(0x31u, RDM_CC_SET_RESPONSE);

    TEST_ASSERT_EQUAL_HEX8(0x00u, RDM_RESPONSE_ACK);
    TEST_ASSERT_EQUAL_HEX8(0x01u, RDM_RESPONSE_ACK_TIMER);
    TEST_ASSERT_EQUAL_HEX8(0x02u, RDM_RESPONSE_NACK_REASON);
    TEST_ASSERT_EQUAL_HEX8(0x03u, RDM_RESPONSE_ACK_OVERFLOW);

    TEST_ASSERT_EQUAL_HEX16(0x0001u, RDM_PID_DISC_UNIQUE_BRANCH);
    TEST_ASSERT_EQUAL_HEX16(0x0002u, RDM_PID_DISC_MUTE);
    TEST_ASSERT_EQUAL_HEX16(0x0003u, RDM_PID_DISC_UN_MUTE);
    TEST_ASSERT_EQUAL_HEX16(0x0050u, RDM_PID_SUPPORTED_PARAMETERS);
    TEST_ASSERT_EQUAL_HEX16(0x0060u, RDM_PID_DEVICE_INFO);
    TEST_ASSERT_EQUAL_HEX16(0x00F0u, RDM_PID_DMX_START_ADDRESS);
    TEST_ASSERT_EQUAL_HEX16(0x1000u, RDM_PID_IDENTIFY_DEVICE);
}

// 6.2.3: "The Message Length field points to the Checksum High Slot", and its range is 24 to 255.
// So slot 2 is 24 + PDL, the packet is that plus the two checksum slots, and the checksum lands at
// exactly the slot the length names.
void test_e120_message_length_points_at_the_checksum_high_slot(void)
{
    static const uint8_t PD[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    for (uint8_t pdl = 0u; pdl <= 8u; pdl++)
    {
        RdmPacket p;
        memset(&p, 0, sizeof(p));
        p.cc = RDM_CC_SET;
        p.pid = RDM_PID_DMX_START_ADDRESS;

        uint8_t buf[64];
        size_t n = protocore_rdm_build(buf, sizeof(buf), &p, pdl ? PD : NULL, pdl);
        TEST_ASSERT_EQUAL_size_t((size_t)RDM_OVERHEAD + pdl, n);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(24u + pdl), buf[2]);
        TEST_ASSERT_EQUAL_UINT8(pdl, buf[23]);
        TEST_ASSERT_EQUAL_size_t((size_t)buf[2] + 2u, n);

        uint16_t cs = protocore_rdm_checksum(buf, buf[2]);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(cs >> 8), buf[buf[2]]);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(cs & 0xFFu), buf[buf[2] + 1u]);

        RdmPacket g;
        TEST_ASSERT_TRUE(protocore_rdm_parse(buf, n, &g, NULL));
        TEST_ASSERT_EQUAL_UINT8(pdl, g.pdl);
        if (pdl)
        {
            TEST_ASSERT_EQUAL_HEX8_ARRAY(PD, g.pdata, pdl);
        }
        else
        {
            TEST_ASSERT_NULL(g.pdata);
        }
    }
}

// A UID is 48 bits, the 16-bit Manufacturer ID above the 32-bit Device ID (E1.20 sec 5.1), and it
// rides the wire big-endian (6.1 Byte Ordering: most significant byte first).
void test_e120_uid_is_manufacturer_above_device(void)
{
    TEST_ASSERT_EQUAL_HEX64(0x123456789ABCULL, protocore_rdm_uid(0x1234u, 0x56789ABCu));
    TEST_ASSERT_EQUAL_HEX64(0xFFFFFFFFFFFFULL, protocore_rdm_uid(0xFFFFu, 0xFFFFFFFFu)); // BROADCAST_ALL_DEVICES_ID
    TEST_ASSERT_EQUAL_HEX64(0x7A70FFFFFFFFULL, protocore_rdm_uid(0x7A70u, 0xFFFFFFFFu)); // ALL_DEVICES_ID for 0x7A70

    RdmPacket p;
    memset(&p, 0, sizeof(p));
    p.dest_uid = protocore_rdm_uid(0x1234u, 0x56789ABCu);
    p.src_uid = protocore_rdm_uid(0x7A70u, 0x000000AAu);
    p.cc = RDM_CC_GET;
    p.pid = RDM_PID_DEVICE_INFO;
    uint8_t buf[32];
    TEST_ASSERT_EQUAL_size_t(RDM_OVERHEAD, protocore_rdm_build(buf, sizeof(buf), &p, NULL, 0u));
    static const uint8_t DEST[6] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    static const uint8_t SRC[6] = {0x7A, 0x70, 0x00, 0x00, 0x00, 0xAA};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DEST, buf + 3, 6u);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SRC, buf + 9, 6u);
}

// E1.20 Table 7-1: preamble of 0xFE, a 0xAA separator, then each UID octet MSB first as a pair
// (byte | 0xAA, byte | 0x55), then the 16-bit additive sum of those 12 encoded octets sent the
// same way. For UID 0x123456789ABC that is, worked out octet by octet:
//   0x12 -> BA 57   0x34 -> BE 75   0x56 -> FE 57
//   0x78 -> FA 7D   0x9A -> BA DF   0xBC -> BE FD
//   sum = BA+57+BE+75+FE+57+FA+7D+BA+DF+BE+FD = 0x0864
//   0x08 -> AA 5D   0x64 -> EE 75
void test_e120_table_7_1_discovery_response_encoding(void)
{
    static const uint8_t EUID_ECS[16] = {0xBA, 0x57, 0xBE, 0x75, 0xFE, 0x57, 0xFA, 0x7D,
                                         0xBA, 0xDF, 0xBE, 0xFD, 0xAA, 0x5D, 0xEE, 0x75};
    const uint64_t uid = 0x123456789ABCULL;

    uint8_t buf[32];
    // Table 7-1 shows the full seven preamble slots; 7.5 allows 0 to 7.
    size_t n = protocore_rdm_build_disc_response(buf, sizeof(buf), uid, 7u);
    TEST_ASSERT_EQUAL_size_t(24u, n); // 7 preamble + separator + 12 EUID + 4 ECS
    for (size_t i = 0; i < 7u; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0xFEu, buf[i]);
    }
    TEST_ASSERT_EQUAL_HEX8(0xAAu, buf[7]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(EUID_ECS, buf + 8, 16u);

    // The checksum slots carry the sum of the twelve encoded UID octets, 0x0864.
    uint16_t sum = protocore_rdm_checksum(EUID_ECS, 12u);
    TEST_ASSERT_EQUAL_HEX16(0x0864u, sum);

    // Every legal preamble length shortens only the preamble; the encoded body never moves.
    for (uint8_t pre = 0u; pre <= 7u; pre++)
    {
        size_t m = protocore_rdm_build_disc_response(buf, sizeof(buf), uid, pre);
        TEST_ASSERT_EQUAL_size_t((size_t)pre + 17u, m);
        TEST_ASSERT_EQUAL_HEX8(0xAAu, buf[pre]);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(EUID_ECS, buf + pre + 1u, 16u);
    }
}

// E1.20 Table 7-2: the decoded octets are recovered by bit-wise AND of each encoded pair, and
// "A valid response shall require the recovered checksum to match that of the EUID."
void test_e120_table_7_2_discovery_response_decoding(void)
{
    // The Table 7-1 response for 0x123456789ABC, written out rather than rebuilt.
    static const uint8_t RESP[24] = {0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xAA,
                                     0xBA, 0x57, 0xBE, 0x75, 0xFE, 0x57, 0xFA, 0x7D,
                                     0xBA, 0xDF, 0xBE, 0xFD, 0xAA, 0x5D, 0xEE, 0x75};
    uint64_t uid = 0u;
    TEST_ASSERT_TRUE(protocore_rdm_decode_disc_response(RESP, sizeof(RESP), &uid));
    TEST_ASSERT_EQUAL_HEX64(0x123456789ABCULL, uid);

    // 7.5: "The controller shall be able to process response packets with 0-7 bytes of preamble."
    for (size_t drop = 0u; drop <= 7u; drop++)
    {
        uid = 0u;
        TEST_ASSERT_TRUE(protocore_rdm_decode_disc_response(RESP + drop, sizeof(RESP) - drop, &uid));
        TEST_ASSERT_EQUAL_HEX64(0x123456789ABCULL, uid);
    }

    // A single flipped encoded octet breaks the sum, so the response is discarded.
    for (size_t i = 8u; i < 20u; i++)
    {
        uint8_t bad[24];
        memcpy(bad, RESP, sizeof(RESP));
        bad[i] ^= 0x10u;
        TEST_ASSERT_FALSE_MESSAGE(protocore_rdm_decode_disc_response(bad, sizeof(bad), &uid), "corrupt EUID accepted");
    }

    // Without the 0xAA separator there is no response, and 16 encoded octets must be present.
    static const uint8_t NOSEP[8] = {0xFE, 0xFE, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_FALSE(protocore_rdm_decode_disc_response(NOSEP, sizeof(NOSEP), &uid));
    TEST_ASSERT_FALSE(protocore_rdm_decode_disc_response(RESP, 23u, &uid));
    TEST_ASSERT_FALSE(protocore_rdm_decode_disc_response(NULL, sizeof(RESP), &uid));
    TEST_ASSERT_FALSE(protocore_rdm_decode_disc_response(RESP, sizeof(RESP), NULL));
}

// Builder and decoder are inverses over the whole UID space sampled across every octet position.
void test_discovery_response_round_trips(void)
{
    static const uint64_t UID[] = {
        0x000000000000ULL, 0xFFFFFFFFFFFFULL, 0x123456789ABCULL, 0x7A70000000AAULL, 0x010204081020ULL,
    };
    for (size_t i = 0; i < sizeof(UID) / sizeof(UID[0]); i++)
    {
        uint8_t buf[32];
        uint64_t back = 0u;
        size_t n = protocore_rdm_build_disc_response(buf, sizeof(buf), UID[i], (uint8_t)(i % 8u));
        TEST_ASSERT_TRUE(n > 0u);
        TEST_ASSERT_TRUE(protocore_rdm_decode_disc_response(buf, n, &back));
        TEST_ASSERT_EQUAL_HEX64(UID[i], back);
    }
}

// 7.5 allows 0 to 7 preamble slots, so 8 has no encoding, and a buffer that cannot hold the whole
// response gets nothing.
void test_discovery_response_builder_guards(void)
{
    uint8_t buf[32];
    TEST_ASSERT_EQUAL_size_t(0u, protocore_rdm_build_disc_response(buf, sizeof(buf), 1ULL, 8u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_rdm_build_disc_response(buf, sizeof(buf), 1ULL, 255u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_rdm_build_disc_response(NULL, sizeof(buf), 1ULL, 7u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_rdm_build_disc_response(buf, 23u, 1ULL, 7u)); // needs 24
    TEST_ASSERT_EQUAL_size_t(24u, protocore_rdm_build_disc_response(buf, 24u, 1ULL, 7u));
}

// E1.20 sec 10.5.1: the DEVICE_INFO GET response carries PDL 0x13 = 19 octets, in the order
// RDM Protocol Version (16), Device Model ID (16), Product Category (16), Software Version ID (32),
// DMX512 Footprint (16), DMX512 Personality (16), DMX512 Start Address (16), Sub-Device Count (16),
// Sensor Count (8) - all big-endian per 6.1. "The version of this standard is 1.0", so an E1.20
// responder reports protocol version major 1, minor 0.
void test_e120_device_info_block(void)
{
    TEST_ASSERT_EQUAL_INT(0x13, PROTOCORE_RDM_DEVICE_INFO_PDL);

    RdmDeviceInfo in;
    in.proto_major = 1u;
    in.proto_minor = 0u;
    in.device_model_id = 0x1234u;
    in.product_category = 0x0100u;
    in.software_version_id = 0x0A0B0C0Du;
    in.dmx_footprint = 3u;
    in.current_personality = 1u;
    in.personality_count = 4u;
    in.dmx_start_address = 100u;
    in.sub_device_count = 0u;
    in.sensor_count = 2u;

    static const uint8_t WANT[19] = {0x01, 0x00,                   // protocol version 1.0
                                     0x12, 0x34,                   // device model id
                                     0x01, 0x00,                   // product category
                                     0x0A, 0x0B, 0x0C, 0x0D,       // software version id
                                     0x00, 0x03,                   // DMX512 footprint
                                     0x01, 0x04,                   // personality: current, count
                                     0x00, 0x64,                   // DMX512 start address 100
                                     0x00, 0x00,                   // sub-device count
                                     0x02};                        // sensor count
    uint8_t pd[24];
    TEST_ASSERT_EQUAL_size_t(19u, protocore_rdm_build_device_info(pd, sizeof(pd), &in));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, pd, 19u);

    RdmDeviceInfo out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_TRUE(protocore_rdm_parse_device_info(WANT, 19u, &out));
    TEST_ASSERT_EQUAL_UINT8(1u, out.proto_major);
    TEST_ASSERT_EQUAL_UINT8(0u, out.proto_minor);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, out.device_model_id);
    TEST_ASSERT_EQUAL_HEX16(0x0100u, out.product_category);
    TEST_ASSERT_EQUAL_HEX32(0x0A0B0C0Du, out.software_version_id);
    TEST_ASSERT_EQUAL_UINT16(3u, out.dmx_footprint);
    TEST_ASSERT_EQUAL_UINT8(1u, out.current_personality);
    TEST_ASSERT_EQUAL_UINT8(4u, out.personality_count);
    TEST_ASSERT_EQUAL_UINT16(100u, out.dmx_start_address);
    TEST_ASSERT_EQUAL_UINT16(0u, out.sub_device_count);
    TEST_ASSERT_EQUAL_UINT8(2u, out.sensor_count);

    // 10.6.3: 0xFFFF is the start address of a device that uses no DMX512 slots.
    in.dmx_start_address = 0xFFFFu;
    TEST_ASSERT_EQUAL_size_t(19u, protocore_rdm_build_device_info(pd, sizeof(pd), &in));
    TEST_ASSERT_EQUAL_HEX8(0xFFu, pd[14]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, pd[15]);

    // A block shorter than the standard's PDL is not a DEVICE_INFO.
    TEST_ASSERT_EQUAL_size_t(0u, protocore_rdm_build_device_info(pd, 18u, &in));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_rdm_build_device_info(NULL, sizeof(pd), &in));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_rdm_build_device_info(pd, sizeof(pd), NULL));
    TEST_ASSERT_FALSE(protocore_rdm_parse_device_info(WANT, 18u, &out));
    TEST_ASSERT_FALSE(protocore_rdm_parse_device_info(NULL, 19u, &out));
    TEST_ASSERT_FALSE(protocore_rdm_parse_device_info(WANT, 19u, NULL));
}

// A DEVICE_INFO block carried as the parameter data of a real GET_COMMAND_RESPONSE comes back out
// of the packet unchanged.
void test_device_info_rides_a_get_response_packet(void)
{
    RdmDeviceInfo in;
    memset(&in, 0, sizeof(in));
    in.proto_major = 1u;
    in.device_model_id = 0xBEEFu;
    in.dmx_footprint = 512u;
    in.dmx_start_address = 1u;
    in.current_personality = 1u;
    in.personality_count = 1u;

    uint8_t pd[19];
    TEST_ASSERT_EQUAL_size_t(19u, protocore_rdm_build_device_info(pd, sizeof(pd), &in));

    RdmPacket p;
    memset(&p, 0, sizeof(p));
    p.dest_uid = protocore_rdm_uid(0x7A70u, 0x000000AAu);
    p.src_uid = protocore_rdm_uid(0x4444u, 0x00000001u);
    p.port_id = RDM_RESPONSE_ACK;
    p.cc = RDM_CC_GET_RESPONSE;
    p.pid = RDM_PID_DEVICE_INFO;

    uint8_t buf[64];
    size_t n = protocore_rdm_build(buf, sizeof(buf), &p, pd, 19u);
    TEST_ASSERT_EQUAL_size_t((size_t)RDM_OVERHEAD + 19u, n);
    TEST_ASSERT_EQUAL_UINT8(43u, buf[2]); // message length 24 + 19

    RdmPacket g;
    TEST_ASSERT_TRUE(protocore_rdm_parse(buf, n, &g, NULL));
    TEST_ASSERT_EQUAL_HEX8(RDM_CC_GET_RESPONSE, g.cc);
    TEST_ASSERT_EQUAL_HEX8(RDM_RESPONSE_ACK, g.port_id);
    RdmDeviceInfo out;
    TEST_ASSERT_TRUE(protocore_rdm_parse_device_info(g.pdata, g.pdl, &out));
    TEST_ASSERT_EQUAL_HEX16(0xBEEFu, out.device_model_id);
    TEST_ASSERT_EQUAL_UINT16(512u, out.dmx_footprint);
}

// 6.2.11: "If the checksum field in the packet does not match the calculated checksum, then the
// packet shall be discarded", and 6.2.1/6.2.2 require the two start codes.
void test_e120_parse_discards_malformed_packets(void)
{
    RdmPacket p;
    memset(&p, 0, sizeof(p));
    p.cc = RDM_CC_GET;
    p.pid = RDM_PID_IDENTIFY_DEVICE;
    uint8_t good[64];
    size_t n = protocore_rdm_build(good, sizeof(good), &p, NULL, 0u);

    RdmPacket g;
    size_t c;
    uint8_t bad[64];

    memcpy(bad, good, n);
    bad[n - 1u] ^= 0xFFu; // checksum low corrupted
    TEST_ASSERT_FALSE(protocore_rdm_parse(bad, n, &g, &c));

    memcpy(bad, good, n);
    bad[n - 2u] ^= 0xFFu; // checksum high corrupted
    TEST_ASSERT_FALSE(protocore_rdm_parse(bad, n, &g, &c));

    memcpy(bad, good, n);
    bad[0] = 0x00u; // not SC_RDM
    TEST_ASSERT_FALSE(protocore_rdm_parse(bad, n, &g, &c));

    memcpy(bad, good, n);
    bad[1] = 0xAAu; // not SC_SUB_MESSAGE
    TEST_ASSERT_FALSE(protocore_rdm_parse(bad, n, &g, &c));

    memcpy(bad, good, n);
    bad[2] = 23u; // 6.2.3: the message length range starts at 24
    TEST_ASSERT_FALSE(protocore_rdm_parse(bad, n, &g, &c));

    memcpy(bad, good, n);
    bad[23] = 5u; // PDL claims 5 octets the message length does not account for
    TEST_ASSERT_FALSE(protocore_rdm_parse(bad, n, &g, &c));

    memcpy(bad, good, n);
    bad[2] = 40u; // a length that agrees with its PDL but runs past the buffer
    bad[23] = 16u;
    TEST_ASSERT_FALSE(protocore_rdm_parse(bad, n, &g, &c));

    TEST_ASSERT_FALSE(protocore_rdm_parse(good, RDM_OVERHEAD - 1u, &g, &c));
    TEST_ASSERT_FALSE(protocore_rdm_parse(NULL, n, &g, &c));
    TEST_ASSERT_FALSE(protocore_rdm_parse(good, n, NULL, &c));
    TEST_ASSERT_TRUE(protocore_rdm_parse(good, n, &g, NULL)); // consumed is optional
}

void test_rdm_build_guards(void)
{
    RdmPacket p;
    memset(&p, 0, sizeof(p));
    p.cc = RDM_CC_GET;
    p.pid = RDM_PID_DEVICE_INFO;
    uint8_t buf[64];
    static const uint8_t PD[2] = {1, 2};

    TEST_ASSERT_EQUAL_size_t(0u, protocore_rdm_build(NULL, sizeof(buf), &p, NULL, 0u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_rdm_build(buf, sizeof(buf), NULL, NULL, 0u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_rdm_build(buf, sizeof(buf), &p, NULL, 2u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_rdm_build(buf, RDM_OVERHEAD - 1u, &p, NULL, 0u));
    TEST_ASSERT_EQUAL_size_t(RDM_OVERHEAD, protocore_rdm_build(buf, RDM_OVERHEAD, &p, NULL, 0u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_rdm_build(buf, RDM_OVERHEAD + 1u, &p, PD, 2u));
}

// ANSI E1.11 (DMX512-A): the packet body is a start code followed by up to 512 slots, and the NULL
// start code 0x00 is dimmer data. Slot numbering is 1-based, so channel n is at offset n.
void test_e111_slot_array(void)
{
    static const uint8_t CH[4] = {10u, 20u, 30u, 255u};
    uint8_t buf[8];
    size_t n = protocore_dmx_build(buf, sizeof(buf), DMX_SC_DIMMER, CH, 4u);
    TEST_ASSERT_EQUAL_size_t(5u, n);
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CH, buf + 1, 4u);

    TEST_ASSERT_EQUAL_UINT8(10u, protocore_dmx_get_channel(buf, n, 1u));
    TEST_ASSERT_EQUAL_UINT8(20u, protocore_dmx_get_channel(buf, n, 2u));
    TEST_ASSERT_EQUAL_UINT8(255u, protocore_dmx_get_channel(buf, n, 4u));
    TEST_ASSERT_EQUAL_UINT8(0u, protocore_dmx_get_channel(buf, n, 0u)); // slot 0 is the start code
    TEST_ASSERT_EQUAL_UINT8(0u, protocore_dmx_get_channel(buf, n, 5u)); // past the packet

    // A start code other than 0x00 is carried through: RDM rides the same wire as SC 0xCC.
    TEST_ASSERT_EQUAL_size_t(5u, protocore_dmx_build(buf, sizeof(buf), RDM_SC, CH, 4u));
    TEST_ASSERT_EQUAL_HEX8(0xCCu, buf[0]);
}

// A full universe is 512 slots, so 512 builds and 513 does not.
void test_e111_universe_is_512_slots(void)
{
    static uint8_t src[DMX_MAX_CHANNELS];
    static uint8_t buf[DMX_MAX_CHANNELS + 1u];
    for (uint16_t i = 0; i < DMX_MAX_CHANNELS; i++)
    {
        src[i] = (uint8_t)(i & 0xFFu);
    }
    size_t n = protocore_dmx_build(buf, sizeof(buf), DMX_SC_DIMMER, src, DMX_MAX_CHANNELS);
    TEST_ASSERT_EQUAL_size_t((size_t)DMX_MAX_CHANNELS + 1u, n);
    TEST_ASSERT_EQUAL_UINT8(src[0], protocore_dmx_get_channel(buf, n, 1u));
    TEST_ASSERT_EQUAL_UINT8(src[DMX_MAX_CHANNELS - 1u], protocore_dmx_get_channel(buf, n, DMX_MAX_CHANNELS));
    TEST_ASSERT_EQUAL_UINT8(0u, protocore_dmx_get_channel(buf, n, DMX_MAX_CHANNELS + 1u));

    TEST_ASSERT_EQUAL_size_t(0u, protocore_dmx_build(buf, sizeof(buf), DMX_SC_DIMMER, src, DMX_MAX_CHANNELS + 1u));
}

void test_dmx_build_guards(void)
{
    static const uint8_t CH[4] = {1u, 2u, 3u, 4u};
    uint8_t buf[8];
    TEST_ASSERT_EQUAL_size_t(0u, protocore_dmx_build(NULL, sizeof(buf), DMX_SC_DIMMER, CH, 4u));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_dmx_build(buf, 4u, DMX_SC_DIMMER, CH, 4u)); // needs 5
    TEST_ASSERT_EQUAL_size_t(0u, protocore_dmx_build(buf, sizeof(buf), DMX_SC_DIMMER, NULL, 4u));

    // No slots at all is a legal packet: the start code alone.
    TEST_ASSERT_EQUAL_size_t(1u, protocore_dmx_build(buf, sizeof(buf), DMX_SC_DIMMER, NULL, 0u));
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[0]);

    TEST_ASSERT_EQUAL_UINT8(0u, protocore_dmx_get_channel(NULL, 8u, 1u));
    TEST_ASSERT_EQUAL_UINT8(0u, protocore_dmx_get_channel(buf, 8u, DMX_MAX_CHANNELS + 1u));
}
