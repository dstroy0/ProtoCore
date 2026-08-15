// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the PROFINET DCP frame codec (services/fieldbus/profinet/profinet.h).
//
// The load-bearing case is test_dcp_header_layout: a DCP PDU is a FrameID followed by exactly ten
// header octets - ServiceID, ServiceType, Xid (4), ResponseDelayFactor (2), DCPDataLength (2), all
// big-endian - and every field's offset is fixed by that. The layout and the constants are those of
// the Wireshark PROFINET dissector (plugins/profinet/packet-pn-dcp.c) and the rt-labs p-net
// pf_dcp_header_t struct, both implementations of IEC 61158-6-10. Writing DCPDataLength one field
// early makes a device read the block data as its own length, which is what this case pins down.

#include "services/fieldbus/profinet/profinet.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// packet-pn-dcp.c PNDCP_SERVICE_ID_*, PNDCP_SERVICE_TYPE_*, PNDCP_OPTION_*, PNDCP_SUBOPTION_*.
void test_dcp_constants(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xFEFC, PN_FRAMEID_DCP_HELLO);
    TEST_ASSERT_EQUAL_HEX16(0xFEFD, PN_FRAMEID_DCP_GETSET);
    TEST_ASSERT_EQUAL_HEX16(0xFEFE, PN_FRAMEID_DCP_IDENT_REQ);
    TEST_ASSERT_EQUAL_HEX16(0xFEFF, PN_FRAMEID_DCP_IDENT_RES);

    TEST_ASSERT_EQUAL_HEX8(0x03, PN_DCP_SERVICE_GET);
    TEST_ASSERT_EQUAL_HEX8(0x04, PN_DCP_SERVICE_SET);
    TEST_ASSERT_EQUAL_HEX8(0x05, PN_DCP_SERVICE_IDENTIFY);
    TEST_ASSERT_EQUAL_HEX8(0x00, PN_DCP_TYPE_REQUEST);
    TEST_ASSERT_EQUAL_HEX8(0x01, PN_DCP_TYPE_RESPONSE_SUCCESS);

    TEST_ASSERT_EQUAL_HEX8(0x01, PN_DCP_OPT_IP);
    TEST_ASSERT_EQUAL_HEX8(0x02, PN_DCP_SUB_IP_PARAM);
    TEST_ASSERT_EQUAL_HEX8(0x02, PN_DCP_OPT_DEVICE);
    TEST_ASSERT_EQUAL_HEX8(0x02, PN_DCP_SUB_DEV_NAME_OF_STATION);
    TEST_ASSERT_EQUAL_HEX8(0x03, PN_DCP_SUB_DEV_ID);
    TEST_ASSERT_EQUAL_HEX8(0xFF, PN_DCP_OPT_ALL);
    TEST_ASSERT_EQUAL_HEX8(0xFF, PN_DCP_SUB_ALL);
    TEST_ASSERT_EQUAL_INT(12, PN_DCP_HDR_LEN); // FrameID(2) + the 10-octet DCP header
}

