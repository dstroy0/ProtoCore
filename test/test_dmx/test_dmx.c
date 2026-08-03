// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the DMX512 + RDM codec (services/peripherals/dmx): the DMX512 slot packet, and the RDM
// (ANSI E1.20) packet build/parse with 48-bit UIDs and the 16-bit additive checksum. Pure
// host tests.

#include "services/peripherals/dmx/dmx.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_dmx_build_and_get()
{
    uint8_t ch[4] = {10, 20, 30, 255};
    uint8_t buf[8];
    size_t n = pc_dmx_build(buf, sizeof(buf), DMX_SC_DIMMER, ch, 4);
    TEST_ASSERT_EQUAL_size_t(5, n); // start code + 4 channels
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(10, buf[1]);

    TEST_ASSERT_EQUAL_UINT8(10, pc_dmx_get_channel(buf, n, 1)); // channel 1 = slot at buf[1]
    TEST_ASSERT_EQUAL_UINT8(255, pc_dmx_get_channel(buf, n, 4));
    TEST_ASSERT_EQUAL_UINT8(0, pc_dmx_get_channel(buf, n, 5)); // out of range
    TEST_ASSERT_EQUAL_UINT8(0, pc_dmx_get_channel(buf, n, 0)); // channels are 1-based

    // 512 channels is the max; 513 is rejected.
    static uint8_t big[513];
    TEST_ASSERT_EQUAL_size_t(0, pc_dmx_build(big, sizeof(big), DMX_SC_DIMMER, big, 513));
}

void test_rdm_uid()
{
    uint64_t uid = pc_rdm_uid(0x4444, 0x12345678);
    TEST_ASSERT_EQUAL_HEX64(0x444412345678ULL, uid);
}

// Encode a UID into a DISC_UNIQUE_BRANCH response (preamble + separator + euid + ecs), then decode it.
static size_t encode_disc_response(uint64_t uid, uint8_t preamble, uint8_t *out)
{
    size_t p = 0;
    for (uint8_t i = 0; i < preamble; i++)
    {
        out[p++] = 0xFE;
    }
    out[p++] = 0xAA; // preamble separator
    size_t euid_start = p;
    for (int i = 0; i < 6; i++)
    {
        uint8_t b = (uint8_t)(uid >> (8 * (5 - i))); // MSB first
        out[p++] = (uint8_t)(b | 0xAA);
        out[p++] = (uint8_t)(b | 0x55);
    }
    uint16_t sum = 0;
    for (int i = 0; i < 12; i++)
    {
        sum = (uint16_t)(sum + out[euid_start + i]);
    }
    out[p++] = (uint8_t)((sum >> 8) | 0xAA);
    out[p++] = (uint8_t)((sum >> 8) | 0x55);
    out[p++] = (uint8_t)((sum & 0xFF) | 0xAA);
    out[p++] = (uint8_t)((sum & 0xFF) | 0x55);
    return p;
}

void test_rdm_decode_disc_response()
{
    uint64_t want = pc_rdm_uid(0x1234, 0x56789ABC);
    uint8_t resp[32];
    size_t n = encode_disc_response(want, 7, resp); // 7-octet preamble (the common case)

    uint64_t got = 0;
    TEST_ASSERT_TRUE(pc_rdm_decode_disc_response(resp, n, &got));
    TEST_ASSERT_EQUAL_HEX64(want, got);

    // Zero preamble (separator only) also decodes.
    n = encode_disc_response(want, 0, resp);
    got = 0;
    TEST_ASSERT_TRUE(pc_rdm_decode_disc_response(resp, n, &got));
    TEST_ASSERT_EQUAL_HEX64(want, got);

    // A corrupted encoded octet fails the checksum (flip a 0xAA-region bit so the sum changes).
    uint8_t bad[32];
    size_t bn = encode_disc_response(want, 2, bad);
    bad[3] ^= 0x10; // euid[0] follows a 2-octet preamble + separator (index 3)
    TEST_ASSERT_FALSE(pc_rdm_decode_disc_response(bad, bn, &got));

    // A missing separator, a truncated response, and null args are rejected.
    uint8_t nosep[8] = {0xFE, 0xFE, 0xFE, 0x00};
    TEST_ASSERT_FALSE(pc_rdm_decode_disc_response(nosep, sizeof(nosep), &got));
    TEST_ASSERT_FALSE(pc_rdm_decode_disc_response(resp, 10, &got)); // fewer than 16 encoded octets
    TEST_ASSERT_FALSE(pc_rdm_decode_disc_response(NULL, n, &got));
    TEST_ASSERT_FALSE(pc_rdm_decode_disc_response(resp, n, NULL));
}

