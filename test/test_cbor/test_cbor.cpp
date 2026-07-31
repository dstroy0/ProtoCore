// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the CBOR encoder (network_drivers/presentation/codec/cbor). Expected
// byte sequences are the canonical RFC 8949 Appendix A diagnostic vectors.

#include "network_drivers/presentation/codec/cbor/cbor.h"
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

static void check(const uint8_t *got, size_t got_len, const uint8_t *exp, size_t exp_len)
{
    TEST_ASSERT_EQUAL_size_t(exp_len, got_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, got, exp_len);
}

void test_uint()
{
    uint8_t b[16];
    CborWriter w;
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_uint(&w, 0);
    TEST_ASSERT_TRUE(pc_cbor_ok(&w));
    uint8_t e0[] = {0x00};
    check(b, pc_cbor_len(&w), e0, 1);
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_uint(&w, 23);
    uint8_t e23[] = {0x17};
    check(b, pc_cbor_len(&w), e23, 1);
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_uint(&w, 24);
    uint8_t e24[] = {0x18, 0x18};
    check(b, pc_cbor_len(&w), e24, 2);
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_uint(&w, 1000);
    uint8_t e1k[] = {0x19, 0x03, 0xe8};
    check(b, pc_cbor_len(&w), e1k, 3);
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_uint(&w, 1000000);
    uint8_t e1m[] = {0x1a, 0x00, 0x0f, 0x42, 0x40};
    check(b, pc_cbor_len(&w), e1m, 5);
}

void test_int()
{
    uint8_t b[16];
    CborWriter w;
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_int(&w, -1);
    uint8_t e[] = {0x20};
    check(b, pc_cbor_len(&w), e, 1);
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_int(&w, -100);
    uint8_t e2[] = {0x38, 0x63};
    check(b, pc_cbor_len(&w), e2, 2);
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_int(&w, -1000);
    uint8_t e3[] = {0x39, 0x03, 0xe7};
    check(b, pc_cbor_len(&w), e3, 3);
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_int(&w, 42); // positive routed through pc_cbor_int
    uint8_t e4[] = {0x18, 0x2a};
    check(b, pc_cbor_len(&w), e4, 2);
}

void test_text()
{
    uint8_t b[16];
    CborWriter w;
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_text(&w, "");
    uint8_t e[] = {0x60};
    check(b, pc_cbor_len(&w), e, 1);
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_text(&w, "a");
    uint8_t e2[] = {0x61, 0x61};
    check(b, pc_cbor_len(&w), e2, 2);
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_text(&w, "IETF");
    uint8_t e3[] = {0x64, 0x49, 0x45, 0x54, 0x46};
    check(b, pc_cbor_len(&w), e3, 5);
}

void test_bytes()
{
    uint8_t b[16];
    CborWriter w;
    pc_cbor_init(&w, b, sizeof(b));
    uint8_t data[] = {1, 2, 3, 4};
    pc_cbor_bytes(&w, data, 4);
    uint8_t e[] = {0x44, 0x01, 0x02, 0x03, 0x04};
    check(b, pc_cbor_len(&w), e, 5);
}

void test_simple()
{
    uint8_t b[8];
    CborWriter w;
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_bool(&w, false);
    uint8_t e[] = {0xf4};
    check(b, pc_cbor_len(&w), e, 1);
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_bool(&w, true);
    uint8_t e2[] = {0xf5};
    check(b, pc_cbor_len(&w), e2, 1);
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_null(&w);
    uint8_t e3[] = {0xf6};
    check(b, pc_cbor_len(&w), e3, 1);
}

void test_float()
{
    uint8_t b[8];
    CborWriter w;
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_float(&w, 1.0f);
    uint8_t e[] = {0xfa, 0x3f, 0x80, 0x00, 0x00};
    check(b, pc_cbor_len(&w), e, 5);
}

void test_array_and_map()
{
    uint8_t b[16];
    CborWriter w;
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_array(&w, 0);
    uint8_t e[] = {0x80};
    check(b, pc_cbor_len(&w), e, 1);
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_array(&w, 3);
    pc_cbor_uint(&w, 1);
    pc_cbor_uint(&w, 2);
    pc_cbor_uint(&w, 3);
    uint8_t e2[] = {0x83, 0x01, 0x02, 0x03};
    check(b, pc_cbor_len(&w), e2, 4);
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_map(&w, 2);
    pc_cbor_uint(&w, 1);
    pc_cbor_uint(&w, 2);
    pc_cbor_uint(&w, 3);
    pc_cbor_uint(&w, 4);
    uint8_t e3[] = {0xa2, 0x01, 0x02, 0x03, 0x04};
    check(b, pc_cbor_len(&w), e3, 5);
}

