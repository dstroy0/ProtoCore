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

#include "network_drivers/presentation/http/http3/quic_varint/quic_varint.h"
#include <string.h>

#include <unity.h>

static uint8_t quic_varint_work[16]; // the borrow an entry takes; QuicVarint never reads it

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

    QuicVarint.len_args.value = value;
    QuicVarint.len(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(n, QuicVarint.n);

    memset(out, 0xAA, sizeof(out));
    QuicVarint.encode_args.out = out;
    QuicVarint.encode_args.cap = sizeof(out);
    QuicVarint.encode_args.value = value;
    QuicVarint.encode(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(n, QuicVarint.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes, out, n);

    QuicVarint.decode_args.in = bytes;
    QuicVarint.decode_args.len = n;
    QuicVarint.decode_args.value = &v;
    QuicVarint.decode_args.consumed = &consumed;
    QuicVarint.decode(quic_varint_work);
    TEST_ASSERT_TRUE(QuicVarint.ok);
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
    QuicVarint.decode_args.in = NM;
    QuicVarint.decode_args.len = sizeof(NM);
    QuicVarint.decode_args.value = &v;
    QuicVarint.decode_args.consumed = &consumed;
    QuicVarint.decode(quic_varint_work);
    TEST_ASSERT_TRUE(QuicVarint.ok);
    TEST_ASSERT_EQUAL_UINT(2u, consumed);
    TEST_ASSERT_EQUAL_UINT64(37u, v);

    // The encoder still emits the shortest form for the same value.
    uint8_t out[8];
    QuicVarint.encode_args.out = out;
    QuicVarint.encode_args.cap = sizeof(out);
    QuicVarint.encode_args.value = 37u;
    QuicVarint.encode(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(1u, QuicVarint.n);
    TEST_ASSERT_EQUAL_HEX8(0x25, out[0]);
}

// RFC 9000 Table 4 ranges: 0-63 in 1 byte, 0-16383 in 2, 0-1073741823 in 4, 0-4611686018427387903
// in 8. Each pair below is the last value of one row and the first of the next.
void test_table4_length_boundaries(void)
{
    QuicVarint.len_args.value = 0u;
    QuicVarint.len(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(1u, QuicVarint.n);
    QuicVarint.len_args.value = 63u;
    QuicVarint.len(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(1u, QuicVarint.n);
    QuicVarint.len_args.value = 64u;
    QuicVarint.len(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(2u, QuicVarint.n);
    QuicVarint.len_args.value = 16383u;
    QuicVarint.len(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(2u, QuicVarint.n);
    QuicVarint.len_args.value = 16384u;
    QuicVarint.len(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(4u, QuicVarint.n);
    QuicVarint.len_args.value = 1073741823u;
    QuicVarint.len(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(4u, QuicVarint.n);
    QuicVarint.len_args.value = 1073741824u;
    QuicVarint.len(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(8u, QuicVarint.n);
    QuicVarint.len_args.value = 4611686018427387903ull;
    QuicVarint.len(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(8u, QuicVarint.n);

    // 2^62-1 is the top of the last row, and QUIC_VARINT_MAX must be that same number.
    TEST_ASSERT_EQUAL_UINT64(4611686018427387903ull, (uint64_t)QUIC_VARINT_MAX);
}

// Each row's boundary value encodes with its row's 2MSB prefix (00 / 01 / 10 / 11) and zero body
// bits elsewhere, which is what makes the first value of a row distinguishable from the last of the
// previous one on the wire.
void test_boundary_encodings_carry_their_prefix(void)
{
    uint8_t out[8];

    QuicVarint.encode_args.out = out;
    QuicVarint.encode_args.cap = sizeof(out);
    QuicVarint.encode_args.value = 63u;
    QuicVarint.encode(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(1u, QuicVarint.n);
    TEST_ASSERT_EQUAL_HEX8(0x3f, out[0]); // 00 111111

    static const uint8_t WANT64[2] = {0x40, 0x40}; // 01 000000 01000000
    QuicVarint.encode_args.out = out;
    QuicVarint.encode_args.cap = sizeof(out);
    QuicVarint.encode_args.value = 64u;
    QuicVarint.encode(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(2u, QuicVarint.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT64, out, 2);

    static const uint8_t WANT16383[2] = {0x7f, 0xff}; // 01 111111 11111111
    QuicVarint.encode_args.out = out;
    QuicVarint.encode_args.cap = sizeof(out);
    QuicVarint.encode_args.value = 16383u;
    QuicVarint.encode(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(2u, QuicVarint.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT16383, out, 2);

    static const uint8_t WANT16384[4] = {0x80, 0x00, 0x40, 0x00}; // 10 + 0x00004000
    QuicVarint.encode_args.out = out;
    QuicVarint.encode_args.cap = sizeof(out);
    QuicVarint.encode_args.value = 16384u;
    QuicVarint.encode(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(4u, QuicVarint.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT16384, out, 4);

    static const uint8_t WANTMAX[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}; // 11 + all ones
    QuicVarint.encode_args.out = out;
    QuicVarint.encode_args.cap = sizeof(out);
    QuicVarint.encode_args.value = QUIC_VARINT_MAX;
    QuicVarint.encode(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(8u, QuicVarint.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANTMAX, out, 8);
}

// Table 4 stops at 2^62-1, so anything above it has no encoding at all.
void test_above_the_62_bit_range_is_refused(void)
{
    uint8_t out[8];
    QuicVarint.len_args.value = QUIC_VARINT_MAX + 1u;
    QuicVarint.len(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicVarint.n);
    QuicVarint.len_args.value = 0xFFFFFFFFFFFFFFFFull;
    QuicVarint.len(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicVarint.n);
    QuicVarint.encode_args.out = out;
    QuicVarint.encode_args.cap = sizeof(out);
    QuicVarint.encode_args.value = QUIC_VARINT_MAX + 1u;
    QuicVarint.encode(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicVarint.n);
}

// A buffer shorter than the encoding writes nothing rather than a truncated integer.
void test_encode_refuses_a_short_buffer(void)
{
    uint8_t out[8];
    memset(out, 0xAA, sizeof(out));
    QuicVarint.encode_args.out = out;
    QuicVarint.encode_args.cap = 1;
    QuicVarint.encode_args.value = 16384u;
    QuicVarint.encode(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicVarint.n);
    TEST_ASSERT_EQUAL_HEX8(0xAA, out[0]);
    QuicVarint.encode_args.out = out;
    QuicVarint.encode_args.cap = 3;
    QuicVarint.encode_args.value = 16384u;
    QuicVarint.encode(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicVarint.n);
    QuicVarint.encode_args.out = out;
    QuicVarint.encode_args.cap = 7;
    QuicVarint.encode_args.value = QUIC_VARINT_MAX;
    QuicVarint.encode(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicVarint.n);
    QuicVarint.encode_args.out = out;
    QuicVarint.encode_args.cap = 0;
    QuicVarint.encode_args.value = 0u;
    QuicVarint.encode(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicVarint.n);

    // cap exactly equal to the encoding length is enough.
    QuicVarint.encode_args.out = out;
    QuicVarint.encode_args.cap = 4;
    QuicVarint.encode_args.value = 16384u;
    QuicVarint.encode(quic_varint_work);
    TEST_ASSERT_EQUAL_UINT(4u, QuicVarint.n);
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

    QuicVarint.decode_args.in = TRUNC4;
    QuicVarint.decode_args.len = sizeof(TRUNC4);
    QuicVarint.decode_args.value = &v;
    QuicVarint.decode_args.consumed = &consumed;
    QuicVarint.decode(quic_varint_work);
    TEST_ASSERT_FALSE(QuicVarint.ok);
    QuicVarint.decode_args.in = TRUNC8;
    QuicVarint.decode_args.len = sizeof(TRUNC8);
    QuicVarint.decode_args.value = &v;
    QuicVarint.decode_args.consumed = &consumed;
    QuicVarint.decode(quic_varint_work);
    TEST_ASSERT_FALSE(QuicVarint.ok);
    QuicVarint.decode_args.in = TRUNC2;
    QuicVarint.decode_args.len = sizeof(TRUNC2);
    QuicVarint.decode_args.value = &v;
    QuicVarint.decode_args.consumed = &consumed;
    QuicVarint.decode(quic_varint_work);
    TEST_ASSERT_FALSE(QuicVarint.ok);
    QuicVarint.decode_args.in = TRUNC2;
    QuicVarint.decode_args.len = 0;
    QuicVarint.decode_args.value = &v;
    QuicVarint.decode_args.consumed = &consumed;
    QuicVarint.decode(quic_varint_work);
    TEST_ASSERT_FALSE(QuicVarint.ok);
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
        QuicVarint.encode_args.out = buf;
        QuicVarint.encode_args.cap = sizeof(buf);
        QuicVarint.encode_args.value = VALUES[i];
        QuicVarint.encode(quic_varint_work);
        size_t n = QuicVarint.n;
        TEST_ASSERT_TRUE(n == 1u || n == 2u || n == 4u || n == 8u);
        QuicVarint.decode_args.in = buf;
        QuicVarint.decode_args.len = n;
        QuicVarint.decode_args.value = &v;
        QuicVarint.decode_args.consumed = &consumed;
        QuicVarint.decode(quic_varint_work);
        TEST_ASSERT_TRUE(QuicVarint.ok);
        TEST_ASSERT_EQUAL_UINT(n, consumed);
        TEST_ASSERT_EQUAL_UINT64(VALUES[i], v);
    }
}