void test_rdm_build_disc_response()
{
    uint64_t uid = pc_rdm_uid(0x1234, 0x56789ABC);
    uint8_t got[32];
    uint8_t ref[32];

    // The builder must match the test's independent reference encoder byte-for-byte, at every preamble length.
    for (uint8_t pre = 0; pre <= 7; pre++)
    {
        size_t rn = encode_disc_response(uid, pre, ref);
        size_t gn = pc_rdm_build_disc_response(got, sizeof(got), uid, pre);
        TEST_ASSERT_EQUAL_size_t(rn, gn);
        TEST_ASSERT_EQUAL_size_t((size_t)pre + 17, gn);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(ref, got, gn);
    }

    // And it round-trips through the decoder.
    size_t n = pc_rdm_build_disc_response(got, sizeof(got), uid, 7);
    uint64_t back = 0;
    TEST_ASSERT_TRUE(pc_rdm_decode_disc_response(got, n, &back));
    TEST_ASSERT_EQUAL_HEX64(uid, back);

    // Guards: preamble > 7, a null buffer, and a too-small buffer fail closed.
    TEST_ASSERT_EQUAL_size_t(0, pc_rdm_build_disc_response(got, sizeof(got), uid, 8));
    TEST_ASSERT_EQUAL_size_t(0, pc_rdm_build_disc_response(NULL, sizeof(got), uid, 7));
    TEST_ASSERT_EQUAL_size_t(0, pc_rdm_build_disc_response(got, 16, uid, 7)); // a 7-preamble response needs 24
}

