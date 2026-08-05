// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/profinet: the PROFINET DCP frame codec.

#include "services/fieldbus/profinet/profinet.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_header_roundtrip(void)
{
    uint8_t out[16];
    size_t n = pc_pn_dcp_header(PN_FRAMEID_DCP_IDENT_REQ, PN_DCP_SERVICE_IDENTIFY, PN_DCP_TYPE_REQUEST, 0x11223344, 8,
                                out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(10, n);
    const uint8_t expect[] = {0xFE, 0xFE, 0x05, 0x00, 0x11, 0x22, 0x33, 0x44, 0x00, 0x08};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, 10);

    PnDcpHeader h;
    TEST_ASSERT_TRUE(pc_pn_dcp_parse_header(out, n, &h));
    TEST_ASSERT_EQUAL_HEX16(PN_FRAMEID_DCP_IDENT_REQ, h.frame_id);
    TEST_ASSERT_EQUAL_HEX8(PN_DCP_SERVICE_IDENTIFY, h.service_id);
    TEST_ASSERT_EQUAL_HEX32(0x11223344, h.xid);
    TEST_ASSERT_EQUAL_UINT16(8, h.data_length);
}

void test_block_even_padding(void)
{
    // NameOfStation "plc" is 3 bytes (odd) -> padded to an even total, filler not counted in length.
    uint8_t out[16];
    size_t n =
        pc_pn_dcp_block(PN_DCP_OPT_DEVICE, PN_DCP_SUB_DEV_NAME_OF_STATION, (const uint8_t *)"plc", 3, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(4 + 3 + 1, n); // 4 header + 3 value + 1 pad
    const uint8_t expect[] = {0x02, 0x02, 0x00, 0x03, 'p', 'l', 'c', 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, n);
    // Even value -> no pad.
    n = pc_pn_dcp_block(PN_DCP_OPT_IP, PN_DCP_SUB_IP_PARAM, (const uint8_t *)"ABCD", 4, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(8, n);
}

typedef struct
{
    int count;
    uint8_t opt[4];
    uint8_t sub[4];
    size_t len[4];
} Seen;
static void collect(uint8_t option, uint8_t suboption, const uint8_t *value, size_t value_len, void *arg)
{
    (void)value;
    Seen *s = (Seen *)arg;
    if (s->count < 4)
    {
        s->opt[s->count] = option;
        s->sub[s->count] = suboption;
        s->len[s->count] = value_len;
        s->count++;
    }
}

void test_walk_blocks(void)
{
    uint8_t buf[64];
    size_t n = 0;
    n += pc_pn_dcp_block(PN_DCP_OPT_DEVICE, PN_DCP_SUB_DEV_NAME_OF_STATION, (const uint8_t *)"plc", 3, buf + n,
                         sizeof(buf) - n);
    n += pc_pn_dcp_block(PN_DCP_OPT_IP, PN_DCP_SUB_IP_PARAM, (const uint8_t *)"ABCD", 4, buf + n, sizeof(buf) - n);

    Seen s = {0, {0}, {0}, {0}};
    TEST_ASSERT_TRUE(pc_pn_dcp_walk(buf, n, collect, &s));
    TEST_ASSERT_EQUAL_INT(2, s.count);
    TEST_ASSERT_EQUAL_HEX8(PN_DCP_OPT_DEVICE, s.opt[0]);
    TEST_ASSERT_EQUAL_HEX8(PN_DCP_SUB_DEV_NAME_OF_STATION, s.sub[0]);
    TEST_ASSERT_EQUAL_size_t(3, s.len[0]); // pad not counted
    TEST_ASSERT_EQUAL_HEX8(PN_DCP_OPT_IP, s.opt[1]);
    TEST_ASSERT_EQUAL_size_t(4, s.len[1]);
}

void test_walk_rejects_truncated(void)
{
    // blockLength claims 10 but only 2 value bytes present.
    uint8_t bad[6] = {0x02, 0x02, 0x00, 0x0A, 0x01, 0x02};
    TEST_ASSERT_FALSE(pc_pn_dcp_walk(bad, sizeof(bad), NULL, NULL));
}

// Null-buffer / short-buffer / oversize guards on the header, block, and parse entry points.
void test_pn_guards(void)
{
    uint8_t out[16];
    TEST_ASSERT_EQUAL_size_t(0, pc_pn_dcp_header(0, 0, 0, 0, 0, NULL, sizeof(out)));                // null out
    TEST_ASSERT_EQUAL_size_t(0, pc_pn_dcp_header(0, 0, 0, 0, 0, out, 4));                           // cap < header len
    TEST_ASSERT_EQUAL_size_t(0, pc_pn_dcp_block(0, 0, (const uint8_t *)"x", 1, NULL, sizeof(out))); // null out
    TEST_ASSERT_EQUAL_size_t(0, pc_pn_dcp_block(0, 0, NULL, 5, out, sizeof(out)));            // len but null value
    TEST_ASSERT_EQUAL_size_t(0, pc_pn_dcp_block(0, 0, (const uint8_t *)"sixval", 6, out, 4)); // block > cap

    PnDcpHeader h;
    TEST_ASSERT_FALSE(pc_pn_dcp_parse_header(NULL, 10, &h)); // null frame
    uint8_t shortf[9] = {0};
    TEST_ASSERT_FALSE(pc_pn_dcp_parse_header(shortf, sizeof(shortf), &h)); // len < header len
}

void test_block_zero_length_value(void)
{
    // value_len == 0 (value may be null) is legal: exercises the "value_len is falsy" path in
    // pc_pn_dcp_block's guard/memcpy-skip, and the blen==0 (-> NULL value) path in the walk callback.
    uint8_t out[16];
    size_t n = pc_pn_dcp_block(PN_DCP_OPT_DEVICE, PN_DCP_SUB_DEV_ID, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(4, n);
    const uint8_t expect[] = {0x02, 0x03, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, n);

    Seen s = {0, {0}, {0}, {0}};
    TEST_ASSERT_TRUE(pc_pn_dcp_walk(out, n, collect, &s));
    TEST_ASSERT_EQUAL_INT(1, s.count);
    TEST_ASSERT_EQUAL_size_t(0, s.len[0]);
}

void test_block_value_len_too_large(void)
{
    // value_len > 0xFFFF cannot fit in the 16-bit blockLength field, regardless of cap.
    uint8_t out[16];
    uint8_t dummy = 0;
    TEST_ASSERT_EQUAL_size_t(0, pc_pn_dcp_block(0, 0, &dummy, 0x10000, out, sizeof(out)));
}

void test_parse_header_null_out(void)
{
    // Valid frame/len but a null destination struct.
    uint8_t frame[PN_DCP_HDR_LEN] = {0};
    TEST_ASSERT_FALSE(pc_pn_dcp_parse_header(frame, sizeof(frame), NULL));
}

void test_walk_null_callback(void)
{
    // cb == NULL over a well-formed (non-truncated) block list: the walk still succeeds, it just
    // skips invoking the callback for each block.
    uint8_t buf[16];
    size_t n = pc_pn_dcp_block(PN_DCP_OPT_IP, PN_DCP_SUB_IP_PARAM, (const uint8_t *)"ABCD", 4, buf, sizeof(buf));
    TEST_ASSERT_TRUE(pc_pn_dcp_walk(buf, n, NULL, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_header_roundtrip);
    RUN_TEST(test_block_even_padding);
    RUN_TEST(test_walk_blocks);
    RUN_TEST(test_walk_rejects_truncated);
    RUN_TEST(test_pn_guards);
    RUN_TEST(test_block_zero_length_value);
    RUN_TEST(test_block_value_len_too_large);
    RUN_TEST(test_parse_header_null_out);
    RUN_TEST(test_walk_null_callback);
    return UNITY_END();
}
