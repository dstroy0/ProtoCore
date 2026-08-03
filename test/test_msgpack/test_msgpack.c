// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the MessagePack encoder and decoder
// (network_drivers/presentation/codec/msgpack). Expected byte sequences follow the
// MessagePack spec encodings; the decoder tests parse spec-form bytes and
// round-trip the encoder output.

#include "network_drivers/presentation/codec/msgpack/msgpack.h"
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
    pc_span w;
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_uint(&w, 0);
    uint8_t e0[] = {0x00};
    check(b, pc_span_len(w), e0, 1);
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_uint(&w, 127);
    uint8_t e7f[] = {0x7f};
    check(b, pc_span_len(w), e7f, 1);
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_uint(&w, 128);
    uint8_t e80[] = {0xcc, 0x80};
    check(b, pc_span_len(w), e80, 2);
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_uint(&w, 256);
    uint8_t e256[] = {0xcd, 0x01, 0x00};
    check(b, pc_span_len(w), e256, 3);
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_uint(&w, 65536);
    uint8_t e64k[] = {0xce, 0x00, 0x01, 0x00, 0x00};
    check(b, pc_span_len(w), e64k, 5);
}

void test_int()
{
    uint8_t b[16];
    pc_span w;
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_int(&w, -1);
    uint8_t e[] = {0xff};
    check(b, pc_span_len(w), e, 1);
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_int(&w, -32);
    uint8_t e32[] = {0xe0};
    check(b, pc_span_len(w), e32, 1);
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_int(&w, -33);
    uint8_t e33[] = {0xd0, 0xdf};
    check(b, pc_span_len(w), e33, 2);
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_int(&w, -129);
    uint8_t e129[] = {0xd1, 0xff, 0x7f};
    check(b, pc_span_len(w), e129, 3);
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_int(&w, 42); // positive via pc_msgpack_int -> fixint
    uint8_t e42[] = {0x2a};
    check(b, pc_span_len(w), e42, 1);
}

void test_str()
{
    uint8_t b[40];
    pc_span w;
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_str(&w, "");
    uint8_t e[] = {0xa0};
    check(b, pc_span_len(w), e, 1);
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_str(&w, "a");
    uint8_t e2[] = {0xa1, 0x61};
    check(b, pc_span_len(w), e2, 2);
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_str(&w, "hello");
    uint8_t e3[] = {0xa5, 0x68, 0x65, 0x6c, 0x6c, 0x6f};
    check(b, pc_span_len(w), e3, 6);
}

// A NULL string pointer takes the "" branch of pc_msgpack_str's length ternary.
void test_str_null_pointer()
{
    uint8_t b[4];
    pc_span w;
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_str(&w, NULL);
    uint8_t e[] = {0xa0}; // zero-length fixstr, same as pc_msgpack_str(&w, "")
    check(b, pc_span_len(w), e, 1);
}

void test_bytes()
{
    uint8_t b[16];
    pc_span w;
    w = pc_span_from(b, sizeof(b));
    uint8_t data[] = {1, 2, 3};
    pc_msgpack_bytes(&w, data, 3);
    uint8_t e[] = {0xc4, 0x03, 0x01, 0x02, 0x03};
    check(b, pc_span_len(w), e, 5);
}

void test_simple()
{
    uint8_t b[8];
    pc_span w;
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_bool(&w, PROTO_FALSE);
    uint8_t e[] = {0xc2};
    check(b, pc_span_len(w), e, 1);
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_bool(&w, PROTO_TRUE);
    uint8_t e2[] = {0xc3};
    check(b, pc_span_len(w), e2, 1);
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_null(&w);
    uint8_t e3[] = {0xc0};
    check(b, pc_span_len(w), e3, 1);
}

void test_float()
{
    uint8_t b[8];
    pc_span w;
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_float(&w, 1.0f);
    uint8_t e[] = {0xca, 0x3f, 0x80, 0x00, 0x00};
    check(b, pc_span_len(w), e, 5);
}

void test_array_and_map()
{
    uint8_t b[16];
    pc_span w;
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_array(&w, 3);
    pc_msgpack_uint(&w, 1);
    pc_msgpack_uint(&w, 2);
    pc_msgpack_uint(&w, 3);
    uint8_t e[] = {0x93, 0x01, 0x02, 0x03};
    check(b, pc_span_len(w), e, 4);
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_map(&w, 1);
    pc_msgpack_uint(&w, 1);
    pc_msgpack_uint(&w, 2);
    uint8_t e2[] = {0x81, 0x01, 0x02};
    check(b, pc_span_len(w), e2, 3);
}

void test_overflow_fails_closed()
{
    uint8_t b[2];
    pc_span w;
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_uint(&w, 65536); // needs 5 bytes
    TEST_ASSERT_FALSE(pc_span_ok(w));
    TEST_ASSERT_EQUAL_size_t(5, pc_span_len(w));
}

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------