// Build a GET DEVICE_INFO (no parameter data), parse it back, verify the checksum holds.
void test_rdm_get_roundtrip()
{
    RdmPacket p;
    memset(&p, 0, sizeof(p));
    p.dest_uid = pc_rdm_uid(0x4444, 0x00000001);
    p.src_uid = pc_rdm_uid(0x7A70, 0x000000AA);
    p.tn = 5;
    p.port_id = 1;
    p.msg_count = 0;
    p.sub_device = 0;
    p.cc = RDM_CC_GET;
    p.pid = RDM_PID_DEVICE_INFO;
    p.pdl = 0;

    uint8_t buf[64];
    size_t n = pc_rdm_build(buf, sizeof(buf), &p, NULL, 0);
    TEST_ASSERT_EQUAL_size_t(RDM_OVERHEAD, n); // 24-octet message + 2 checksum, pdl 0
    TEST_ASSERT_EQUAL_HEX8(0xCC, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(24, buf[2]);

    RdmPacket g;
    size_t c;
    TEST_ASSERT_TRUE(pc_rdm_parse(buf, n, &g, &c));
    TEST_ASSERT_EQUAL_HEX64(p.dest_uid, g.dest_uid);
    TEST_ASSERT_EQUAL_HEX64(p.src_uid, g.src_uid);
    TEST_ASSERT_EQUAL_UINT8(5, g.tn);
    TEST_ASSERT_EQUAL_HEX8(RDM_CC_GET, g.cc);
    TEST_ASSERT_EQUAL_HEX16(RDM_PID_DEVICE_INFO, g.pid);
    TEST_ASSERT_EQUAL_UINT8(0, g.pdl);
    TEST_ASSERT_EQUAL_size_t(n, c);
}

// A SET with parameter data round-trips, and the parameter slice matches.
void test_rdm_set_with_data()
{
    RdmPacket p;
    memset(&p, 0, sizeof(p));
    p.dest_uid = pc_rdm_uid(0x4444, 0x00000001);
    p.src_uid = pc_rdm_uid(0x7A70, 0x000000AA);
    p.tn = 9;
    p.port_id = 1;
    p.cc = RDM_CC_SET;
    p.pid = RDM_PID_DMX_START_ADDRESS;
    const uint8_t addr[2] = {0x00, 0x64}; // start address 100, big-endian

    uint8_t buf[64];
    size_t n = pc_rdm_build(buf, sizeof(buf), &p, addr, 2);
    TEST_ASSERT_EQUAL_size_t(RDM_OVERHEAD + 2, n);
    TEST_ASSERT_EQUAL_HEX8(26, buf[2]); // message length 24 + pdl 2

    RdmPacket g;
    size_t c;
    TEST_ASSERT_TRUE(pc_rdm_parse(buf, n, &g, &c));
    TEST_ASSERT_EQUAL_HEX8(RDM_CC_SET, g.cc);
    TEST_ASSERT_EQUAL_HEX16(RDM_PID_DMX_START_ADDRESS, g.pid);
    TEST_ASSERT_EQUAL_UINT8(2, g.pdl);
    TEST_ASSERT_EQUAL_MEMORY(addr, g.pdata, 2);
}

void test_rdm_device_info()
{
    RdmDeviceInfo in;
    in.proto_major = 1;
    in.proto_minor = 0;
    in.device_model_id = 0x1234;
    in.product_category = 0x0100;
    in.software_version_id = 0x0A0B0C0Du;
    in.dmx_footprint = 3;
    in.current_personality = 1;
    in.personality_count = 4;
    in.dmx_start_address = 100;
    in.sub_device_count = 0;
    in.sensor_count = 2;

    // Packs the 19-octet big-endian DEVICE_INFO block byte-exact (E1.20 Table A-15 field order).
    uint8_t pd[24];
    size_t n = pc_rdm_build_device_info(pd, sizeof(pd), &in);
    TEST_ASSERT_EQUAL_size_t(PC_RDM_DEVICE_INFO_PDL, n);
    const uint8_t expect[] = {0x01, 0x00, 0x12, 0x34, 0x01, 0x00, 0x0A, 0x0B, 0x0C, 0x0D,
                              0x00, 0x03, 0x01, 0x04, 0x00, 0x64, 0x00, 0x00, 0x02};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, pd, n);

    // Round-trips back to the same fields.
    RdmDeviceInfo out;
    TEST_ASSERT_TRUE(pc_rdm_parse_device_info(pd, (uint8_t)n, &out));
    TEST_ASSERT_EQUAL_UINT8(1, out.proto_major);
    TEST_ASSERT_EQUAL_UINT8(0, out.proto_minor);
    TEST_ASSERT_EQUAL_HEX16(0x1234, out.device_model_id);
    TEST_ASSERT_EQUAL_HEX16(0x0100, out.product_category);
    TEST_ASSERT_EQUAL_HEX32(0x0A0B0C0Du, out.software_version_id);
    TEST_ASSERT_EQUAL_UINT16(3, out.dmx_footprint);
    TEST_ASSERT_EQUAL_UINT8(1, out.current_personality);
    TEST_ASSERT_EQUAL_UINT8(4, out.personality_count);
    TEST_ASSERT_EQUAL_UINT16(100, out.dmx_start_address);
    TEST_ASSERT_EQUAL_UINT16(0, out.sub_device_count);
    TEST_ASSERT_EQUAL_UINT8(2, out.sensor_count);

    // End-to-end: the block rides a real DEVICE_INFO GET-response packet and parses back out.
    RdmPacket p;
    memset(&p, 0, sizeof(p));
    p.dest_uid = pc_rdm_uid(0x4444, 0x00000001);
    p.src_uid = pc_rdm_uid(0x7A70, 0x000000AA);
    p.cc = RDM_CC_GET_RESPONSE;
    p.pid = RDM_PID_DEVICE_INFO;
    uint8_t buf[64];
    size_t pn = pc_rdm_build(buf, sizeof(buf), &p, pd, (uint8_t)n);
    TEST_ASSERT_EQUAL_size_t(RDM_OVERHEAD + PC_RDM_DEVICE_INFO_PDL, pn);
    RdmPacket g;
    size_t c;
    TEST_ASSERT_TRUE(pc_rdm_parse(buf, pn, &g, &c));
    TEST_ASSERT_EQUAL_HEX16(RDM_PID_DEVICE_INFO, g.pid);
    RdmDeviceInfo out2;
    TEST_ASSERT_TRUE(pc_rdm_parse_device_info(g.pdata, g.pdl, &out2));
    TEST_ASSERT_EQUAL_HEX16(0x1234, out2.device_model_id);
    TEST_ASSERT_EQUAL_UINT16(100, out2.dmx_start_address);

    // Guards: short cap / pdl and nulls fail closed.
    TEST_ASSERT_EQUAL_size_t(0, pc_rdm_build_device_info(pd, 18, &in));
    TEST_ASSERT_EQUAL_size_t(0, pc_rdm_build_device_info(NULL, sizeof(pd), &in));
    TEST_ASSERT_FALSE(pc_rdm_parse_device_info(pd, 18, &out));
    TEST_ASSERT_FALSE(pc_rdm_parse_device_info(NULL, 19, &out));
}

