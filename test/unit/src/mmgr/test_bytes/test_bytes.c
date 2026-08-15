// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the byte verbs (mmgr/bytes.h).
//
// RFC 4251 sec 5 defines the three shapes these verbs move and prints a worked encoding for each:
// a uint32 ("the value 699921578 (0x29b7f4aa) is stored as 29 b7 f4 aa"), a string ("the US-ASCII
// string "testing" is represented as 00 00 00 07 t e s t i n g"), and a table of five mpints.
//
// test_rfc4251_mpint_examples is the load-bearing case: the mpint table is the only place the RFC
// publishes the leading-zero rule as octets, and mpint_fixed exists to undo it. Getting the strip
// wrong turns a shared secret into a value shifted by one byte, which fails as a MAC mismatch far
// from here.

#include "mmgr/bytes.h"
#include <string.h>

#include <unity.h>

#define CAP 32u

static uint8_t store[CAP];

void setUp(void)
{
    memset(store, 0, sizeof(store));
}

void tearDown(void)
{
}

// ---- the RFC 4251 sec 5 encodings ------------------------------------------

// "the value 699921578 (0x29b7f4aa) is stored as 29 b7 f4 aa"
void test_rfc4251_uint32_encoding(void)
{
    static const uint8_t WIRE[4] = {0x29, 0xb7, 0xf4, 0xaa};

    protocore_span w = span.from(store, CAP);
    bytes.put_be(&w, 699921578u, 4);
    TEST_ASSERT_EQUAL_size_t(4, span.len(w));
    TEST_ASSERT_FALSE(w.overflow);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WIRE, store, 4);

    // The same octets read back, through the cursor form and the offset form.
    protocore_cspan r = span.cfrom(WIRE, sizeof(WIRE));
    uint64_t v = 0;
    TEST_ASSERT_TRUE(bytes.take_be(&r, 4, &v));
    TEST_ASSERT_EQUAL_HEX64(699921578u, v);

    size_t off = 0;
    uint32_t u = 0;
    TEST_ASSERT_TRUE(bytes.rd_u32(WIRE, sizeof(WIRE), &off, &u));
    TEST_ASSERT_EQUAL_HEX32(699921578u, u);
    TEST_ASSERT_EQUAL_size_t(4, off);
}

