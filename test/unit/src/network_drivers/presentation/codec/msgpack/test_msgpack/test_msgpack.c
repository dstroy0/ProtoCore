// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the MessagePack codec (network_drivers/presentation/codec/msgpack/msgpack.h).
//
// The MessagePack specification (github.com/msgpack/msgpack spec.md) publishes one table that fixes
// this whole format: "format name | first byte (in binary) | first byte (in hex)". Every expected
// byte below is read off that table and the per-family diagrams beside it, never off this encoder.
//
// test_spec_first_byte_table is the load-bearing case: it walks each family across the exact value
// where the spec's range ends, so a boundary written one off - 0x7f/0x80 for fixint, 31/32 for
// fixstr, 15/16 for fixarray - changes the first byte and the case fails.

#include "network_drivers/presentation/codec/msgpack/msgpack.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static uint8_t g_buf[128];

// Encode through @p put and check the bytes the spec assigns.
#define ENC(call, want)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        protocore_span w_ = span.from(g_buf, sizeof(g_buf));                                                           \
        call;                                                                                                          \
        TEST_ASSERT_TRUE(span.ok(w_));                                                                                  \
        TEST_ASSERT_EQUAL_UINT(sizeof(want) - 1, span.len(w_));                                                        \
        TEST_ASSERT_EQUAL_MEMORY(want, g_buf, sizeof(want) - 1);                                                       \
    } while (0)

// Bind a reader over a literal byte string, whose length is its size minus the C terminator.
#define RD(lit) span.cfrom((const uint8_t *)(lit), sizeof(lit) - 1)

// The spec's overview table, family by family, at each range boundary it states.
//
//   positive fixint 0x00-0x7f        uint 8 0xcc   uint 16 0xcd   uint 32 0xce   uint 64 0xcf
//   negative fixint 0xe0-0xff        int 8  0xd0   int 16  0xd1   int 32  0xd2   int 64  0xd3
//   fixstr 0xa0-0xbf                 str 8  0xd9   str 16  0xda   str 32  0xdb
//   bin 8 0xc4   bin 16 0xc5         fixarray 0x90-0x9f   array 16 0xdc
//   fixmap 0x80-0x8f                 map 16 0xde          nil 0xc0   false 0xc2   true 0xc3
void test_spec_first_byte_table(void)
{
    // positive fixint stores a 7-bit unsigned integer, so 0x7f is its last value
    ENC(MsgPack.put_uint(&w_, 0), "\x00");
    ENC(MsgPack.put_uint(&w_, 127), "\x7f");
    ENC(MsgPack.put_uint(&w_, 128), "\xcc\x80");
    ENC(MsgPack.put_uint(&w_, 255), "\xcc\xff");
    // uint 16/32/64 store their argument big-endian
    ENC(MsgPack.put_uint(&w_, 256), "\xcd\x01\x00");
    ENC(MsgPack.put_uint(&w_, 65535), "\xcd\xff\xff");
    ENC(MsgPack.put_uint(&w_, 65536), "\xce\x00\x01\x00\x00");
    ENC(MsgPack.put_uint(&w_, 4294967295ULL), "\xce\xff\xff\xff\xff");
    ENC(MsgPack.put_uint(&w_, 4294967296ULL), "\xcf\x00\x00\x00\x01\x00\x00\x00\x00");

    // negative fixint 111YYYYY is an 8-bit signed integer, so 0xe0 is -32 and 0xff is -1
    ENC(MsgPack.put_int(&w_, -1), "\xff");
    ENC(MsgPack.put_int(&w_, -32), "\xe0");
    ENC(MsgPack.put_int(&w_, -33), "\xd0\xdf");   // -33 as an int 8 is 0x100 - 33 = 0xdf
    ENC(MsgPack.put_int(&w_, -128), "\xd0\x80");  // int 8 low bound
    ENC(MsgPack.put_int(&w_, -129), "\xd1\xff\x7f");
    ENC(MsgPack.put_int(&w_, -32768), "\xd1\x80\x00");
    ENC(MsgPack.put_int(&w_, -32769), "\xd2\xff\xff\x7f\xff");
    ENC(MsgPack.put_int(&w_, -2147483648LL), "\xd2\x80\x00\x00\x00");
    ENC(MsgPack.put_int(&w_, -2147483649LL), "\xd3\xff\xff\xff\xff\x7f\xff\xff\xff");
    // a non-negative int takes the uint family, which is shorter for the same value
    ENC(MsgPack.put_int(&w_, 1), "\x01");

    // fixstr 101XXXXX carries a 5-bit length, so 31 is its last
    ENC(MsgPack.put_str(&w_, ""), "\xa0");
    ENC(MsgPack.put_str(&w_, "a"), "\xa1\x61");
    ENC(MsgPack.put_str_n(&w_, "0123456789012345678901234567890", 31),
        "\xbf" "0123456789012345678901234567890");
    ENC(MsgPack.put_str_n(&w_, "01234567890123456789012345678901", 32),
        "\xd9\x20" "01234567890123456789012345678901");

    // bin 8 carries an 8-bit length; bin has no fix form
    ENC(MsgPack.put_bytes(&w_, (const uint8_t *)"", 0), "\xc4\x00");
    ENC(MsgPack.put_bytes(&w_, (const uint8_t *)"\x01\x02", 2), "\xc4\x02\x01\x02");

    // fixarray 1001XXXX and fixmap 1000XXXX carry a 4-bit count, so 15 is the last
    ENC(MsgPack.put_array(&w_, 0), "\x90");
    ENC(MsgPack.put_array(&w_, 15), "\x9f");
    ENC(MsgPack.put_array(&w_, 16), "\xdc\x00\x10");
    ENC(MsgPack.put_map(&w_, 0), "\x80");
    ENC(MsgPack.put_map(&w_, 15), "\x8f");
    ENC(MsgPack.put_map(&w_, 16), "\xde\x00\x10");

    ENC(MsgPack.put_null(&w_), "\xc0");
    ENC(MsgPack.put_bool(&w_, PROTO_FALSE), "\xc2");
    ENC(MsgPack.put_bool(&w_, PROTO_TRUE), "\xc3");
}

