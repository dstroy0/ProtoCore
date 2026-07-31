// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the QUIC variable-length integer codec (network_drivers/presentation/http/http3/
// pc_quic_varint, RFC 9000 sec 16) against the Appendix A.1 worked examples, plus the non-minimal
// decode, length computation, overflow, and truncation guards.

#include "network_drivers/presentation/http/http3/quic_varint.h"
#include <stdint.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

static void check(uint64_t value, const uint8_t *bytes, size_t n)
{
    TEST_ASSERT_EQUAL_INT((int)n, (int)pc_quic_varint_len(value));
    uint8_t out[8];
    TEST_ASSERT_EQUAL_INT((int)n, (int)pc_quic_varint_encode(out, sizeof out, value));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes, out, n);
    uint64_t v = 0;
    size_t c = 0;
    TEST_ASSERT_TRUE(pc_quic_varint_decode(bytes, n, &v, &c));
    TEST_ASSERT_EQUAL_INT((int)n, (int)c);
    TEST_ASSERT_TRUE(v == value);
}

void test_rfc_examples()
{
    // RFC 9000 Appendix A.1
    const uint8_t one[1] = {0x25};
    check(37u, one, 1);
    const uint8_t two[2] = {0x7b, 0xbd};
    check(15293u, two, 2);
    const uint8_t four[4] = {0x9d, 0x7f, 0x3e, 0x7d};
    check(494878333u, four, 4);
    const uint8_t eight[8] = {0xc2, 0x19, 0x7c, 0x5e, 0xff, 0x14, 0xe8, 0x8c};
    check(151288809941952652ull, eight, 8);
}

void test_non_minimal_decode()
{
    // The RFC's two-byte encoding of 37 must decode to 37 (consuming 2 bytes).
    const uint8_t nm[2] = {0x40, 0x25};
    uint64_t v = 0;
    size_t c = 0;
    TEST_ASSERT_TRUE(pc_quic_varint_decode(nm, 2, &v, &c));
    TEST_ASSERT_EQUAL_INT(2, (int)c);
    TEST_ASSERT_TRUE(v == 37u);
}

void test_boundaries_and_guards()
{
    uint8_t out[8];
    // Length boundaries.
    TEST_ASSERT_EQUAL_INT(1, (int)pc_quic_varint_len(63));
    TEST_ASSERT_EQUAL_INT(2, (int)pc_quic_varint_len(64));
    TEST_ASSERT_EQUAL_INT(2, (int)pc_quic_varint_len(16383));
    TEST_ASSERT_EQUAL_INT(4, (int)pc_quic_varint_len(16384));
    TEST_ASSERT_EQUAL_INT(4, (int)pc_quic_varint_len(1073741823));
    TEST_ASSERT_EQUAL_INT(8, (int)pc_quic_varint_len(1073741824));
    TEST_ASSERT_EQUAL_INT(8, (int)pc_quic_varint_len(QUIC_VARINT_MAX));
    // Above 2^62-1: not representable.
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_varint_len(QUIC_VARINT_MAX + 1));
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_varint_encode(out, sizeof out, QUIC_VARINT_MAX + 1));
    // Too-small output buffer.
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_varint_encode(out, 1, 16384));
    // Truncated input (first byte announces 4 bytes, only 2 present).
    uint64_t v = 0;
    size_t c = 0;
    const uint8_t trunc[2] = {0x9d, 0x7f};
    TEST_ASSERT_FALSE(pc_quic_varint_decode(trunc, 2, &v, &c));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_rfc_examples);
    RUN_TEST(test_non_minimal_decode);
    RUN_TEST(test_boundaries_and_guards);
    return UNITY_END();
}