// "the US-ASCII string "testing" is represented as 00 00 00 07 t e s t i n g"
void test_rfc4251_string_encoding(void)
{
    static const uint8_t WIRE[11] = {0x00, 0x00, 0x00, 0x07, 0x74, 0x65, 0x73, 0x74, 0x69, 0x6e, 0x67};

    protocore_span w = span.from(store, CAP);
    bytes.put_be(&w, 7u, 4);
    bytes.raw(&w, "testing", 7);
    TEST_ASSERT_EQUAL_size_t(sizeof(WIRE), span.len(w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WIRE, store, sizeof(WIRE));

    size_t off = 0;
    const uint8_t *s = NULL;
    uint32_t slen = 0;
    TEST_ASSERT_TRUE(bytes.rd_str(WIRE, sizeof(WIRE), &off, &s, &slen));
    TEST_ASSERT_EQUAL_UINT32(7u, slen);
    TEST_ASSERT_EQUAL_PTR(WIRE + 4, s); // a view into the payload, not a copy
    TEST_ASSERT_EQUAL_size_t(11, off);
}

// RFC 4251 sec 5's mpint table, fed to mpint_fixed as the data partition of each representation.
// The rule the table encodes: a positive number whose top bit would be set carries a leading zero
// byte, which is padding rather than magnitude, and a leading 0xff belongs to a negative value and
// is not padding at all.
//
//   value               representation        data partition
//   0                   00 00 00 00           (empty)
//   9a378f9b2e332a7     00 00 00 08 09 ...    09 a3 78 f9 b2 e3 32 a7
//   80                  00 00 00 02 00 80     00 80
//   -deadbeef           00 00 00 05 ff ...    ff 21 52 41 11
void test_rfc4251_mpint_examples(void)
{
    uint8_t out[8];

    // 0: no data at all, so every lane of the destination is zero.
    static const uint8_t M0[1] = {0x00};
    memset(out, 0xAA, sizeof(out));
    TEST_ASSERT_TRUE(bytes.mpint_fixed(M0, 0, out, sizeof(out)));
    static const uint8_t ZERO[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ZERO, out, sizeof(out));

    // 9a378f9b2e332a7: eight data bytes, none of them a leading zero, into an eight-byte field.
    static const uint8_t M1[8] = {0x09, 0xa3, 0x78, 0xf9, 0xb2, 0xe3, 0x32, 0xa7};
    memset(out, 0xAA, sizeof(out));
    TEST_ASSERT_TRUE(bytes.mpint_fixed(M1, sizeof(M1), out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(M1, out, sizeof(out));

    // 80: the RFC's mandated pad byte comes off, and the magnitude right-aligns.
    static const uint8_t M2[2] = {0x00, 0x80};
    static const uint8_t W2[4] = {0x00, 0x00, 0x00, 0x80};
    uint8_t four[4];
    memset(four, 0xAA, sizeof(four));
    TEST_ASSERT_TRUE(bytes.mpint_fixed(M2, sizeof(M2), four, sizeof(four)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(W2, four, sizeof(four));

    // -deadbeef: the leading byte is 0xff, not zero, so nothing is stripped.
    static const uint8_t M3[5] = {0xff, 0x21, 0x52, 0x41, 0x11};
    static const uint8_t W3[8] = {0x00, 0x00, 0x00, 0xff, 0x21, 0x52, 0x41, 0x11};
    memset(out, 0xAA, sizeof(out));
    TEST_ASSERT_TRUE(bytes.mpint_fixed(M3, sizeof(M3), out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(W3, out, sizeof(out));
}

// A magnitude wider than the destination is refused rather than truncated.
void test_mpint_wider_than_the_destination_is_refused(void)
{
    static const uint8_t M[8] = {0x09, 0xa3, 0x78, 0xf9, 0xb2, 0xe3, 0x32, 0xa7};
    uint8_t four[4];
    memset(four, 0xAA, sizeof(four));
    TEST_ASSERT_FALSE(bytes.mpint_fixed(M, sizeof(M), four, sizeof(four)));
    TEST_ASSERT_EQUAL_HEX8(0xAAu, four[0]); // untouched on refusal

    // Only the leading zeros count toward the width: the same bytes behind five zero pad bytes fit.
    static const uint8_t PADDED[9] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x22, 0x33, 0x44};
    static const uint8_t WANT[4] = {0x11, 0x22, 0x33, 0x44};
    TEST_ASSERT_TRUE(bytes.mpint_fixed(PADDED, sizeof(PADDED), four, sizeof(four)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, four, sizeof(four));
}

// ---- appending into a write region -----------------------------------------

// A byte lands and the cursor counts it.
void test_put_writes_and_counts(void)
{
    protocore_span w = span.from(store, CAP);
    bytes.put(&w, 0xA5u);
    bytes.put(&w, 0x5Au);
    TEST_ASSERT_EQUAL_size_t(2, span.len(w));
    TEST_ASSERT_FALSE(w.overflow);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, store[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5Au, store[1]);
    TEST_ASSERT_TRUE(span.ok(w));
}

// Past the capacity nothing is stored, the flag latches, and the cursor keeps counting, so it
// reports the capacity the payload needed.
void test_put_past_cap_reports_the_capacity_needed(void)
{
    protocore_span w = span.from(store, 4);
    for (unsigned i = 0; i < 10u; i++)
    {
        bytes.put(&w, (uint8_t)(0x10u + i));
    }
    TEST_ASSERT_TRUE(w.overflow);
    TEST_ASSERT_EQUAL_size_t(10, span.len(w));
    TEST_ASSERT_EQUAL_HEX8(0x13u, store[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, store[4]);
    TEST_ASSERT_EQUAL_size_t(0, span.room(w));
    TEST_ASSERT_FALSE(span.ok(w));
}

// A width goes out most significant byte first at every width RFC 4251 sec 5 names, 1 through 8.
void test_put_be_writes_most_significant_byte_first(void)
{
    static const uint8_t FULL[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    for (int32_t n = 1; n <= 8; n++)
    {
        memset(store, 0, sizeof(store));
        protocore_span w = span.from(store, CAP);
        bytes.put_be(&w, 0x0123456789ABCDEFull, n);
        TEST_ASSERT_EQUAL_size_t((size_t)n, span.len(w));
        // The low n bytes of the value, in decreasing significance: the tail of the full string.
        TEST_ASSERT_EQUAL_HEX8_ARRAY(FULL + (8 - n), store, (size_t)n);
        TEST_ASSERT_EQUAL_HEX8(0x00u, store[n]);
    }
}

// A width that does not fit still counts every byte it would have needed.
void test_put_be_past_cap_counts_its_whole_width(void)
{
    protocore_span w = span.from(store, 3);
    bytes.put_be(&w, 0x0123456789ABCDEFull, 8);
    TEST_ASSERT_TRUE(w.overflow);
    TEST_ASSERT_EQUAL_size_t(8, span.len(w));
    static const uint8_t HEAD[3] = {0x01, 0x23, 0x45};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(HEAD, store, 3);
    TEST_ASSERT_EQUAL_HEX8(0x00u, store[3]);
}

// A raw block is stored whole or not at all, and the cursor advances by its length either way.
void test_raw_stores_whole_or_not_at_all(void)
{
    static const uint8_t ABCD[4] = {'a', 'b', 'c', 'd'};
    protocore_span w = span.from(store, 4);
    bytes.raw(&w, ABCD, 4);
    TEST_ASSERT_FALSE(w.overflow);
    TEST_ASSERT_EQUAL_size_t(4, span.len(w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ABCD, store, 4);

    bytes.raw(&w, "ef", 2);
    TEST_ASSERT_TRUE(w.overflow);
    TEST_ASSERT_EQUAL_size_t(6, span.len(w));
    TEST_ASSERT_EQUAL_HEX8(0x00u, store[4]); // nothing past the capacity
}

// ---- taking out of a read region -------------------------------------------

// A width comes back in network order and the cursor lands past it.
void test_take_be_advances_by_the_width(void)
{
    static const uint8_t WIRE[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    protocore_cspan r = span.cfrom(WIRE, sizeof(WIRE));
    uint64_t v = 0;

    TEST_ASSERT_TRUE(bytes.take_be(&r, 2, &v));
    TEST_ASSERT_EQUAL_HEX64(0x0102u, v);
    TEST_ASSERT_EQUAL_size_t(2, r.pos);

    TEST_ASSERT_TRUE(bytes.take_be(&r, 4, &v));
    TEST_ASSERT_EQUAL_HEX64(0x03040506u, v);
    TEST_ASSERT_EQUAL_size_t(6, r.pos);

    TEST_ASSERT_TRUE(bytes.take_be(&r, 2, &v));
    TEST_ASSERT_EQUAL_HEX64(0x0708u, v);
    TEST_ASSERT_TRUE(span.cok(r));
}

// Consuming exactly the whole region is not an error; one byte more is.
void test_take_be_at_the_end_and_past_it(void)
{
    static const uint8_t WIRE[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    protocore_cspan r = span.cfrom(WIRE, sizeof(WIRE));
    uint64_t v = 0;

    TEST_ASSERT_TRUE(bytes.take_be(&r, 4, &v));
    TEST_ASSERT_EQUAL_HEX64(0xAABBCCDDu, v);
    TEST_ASSERT_FALSE(r.err);

    TEST_ASSERT_FALSE(bytes.take_be(&r, 1, &v));
    TEST_ASSERT_TRUE(r.err);
    TEST_ASSERT_EQUAL_size_t(4, r.pos);
}

// A refused read leaves the cursor and the destination alone, and the flag stays set afterwards.
void test_take_be_refusal_is_sticky(void)
{
    static const uint8_t WIRE[2] = {0x11, 0x22};
    protocore_cspan r = span.cfrom(WIRE, sizeof(WIRE));
    uint64_t v = 0xDEADBEEFu;

    TEST_ASSERT_FALSE(bytes.take_be(&r, 8, &v));
    TEST_ASSERT_TRUE(r.err);
    TEST_ASSERT_EQUAL_size_t(0, r.pos);
    TEST_ASSERT_EQUAL_HEX64(0xDEADBEEFu, v);

    TEST_ASSERT_TRUE(bytes.take_be(&r, 2, &v));
    TEST_ASSERT_EQUAL_HEX64(0x1122u, v);
    TEST_ASSERT_TRUE(r.err);
}

// A zero-width take yields zero and moves nothing.
void test_take_be_zero_width(void)
{
    static const uint8_t WIRE[2] = {0x11, 0x22};
    protocore_cspan r = span.cfrom(WIRE, sizeof(WIRE));
    uint64_t v = 0xFFu;
    TEST_ASSERT_TRUE(bytes.take_be(&r, 0, &v));
    TEST_ASSERT_EQUAL_HEX64(0, v);
    TEST_ASSERT_EQUAL_size_t(0, r.pos);
    TEST_ASSERT_FALSE(r.err);
}

// ---- the offset-passing reads ----------------------------------------------

// Four bytes are needed; three are refused, and an offset already past the end is refused rather
// than subtracted into a wrap.
void test_rd_u32_short_read_is_refused(void)
{
    static const uint8_t P[3] = {0x01, 0x02, 0x03};
    size_t off = 0;
    uint32_t v = 0xA5A5A5A5u;
    TEST_ASSERT_FALSE(bytes.rd_u32(P, sizeof(P), &off, &v));
    TEST_ASSERT_EQUAL_size_t(0, off);
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, v);

    off = sizeof(P) + 4u;
    TEST_ASSERT_FALSE(bytes.rd_u32(P, sizeof(P), &off, &v));
}

// A length reaching past the end rewinds the offset, so the caller can name the field that failed.
void test_rd_str_overlong_length_rewinds(void)
{
    static const uint8_t P[7] = {0x00, 0x00, 0x00, 0x09, 'a', 'b', 'c'};
    size_t off = 0;
    const uint8_t *s = NULL;
    uint32_t slen = 0;
    TEST_ASSERT_FALSE(bytes.rd_str(P, sizeof(P), &off, &s, &slen));
    TEST_ASSERT_EQUAL_size_t(0, off);
    TEST_ASSERT_NULL(s);
}

// The peer picks the length prefix, and it is a full u32. A bound formed as a sum would wrap where
// size_t is 32 bits and admit the read; the refusal has to come from the space that remains.
void test_rd_str_full_range_length_cannot_wrap_the_bound(void)
{
    static const uint8_t P[8] = {0xFF, 0xFF, 0xFF, 0xFF, 'a', 'b', 'c', 'd'};
    size_t off = 0;
    const uint8_t *s = NULL;
    uint32_t slen = 0;
    TEST_ASSERT_FALSE(bytes.rd_str(P, sizeof(P), &off, &s, &slen));
    TEST_ASSERT_EQUAL_size_t(0, off);
    TEST_ASSERT_NULL(s);

    static const uint8_t Q[5] = {0x7F, 0xFF, 0xFF, 0xFF, 'a'};
    off = 0;
    TEST_ASSERT_FALSE(bytes.rd_str(Q, sizeof(Q), &off, &s, &slen));
    TEST_ASSERT_EQUAL_size_t(0, off);
}

// A blob ending exactly at the end of the payload is accepted, and an empty one is legal: RFC 4251
// sec 5 admits "zero (= empty string) or more bytes".
void test_rd_str_exact_fit_and_empty_string(void)
{
    static const uint8_t P[12] = {0x00, 0x00, 0x00, 0x04, 'a', 'b', 'c', 'd', 0x00, 0x00, 0x00, 0x00};
    size_t off = 0;
    const uint8_t *s = NULL;
    uint32_t slen = 0;

    TEST_ASSERT_TRUE(bytes.rd_str(P, sizeof(P), &off, &s, &slen));
    TEST_ASSERT_EQUAL_UINT32(4u, slen);
    TEST_ASSERT_EQUAL_PTR(P + 4, s);
    TEST_ASSERT_EQUAL_size_t(8, off);

    TEST_ASSERT_TRUE(bytes.rd_str(P, sizeof(P), &off, &s, &slen));
    TEST_ASSERT_EQUAL_UINT32(0u, slen);
    TEST_ASSERT_EQUAL_size_t(12, off);
}