// float 32 stores an IEEE 754 single, big-endian, behind 0xca.
//
//   1.0f  = sign 0, biased exponent 127 = 0x7f, mantissa 0
//         -> 0 01111111 000...  = 0x3F800000
//   -2.0f = sign 1, biased exponent 128 = 0x80, mantissa 0
//         -> 1 10000000 000...  = 0xC0000000
//   0.0f  = all bits clear      = 0x00000000
void test_spec_float32(void)
{
    ENC(MsgPack.put_float(&w_, 1.0f), "\xca\x3f\x80\x00\x00");
    ENC(MsgPack.put_float(&w_, -2.0f), "\xca\xc0\x00\x00\x00");
    ENC(MsgPack.put_float(&w_, 0.0f), "\xca\x00\x00\x00\x00");
}

// put_label is the encoding's choice of spelling for a map key. The binary packs carry the numeric
// form, so it lands as an int and the name is not written.
void test_label_is_the_numeric_spelling(void)
{
    ENC(MsgPack.put_label(&w_, "bn", -2), "\xfe"); // -2 as a negative fixint is 0x100 - 2
}

// peek names the item at the cursor without moving it, per the spec's "first byte" table.
void test_peek_maps_first_byte_to_type(void)
{
    struct
    {
        uint8_t b;
        protocore_codec_type t;
    } static const CASES[] = {
        {0x00, PROTOCORE_CODEC_UINT},  {0x7f, PROTOCORE_CODEC_UINT},  {0x80, PROTOCORE_CODEC_MAP},
        {0x8f, PROTOCORE_CODEC_MAP},   {0x90, PROTOCORE_CODEC_ARRAY}, {0x9f, PROTOCORE_CODEC_ARRAY},
        {0xa0, PROTOCORE_CODEC_STR},   {0xbf, PROTOCORE_CODEC_STR},   {0xc0, PROTOCORE_CODEC_NULL},
        {0xc2, PROTOCORE_CODEC_BOOL},  {0xc3, PROTOCORE_CODEC_BOOL},  {0xc4, PROTOCORE_CODEC_BYTES},
        {0xc5, PROTOCORE_CODEC_BYTES}, {0xc6, PROTOCORE_CODEC_BYTES}, {0xca, PROTOCORE_CODEC_FLOAT},
        {0xcb, PROTOCORE_CODEC_FLOAT}, {0xcc, PROTOCORE_CODEC_UINT},  {0xcf, PROTOCORE_CODEC_UINT},
        {0xd0, PROTOCORE_CODEC_INT},   {0xd3, PROTOCORE_CODEC_INT},   {0xd9, PROTOCORE_CODEC_STR},
        {0xdb, PROTOCORE_CODEC_STR},   {0xdc, PROTOCORE_CODEC_ARRAY}, {0xdd, PROTOCORE_CODEC_ARRAY},
        {0xde, PROTOCORE_CODEC_MAP},   {0xdf, PROTOCORE_CODEC_MAP},   {0xe0, PROTOCORE_CODEC_INT},
        {0xff, PROTOCORE_CODEC_INT},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        uint8_t one = CASES[i].b;
        protocore_cspan r = span.cfrom(&one, 1);
        TEST_ASSERT_EQUAL_INT(CASES[i].t, MsgPack.peek(&r));
        TEST_ASSERT_EQUAL_UINT(0u, r.pos); // peek does not consume
    }
}

