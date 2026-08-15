// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the QUIC variable-length integer codec
// (network_drivers/presentation/http/http3/quic_varint.h).
//
// RFC 9000 Appendix A.1 publishes four sample encodings and the values they decode to. Those four
// pairs are the load-bearing case: they pin the two-bit length prefix, the big-endian body, and the
// masking of the prefix out of the first byte all at once, and every length class of Table 4 is
// represented. The rest of the file walks Table 4's range column, which fixes exactly where the
// encoder must step from 1 to 2 to 4 to 8 bytes.

#include "network_drivers/presentation/http/http3/quic_varint.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// One published pair: the length, the encoding, and the decode all have to agree.
static void vector(uint64_t value, const uint8_t *bytes, size_t n)
{
    uint8_t out[8];
    uint64_t v = 0;
    size_t consumed = 0;

    TEST_ASSERT_EQUAL_UINT(n, protocore_quic_varint_len(value));

    memset(out, 0xAA, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(n, protocore_quic_varint_encode(out, sizeof(out), value));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes, out, n);

    TEST_ASSERT_TRUE(protocore_quic_varint_decode(bytes, n, &v, &consumed));
    TEST_ASSERT_EQUAL_UINT(n, consumed);
    TEST_ASSERT_EQUAL_UINT64(value, v);
}

// RFC 9000 Appendix A.1: "the eight-byte sequence 0xc2197c5eff14e88c decodes to the decimal value
// 151,288,809,941,952,652; the four-byte sequence 0x9d7f3e7d decodes to 494,878,333; the two-byte
// sequence 0x7bbd decodes to 15,293; and the single byte 0x25 decodes to 37".
void test_rfc9000_appendix_a1_vectors(void)
{
    static const uint8_t ONE[1] = {0x25};
    static const uint8_t TWO[2] = {0x7b, 0xbd};
    static const uint8_t FOUR[4] = {0x9d, 0x7f, 0x3e, 0x7d};
    static const uint8_t EIGHT[8] = {0xc2, 0x19, 0x7c, 0x5e, 0xff, 0x14, 0xe8, 0x8c};

    vector(37u, ONE, 1);
    vector(15293u, TWO, 2);
    vector(494878333u, FOUR, 4);
    vector(151288809941952652ull, EIGHT, 8);
}

// Appendix A.1's last clause: 37 is also spelled 0x4025. Sec 16 permits a non-minimal encoding for
// every field but a frame type, so the decoder must take it and report two bytes consumed.
void test_non_minimal_encoding_decodes(void)
{
    static const uint8_t NM[2] = {0x40, 0x25};
    uint64_t v = 0;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(protocore_quic_varint_decode(NM, sizeof(NM), &v, &consumed));
    TEST_ASSERT_EQUAL_UINT(2u, consumed);
    TEST_ASSERT_EQUAL_UINT64(37u, v);

    // The encoder still emits the shortest form for the same value.
    uint8_t out[8];
    TEST_ASSERT_EQUAL_UINT(1u, protocore_quic_varint_encode(out, sizeof(out), 37u));
    TEST_ASSERT_EQUAL_HEX8(0x25, out[0]);
}

// RFC 9000 Table 4 ranges: 0-63 in 1 byte, 0-16383 in 2, 0-1073741823 in 4, 0-4611686018427387903
// in 8. Each pair below is the last value of one row and the first of the next.
void test_table4_length_boundaries(void)
{
    TEST_ASSERT_EQUAL_UINT(1u, protocore_quic_varint_len(0u));
    TEST_ASSERT_EQUAL_UINT(1u, protocore_quic_varint_len(63u));
    TEST_ASSERT_EQUAL_UINT(2u, protocore_quic_varint_len(64u));
    TEST_ASSERT_EQUAL_UINT(2u, protocore_quic_varint_len(16383u));
    TEST_ASSERT_EQUAL_UINT(4u, protocore_quic_varint_len(16384u));
    TEST_ASSERT_EQUAL_UINT(4u, protocore_quic_varint_len(1073741823u));
    TEST_ASSERT_EQUAL_UINT(8u, protocore_quic_varint_len(1073741824u));
    TEST_ASSERT_EQUAL_UINT(8u, protocore_quic_varint_len(4611686018427387903ull));

    // 2^62-1 is the top of the last row, and QUIC_VARINT_MAX must be that same number.
    TEST_ASSERT_EQUAL_UINT64(4611686018427387903ull, (uint64_t)QUIC_VARINT_MAX);
}