void test_rdm_parse_rejects_bad()
{
    RdmPacket p;
    memset(&p, 0, sizeof(p));
    p.cc = RDM_CC_GET;
    p.pid = RDM_PID_IDENTIFY_DEVICE;
    uint8_t buf[64];
    size_t n = pc_rdm_build(buf, sizeof(buf), &p, NULL, 0);

    RdmPacket g;
    size_t c;
    uint8_t bad_cs[64];
    memcpy(bad_cs, buf, n);
    bad_cs[n - 1] ^= 0xFF; // corrupt the checksum
    TEST_ASSERT_FALSE(pc_rdm_parse(bad_cs, n, &g, &c));

    uint8_t bad_sc[64];
    memcpy(bad_sc, buf, n);
    bad_sc[0] = 0xAA; // not an RDM start code
    TEST_ASSERT_FALSE(pc_rdm_parse(bad_sc, n, &g, &c));
}

// Builder cap/null guards and the remaining pc_rdm_parse rejects.
void test_dmx_rdm_error_paths()
{
    uint8_t ch[4] = {1, 2, 3, 4};
    uint8_t small[3];
    TEST_ASSERT_EQUAL_size_t(0, pc_dmx_build(small, sizeof(small), 0, ch, 4)); // needs 5, cap 3

    RdmPacket p;
    memset(&p, 0, sizeof(p));
    p.cc = RDM_CC_GET;
    p.pid = RDM_PID_DEVICE_INFO;
    uint8_t buf[64];
    TEST_ASSERT_EQUAL_size_t(0, pc_rdm_build(NULL, sizeof(buf), &p, NULL, 0));  // null buf
    TEST_ASSERT_EQUAL_size_t(0, pc_rdm_build(buf, sizeof(buf), NULL, NULL, 0)); // null packet
    TEST_ASSERT_EQUAL_size_t(0, pc_rdm_build(buf, sizeof(buf), &p, NULL, 2));      // pdl but null pdata
    TEST_ASSERT_EQUAL_size_t(0, pc_rdm_build(buf, 8, &p, NULL, 0));                // cap too small

    size_t n = pc_rdm_build(buf, sizeof(buf), &p, NULL, 0);
    RdmPacket g;
    size_t c;
    TEST_ASSERT_FALSE(pc_rdm_parse(NULL, n, &g, &c)); // null buf
    TEST_ASSERT_FALSE(pc_rdm_parse(buf, 5, &g, &c));     // len < RDM_OVERHEAD

    uint8_t bad_ml[64];
    memcpy(bad_ml, buf, n);
    bad_ml[2] = 20; // message length below the fixed 24-octet header
    TEST_ASSERT_FALSE(pc_rdm_parse(bad_ml, n, &g, &c));

    uint8_t bad_pdl[64];
    memcpy(bad_pdl, buf, n);
    bad_pdl[23] = 5; // pdl 5 but ml stays 24 -> ml != 24 + pdl
    TEST_ASSERT_FALSE(pc_rdm_parse(bad_pdl, n, &g, &c));

    uint8_t trunc[64];
    memcpy(trunc, buf, n);
    trunc[2] = 40;                                     // ml 40 -> total 42
    trunc[23] = 16;                                    // pdl 16 so ml == 24 + pdl (passes the pdl check)
    TEST_ASSERT_FALSE(pc_rdm_parse(trunc, n, &g, &c)); // buffered n < 42
}