// 0xc1 is listed "(never used)" and 0xc7-0xc9 / 0xd4-0xd8 are the ext family this codec does not
// carry. All of them report INVALID rather than being read as something else.
void test_never_used_and_ext_are_invalid(void)
{
    static const uint8_t BAD[] = {0xc1, 0xc7, 0xc8, 0xc9, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8};
    for (size_t i = 0; i < sizeof(BAD); i++)
    {
        uint8_t one = BAD[i];
        protocore_cspan r = span.cfrom(&one, 1);
        TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_INVALID, MsgPack.peek(&r));
    }
    // an empty region has nothing to name either
    protocore_cspan empty = span.cfrom(NULL, 0);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_INVALID, MsgPack.peek(&empty));
}

// Each integer width decodes to the value the spec's diagram stores in it.
void test_decode_int_widths(void)
{
    uint64_t u = 0;
    int64_t s = 0;
    protocore_cspan r;

    r = RD("\x7f");
    TEST_ASSERT_TRUE(MsgPack.get_uint(&r, &u));
    TEST_ASSERT_EQUAL_UINT64(127u, u);
    r = RD("\xcc\x80");
    TEST_ASSERT_TRUE(MsgPack.get_uint(&r, &u));
    TEST_ASSERT_EQUAL_UINT64(128u, u);
    r = RD("\xcd\x01\x00");
    TEST_ASSERT_TRUE(MsgPack.get_uint(&r, &u));
    TEST_ASSERT_EQUAL_UINT64(256u, u);
    r = RD("\xce\x00\x01\x00\x00");
    TEST_ASSERT_TRUE(MsgPack.get_uint(&r, &u));
    TEST_ASSERT_EQUAL_UINT64(65536u, u);
    r = RD("\xcf\x00\x00\x00\x01\x00\x00\x00\x00");
    TEST_ASSERT_TRUE(MsgPack.get_uint(&r, &u));
    TEST_ASSERT_EQUAL_UINT64(4294967296ULL, u);

    r = RD("\xff");
    TEST_ASSERT_TRUE(MsgPack.get_int(&r, &s));
    TEST_ASSERT_EQUAL_INT64(-1, s);
    r = RD("\xe0");
    TEST_ASSERT_TRUE(MsgPack.get_int(&r, &s));
    TEST_ASSERT_EQUAL_INT64(-32, s);
    r = RD("\xd0\x80");
    TEST_ASSERT_TRUE(MsgPack.get_int(&r, &s));
    TEST_ASSERT_EQUAL_INT64(-128, s);
    r = RD("\xd1\x80\x00");
    TEST_ASSERT_TRUE(MsgPack.get_int(&r, &s));
    TEST_ASSERT_EQUAL_INT64(-32768, s);
    r = RD("\xd2\x80\x00\x00\x00");
    TEST_ASSERT_TRUE(MsgPack.get_int(&r, &s));
    TEST_ASSERT_EQUAL_INT64(-2147483648LL, s);
    r = RD("\xd3\xff\xff\xff\xff\x7f\xff\xff\xff");
    TEST_ASSERT_TRUE(MsgPack.get_int(&r, &s));
    TEST_ASSERT_EQUAL_INT64(-2147483649LL, s);
    // the int reader also accepts the uint family, since those values are integers too
    r = RD("\xcd\x01\x00");
    TEST_ASSERT_TRUE(MsgPack.get_int(&r, &s));
    TEST_ASSERT_EQUAL_INT64(256, s);
}