void test_decode_uint()
{
    // positive fixint, uint8, uint16, uint32, uint64
    uint8_t in[] = {0x00, 0x7f, 0xcc, 0x80, 0xcd, 0x01, 0x00, 0xce, 0x00, 0x01, 0x00,
                    0x00, 0xcf, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
    pc_cspan r;
    r = pc_cspan_from(in, sizeof(in));
    uint64_t v;
    TEST_ASSERT_EQUAL(PC_CODEC_UINT, pc_msgpack_peek(&r));
    TEST_ASSERT_TRUE(pc_msgpack_read_uint(&r, &v));
    TEST_ASSERT_EQUAL_UINT64(0, v);
    TEST_ASSERT_TRUE(pc_msgpack_read_uint(&r, &v));
    TEST_ASSERT_EQUAL_UINT64(127, v);
    TEST_ASSERT_TRUE(pc_msgpack_read_uint(&r, &v));
    TEST_ASSERT_EQUAL_UINT64(128, v);
    TEST_ASSERT_TRUE(pc_msgpack_read_uint(&r, &v));
    TEST_ASSERT_EQUAL_UINT64(256, v);
    TEST_ASSERT_TRUE(pc_msgpack_read_uint(&r, &v));
    TEST_ASSERT_EQUAL_UINT64(65536, v);
    TEST_ASSERT_TRUE(pc_msgpack_read_uint(&r, &v));
    TEST_ASSERT_EQUAL_UINT64(0x100000000ULL, v);
    TEST_ASSERT_TRUE(pc_cspan_ok(r));
    TEST_ASSERT_EQUAL(PC_CODEC_INVALID, pc_msgpack_peek(&r)); // exhausted
}

void test_decode_int()
{
    // negative fixint (-1, -32), int8 (-128), int16 (-32768), int32 (-2147483648)
    uint8_t in[] = {0xff, 0xe0, 0xd0, 0x80, 0xd1, 0x80, 0x00, 0xd2, 0x80, 0x00, 0x00, 0x00};
    pc_cspan r;
    r = pc_cspan_from(in, sizeof(in));
    int64_t v;
    TEST_ASSERT_EQUAL(PC_CODEC_INT, pc_msgpack_peek(&r));
    TEST_ASSERT_TRUE(pc_msgpack_read_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(-1, v);
    TEST_ASSERT_TRUE(pc_msgpack_read_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(-32, v);
    TEST_ASSERT_TRUE(pc_msgpack_read_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(-128, v);
    TEST_ASSERT_TRUE(pc_msgpack_read_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(-32768, v);
    TEST_ASSERT_TRUE(pc_msgpack_read_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(-2147483648LL, v);
    TEST_ASSERT_TRUE(pc_cspan_ok(r));
    // read_int also accepts an unsigned encoding
    uint8_t u[] = {0xcc, 0x80};
    r = pc_cspan_from(u, sizeof(u));
    TEST_ASSERT_TRUE(pc_msgpack_read_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(128, v);
}

void test_decode_str_and_bytes()
{
    uint8_t in[] = {0xa3, 'a', 'b', 'c', 0xc4, 0x02, 0xde, 0xad}; // fixstr "abc", bin8 {de ad}
    pc_cspan r;
    r = pc_cspan_from(in, sizeof(in));
    const char *s;
    size_t n;
    TEST_ASSERT_EQUAL(PC_CODEC_STR, pc_msgpack_peek(&r));
    TEST_ASSERT_TRUE(pc_msgpack_read_str(&r, &s, &n));
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_CHAR_ARRAY("abc", s, 3);
    const uint8_t *bin;
    TEST_ASSERT_EQUAL(PC_CODEC_BYTES, pc_msgpack_peek(&r));
    TEST_ASSERT_TRUE(pc_msgpack_read_bytes(&r, &bin, &n));
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL_UINT8(0xde, bin[0]);
    TEST_ASSERT_EQUAL_UINT8(0xad, bin[1]);
}

void test_decode_simple_and_float()
{
    uint8_t in[] = {0xc0, 0xc2, 0xc3, 0xca, 0x3f, 0x80, 0x00, 0x00}; // nil false true float32(1.0)
    pc_cspan r;
    r = pc_cspan_from(in, sizeof(in));
    proto_bool bv;
    float fv;
    TEST_ASSERT_EQUAL(PC_CODEC_NULL, pc_msgpack_peek(&r));
    TEST_ASSERT_TRUE(pc_msgpack_read_null(&r));
    TEST_ASSERT_EQUAL(PC_CODEC_BOOL, pc_msgpack_peek(&r));
    TEST_ASSERT_TRUE(pc_msgpack_read_bool(&r, &bv));
    TEST_ASSERT_FALSE(bv);
    TEST_ASSERT_TRUE(pc_msgpack_read_bool(&r, &bv));
    TEST_ASSERT_TRUE(bv);
    TEST_ASSERT_EQUAL(PC_CODEC_FLOAT, pc_msgpack_peek(&r));
    TEST_ASSERT_TRUE(pc_msgpack_read_float(&r, &fv));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, fv);
    // float64 (0xcb) narrows to float
    uint8_t d[] = {0xcb, 0x40, 0x09, 0x21, 0xfb, 0x54, 0x44, 0x2d, 0x18}; // pi as double
    r = pc_cspan_from(d, sizeof(d));
    TEST_ASSERT_TRUE(pc_msgpack_read_float(&r, &fv));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.14159f, fv);
}

void test_decode_array_and_map()
{
    uint8_t in[] = {0x93, 0x01, 0x02, 0x03, 0x81, 0xa1, 'k', 0x2a}; // [1,2,3] {"k":42}
    pc_cspan r;
    r = pc_cspan_from(in, sizeof(in));
    size_t count;
    TEST_ASSERT_EQUAL(PC_CODEC_ARRAY, pc_msgpack_peek(&r));
    TEST_ASSERT_TRUE(pc_msgpack_read_array(&r, &count));
    TEST_ASSERT_EQUAL_size_t(3, count);
    uint64_t v;
    for (size_t i = 0; i < count; i++)
    {
        TEST_ASSERT_TRUE(pc_msgpack_read_uint(&r, &v));
        TEST_ASSERT_EQUAL_UINT64(i + 1, v);
    }
    TEST_ASSERT_EQUAL(PC_CODEC_MAP, pc_msgpack_peek(&r));
    TEST_ASSERT_TRUE(pc_msgpack_read_map(&r, &count));
    TEST_ASSERT_EQUAL_size_t(1, count);
    const char *s;
    size_t n;
    TEST_ASSERT_TRUE(pc_msgpack_read_str(&r, &s, &n));
    TEST_ASSERT_EQUAL_CHAR_ARRAY("k", s, 1);
    TEST_ASSERT_TRUE(pc_msgpack_read_uint(&r, &v));
    TEST_ASSERT_EQUAL_UINT64(42, v);
}

void test_decode_roundtrip()
{
    // Encode a small document, then decode it back and check each field.
    uint8_t b[64];
    pc_span w;
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_map(&w, 3);
    pc_msgpack_str(&w, "id");
    pc_msgpack_uint(&w, 4242);
    pc_msgpack_str(&w, "t");
    pc_msgpack_float(&w, 21.5f);
    pc_msgpack_str(&w, "ok");
    pc_msgpack_bool(&w, PROTO_TRUE);
    TEST_ASSERT_TRUE(pc_span_ok(w));

    pc_cspan r;
    r = pc_cspan_from(b, pc_span_len(w));
    size_t count;
    TEST_ASSERT_TRUE(pc_msgpack_read_map(&r, &count));
    TEST_ASSERT_EQUAL_size_t(3, count);
    const char *k;
    size_t kn;
    uint64_t uv;
    float fv;
    proto_bool bv;
    TEST_ASSERT_TRUE(pc_msgpack_read_str(&r, &k, &kn));
    TEST_ASSERT_EQUAL_CHAR_ARRAY("id", k, 2);
    TEST_ASSERT_TRUE(pc_msgpack_read_uint(&r, &uv));
    TEST_ASSERT_EQUAL_UINT64(4242, uv);
    TEST_ASSERT_TRUE(pc_msgpack_read_str(&r, &k, &kn));
    TEST_ASSERT_EQUAL_CHAR_ARRAY("t", k, 1);
    TEST_ASSERT_TRUE(pc_msgpack_read_float(&r, &fv));
    TEST_ASSERT_EQUAL_FLOAT(21.5f, fv);
    TEST_ASSERT_TRUE(pc_msgpack_read_str(&r, &k, &kn));
    TEST_ASSERT_EQUAL_CHAR_ARRAY("ok", k, 2);
    TEST_ASSERT_TRUE(pc_msgpack_read_bool(&r, &bv));
    TEST_ASSERT_TRUE(bv);
    TEST_ASSERT_TRUE(pc_cspan_ok(r));
}

void test_decode_fails_closed()
{
    pc_cspan r;
    // truncated uint16 (header says read 2 more bytes, only 1 present)
    uint8_t t1[] = {0xcd, 0x01};
    r = pc_cspan_from(t1, sizeof(t1));
    uint64_t v;
    TEST_ASSERT_FALSE(pc_msgpack_read_uint(&r, &v));
    TEST_ASSERT_FALSE(pc_cspan_ok(r));
    // truncated str (len 5, only 2 bytes present)
    uint8_t t2[] = {0xa5, 'h', 'i'};
    r = pc_cspan_from(t2, sizeof(t2));
    const char *s;
    size_t n;
    TEST_ASSERT_FALSE(pc_msgpack_read_str(&r, &s, &n));
    TEST_ASSERT_FALSE(pc_cspan_ok(r));
    // type mismatch: read_uint on a bool
    uint8_t t3[] = {0xc3};
    r = pc_cspan_from(t3, sizeof(t3));
    TEST_ASSERT_FALSE(pc_msgpack_read_uint(&r, &v));
    TEST_ASSERT_FALSE(pc_cspan_ok(r));
    // unsupported byte (0xc1) peeks INVALID and any read fails
    uint8_t t4[] = {0xc1};
    r = pc_cspan_from(t4, sizeof(t4));
    TEST_ASSERT_EQUAL(PC_CODEC_INVALID, pc_msgpack_peek(&r));
    TEST_ASSERT_FALSE(pc_msgpack_read_int(&r, (int64_t *)&v));
    // empty buffer
    r = pc_cspan_from(t4, 0);
    TEST_ASSERT_EQUAL(PC_CODEC_INVALID, pc_msgpack_peek(&r));
    TEST_ASSERT_FALSE(pc_msgpack_read_null(&r));
}

// Round-trip every wide encoding so both the encoder width branches (u64, i32,
// i64, str8/str16, bin16, array16, map16, str_n) and the matching decoder paths run.
void test_wide_roundtrip()
{
    static uint8_t b[2048];
    pc_span w;
    pc_cspan r;
    uint64_t uv;
    int64_t iv;
    size_t n, cnt;
    const char *sp;
    const uint8_t *bp;

    w = pc_span_from(b, sizeof(b));
    pc_msgpack_uint(&w, 0x123456789ULL); // uint64 (0xcf)
    TEST_ASSERT_EQUAL_UINT8(0xcf, b[0]);
    r = pc_cspan_from(b, pc_span_len(w));
    TEST_ASSERT_TRUE(pc_msgpack_read_uint(&r, &uv));
    TEST_ASSERT_EQUAL_UINT64(0x123456789ULL, uv);

    w = pc_span_from(b, sizeof(b));
    pc_msgpack_int(&w, -70000); // int32 (0xd2)
    TEST_ASSERT_EQUAL_UINT8(0xd2, b[0]);
    r = pc_cspan_from(b, pc_span_len(w));
    TEST_ASSERT_TRUE(pc_msgpack_read_int(&r, &iv));
    TEST_ASSERT_EQUAL_INT64(-70000, iv);

    w = pc_span_from(b, sizeof(b));
    pc_msgpack_int(&w, -5000000000LL); // int64 (0xd3)
    TEST_ASSERT_EQUAL_UINT8(0xd3, b[0]);
    r = pc_cspan_from(b, pc_span_len(w));
    TEST_ASSERT_TRUE(pc_msgpack_read_int(&r, &iv));
    TEST_ASSERT_EQUAL_INT64(-5000000000LL, iv);

    w = pc_span_from(b, sizeof(b));
    pc_msgpack_int(&w, 5000000000LL); // positive crossing into a wide int
    r = pc_cspan_from(b, pc_span_len(w));
    TEST_ASSERT_TRUE(pc_msgpack_read_int(&r, &iv));
    TEST_ASSERT_EQUAL_INT64(5000000000LL, iv);

    char s40[41];
    for (int i = 0; i < 40; i++)
    {
        s40[i] = 'x';
    }
    s40[40] = '\0';
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_str(&w, s40); // str8 (0xd9)
    TEST_ASSERT_EQUAL_UINT8(0xd9, b[0]);
    r = pc_cspan_from(b, pc_span_len(w));
    TEST_ASSERT_TRUE(pc_msgpack_read_str(&r, &sp, &n));
    TEST_ASSERT_EQUAL_size_t(40, n);

    char s300[301];
    for (int i = 0; i < 300; i++)
    {
        s300[i] = 'y';
    }
    s300[300] = '\0';
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_str(&w, s300); // str16 (0xda)
    TEST_ASSERT_EQUAL_UINT8(0xda, b[0]);
    r = pc_cspan_from(b, pc_span_len(w));
    TEST_ASSERT_TRUE(pc_msgpack_read_str(&r, &sp, &n));
    TEST_ASSERT_EQUAL_size_t(300, n);

    w = pc_span_from(b, sizeof(b));
    pc_msgpack_str_n(&w, "hi", 2); // explicit-length fixstr
    r = pc_cspan_from(b, pc_span_len(w));
    TEST_ASSERT_TRUE(pc_msgpack_read_str(&r, &sp, &n));
    TEST_ASSERT_EQUAL_size_t(2, n);

    static uint8_t big[300];
    for (int i = 0; i < 300; i++)
    {
        big[i] = (uint8_t)i;
    }
    w = pc_span_from(b, sizeof(b));
    pc_msgpack_bytes(&w, big, 300); // bin16 (0xc5)
    TEST_ASSERT_EQUAL_UINT8(0xc5, b[0]);
    r = pc_cspan_from(b, pc_span_len(w));
    TEST_ASSERT_TRUE(pc_msgpack_read_bytes(&r, &bp, &n));
    TEST_ASSERT_EQUAL_size_t(300, n);

    w = pc_span_from(b, sizeof(b));
    pc_msgpack_array(&w, 20); // array16 (0xdc)
    for (int i = 0; i < 20; i++)
    {
        pc_msgpack_uint(&w, (uint64_t)i);
    }
    TEST_ASSERT_EQUAL_UINT8(0xdc, b[0]);
    r = pc_cspan_from(b, pc_span_len(w));
    TEST_ASSERT_TRUE(pc_msgpack_read_array(&r, &cnt));
    TEST_ASSERT_EQUAL_size_t(20, cnt);
    for (int i = 0; i < 20; i++)
    {
        TEST_ASSERT_TRUE(pc_msgpack_read_uint(&r, &uv));
        TEST_ASSERT_EQUAL_UINT64((uint64_t)i, uv);
    }

    w = pc_span_from(b, sizeof(b));
    pc_msgpack_map(&w, 20); // map16 (0xde)
    for (int i = 0; i < 20; i++)
    {
        pc_msgpack_uint(&w, (uint64_t)i);
        pc_msgpack_uint(&w, (uint64_t)i);
    }
    TEST_ASSERT_EQUAL_UINT8(0xde, b[0]);
    r = pc_cspan_from(b, pc_span_len(w));
    TEST_ASSERT_TRUE(pc_msgpack_read_map(&r, &cnt));
    TEST_ASSERT_EQUAL_size_t(20, cnt);
}

// Wide-type decoder error paths: truncated str16/bin16/array16 headers + bodies.
void test_decode_wide_fails_closed()
{
    pc_cspan r;
    const char *s;
    const uint8_t *bp;
    size_t n;
    // str16 header claims 300 bytes, body absent
    uint8_t t1[] = {0xda, 0x01, 0x2c, 'a'};
    r = pc_cspan_from(t1, sizeof(t1));
    TEST_ASSERT_FALSE(pc_msgpack_read_str(&r, &s, &n));
    TEST_ASSERT_FALSE(pc_cspan_ok(r));
    // bin16 truncated header (only one length byte)
    uint8_t t2[] = {0xc5, 0x01};
    r = pc_cspan_from(t2, sizeof(t2));
    TEST_ASSERT_FALSE(pc_msgpack_read_bytes(&r, &bp, &n));
    // array16 header truncated
    uint8_t t3[] = {0xdc, 0x00};
    r = pc_cspan_from(t3, sizeof(t3));
    size_t cnt;
    TEST_ASSERT_FALSE(pc_msgpack_read_array(&r, &cnt));
    // uint64 truncated
    uint8_t t4[] = {0xcf, 0x00, 0x00};
    r = pc_cspan_from(t4, sizeof(t4));
    uint64_t uv;
    TEST_ASSERT_FALSE(pc_msgpack_read_uint(&r, &uv));
}

// str32/bin32/array32/map32 headers (len/count > 0xffff): the widest encoder branch.
void test_encode_wide32()
{
    static uint8_t out[0x10000 + 8];
    static uint8_t data[0x10000]; // 65536 bytes -> forces the 32-bit length form
    pc_span w;

    w = pc_span_from(out, sizeof(out));
    pc_msgpack_str_n(&w, (const char *)data, 0x10000); // str32 (0xdb)
    TEST_ASSERT_TRUE(pc_span_ok(w));
    TEST_ASSERT_EQUAL_UINT8(0xdb, out[0]);
    TEST_ASSERT_EQUAL_size_t(5 + 0x10000, pc_span_len(w));
    // decode it back: exercises read_blob's 32-bit (f32) length branch
    pc_cspan r;
    r = pc_cspan_from(out, pc_span_len(w));
    const char *sp;
    size_t n;
    TEST_ASSERT_TRUE(pc_msgpack_read_str(&r, &sp, &n));
    TEST_ASSERT_EQUAL_size_t(0x10000, n);

    w = pc_span_from(out, sizeof(out));
    pc_msgpack_bytes(&w, data, 0x10000); // bin32 (0xc6)
    TEST_ASSERT_EQUAL_UINT8(0xc6, out[0]);

    uint8_t hdr[8];
    w = pc_span_from(hdr, sizeof(hdr));
    pc_msgpack_array(&w, 0x10000); // array32 (0xdd)
    TEST_ASSERT_EQUAL_UINT8(0xdd, hdr[0]);
    TEST_ASSERT_EQUAL_size_t(5, pc_span_len(w));

    w = pc_span_from(hdr, sizeof(hdr));
    pc_msgpack_map(&w, 0x10000); // map32 (0xdf)
    TEST_ASSERT_EQUAL_UINT8(0xdf, hdr[0]);
    TEST_ASSERT_EQUAL_size_t(5, pc_span_len(w));
}

static void peek_is(uint8_t byte, pc_codec_type want)
{
    pc_cspan r;
    r = pc_cspan_from(&byte, 1);
    TEST_ASSERT_EQUAL(want, pc_msgpack_peek(&r));
}

// peek reports the right type for every wide (multi-byte) format marker.
void test_peek_wide_types()
{
    peek_is(0xcc, PC_CODEC_UINT);
    peek_is(0xcd, PC_CODEC_UINT);
    peek_is(0xce, PC_CODEC_UINT);
    peek_is(0xcf, PC_CODEC_UINT);
    peek_is(0xd0, PC_CODEC_INT);
    peek_is(0xd1, PC_CODEC_INT);
    peek_is(0xd2, PC_CODEC_INT);
    peek_is(0xd3, PC_CODEC_INT);
    peek_is(0xd9, PC_CODEC_STR);
    peek_is(0xda, PC_CODEC_STR);
    peek_is(0xdb, PC_CODEC_STR);
    peek_is(0xdc, PC_CODEC_ARRAY);
    peek_is(0xdd, PC_CODEC_ARRAY);
    peek_is(0xde, PC_CODEC_MAP);
    peek_is(0xdf, PC_CODEC_MAP);
}

// read_int accepts a positive fixint and every unsigned/signed width.
void test_read_int_all_widths()
{
    pc_cspan r;
    int64_t v;
    uint8_t fixp[] = {0x05}; // positive fixint via read_int
    r = pc_cspan_from(fixp, sizeof(fixp));
    TEST_ASSERT_TRUE(pc_msgpack_read_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(5, v);
    uint8_t u16[] = {0xcd, 0x01, 0x00}; // uint16 via read_int
    r = pc_cspan_from(u16, sizeof(u16));
    TEST_ASSERT_TRUE(pc_msgpack_read_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(256, v);
    uint8_t u32[] = {0xce, 0x00, 0x01, 0x00, 0x00}; // uint32 via read_int
    r = pc_cspan_from(u32, sizeof(u32));
    TEST_ASSERT_TRUE(pc_msgpack_read_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(65536, v);
    uint8_t u64[] = {0xcf, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00}; // uint64 via read_int
    r = pc_cspan_from(u64, sizeof(u64));
    TEST_ASSERT_TRUE(pc_msgpack_read_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64(0x100000000LL, v);
    uint8_t i64[] = {0xd3, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00}; // int64
    r = pc_cspan_from(i64, sizeof(i64));
    TEST_ASSERT_TRUE(pc_msgpack_read_int(&r, &v));
    TEST_ASSERT_EQUAL_INT64((int64_t)0xffffffff00000000ULL, v);
}

// Every reader on a 0-length (exhausted) buffer sets the sticky error and returns false.
void test_read_on_empty_reader()
{
    pc_cspan r;
    uint8_t dummy = 0;
    uint64_t uv;
    int64_t iv;
    proto_bool bv;
    float fv;
    const char *s;
    const uint8_t *bp;
    size_t n, c;
    r = pc_cspan_from(&dummy, 0);
    TEST_ASSERT_FALSE(pc_msgpack_read_uint(&r, &uv));
    r = pc_cspan_from(&dummy, 0);
    TEST_ASSERT_FALSE(pc_msgpack_read_int(&r, &iv));
    r = pc_cspan_from(&dummy, 0);
    TEST_ASSERT_FALSE(pc_msgpack_read_bool(&r, &bv));
    r = pc_cspan_from(&dummy, 0);
    TEST_ASSERT_FALSE(pc_msgpack_read_float(&r, &fv));
    r = pc_cspan_from(&dummy, 0);
    TEST_ASSERT_FALSE(pc_msgpack_read_str(&r, &s, &n));
    r = pc_cspan_from(&dummy, 0);
    TEST_ASSERT_FALSE(pc_msgpack_read_bytes(&r, &bp, &n));
    r = pc_cspan_from(&dummy, 0);
    TEST_ASSERT_FALSE(pc_msgpack_read_array(&r, &c));
    r = pc_cspan_from(&dummy, 0);
    TEST_ASSERT_FALSE(pc_msgpack_read_map(&r, &c));
}

// A typed reader on a byte of the wrong family fails closed (default/else branches).
void test_read_wrong_type_byte()
{
    pc_cspan r;
    uint8_t nilb = 0xc0; // nil: not a bool/float/str/bin/array/map/int
    proto_bool bv;
    float fv;
    const char *s;
    const uint8_t *bp;
    size_t n, c;
    r = pc_cspan_from(&nilb, 1);
    TEST_ASSERT_FALSE(pc_msgpack_read_bool(&r, &bv));
    r = pc_cspan_from(&nilb, 1);
    TEST_ASSERT_FALSE(pc_msgpack_read_float(&r, &fv));
    r = pc_cspan_from(&nilb, 1);
    TEST_ASSERT_FALSE(pc_msgpack_read_str(&r, &s, &n));
    r = pc_cspan_from(&nilb, 1);
    TEST_ASSERT_FALSE(pc_msgpack_read_bytes(&r, &bp, &n));
    r = pc_cspan_from(&nilb, 1);
    TEST_ASSERT_FALSE(pc_msgpack_read_array(&r, &c));
    r = pc_cspan_from(&nilb, 1);
    TEST_ASSERT_FALSE(pc_msgpack_read_map(&r, &c));
    int64_t iv;
    r = pc_cspan_from(&nilb, 1);
    TEST_ASSERT_FALSE(pc_msgpack_read_int(&r, &iv)); // switch default
}

// Each width's argument bytes are truncated: take_be fails and the read returns false.
void test_read_truncated_widths()
{
    pc_cspan r;
    uint64_t uv;
    int64_t iv;
    float fv;
    const char *s;
    size_t n, c;
    uint8_t u8[] = {0xcc};
    r = pc_cspan_from(u8, sizeof(u8)); // uint8 arg missing
    TEST_ASSERT_FALSE(pc_msgpack_read_uint(&r, &uv));
    uint8_t u32[] = {0xce, 0x00, 0x00};
    r = pc_cspan_from(u32, sizeof(u32)); // uint32 short
    TEST_ASSERT_FALSE(pc_msgpack_read_uint(&r, &uv));
    uint8_t iu8[] = {0xcc};
    r = pc_cspan_from(iu8, sizeof(iu8));
    TEST_ASSERT_FALSE(pc_msgpack_read_int(&r, &iv));
    uint8_t iu16[] = {0xcd, 0x00};
    r = pc_cspan_from(iu16, sizeof(iu16));
    TEST_ASSERT_FALSE(pc_msgpack_read_int(&r, &iv));
    uint8_t iu32[] = {0xce, 0x00, 0x00};
    r = pc_cspan_from(iu32, sizeof(iu32));
    TEST_ASSERT_FALSE(pc_msgpack_read_int(&r, &iv));
    uint8_t iu64[] = {0xcf, 0x00};
    r = pc_cspan_from(iu64, sizeof(iu64));
    TEST_ASSERT_FALSE(pc_msgpack_read_int(&r, &iv));
    uint8_t i8[] = {0xd0};
    r = pc_cspan_from(i8, sizeof(i8));
    TEST_ASSERT_FALSE(pc_msgpack_read_int(&r, &iv));
    uint8_t i16[] = {0xd1, 0x00};
    r = pc_cspan_from(i16, sizeof(i16));
    TEST_ASSERT_FALSE(pc_msgpack_read_int(&r, &iv));
    uint8_t i32[] = {0xd2, 0x00, 0x00};
    r = pc_cspan_from(i32, sizeof(i32));
    TEST_ASSERT_FALSE(pc_msgpack_read_int(&r, &iv));
    uint8_t i64[] = {0xd3, 0x00, 0x00};
    r = pc_cspan_from(i64, sizeof(i64));
    TEST_ASSERT_FALSE(pc_msgpack_read_int(&r, &iv));
    uint8_t f32[] = {0xca, 0x00, 0x00};
    r = pc_cspan_from(f32, sizeof(f32)); // float32 short
    TEST_ASSERT_FALSE(pc_msgpack_read_float(&r, &fv));
    uint8_t f64[] = {0xcb, 0x00, 0x00};
    r = pc_cspan_from(f64, sizeof(f64)); // float64 short
    TEST_ASSERT_FALSE(pc_msgpack_read_float(&r, &fv));
    uint8_t s8[] = {0xd9};
    r = pc_cspan_from(s8, sizeof(s8)); // str8 length missing
    TEST_ASSERT_FALSE(pc_msgpack_read_str(&r, &s, &n));
    uint8_t s32[] = {0xdb, 0x00, 0x00};
    r = pc_cspan_from(s32, sizeof(s32)); // str32 length short
    TEST_ASSERT_FALSE(pc_msgpack_read_str(&r, &s, &n));
    uint8_t a32[] = {0xdd, 0x00, 0x00};
    r = pc_cspan_from(a32, sizeof(a32)); // array32 count short
    TEST_ASSERT_FALSE(pc_msgpack_read_array(&r, &c));
    uint8_t m32[] = {0xdf, 0x00, 0x00};
    r = pc_cspan_from(m32, sizeof(m32)); // map32 count short
    TEST_ASSERT_FALSE(pc_msgpack_read_map(&r, &c));
}

// read_nil's byte check fails closed when a value is present but isn't 0xc0
// (distinct from the empty-buffer / sticky-error branches covered elsewhere).
void test_read_nil_wrong_byte()
{
    pc_cspan r;
    uint8_t b = 0xc3; // bool `true`, not nil
    r = pc_cspan_from(&b, 1);
    TEST_ASSERT_FALSE(pc_msgpack_read_null(&r));
    TEST_ASSERT_FALSE(pc_cspan_ok(r));
}

// Once r->err is sticky, every reader entrypoint must fail closed on the
// `r->err ||` branch itself, without ever reaching the pos/byte checks that
// follow it. A truncated uint8 (0xcc with no argument byte) sets err with pos
// still short of len, so the follow-on calls hit err==true, not pos>=len.
void test_reads_after_sticky_error()
{
    uint8_t truncated[] = {0xcc}; // uint8 marker, argument byte missing
    pc_cspan r;
    r = pc_cspan_from(truncated, sizeof(truncated));
    uint64_t uv;
    TEST_ASSERT_FALSE(pc_msgpack_read_uint(&r, &uv)); // sets r->err = true
    TEST_ASSERT_FALSE(pc_cspan_ok(r));

    int64_t iv;
    proto_bool bv;
    float fv;
    const char *sp;
    const uint8_t *bp;
    size_t n, c;
    TEST_ASSERT_EQUAL(PC_CODEC_INVALID, pc_msgpack_peek(&r));
    TEST_ASSERT_FALSE(pc_msgpack_read_uint(&r, &uv));
    TEST_ASSERT_FALSE(pc_msgpack_read_int(&r, &iv));
    TEST_ASSERT_FALSE(pc_msgpack_read_bool(&r, &bv));
    TEST_ASSERT_FALSE(pc_msgpack_read_null(&r));
    TEST_ASSERT_FALSE(pc_msgpack_read_float(&r, &fv));
    TEST_ASSERT_FALSE(pc_msgpack_read_str(&r, &sp, &n));
    TEST_ASSERT_FALSE(pc_msgpack_read_bytes(&r, &bp, &n));
    TEST_ASSERT_FALSE(pc_msgpack_read_array(&r, &c));
    TEST_ASSERT_FALSE(pc_msgpack_read_map(&r, &c));
}

// read_blob's fixstr check (want_str && b>=0xa0 && b<=0xbf) needs a byte below
// the fixstr lower bound with want_str true to exercise the "b>=0xa0" false leg;
// every other test in this file uses a byte >= 0xa0.
void test_read_str_below_fixstr_range()
{
    pc_cspan r;
    uint8_t b = 0x05; // positive fixint: valid byte, but not any str-family marker
    r = pc_cspan_from(&b, 1);
    const char *s;
    size_t n;
    TEST_ASSERT_FALSE(pc_msgpack_read_str(&r, &s, &n));
}

// read_count's fix-range check (b>=fix_lo && b<=fix_hi) needs a byte below
// fix_lo to exercise the "b>=fix_lo" false leg; every other test in this file
// uses a byte at or above the fixarray/fixmap lower bound.
void test_read_array_below_fixarray_range()
{
    pc_cspan r;
    uint8_t b = 0x05; // positive fixint: below fixarray's 0x90 lower bound
    r = pc_cspan_from(&b, 1);
    size_t c;
    TEST_ASSERT_FALSE(pc_msgpack_read_array(&r, &c));
}

// read_count's f32 (array32/map32) header take_be call has only been exercised
// on its failure leg elsewhere (truncated headers); decode valid array32/map32
// headers here so the take_be-succeeds leg runs too.
void test_read_count_wide32_success()
{
    pc_cspan r;
    uint8_t arr32[] = {0xdd, 0x00, 0x00, 0x00, 0x02}; // array32 header, count = 2
    r = pc_cspan_from(arr32, sizeof(arr32));
    size_t c;
    TEST_ASSERT_TRUE(pc_msgpack_read_array(&r, &c));
    TEST_ASSERT_EQUAL_size_t(2, c);

    uint8_t map32[] = {0xdf, 0x00, 0x00, 0x00, 0x03}; // map32 header, count = 3
    r = pc_cspan_from(map32, sizeof(map32));
    TEST_ASSERT_TRUE(pc_msgpack_read_map(&r, &c));
    TEST_ASSERT_EQUAL_size_t(3, c);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_encode_wide32);
    RUN_TEST(test_peek_wide_types);
    RUN_TEST(test_read_int_all_widths);
    RUN_TEST(test_read_on_empty_reader);
    RUN_TEST(test_read_wrong_type_byte);
    RUN_TEST(test_read_truncated_widths);
    RUN_TEST(test_uint);
    RUN_TEST(test_wide_roundtrip);
    RUN_TEST(test_decode_wide_fails_closed);
    RUN_TEST(test_int);
    RUN_TEST(test_str);
    RUN_TEST(test_str_null_pointer);
    RUN_TEST(test_bytes);
    RUN_TEST(test_simple);
    RUN_TEST(test_float);
    RUN_TEST(test_array_and_map);
    RUN_TEST(test_overflow_fails_closed);
    RUN_TEST(test_decode_uint);
    RUN_TEST(test_decode_int);
    RUN_TEST(test_decode_str_and_bytes);
    RUN_TEST(test_decode_simple_and_float);
    RUN_TEST(test_decode_array_and_map);
    RUN_TEST(test_decode_roundtrip);
    RUN_TEST(test_decode_fails_closed);
    RUN_TEST(test_read_nil_wrong_byte);
    RUN_TEST(test_reads_after_sticky_error);
    RUN_TEST(test_read_str_below_fixstr_range);
    RUN_TEST(test_read_array_below_fixarray_range);
    RUN_TEST(test_read_count_wide32_success);
    return UNITY_END();
}