// Each row's boundary value encodes with its row's 2MSB prefix (00 / 01 / 10 / 11) and zero body
// bits elsewhere, which is what makes the first value of a row distinguishable from the last of the
// previous one on the wire.
void test_boundary_encodings_carry_their_prefix(void)
{
    uint8_t out[8];

    TEST_ASSERT_EQUAL_UINT(1u, protocore_quic_varint_encode(out, sizeof(out), 63u));
    TEST_ASSERT_EQUAL_HEX8(0x3f, out[0]); // 00 111111

    static const uint8_t WANT64[2] = {0x40, 0x40}; // 01 000000 01000000
    TEST_ASSERT_EQUAL_UINT(2u, protocore_quic_varint_encode(out, sizeof(out), 64u));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT64, out, 2);

    static const uint8_t WANT16383[2] = {0x7f, 0xff}; // 01 111111 11111111
    TEST_ASSERT_EQUAL_UINT(2u, protocore_quic_varint_encode(out, sizeof(out), 16383u));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT16383, out, 2);

    static const uint8_t WANT16384[4] = {0x80, 0x00, 0x40, 0x00}; // 10 + 0x00004000
    TEST_ASSERT_EQUAL_UINT(4u, protocore_quic_varint_encode(out, sizeof(out), 16384u));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT16384, out, 4);

    static const uint8_t WANTMAX[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}; // 11 + all ones
    TEST_ASSERT_EQUAL_UINT(8u, protocore_quic_varint_encode(out, sizeof(out), QUIC_VARINT_MAX));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANTMAX, out, 8);
}

// Table 4 stops at 2^62-1, so anything above it has no encoding at all.
void test_above_the_62_bit_range_is_refused(void)
{
    uint8_t out[8];
    TEST_ASSERT_EQUAL_UINT(0u, protocore_quic_varint_len(QUIC_VARINT_MAX + 1u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_quic_varint_len(0xFFFFFFFFFFFFFFFFull));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_quic_varint_encode(out, sizeof(out), QUIC_VARINT_MAX + 1u));
}

// A buffer shorter than the encoding writes nothing rather than a truncated integer.
void test_encode_refuses_a_short_buffer(void)
{
    uint8_t out[8];
    memset(out, 0xAA, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_quic_varint_encode(out, 1, 16384u));
    TEST_ASSERT_EQUAL_HEX8(0xAA, out[0]);
    TEST_ASSERT_EQUAL_UINT(0u, protocore_quic_varint_encode(out, 3, 16384u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_quic_varint_encode(out, 7, QUIC_VARINT_MAX));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_quic_varint_encode(out, 0, 0u));

    // cap exactly equal to the encoding length is enough.
    TEST_ASSERT_EQUAL_UINT(4u, protocore_quic_varint_encode(out, 4, 16384u));
}

// The first byte announces the total length, so a buffer holding fewer bytes than that is refused
// rather than read past its end.
void test_decode_refuses_a_truncated_input(void)
{
    static const uint8_t TRUNC4[2] = {0x9d, 0x7f}; // prefix 10 -> 4 bytes, only 2 present
    static const uint8_t TRUNC8[7] = {0xc2, 0x19, 0x7c, 0x5e, 0xff, 0x14, 0xe8};
    static const uint8_t TRUNC2[1] = {0x7b};
    uint64_t v = 0xDEADBEEFu;
    size_t consumed = 99u;

    TEST_ASSERT_FALSE(protocore_quic_varint_decode(TRUNC4, sizeof(TRUNC4), &v, &consumed));
    TEST_ASSERT_FALSE(protocore_quic_varint_decode(TRUNC8, sizeof(TRUNC8), &v, &consumed));
    TEST_ASSERT_FALSE(protocore_quic_varint_decode(TRUNC2, sizeof(TRUNC2), &v, &consumed));
    TEST_ASSERT_FALSE(protocore_quic_varint_decode(TRUNC2, 0, &v, &consumed));
}

// Encode then decode returns the same value for every length class and both ends of each row.
void test_round_trip_over_every_length_class(void)
{
    static const uint64_t VALUES[] = {
        0u,
        1u,
        63u,
        64u,
        16383u,
        16384u,
        1u << 20,
        1073741823u,
        1073741824u,
        1ull << 40,
        QUIC_VARINT_MAX - 1u,
        QUIC_VARINT_MAX,
    };
    for (size_t i = 0; i < sizeof(VALUES) / sizeof(VALUES[0]); i++)
    {
        uint8_t buf[8];
        uint64_t v = 0;
        size_t consumed = 0;
        size_t n = protocore_quic_varint_encode(buf, sizeof(buf), VALUES[i]);
        TEST_ASSERT_TRUE(n == 1u || n == 2u || n == 4u || n == 8u);
        TEST_ASSERT_TRUE(protocore_quic_varint_decode(buf, n, &v, &consumed));
        TEST_ASSERT_EQUAL_UINT(n, consumed);
        TEST_ASSERT_EQUAL_UINT64(VALUES[i], v);
    }
}