// str and bin decode to a view into the source region: the pointer is inside the input, not a copy.
void test_decode_str_and_bin_alias_the_input(void)
{
    static const uint8_t IN[] = {0xa3, 'a', 'b', 'c', 0xc4, 0x02, 0xde, 0xad};
    protocore_cspan r = span.cfrom(IN, sizeof(IN));
    const char *s = NULL;
    size_t n = 0;
    TEST_ASSERT_TRUE(MsgPack.get_str(&r, &s, &n));
    TEST_ASSERT_EQUAL_UINT(3u, n);
    TEST_ASSERT_EQUAL_PTR(&IN[1], s);
    TEST_ASSERT_EQUAL_MEMORY("abc", s, 3);

    const uint8_t *b = NULL;
    TEST_ASSERT_TRUE(MsgPack.get_bytes(&r, &b, &n));
    TEST_ASSERT_EQUAL_UINT(2u, n);
    TEST_ASSERT_EQUAL_PTR(&IN[6], b);
    TEST_ASSERT_EQUAL_UINT(sizeof(IN), r.pos);
}

// str and bin are separate families: a str header is not read as bin, and the other way round.
void test_str_and_bin_families_do_not_cross(void)
{
    const uint8_t *b = NULL;
    const char *s = NULL;
    size_t n = 0;
    protocore_cspan r = RD("\xa1\x61"); // fixstr
    TEST_ASSERT_FALSE(MsgPack.get_bytes(&r, &b, &n));
    TEST_ASSERT_FALSE(span.cok(r));

    protocore_cspan r2 = RD("\xc4\x01\x61"); // bin 8
    TEST_ASSERT_FALSE(MsgPack.get_str(&r2, &s, &n));
    TEST_ASSERT_FALSE(span.cok(r2));
}

// nil, the two booleans and float 32 decode to what their one byte stands for.
void test_decode_nil_bool_float(void)
{
    proto_bool b = PROTO_TRUE;
    float f = 0.0f;
    protocore_cspan r = RD("\xc0");
    TEST_ASSERT_TRUE(MsgPack.get_null(&r));

    r = RD("\xc2");
    TEST_ASSERT_TRUE(MsgPack.get_bool(&r, &b));
    TEST_ASSERT_FALSE(b);
    r = RD("\xc3");
    TEST_ASSERT_TRUE(MsgPack.get_bool(&r, &b));
    TEST_ASSERT_TRUE(b);

    r = RD("\xca\x3f\x80\x00\x00");
    TEST_ASSERT_TRUE(MsgPack.get_float(&r, &f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, f);
    // float 64: 1.5 is sign 0, biased exponent 1023 = 0x3ff, mantissa 0x8000000000000
    r = RD("\xcb\x3f\xf8\x00\x00\x00\x00\x00\x00");
    TEST_ASSERT_TRUE(MsgPack.get_float(&r, &f));
    TEST_ASSERT_EQUAL_FLOAT(1.5f, f);
}

// A map of two members, written header-then-items the way the spec defines a definite-length map,
// reads back item for item.
void test_map_round_trip(void)
{
    protocore_span w = span.from(g_buf, sizeof(g_buf));
    MsgPack.put_map(&w, 2);
    MsgPack.put_str(&w, "id");
    MsgPack.put_uint(&w, 4294967295ULL);
    MsgPack.put_str(&w, "on");
    MsgPack.put_bool(&w, PROTO_TRUE);
    TEST_ASSERT_TRUE(span.ok(w));

    protocore_cspan r = span.produced(w);
    size_t count = 0;
    const char *k = NULL;
    size_t klen = 0;
    uint64_t v = 0;
    proto_bool on = PROTO_FALSE;

    TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_MAP, MsgPack.peek(&r));
    TEST_ASSERT_TRUE(MsgPack.get_map(&r, &count));
    TEST_ASSERT_EQUAL_UINT(2u, count);
    TEST_ASSERT_TRUE(MsgPack.get_str(&r, &k, &klen));
    TEST_ASSERT_EQUAL_MEMORY("id", k, 2);
    TEST_ASSERT_TRUE(MsgPack.get_uint(&r, &v));
    TEST_ASSERT_EQUAL_UINT64(4294967295ULL, v);
    TEST_ASSERT_TRUE(MsgPack.get_str(&r, &k, &klen));
    TEST_ASSERT_EQUAL_MEMORY("on", k, 2);
    TEST_ASSERT_TRUE(MsgPack.get_bool(&r, &on));
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_TRUE(span.cok(r));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_INVALID, MsgPack.peek(&r)); // nothing left
}