// The header is FrameID(2) ServiceID(1) ServiceType(1) Xid(4) ResponseDelayFactor(2)
// DCPDataLength(2), every multi-octet field big-endian.
void test_dcp_header_layout(void)
{
    uint8_t out[16];
    size_t n = protocore_pn_dcp_header(PN_FRAMEID_DCP_IDENT_REQ, PN_DCP_SERVICE_IDENTIFY, PN_DCP_TYPE_REQUEST,
                                       0x11223344u, 0x0001, 0x0008, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(PN_DCP_HDR_LEN, n);
    static const uint8_t WANT[12] = {
        0xFE, 0xFE,             // FrameID, DCP Identify request
        0x05,                   // ServiceID Identify
        0x00,                   // ServiceType Request
        0x11, 0x22, 0x33, 0x44, // Xid
        0x00, 0x01,             // ResponseDelayFactor
        0x00, 0x08,             // DCPDataLength
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, PN_DCP_HDR_LEN);

    PnDcpHeader h;
    TEST_ASSERT_TRUE(protocore_pn_dcp_parse_header(out, n, &h));
    TEST_ASSERT_EQUAL_HEX16(PN_FRAMEID_DCP_IDENT_REQ, h.frame_id);
    TEST_ASSERT_EQUAL_HEX8(PN_DCP_SERVICE_IDENTIFY, h.service_id);
    TEST_ASSERT_EQUAL_HEX8(PN_DCP_TYPE_REQUEST, h.service_type);
    TEST_ASSERT_EQUAL_HEX32(0x11223344u, h.xid);
    TEST_ASSERT_EQUAL_UINT16(0x0001, h.response_delay);
    TEST_ASSERT_EQUAL_UINT16(0x0008, h.data_length);
}

// Every header field is carried at its full width, so the extremes of each survive the round trip.
void test_dcp_header_field_widths(void)
{
    uint8_t out[16];
    PnDcpHeader h;

    TEST_ASSERT_EQUAL_UINT(PN_DCP_HDR_LEN,
                           protocore_pn_dcp_header(0xFFFF, 0xFF, 0xFF, 0xFFFFFFFFu, 0xFFFF, 0xFFFF, out, sizeof(out)));
    TEST_ASSERT_TRUE(protocore_pn_dcp_parse_header(out, PN_DCP_HDR_LEN, &h));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, h.frame_id);
    TEST_ASSERT_EQUAL_HEX8(0xFF, h.service_id);
    TEST_ASSERT_EQUAL_HEX8(0xFF, h.service_type);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, h.xid);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, h.response_delay);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, h.data_length);

    TEST_ASSERT_EQUAL_UINT(PN_DCP_HDR_LEN, protocore_pn_dcp_header(0, 0, 0, 0, 0, 0, out, sizeof(out)));
    TEST_ASSERT_TRUE(protocore_pn_dcp_parse_header(out, PN_DCP_HDR_LEN, &h));
    TEST_ASSERT_EQUAL_HEX32(0u, h.xid);
    TEST_ASSERT_EQUAL_UINT16(0, h.data_length);
}