// A write that does not fit sets overflow but still reports the needed size.
void test_overflow_fails_closed()
{
    uint8_t b[2];
    CborWriter w;
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_uint(&w, 1000000); // needs 5 bytes
    TEST_ASSERT_FALSE(pc_cbor_ok(&w));
    TEST_ASSERT_EQUAL_size_t(5, pc_cbor_len(&w));
}

// ---- decoder ----

void test_decode_uint()
{
    uint8_t buf[16];
    CborWriter w;
    pc_cbor_init(&w, buf, sizeof(buf));
    pc_cbor_uint(&w, 1000);
    CborReader r;
    pc_cbor_reader_init(&r, buf, pc_cbor_len(&w));
    TEST_ASSERT_EQUAL_INT(CborType::CBOR_TYPE_UINT, pc_cbor_peek(&r));
    uint64_t v;
    TEST_ASSERT_TRUE(pc_cbor_read_uint(&r, &v));
    TEST_ASSERT_EQUAL_UINT64(1000, v);
    TEST_ASSERT_TRUE(pc_cbor_reader_ok(&r));
}

void test_decode_int()
{
    uint8_t buf[16];
    CborWriter w;
    pc_cbor_init(&w, buf, sizeof(buf));
    pc_cbor_int(&w, -100);
    CborReader r;
    pc_cbor_reader_init(&r, buf, pc_cbor_len(&w));
    int64_t v;
    TEST_ASSERT_TRUE(pc_cbor_read_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(-100, v);
}

void test_decode_float_roundtrip()
{
    uint8_t buf[8];
    CborWriter w;
    pc_cbor_init(&w, buf, sizeof(buf));
    pc_cbor_float(&w, 3.5f);
    CborReader r;
    pc_cbor_reader_init(&r, buf, pc_cbor_len(&w));
    float f;
    TEST_ASSERT_TRUE(pc_cbor_read_float(&r, &f));
    TEST_ASSERT_EQUAL_FLOAT(3.5f, f);
}

// Round-trip a whole map: {"heap":42000,"name":"esp","on":true}.
void test_decode_roundtrip_map()
{
    uint8_t buf[64];
    CborWriter w;
    pc_cbor_init(&w, buf, sizeof(buf));
    pc_cbor_map(&w, 3);
    pc_cbor_text(&w, "heap");
    pc_cbor_uint(&w, 42000);
    pc_cbor_text(&w, "name");
    pc_cbor_text(&w, "esp");
    pc_cbor_text(&w, "on");
    pc_cbor_bool(&w, true);

    CborReader r;
    pc_cbor_reader_init(&r, buf, pc_cbor_len(&w));
    size_t n;
    TEST_ASSERT_TRUE(pc_cbor_read_map(&r, &n));
    TEST_ASSERT_EQUAL_size_t(3, n);
    const char *k;
    size_t kl;
    uint64_t u;
    const char *s;
    size_t sl;
    bool b;
    TEST_ASSERT_TRUE(pc_cbor_read_text(&r, &k, &kl));
    TEST_ASSERT_EQUAL_MEMORY("heap", k, 4);
    TEST_ASSERT_TRUE(pc_cbor_read_uint(&r, &u));
    TEST_ASSERT_EQUAL_UINT64(42000, u);
    TEST_ASSERT_TRUE(pc_cbor_read_text(&r, &k, &kl));
    TEST_ASSERT_EQUAL_MEMORY("name", k, 4);
    TEST_ASSERT_TRUE(pc_cbor_read_text(&r, &s, &sl));
    TEST_ASSERT_EQUAL_size_t(3, sl);
    TEST_ASSERT_EQUAL_MEMORY("esp", s, 3);
    TEST_ASSERT_TRUE(pc_cbor_read_text(&r, &k, &kl));
    TEST_ASSERT_EQUAL_MEMORY("on", k, 2);
    TEST_ASSERT_TRUE(pc_cbor_read_bool(&r, &b));
    TEST_ASSERT_TRUE(b);
    TEST_ASSERT_TRUE(pc_cbor_reader_ok(&r));
    TEST_ASSERT_EQUAL_INT(CborType::CBOR_TYPE_INVALID, pc_cbor_peek(&r)); // everything consumed
}

// A null string pointer takes the ternary's false branch (0 length) instead of
// strnlen(), still emitting a valid empty text item.
void test_cbor_text_null_ptr()
{
    uint8_t b[4];
    CborWriter w;
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_text(&w, nullptr);
    TEST_ASSERT_TRUE(pc_cbor_ok(&w));
    uint8_t e[] = {0x60};
    check(b, pc_cbor_len(&w), e, 1);
}

// Once err is sticky-set by a failed read, every decoder entry point (which all
// funnel through read_head, or check r->err directly) must fail closed on the
// r->err branch alone, without re-examining pos/len or the payload.
void test_cbor_reader_sticky_err_repeat()
{
    CborReader r;
    uint64_t uv;
    int64_t iv;
    bool bv;
    float fv;
    size_t c, sl;
    const char *s;
    const uint8_t *bp;

    uint8_t rsv[] = {0x1c}; // reserved additional-info -> sets err on first read
    pc_cbor_reader_init(&r, rsv, sizeof(rsv));
    TEST_ASSERT_FALSE(pc_cbor_read_uint(&r, &uv));
    TEST_ASSERT_FALSE(pc_cbor_reader_ok(&r));

    // r is now sticky-erred; every further call must fail closed immediately.
    TEST_ASSERT_FALSE(pc_cbor_read_uint(&r, &uv));
    TEST_ASSERT_FALSE(pc_cbor_read_int(&r, &iv));
    TEST_ASSERT_FALSE(pc_cbor_read_bool(&r, &bv));
    TEST_ASSERT_FALSE(pc_cbor_read_null(&r));
    TEST_ASSERT_FALSE(pc_cbor_read_float(&r, &fv));
    TEST_ASSERT_FALSE(pc_cbor_read_text(&r, &s, &sl));
    TEST_ASSERT_FALSE(pc_cbor_read_bytes(&r, &bp, &sl));
    TEST_ASSERT_FALSE(pc_cbor_read_array(&r, &c));
    TEST_ASSERT_FALSE(pc_cbor_read_map(&r, &c));
    TEST_ASSERT_EQUAL_INT(CborType::CBOR_TYPE_INVALID, pc_cbor_peek(&r));
}

// pc_cbor_peek branches not hit by test_peek_each_type: an empty buffer
// (pos>=len with no prior error), bool false (info==20, only true/info==21 was
// tested), and a raw double marker (0xfb, info==27, only float32/info==26 was
// tested).
void test_peek_edge_cases()
{
    CborReader r;
    uint8_t d = 0;
    pc_cbor_reader_init(&r, &d, 0);
    TEST_ASSERT_EQUAL_INT(CborType::CBOR_TYPE_INVALID, pc_cbor_peek(&r));

    uint8_t bf[] = {0xf4}; // bool false -> info == 20
    pc_cbor_reader_init(&r, bf, sizeof(bf));
    TEST_ASSERT_EQUAL_INT(CborType::CBOR_TYPE_BOOL, pc_cbor_peek(&r));

    uint8_t dbl[] = {0xfb, 0, 0, 0, 0, 0, 0, 0, 0}; // double marker -> info == 27
    pc_cbor_reader_init(&r, dbl, sizeof(dbl));
    TEST_ASSERT_EQUAL_INT(CborType::CBOR_TYPE_FLOAT, pc_cbor_peek(&r));
}

// read_str: the major type matches but the declared length overruns the
// buffer -- distinct from the wrong-major-type branch already covered by
// test_cbor_decode_more_types.
void test_cbor_read_str_length_overrun()
{
    CborReader r;
    const char *s;
    size_t sl;
    const uint8_t *bp;

    uint8_t txt[] = {0x65, 'h', 'e', 'l', 'l'}; // text(5) but only 4 bytes follow
    pc_cbor_reader_init(&r, txt, sizeof(txt));
    TEST_ASSERT_FALSE(pc_cbor_read_text(&r, &s, &sl));
    TEST_ASSERT_FALSE(pc_cbor_reader_ok(&r));

    uint8_t byt[] = {0x45, 1, 2, 3}; // bytes(5) but only 3 bytes follow
    pc_cbor_reader_init(&r, byt, sizeof(byt));
    TEST_ASSERT_FALSE(pc_cbor_read_bytes(&r, &bp, &sl));
    TEST_ASSERT_FALSE(pc_cbor_reader_ok(&r));
}

// A buffer shorter than the encoded item fails closed.
void test_decode_truncated()
{
    uint8_t buf[8];
    CborWriter w;
    pc_cbor_init(&w, buf, sizeof(buf));
    pc_cbor_uint(&w, 1000000); // 5 bytes
    CborReader r;
    pc_cbor_reader_init(&r, buf, 3); // only 3 bytes visible
    uint64_t v;
    TEST_ASSERT_FALSE(pc_cbor_read_uint(&r, &v));
    TEST_ASSERT_FALSE(pc_cbor_reader_ok(&r));
}

// Reading the wrong type sets the error flag.
void test_decode_type_mismatch()
{
    uint8_t buf[8];
    CborWriter w;
    pc_cbor_init(&w, buf, sizeof(buf));
    pc_cbor_text(&w, "x");
    CborReader r;
    pc_cbor_reader_init(&r, buf, pc_cbor_len(&w));
    uint64_t v;
    TEST_ASSERT_FALSE(pc_cbor_read_uint(&r, &v));
    TEST_ASSERT_FALSE(pc_cbor_reader_ok(&r));
}

// pc_cbor_peek classifies every major type, and reports INVALID for tags / unassigned.
void test_peek_each_type()
{
    uint8_t b[16];
    CborWriter w;
    CborReader r;
    uint8_t d[2] = {1, 2};

    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_int(&w, -5);
    pc_cbor_reader_init(&r, b, pc_cbor_len(&w));
    TEST_ASSERT_EQUAL_INT(CborType::CBOR_TYPE_INT, pc_cbor_peek(&r));
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_bytes(&w, d, 2);
    pc_cbor_reader_init(&r, b, pc_cbor_len(&w));
    TEST_ASSERT_EQUAL_INT(CborType::CBOR_TYPE_BYTES, pc_cbor_peek(&r));
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_text(&w, "x");
    pc_cbor_reader_init(&r, b, pc_cbor_len(&w));
    TEST_ASSERT_EQUAL_INT(CborType::CBOR_TYPE_TEXT, pc_cbor_peek(&r));
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_array(&w, 1);
    pc_cbor_reader_init(&r, b, pc_cbor_len(&w));
    TEST_ASSERT_EQUAL_INT(CborType::CBOR_TYPE_ARRAY, pc_cbor_peek(&r));
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_map(&w, 1);
    pc_cbor_reader_init(&r, b, pc_cbor_len(&w));
    TEST_ASSERT_EQUAL_INT(CborType::CBOR_TYPE_MAP, pc_cbor_peek(&r));
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_bool(&w, true);
    pc_cbor_reader_init(&r, b, pc_cbor_len(&w));
    TEST_ASSERT_EQUAL_INT(CborType::CBOR_TYPE_BOOL, pc_cbor_peek(&r));
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_null(&w);
    pc_cbor_reader_init(&r, b, pc_cbor_len(&w));
    TEST_ASSERT_EQUAL_INT(CborType::CBOR_TYPE_NULL, pc_cbor_peek(&r));
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_float(&w, 1.5f);
    pc_cbor_reader_init(&r, b, pc_cbor_len(&w));
    TEST_ASSERT_EQUAL_INT(CborType::CBOR_TYPE_FLOAT, pc_cbor_peek(&r));

    const uint8_t tag[] = {0xc0}; // major 6 (tag) is unsupported
    pc_cbor_reader_init(&r, tag, 1);
    TEST_ASSERT_EQUAL_INT(CborType::CBOR_TYPE_INVALID, pc_cbor_peek(&r));
    const uint8_t simple[] = {0xe0}; // major 7, unassigned simple value
    pc_cbor_reader_init(&r, simple, 1);
    TEST_ASSERT_EQUAL_INT(CborType::CBOR_TYPE_INVALID, pc_cbor_peek(&r));
}

// A uint above 0xFFFFFFFF uses the 8-byte (0x1b) head and round-trips.
void test_uint_8byte()
{
    uint8_t b[16];
    CborWriter w;
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_uint(&w, 0x123456789ULL);
    TEST_ASSERT_TRUE(pc_cbor_ok(&w));
    TEST_ASSERT_EQUAL_size_t(9, pc_cbor_len(&w));
    TEST_ASSERT_EQUAL_HEX8(0x1b, b[0]);
    CborReader r;
    pc_cbor_reader_init(&r, b, pc_cbor_len(&w));
    uint64_t v;
    TEST_ASSERT_TRUE(pc_cbor_read_uint(&r, &v));
    TEST_ASSERT_EQUAL_UINT64(0x123456789ULL, v);
}

// A double-encoded (0xfb) float is read back narrowed to float.
void test_read_double_encoded_float()
{
    const uint8_t dbl[] = {0xfb, 0x40, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // 2.5
    CborReader r;
    pc_cbor_reader_init(&r, dbl, sizeof(dbl));
    float f;
    TEST_ASSERT_TRUE(pc_cbor_read_float(&r, &f));
    TEST_ASSERT_EQUAL_FLOAT(2.5f, f);
}

// pc_cbor_read_map on a non-map sets the error flag.
void test_read_map_type_mismatch()
{
    uint8_t b[8];
    CborWriter w;
    pc_cbor_init(&w, b, sizeof(b));
    pc_cbor_uint(&w, 5);
    CborReader r;
    pc_cbor_reader_init(&r, b, pc_cbor_len(&w));
    size_t n;
    TEST_ASSERT_FALSE(pc_cbor_read_map(&r, &n));
    TEST_ASSERT_FALSE(pc_cbor_reader_ok(&r));
}

// Decoder happy paths + wrong-type rejections not covered elsewhere: read_int
// (positive / wrong major), read_bool (false / non-bool), read_null (valid /
// non-null), read_float (double / non-float), read_bytes, read_array (valid / wrong).
void test_cbor_decode_more_types()
{
    CborReader r;
    int64_t iv;
    bool bv;
    float fv;
    const uint8_t *bp;
    const char *s;
    size_t n, sl, c;

    uint8_t p[] = {0x05}; // positive int via read_int (major 0)
    pc_cbor_reader_init(&r, p, sizeof(p));
    TEST_ASSERT_TRUE(pc_cbor_read_int(&r, &iv));
    TEST_ASSERT_EQUAL_INT64(5, iv);
    uint8_t wt[] = {0x40}; // empty byte string (major 2) -> not an int
    pc_cbor_reader_init(&r, wt, sizeof(wt));
    TEST_ASSERT_FALSE(pc_cbor_read_int(&r, &iv));

    uint8_t bf[] = {0xf4}; // false
    pc_cbor_reader_init(&r, bf, sizeof(bf));
    TEST_ASSERT_TRUE(pc_cbor_read_bool(&r, &bv));
    TEST_ASSERT_FALSE(bv);
    uint8_t bn[] = {0xf6}; // null -> not a bool
    pc_cbor_reader_init(&r, bn, sizeof(bn));
    TEST_ASSERT_FALSE(pc_cbor_read_bool(&r, &bv));

    uint8_t nz[] = {0xf6}; // null
    pc_cbor_reader_init(&r, nz, sizeof(nz));
    TEST_ASSERT_TRUE(pc_cbor_read_null(&r));
    uint8_t nt[] = {0xf5}; // true -> not null
    pc_cbor_reader_init(&r, nt, sizeof(nt));
    TEST_ASSERT_FALSE(pc_cbor_read_null(&r));

    uint8_t fbad[] = {0xf4}; // bool -> not a float
    pc_cbor_reader_init(&r, fbad, sizeof(fbad));
    TEST_ASSERT_FALSE(pc_cbor_read_float(&r, &fv));

    uint8_t by[] = {0x42, 0xde, 0xad}; // byte string of 2
    pc_cbor_reader_init(&r, by, sizeof(by));
    TEST_ASSERT_TRUE(pc_cbor_read_bytes(&r, &bp, &n));
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL_UINT8(0xde, bp[0]);

    uint8_t ar[] = {0x83}; // array(3)
    pc_cbor_reader_init(&r, ar, sizeof(ar));
    TEST_ASSERT_TRUE(pc_cbor_read_array(&r, &c));
    TEST_ASSERT_EQUAL_size_t(3, c);
    uint8_t aw[] = {0x00}; // uint -> not an array
    pc_cbor_reader_init(&r, aw, sizeof(aw));
    TEST_ASSERT_FALSE(pc_cbor_read_array(&r, &c));

    uint8_t tw[] = {0x00}; // uint -> not text
    pc_cbor_reader_init(&r, tw, sizeof(tw));
    TEST_ASSERT_FALSE(pc_cbor_read_text(&r, &s, &sl));
}

// read_head rejects reserved additional-info (28-31) and truncated float args.
void test_cbor_head_reserved_and_trunc()
{
    CborReader r;
    uint64_t v;
    float fv;
    uint8_t rsv[] = {0x1c}; // major 0, additional-info 28 (reserved / indefinite)
    pc_cbor_reader_init(&r, rsv, sizeof(rsv));
    TEST_ASSERT_FALSE(pc_cbor_read_uint(&r, &v));
    TEST_ASSERT_FALSE(pc_cbor_reader_ok(&r));
    uint8_t f4[] = {0xfa, 0x00, 0x00}; // float32 arg short
    pc_cbor_reader_init(&r, f4, sizeof(f4));
    TEST_ASSERT_FALSE(pc_cbor_read_float(&r, &fv));
    uint8_t f8[] = {0xfb, 0x00, 0x00}; // float64 arg short
    pc_cbor_reader_init(&r, f8, sizeof(f8));
    TEST_ASSERT_FALSE(pc_cbor_read_float(&r, &fv));
}

// Every reader on a 0-length buffer fails closed.
void test_cbor_read_empty()
{
    CborReader r;
    uint8_t d = 0;
    uint64_t uv;
    int64_t iv;
    bool bv;
    float fv;
    const char *s;
    const uint8_t *bp;
    size_t n, c;
    pc_cbor_reader_init(&r, &d, 0);
    TEST_ASSERT_FALSE(pc_cbor_read_uint(&r, &uv));
    pc_cbor_reader_init(&r, &d, 0);
    TEST_ASSERT_FALSE(pc_cbor_read_int(&r, &iv));
    pc_cbor_reader_init(&r, &d, 0);
    TEST_ASSERT_FALSE(pc_cbor_read_bool(&r, &bv));
    pc_cbor_reader_init(&r, &d, 0);
    TEST_ASSERT_FALSE(pc_cbor_read_null(&r));
    pc_cbor_reader_init(&r, &d, 0);
    TEST_ASSERT_FALSE(pc_cbor_read_float(&r, &fv));
    pc_cbor_reader_init(&r, &d, 0);
    TEST_ASSERT_FALSE(pc_cbor_read_text(&r, &s, &n));
    pc_cbor_reader_init(&r, &d, 0);
    TEST_ASSERT_FALSE(pc_cbor_read_bytes(&r, &bp, &n));
    pc_cbor_reader_init(&r, &d, 0);
    TEST_ASSERT_FALSE(pc_cbor_read_array(&r, &c));
    pc_cbor_reader_init(&r, &d, 0);
    TEST_ASSERT_FALSE(pc_cbor_read_map(&r, &c));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_cbor_decode_more_types);
    RUN_TEST(test_cbor_head_reserved_and_trunc);
    RUN_TEST(test_cbor_read_empty);
    RUN_TEST(test_uint);
    RUN_TEST(test_peek_each_type);
    RUN_TEST(test_uint_8byte);
    RUN_TEST(test_read_double_encoded_float);
    RUN_TEST(test_read_map_type_mismatch);
    RUN_TEST(test_int);
    RUN_TEST(test_text);
    RUN_TEST(test_bytes);
    RUN_TEST(test_simple);
    RUN_TEST(test_float);
    RUN_TEST(test_array_and_map);
    RUN_TEST(test_overflow_fails_closed);
    RUN_TEST(test_cbor_text_null_ptr);
    RUN_TEST(test_cbor_reader_sticky_err_repeat);
    RUN_TEST(test_peek_edge_cases);
    RUN_TEST(test_cbor_read_str_length_overrun);
    RUN_TEST(test_decode_uint);
    RUN_TEST(test_decode_int);
    RUN_TEST(test_decode_float_roundtrip);
    RUN_TEST(test_decode_roundtrip_map);
    RUN_TEST(test_decode_truncated);
    RUN_TEST(test_decode_type_mismatch);
    return UNITY_END();
}