// An array header of 16 or more takes the array 16 form, and its count survives the round trip.
void test_array_16_round_trip(void)
{
    protocore_span w = span.from(g_buf, sizeof(g_buf));
    MsgPack.put_array(&w, 16);
    for (uint64_t i = 0; i < 16; i++)
    {
        MsgPack.put_uint(&w, i);
    }
    TEST_ASSERT_TRUE(span.ok(w));
    TEST_ASSERT_EQUAL_UINT(3u + 16u, span.len(w)); // array 16 header is 3 bytes, each item a fixint

    protocore_cspan r = span.produced(w);
    size_t count = 0;
    TEST_ASSERT_TRUE(MsgPack.get_array(&r, &count));
    TEST_ASSERT_EQUAL_UINT(16u, count);
    for (uint64_t i = 0; i < 16; i++)
    {
        uint64_t v = 0;
        TEST_ASSERT_TRUE(MsgPack.get_uint(&r, &v));
        TEST_ASSERT_EQUAL_UINT64(i, v);
    }
}

// A header whose payload runs past the end of the region is refused, not read out of bounds.
void test_truncated_input_fails_closed(void)
{
    const char *s = NULL;
    const uint8_t *b = NULL;
    size_t n = 0;
    uint64_t u = 0;
    size_t count = 0;

    protocore_cspan r = RD("\xa3\x61"); // fixstr of 3 with 1 byte present
    TEST_ASSERT_FALSE(MsgPack.get_str(&r, &s, &n));
    TEST_ASSERT_FALSE(span.cok(r));

    r = RD("\xc4\x04\x00"); // bin 8 of 4 with 1 byte present
    TEST_ASSERT_FALSE(MsgPack.get_bytes(&r, &b, &n));

    r = RD("\xcd\x01"); // uint 16 missing its second argument byte
    TEST_ASSERT_FALSE(MsgPack.get_uint(&r, &u));

    r = RD("\xdc\x00"); // array 16 missing a count byte
    TEST_ASSERT_FALSE(MsgPack.get_array(&r, &count));

    r = RD("\xd9"); // str 8 with no length byte
    TEST_ASSERT_FALSE(MsgPack.get_str(&r, &s, &n));
}

// The error is sticky: once a read has run off the end, later reads report failure instead of
// resuming somewhere in the middle of an item.
void test_error_is_sticky(void)
{
    static const uint8_t IN[] = {0xcd, 0x01, 0x00}; // a valid uint 16 follows nothing
    protocore_cspan r = span.cfrom(IN, 1);          // but the region ends after the format byte
    uint64_t u = 0;
    TEST_ASSERT_FALSE(MsgPack.get_uint(&r, &u));
    TEST_ASSERT_FALSE(span.cok(r));

    proto_bool b = PROTO_FALSE;
    TEST_ASSERT_FALSE(MsgPack.get_bool(&r, &b));
    TEST_ASSERT_TRUE(MsgPack.peek(&r) == PROTOCORE_CODEC_INVALID);
}

// A write that does not fit latches overflow and keeps counting, so span.len reports the capacity the
// buffer should have had rather than only that it failed.
void test_overflow_reports_the_size_needed(void)
{
    uint8_t small[4];
    protocore_span w = span.from(small, sizeof(small));
    MsgPack.put_str_n(&w, "abcdefghij", 10); // 1 header byte + 10 data bytes
    TEST_ASSERT_FALSE(span.ok(w));
    TEST_ASSERT_TRUE(span.has_storage(w));
    TEST_ASSERT_EQUAL_UINT(11u, span.len(w));
    // produced() yields nothing once the region overflowed
    protocore_cspan r = span.produced(w);
    TEST_ASSERT_EQUAL_UINT(0u, r.len);
}

// A null source string encodes as the empty string rather than reading through the pointer.
void test_null_string_is_empty(void)
{
    ENC(MsgPack.put_str(&w_, NULL), "\xa0");
}
