// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/snp: the GE Fanuc SNP serial frame codec.

#include "services/fieldbus/snp/snp.h"
#include <string.h>
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_bcc(void)
{
    const uint8_t b[] = {0x01, 0x03, 0x10, 0x20, 0x30};
    // sum = 0x01+0x03+0x10+0x20+0x30 = 0x64.
    TEST_ASSERT_EQUAL_HEX8(0x64, pc_snp_bcc(b, sizeof(b)));
    // Wrap-around low byte.
    const uint8_t w[] = {0xFF, 0x02};
    TEST_ASSERT_EQUAL_HEX8(0x01, pc_snp_bcc(w, sizeof(w)));
}

void test_build_and_parse(void)
{
    uint8_t data[3] = {0x10, 0x20, 0x30};
    uint8_t buf[16];
    size_t n = pc_snp_build(Snp::SNP_SOH, data, 3, buf, sizeof(buf));
    // [01][03][10 20 30][BCC] ; BCC = 01+03+10+20+30 = 0x64.
    const uint8_t expect[] = {0x01, 0x03, 0x10, 0x20, 0x30, 0x64};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);

    SnpFrame f;
    TEST_ASSERT_TRUE(pc_snp_parse(buf, n, &f));
    TEST_ASSERT_EQUAL_HEX8(Snp::SNP_SOH, f.control);
    TEST_ASSERT_EQUAL_size_t(3, f.data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, f.data, 3);
}

void test_empty_data(void)
{
    uint8_t buf[8];
    size_t n = pc_snp_build(Snp::SNP_ENQ, nullptr, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(3, n);
    SnpFrame f;
    TEST_ASSERT_TRUE(pc_snp_parse(buf, n, &f));
    TEST_ASSERT_EQUAL_size_t(0, f.data_len);
    TEST_ASSERT_NULL(f.data);
}

void test_parse_rejects(void)
{
    uint8_t data[2] = {0xAA, 0xBB};
    uint8_t buf[8];
    size_t n = pc_snp_build(Snp::SNP_SOH, data, 2, buf, sizeof(buf));
    SnpFrame f;
    buf[n - 1] ^= 0xFF; // bad BCC
    TEST_ASSERT_FALSE(pc_snp_parse(buf, n, &f));
    TEST_ASSERT_FALSE(pc_snp_parse(buf, 2, &f)); // too short
    // Length field says more data than the buffer holds.
    uint8_t bad[4] = {0x01, 0x10, 0x00, 0x11};
    TEST_ASSERT_FALSE(pc_snp_parse(bad, sizeof(bad), &f));
}

// pc_snp_build rejects a null output, a length with a null data pointer, and a
// destination too small for the framed message.
void test_snp_build_guards(void)
{
    uint8_t out[16];
    const uint8_t data[2] = {1, 2};
    TEST_ASSERT_EQUAL_size_t(0, pc_snp_build(0x10, data, 2, nullptr, sizeof(out))); // null out
    TEST_ASSERT_EQUAL_size_t(0, pc_snp_build(0x10, nullptr, 2, out, sizeof(out)));  // len but null data
    TEST_ASSERT_EQUAL_size_t(0, pc_snp_build(0x10, data, 2, out, 3));               // needs 5, cap 3
    // data_len above 255 doesn't fit the one-byte length field; out/data are otherwise
    // valid so the data_len > 255 arm itself is what's under test (the function returns
    // before touching data, so a short backing array is fine).
    TEST_ASSERT_EQUAL_size_t(0, pc_snp_build(0x10, data, 256, out, sizeof(out)));
}

// pc_snp_parse rejects a null frame and a null output pointer.
void test_snp_parse_guards(void)
{
    uint8_t buf[8];
    SnpFrame f;
    size_t n = pc_snp_build(Snp::SNP_ENQ, nullptr, 0, buf, sizeof(buf));
    TEST_ASSERT_FALSE(pc_snp_parse(nullptr, n, &f));  // null frame
    TEST_ASSERT_FALSE(pc_snp_parse(buf, n, nullptr)); // null out
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bcc);
    RUN_TEST(test_build_and_parse);
    RUN_TEST(test_empty_data);
    RUN_TEST(test_parse_rejects);
    RUN_TEST(test_snp_build_guards);
    RUN_TEST(test_snp_parse_guards);
    return UNITY_END();
}