// Remaining pc_dmx_build / pc_dmx_get_channel branch combinations: null buf, n == 0
// (a valid, empty-channel build), n != 0 with a null channels pointer, a null get_channel
// buf, and a channel number above DMX_MAX_CHANNELS.
void test_dmx_build_get_channel_branches()
{
    uint8_t ch[4] = {1, 2, 3, 4};
    uint8_t buf[8];

    TEST_ASSERT_EQUAL_size_t(0, pc_dmx_build(NULL, sizeof(buf), DMX_SC_DIMMER, ch, 4)); // null buf

    // n == 0 is a valid, empty-channel build: just the start code, no memcpy.
    size_t n0 = pc_dmx_build(buf, sizeof(buf), DMX_SC_DIMMER, NULL, 0);
    TEST_ASSERT_EQUAL_size_t(1, n0);
    TEST_ASSERT_EQUAL_HEX8(DMX_SC_DIMMER, buf[0]);

    TEST_ASSERT_EQUAL_size_t(0, pc_dmx_build(buf, sizeof(buf), DMX_SC_DIMMER, NULL, 4)); // n!=0, null channels

    TEST_ASSERT_EQUAL_UINT8(0, pc_dmx_get_channel(NULL, sizeof(buf), 1));                // null buf
    TEST_ASSERT_EQUAL_UINT8(0, pc_dmx_get_channel(buf, sizeof(buf), DMX_MAX_CHANNELS + 1)); // ch > max
}

// Remaining pc_rdm_parse branch combinations: null out, a wrong sub-start code (buf[0] is
// still valid so the first half of the start-code check is false), and a null consumed
// pointer on an otherwise-successful parse.
void test_rdm_parse_null_out_and_consumed()
{
    RdmPacket p;
    memset(&p, 0, sizeof(p));
    p.cc = RDM_CC_GET;
    p.pid = RDM_PID_DEVICE_INFO;
    uint8_t buf[64];
    size_t n = pc_rdm_build(buf, sizeof(buf), &p, NULL, 0);

    size_t c;
    TEST_ASSERT_FALSE(pc_rdm_parse(buf, n, NULL, &c)); // null out

    RdmPacket g;
    TEST_ASSERT_TRUE(pc_rdm_parse(buf, n, &g, NULL)); // consumed is optional

    uint8_t bad_sub_sc[64];
    memcpy(bad_sub_sc, buf, n);
    bad_sub_sc[1] = 0xAA; // wrong sub-start code; buf[0] is still RDM_SC
    TEST_ASSERT_FALSE(pc_rdm_parse(bad_sub_sc, n, &g, &c));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_dmx_build_and_get);
    RUN_TEST(test_rdm_uid);
    RUN_TEST(test_rdm_decode_disc_response);
    RUN_TEST(test_rdm_build_disc_response);
    RUN_TEST(test_rdm_get_roundtrip);
    RUN_TEST(test_rdm_set_with_data);
    RUN_TEST(test_rdm_device_info);
    RUN_TEST(test_rdm_parse_rejects_bad);
    RUN_TEST(test_dmx_rdm_error_paths);
    RUN_TEST(test_dmx_build_get_channel_branches);
    RUN_TEST(test_rdm_parse_null_out_and_consumed);
    return UNITY_END();
}