// A DCP block is option(1) suboption(1) blockLength(2) then the value, and "if we have an odd
// number of bytes in this block, add a padding byte". The pad is not counted in blockLength, so an
// odd-length value makes a block one octet longer than the length field says.
void test_dcp_block_layout_and_padding(void)
{
    uint8_t out[64];

    // "et200sp" is seven octets, so the block declares 7 and occupies 4 + 7 + 1 = 12
    const char *name = "et200sp";
    size_t n = protocore_pn_dcp_block(PN_DCP_OPT_DEVICE, PN_DCP_SUB_DEV_NAME_OF_STATION, (const uint8_t *)name, 7, out,
                                      sizeof(out));
    TEST_ASSERT_EQUAL_UINT(12u, n);
    static const uint8_t WANT[12] = {0x02, 0x02, 0x00, 0x07, 'e', 't', '2', '0', '0', 's', 'p', 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, 12);

    // an even-length value needs no pad: an IPParameter block is 12 octets of IP + mask + gateway
    static const uint8_t IPPARAM[12] = {192, 168, 1, 85, 255, 255, 255, 0, 192, 168, 1, 1};
    n = protocore_pn_dcp_block(PN_DCP_OPT_IP, PN_DCP_SUB_IP_PARAM, IPPARAM, sizeof(IPPARAM), out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(16u, n);
    TEST_ASSERT_EQUAL_HEX8(PN_DCP_OPT_IP, out[0]);
    TEST_ASSERT_EQUAL_HEX8(PN_DCP_SUB_IP_PARAM, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x0C, out[3]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(IPPARAM, out + 4, sizeof(IPPARAM));

    // an empty value block (the AllSelector an Identify-All request carries) is the 4 header octets
    n = protocore_pn_dcp_block(PN_DCP_OPT_ALL, PN_DCP_SUB_ALL, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(4u, n);
    static const uint8_t ALL[4] = {0xFF, 0xFF, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ALL, out, 4);
}

// One block after another, with the walker landing on each in turn: the odd-length block's pad has
// to be stepped over or every later block is misread.
static struct
{
    int n;
    uint8_t option[4];
    uint8_t suboption[4];
    size_t len[4];
    uint8_t first[4];
} g_walk;

static void collect(uint8_t option, uint8_t suboption, const uint8_t *value, size_t value_len, void *arg)
{
    (void)arg;
    if (g_walk.n < 4)
    {
        g_walk.option[g_walk.n] = option;
        g_walk.suboption[g_walk.n] = suboption;
        g_walk.len[g_walk.n] = value_len;
        g_walk.first[g_walk.n] = value_len ? value[0] : 0;
        g_walk.n++;
    }
}

void test_dcp_walk_steps_over_the_pad(void)
{
    uint8_t blocks[64];
    size_t off = 0;
    const char *name = "plc1"; // even
    off += protocore_pn_dcp_block(PN_DCP_OPT_DEVICE, PN_DCP_SUB_DEV_NAME_OF_STATION, (const uint8_t *)name, 4,
                                  blocks + off, sizeof(blocks) - off);
    static const uint8_t DEVID[5] = {0x01, 0x2A, 0x03, 0x04, 0x05}; // odd, so this block is padded
    off += protocore_pn_dcp_block(PN_DCP_OPT_DEVICE, PN_DCP_SUB_DEV_ID, DEVID, sizeof(DEVID), blocks + off,
                                  sizeof(blocks) - off);
    static const uint8_t IPPARAM[12] = {10, 0, 0, 5, 255, 255, 255, 0, 10, 0, 0, 1};
    off += protocore_pn_dcp_block(PN_DCP_OPT_IP, PN_DCP_SUB_IP_PARAM, IPPARAM, sizeof(IPPARAM), blocks + off,
                                  sizeof(blocks) - off);
    TEST_ASSERT_EQUAL_UINT(8u + 10u + 16u, off);

    memset(&g_walk, 0, sizeof(g_walk));
    TEST_ASSERT_TRUE(protocore_pn_dcp_walk(blocks, off, collect, NULL));
    TEST_ASSERT_EQUAL_INT(3, g_walk.n);

    TEST_ASSERT_EQUAL_HEX8(PN_DCP_OPT_DEVICE, g_walk.option[0]);
    TEST_ASSERT_EQUAL_HEX8(PN_DCP_SUB_DEV_NAME_OF_STATION, g_walk.suboption[0]);
    TEST_ASSERT_EQUAL_UINT(4u, g_walk.len[0]);
    TEST_ASSERT_EQUAL_HEX8('p', g_walk.first[0]);

    TEST_ASSERT_EQUAL_HEX8(PN_DCP_SUB_DEV_ID, g_walk.suboption[1]);
    TEST_ASSERT_EQUAL_UINT(5u, g_walk.len[1]); // the pad is not part of the value
    TEST_ASSERT_EQUAL_HEX8(0x01, g_walk.first[1]);

    TEST_ASSERT_EQUAL_HEX8(PN_DCP_OPT_IP, g_walk.option[2]);
    TEST_ASSERT_EQUAL_UINT(12u, g_walk.len[2]);
    TEST_ASSERT_EQUAL_HEX8(10, g_walk.first[2]);
}

// A whole DCP Identify response as a device answers one: the header's DCPDataLength counts exactly
// the blocks that follow it, and the walk over those blocks consumes them exactly.
void test_identify_response_frame(void)
{
    uint8_t frame[128];
    const char *name = "et200sp";
    static const uint8_t IPPARAM[12] = {192, 168, 1, 85, 255, 255, 255, 0, 192, 168, 1, 1};

    uint8_t blocks[64];
    size_t blen = 0;
    blen += protocore_pn_dcp_block(PN_DCP_OPT_DEVICE, PN_DCP_SUB_DEV_NAME_OF_STATION, (const uint8_t *)name, 7,
                                   blocks + blen, sizeof(blocks) - blen);
    blen += protocore_pn_dcp_block(PN_DCP_OPT_IP, PN_DCP_SUB_IP_PARAM, IPPARAM, sizeof(IPPARAM), blocks + blen,
                                   sizeof(blocks) - blen);

    size_t hlen =
        protocore_pn_dcp_header(PN_FRAMEID_DCP_IDENT_RES, PN_DCP_SERVICE_IDENTIFY, PN_DCP_TYPE_RESPONSE_SUCCESS,
                                0x12345678u, 0, (uint16_t)blen, frame, sizeof(frame));
    TEST_ASSERT_EQUAL_UINT(PN_DCP_HDR_LEN, hlen);
    memcpy(frame + hlen, blocks, blen);

    PnDcpHeader h;
    TEST_ASSERT_TRUE(protocore_pn_dcp_parse_header(frame, hlen + blen, &h));
    TEST_ASSERT_EQUAL_HEX16(PN_FRAMEID_DCP_IDENT_RES, h.frame_id);
    TEST_ASSERT_EQUAL_HEX8(PN_DCP_TYPE_RESPONSE_SUCCESS, h.service_type);
    TEST_ASSERT_EQUAL_HEX32(0x12345678u, h.xid);
    TEST_ASSERT_EQUAL_UINT16(0, h.response_delay); // zero on a response
    TEST_ASSERT_EQUAL_UINT16((uint16_t)blen, h.data_length);
    TEST_ASSERT_EQUAL_UINT(hlen + h.data_length, hlen + blen);

    memset(&g_walk, 0, sizeof(g_walk));
    TEST_ASSERT_TRUE(protocore_pn_dcp_walk(frame + hlen, h.data_length, collect, NULL));
    TEST_ASSERT_EQUAL_INT(2, g_walk.n);
    TEST_ASSERT_EQUAL_UINT(7u, g_walk.len[0]);
    TEST_ASSERT_EQUAL_UINT(12u, g_walk.len[1]);
}

// A block whose declared length runs past the end of the data is refused rather than read past it.
void test_dcp_walk_refuses_an_overrun(void)
{
    static const uint8_t OVERRUN[8] = {0x02, 0x02, 0x00, 0x10, 'a', 'b', 'c', 'd'}; // claims 16, carries 4
    TEST_ASSERT_FALSE(protocore_pn_dcp_walk(OVERRUN, sizeof(OVERRUN), collect, NULL));

    // a trailing fragment shorter than a block header is simply the end of the blocks
    static const uint8_t TRAILING[10] = {0x02, 0x02, 0x00, 0x04, 'p', 'l', 'c', '1', 0x02, 0x03};
    memset(&g_walk, 0, sizeof(g_walk));
    TEST_ASSERT_TRUE(protocore_pn_dcp_walk(TRAILING, sizeof(TRAILING), collect, NULL));
    TEST_ASSERT_EQUAL_INT(1, g_walk.n);

    // a null callback still validates the block chain
    TEST_ASSERT_TRUE(protocore_pn_dcp_walk(TRAILING, 8, NULL, NULL));
    TEST_ASSERT_FALSE(protocore_pn_dcp_walk(OVERRUN, sizeof(OVERRUN), NULL, NULL));
}

// Anything shorter than the header cannot be a DCP PDU, and a builder given less room writes
// nothing.
void test_bounds_refusals(void)
{
    uint8_t out[16];
    PnDcpHeader h;
    static const uint8_t VALUE[4] = {1, 2, 3, 4};

    for (size_t cap = 0; cap < PN_DCP_HDR_LEN; cap++)
    {
        TEST_ASSERT_EQUAL_UINT(0u, protocore_pn_dcp_header(0xFEFE, 5, 0, 0, 0, 0, out, cap));
    }
    TEST_ASSERT_EQUAL_UINT(0u, protocore_pn_dcp_header(0xFEFE, 5, 0, 0, 0, 0, NULL, sizeof(out)));

    for (size_t cap = 0; cap < 8; cap++)
    {
        TEST_ASSERT_EQUAL_UINT(0u, protocore_pn_dcp_block(2, 2, VALUE, sizeof(VALUE), out, cap));
    }
    TEST_ASSERT_EQUAL_UINT(8u, protocore_pn_dcp_block(2, 2, VALUE, sizeof(VALUE), out, 8));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_pn_dcp_block(2, 2, NULL, 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_pn_dcp_block(2, 2, VALUE, sizeof(VALUE), NULL, sizeof(out)));

    uint8_t frame[PN_DCP_HDR_LEN];
    memset(frame, 0, sizeof(frame));
    for (size_t n = 0; n < PN_DCP_HDR_LEN; n++)
    {
        TEST_ASSERT_FALSE(protocore_pn_dcp_parse_header(frame, n, &h));
    }
    TEST_ASSERT_TRUE(protocore_pn_dcp_parse_header(frame, PN_DCP_HDR_LEN, &h));
    TEST_ASSERT_FALSE(protocore_pn_dcp_parse_header(NULL, PN_DCP_HDR_LEN, &h));
    TEST_ASSERT_FALSE(protocore_pn_dcp_parse_header(frame, PN_DCP_HDR_LEN, NULL));
}
