// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the CBOR codec (network_drivers/presentation/codec/cbor/cbor.h).
//
// test_rfc8949_appendix_a_vectors is the load-bearing case: RFC 8949 Appendix A Table 6 prints a
// diagnostic value beside its encoding in hex, and every expected byte string below is copied from
// that table rather than from what this encoder happens to emit. It covers each of the five head
// forms of sec 3 (immediate, 1, 2, 4 and 8 argument octets), both integer majors, byte and text
// strings, arrays, maps and the three simple values.
//
// This encoder writes every float in the sec 3.3 single-precision form (0xfa) and the decoder reads
// single and double precision, so only Table 6's 0xfa rows are in scope here; the half-precision
// rows are not. Indefinite-length items (sec 3.2) and tags (major 6) are likewise outside what this
// codec carries, and the cases below check that it refuses them rather than misreading them.

#include "network_drivers/presentation/codec/cbor/cbor.h"
#include <string.h>

#include <unity.h>

static uint8_t g_buf[64];
static protocore_span g_w;

void setUp(void)
{
    memset(g_buf, 0, sizeof(g_buf));
    g_w = span.from(g_buf, sizeof(g_buf));
}

void tearDown(void)
{
}

// Everything written so far must equal @p exp, with nothing over the bound.
static void expect(const uint8_t *exp, size_t n)
{
    TEST_ASSERT_TRUE(span.ok(g_w));
    TEST_ASSERT_EQUAL_size_t(n, span.len(g_w));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, g_buf, n);
}

// A read-only view over the bytes just encoded.
static protocore_cspan produced(void)
{
    return span.cfrom(g_buf, span.len(g_w));
}

// RFC 8949 Appendix A Table 6, transcribed. Each expected array is the "Encoded" column for the
// "Diagnostic" value named in the comment.
void test_rfc8949_appendix_a_vectors(void)
{
    // 0 | 0x00
    static const uint8_t V_0[] = {0x00};
    Cbor.put_uint(&g_w, 0);
    expect(V_0, sizeof(V_0));

    // 23 | 0x17  (the largest immediate argument, sec 3)
    static const uint8_t V_23[] = {0x17};
    span.reset(&g_w);
    Cbor.put_uint(&g_w, 23);
    expect(V_23, sizeof(V_23));

    // 24 | 0x1818  (the first value needing a 1-octet argument)
    static const uint8_t V_24[] = {0x18, 0x18};
    span.reset(&g_w);
    Cbor.put_uint(&g_w, 24);
    expect(V_24, sizeof(V_24));

    // 100 | 0x1864
    static const uint8_t V_100[] = {0x18, 0x64};
    span.reset(&g_w);
    Cbor.put_uint(&g_w, 100);
    expect(V_100, sizeof(V_100));

    // 1000 | 0x1903e8
    static const uint8_t V_1000[] = {0x19, 0x03, 0xe8};
    span.reset(&g_w);
    Cbor.put_uint(&g_w, 1000);
    expect(V_1000, sizeof(V_1000));

    // 1000000 | 0x1a000f4240
    static const uint8_t V_1M[] = {0x1a, 0x00, 0x0f, 0x42, 0x40};
    span.reset(&g_w);
    Cbor.put_uint(&g_w, 1000000);
    expect(V_1M, sizeof(V_1M));

    // 1000000000000 | 0x1b000000e8d4a51000
    static const uint8_t V_1T[] = {0x1b, 0x00, 0x00, 0x00, 0xe8, 0xd4, 0xa5, 0x10, 0x00};
    span.reset(&g_w);
    Cbor.put_uint(&g_w, 1000000000000ULL);
    expect(V_1T, sizeof(V_1T));

    // 18446744073709551615 | 0x1bffffffffffffffff
    static const uint8_t V_U64MAX[] = {0x1b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    span.reset(&g_w);
    Cbor.put_uint(&g_w, 18446744073709551615ULL);
    expect(V_U64MAX, sizeof(V_U64MAX));

    // -1 | 0x20
    static const uint8_t V_M1[] = {0x20};
    span.reset(&g_w);
    Cbor.put_int(&g_w, -1);
    expect(V_M1, sizeof(V_M1));

    // -10 | 0x29
    static const uint8_t V_M10[] = {0x29};
    span.reset(&g_w);
    Cbor.put_int(&g_w, -10);
    expect(V_M10, sizeof(V_M10));

    // -100 | 0x3863
    static const uint8_t V_M100[] = {0x38, 0x63};
    span.reset(&g_w);
    Cbor.put_int(&g_w, -100);
    expect(V_M100, sizeof(V_M100));

    // -1000 | 0x3903e7
    static const uint8_t V_M1000[] = {0x39, 0x03, 0xe7};
    span.reset(&g_w);
    Cbor.put_int(&g_w, -1000);
    expect(V_M1000, sizeof(V_M1000));

    // 100000.0 | 0xfa47c35000
    static const uint8_t V_1E5[] = {0xfa, 0x47, 0xc3, 0x50, 0x00};
    span.reset(&g_w);
    Cbor.put_float(&g_w, 100000.0f);
    expect(V_1E5, sizeof(V_1E5));

    // 3.4028234663852886e+38 | 0xfa7f7fffff
    static const uint8_t V_FMAX[] = {0xfa, 0x7f, 0x7f, 0xff, 0xff};
    span.reset(&g_w);
    Cbor.put_float(&g_w, 3.4028234663852886e+38f);
    expect(V_FMAX, sizeof(V_FMAX));

    // false | 0xf4   true | 0xf5   null | 0xf6
    static const uint8_t V_FALSE[] = {0xf4};
    span.reset(&g_w);
    Cbor.put_bool(&g_w, PROTO_FALSE);
    expect(V_FALSE, sizeof(V_FALSE));

    static const uint8_t V_TRUE[] = {0xf5};
    span.reset(&g_w);
    Cbor.put_bool(&g_w, PROTO_TRUE);
    expect(V_TRUE, sizeof(V_TRUE));

    static const uint8_t V_NULL[] = {0xf6};
    span.reset(&g_w);
    Cbor.put_null(&g_w);
    expect(V_NULL, sizeof(V_NULL));

    // h'' | 0x40
    static const uint8_t V_BEMPTY[] = {0x40};
    span.reset(&g_w);
    Cbor.put_bytes(&g_w, NULL, 0);
    expect(V_BEMPTY, sizeof(V_BEMPTY));

    // h'01020304' | 0x4401020304
    static const uint8_t RAW[] = {0x01, 0x02, 0x03, 0x04};
    static const uint8_t V_BYTES[] = {0x44, 0x01, 0x02, 0x03, 0x04};
    span.reset(&g_w);
    Cbor.put_bytes(&g_w, RAW, sizeof(RAW));
    expect(V_BYTES, sizeof(V_BYTES));

    // "" | 0x60
    static const uint8_t V_SEMPTY[] = {0x60};
    span.reset(&g_w);
    Cbor.put_str(&g_w, "");
    expect(V_SEMPTY, sizeof(V_SEMPTY));

    // "a" | 0x6161
    static const uint8_t V_A[] = {0x61, 0x61};
    span.reset(&g_w);
    Cbor.put_str(&g_w, "a");
    expect(V_A, sizeof(V_A));

    // "IETF" | 0x6449455446
    static const uint8_t V_IETF[] = {0x64, 0x49, 0x45, 0x54, 0x46};
    span.reset(&g_w);
    Cbor.put_str(&g_w, "IETF");
    expect(V_IETF, sizeof(V_IETF));

    // "\"\\" | 0x62225c
    static const uint8_t V_QUOTE[] = {0x62, 0x22, 0x5c};
    span.reset(&g_w);
    Cbor.put_str(&g_w, "\"\\");
    expect(V_QUOTE, sizeof(V_QUOTE));

    // "ü" | 0x62c3bc  (U+00FC as its two UTF-8 octets)
    static const char U_FC[] = {(char)0xc3, (char)0xbc, '\0'};
    static const uint8_t V_FC[] = {0x62, 0xc3, 0xbc};
    span.reset(&g_w);
    Cbor.put_str(&g_w, U_FC);
    expect(V_FC, sizeof(V_FC));

    // "水" | 0x63e6b0b4  (U+6C34 as its three UTF-8 octets)
    static const char U_6C34[] = {(char)0xe6, (char)0xb0, (char)0xb4, '\0'};
    static const uint8_t V_6C34[] = {0x63, 0xe6, 0xb0, 0xb4};
    span.reset(&g_w);
    Cbor.put_str(&g_w, U_6C34);
    expect(V_6C34, sizeof(V_6C34));

    // [] | 0x80
    static const uint8_t V_ARR0[] = {0x80};
    span.reset(&g_w);
    Cbor.put_array(&g_w, 0);
    expect(V_ARR0, sizeof(V_ARR0));

    // [1, 2, 3] | 0x83010203
    static const uint8_t V_ARR3[] = {0x83, 0x01, 0x02, 0x03};
    span.reset(&g_w);
    Cbor.put_array(&g_w, 3);
    Cbor.put_uint(&g_w, 1);
    Cbor.put_uint(&g_w, 2);
    Cbor.put_uint(&g_w, 3);
    expect(V_ARR3, sizeof(V_ARR3));

    // [1, [2, 3], [4, 5]] | 0x8301820203820405
    static const uint8_t V_NEST[] = {0x83, 0x01, 0x82, 0x02, 0x03, 0x82, 0x04, 0x05};
    span.reset(&g_w);
    Cbor.put_array(&g_w, 3);
    Cbor.put_uint(&g_w, 1);
    Cbor.put_array(&g_w, 2);
    Cbor.put_uint(&g_w, 2);
    Cbor.put_uint(&g_w, 3);
    Cbor.put_array(&g_w, 2);
    Cbor.put_uint(&g_w, 4);
    Cbor.put_uint(&g_w, 5);
    expect(V_NEST, sizeof(V_NEST));

    // [1 .. 25] | 0x98190102030405060708090a0b0c0d0e0f101112131415161718181819
    // 25 items needs the 1-octet count form, and items 24 and 25 need 1-octet arguments of their own.
    static const uint8_t V_ARR25[] = {0x98, 0x19, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                      0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12,
                                      0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x18, 0x18, 0x19};
    span.reset(&g_w);
    Cbor.put_array(&g_w, 25);
    for (uint64_t i = 1; i <= 25; i++)
    {
        Cbor.put_uint(&g_w, i);
    }
    expect(V_ARR25, sizeof(V_ARR25));

    // {} | 0xa0
    static const uint8_t V_MAP0[] = {0xa0};
    span.reset(&g_w);
    Cbor.put_map(&g_w, 0);
    expect(V_MAP0, sizeof(V_MAP0));

    // {1: 2, 3: 4} | 0xa201020304
    static const uint8_t V_MAP2[] = {0xa2, 0x01, 0x02, 0x03, 0x04};
    span.reset(&g_w);
    Cbor.put_map(&g_w, 2);
    Cbor.put_uint(&g_w, 1);
    Cbor.put_uint(&g_w, 2);
    Cbor.put_uint(&g_w, 3);
    Cbor.put_uint(&g_w, 4);
    expect(V_MAP2, sizeof(V_MAP2));

    // {"a": 1, "b": [2, 3]} | 0xa26161016162820203
    static const uint8_t V_MAPAB[] = {0xa2, 0x61, 0x61, 0x01, 0x61, 0x62, 0x82, 0x02, 0x03};
    span.reset(&g_w);
    Cbor.put_map(&g_w, 2);
    Cbor.put_str(&g_w, "a");
    Cbor.put_uint(&g_w, 1);
    Cbor.put_str(&g_w, "b");
    Cbor.put_array(&g_w, 2);
    Cbor.put_uint(&g_w, 2);
    Cbor.put_uint(&g_w, 3);
    expect(V_MAPAB, sizeof(V_MAPAB));

    // ["a", {"b": "c"}] | 0x826161a161626163
    static const uint8_t V_ARRMAP[] = {0x82, 0x61, 0x61, 0xa1, 0x61, 0x62, 0x61, 0x63};
    span.reset(&g_w);
    Cbor.put_array(&g_w, 2);
    Cbor.put_str(&g_w, "a");
    Cbor.put_map(&g_w, 1);
    Cbor.put_str(&g_w, "b");
    Cbor.put_str(&g_w, "c");
    expect(V_ARRMAP, sizeof(V_ARRMAP));

    // {"a": "A", "b": "B", "c": "C", "d": "D", "e": "E"}
    //   | 0xa5616161416162614261636143616461446165614
    static const uint8_t V_MAP5[] = {0xa5, 0x61, 0x61, 0x61, 0x41, 0x61, 0x62, 0x61, 0x42, 0x61, 0x63,
                                     0x61, 0x43, 0x61, 0x64, 0x61, 0x44, 0x61, 0x65, 0x61, 0x45};
    span.reset(&g_w);
    Cbor.put_map(&g_w, 5);
    Cbor.put_str(&g_w, "a");
    Cbor.put_str(&g_w, "A");
    Cbor.put_str(&g_w, "b");
    Cbor.put_str(&g_w, "B");
    Cbor.put_str(&g_w, "c");
    Cbor.put_str(&g_w, "C");
    Cbor.put_str(&g_w, "d");
    Cbor.put_str(&g_w, "D");
    Cbor.put_str(&g_w, "e");
    Cbor.put_str(&g_w, "E");
    expect(V_MAP5, sizeof(V_MAP5));
}

// put_str_n writes the length it is given rather than scanning, so a string holding a NUL keeps its
// declared length: sec 3 major 3 counts octets, not C terminators.
void test_str_n_takes_its_length_from_the_caller(void)
{
    static const char EMBEDDED[] = {'a', '\0', 'b'};
    static const uint8_t WANT[] = {0x63, 0x61, 0x00, 0x62};
    Cbor.put_str_n(&g_w, EMBEDDED, sizeof(EMBEDDED));
    expect(WANT, sizeof(WANT));
}

// A null string is the empty text item, not a fault.
void test_null_string_is_the_empty_text_item(void)
{
    static const uint8_t WANT[] = {0x60};
    Cbor.put_str(&g_w, NULL);
    expect(WANT, sizeof(WANT));
}

// RFC 8428 Table 4 assigns the SenML base name the CBOR label -2, and sec 6 says the CBOR
// representation uses those integers where JSON uses the string. RFC 8949 major 1 encodes -1-n, so
// -2 is 0x21.
void test_put_label_writes_the_cbor_label_number(void)
{
    static const uint8_t WANT[] = {0x21};
    Cbor.put_label(&g_w, "bn", -2);
    expect(WANT, sizeof(WANT));
}

// Every head form round-trips through the decoder, so the 1/2/4/8 argument widths of sec 3 are read
// back at the same width they were written.
void test_head_forms_round_trip(void)
{
    static const uint64_t VALUES[] = {
        0, 23, 24, 255, 256, 65535, 65536, 0xFFFFFFFFULL, 0x100000000ULL, 18446744073709551615ULL};
    for (size_t i = 0; i < sizeof(VALUES) / sizeof(VALUES[0]); i++)
    {
        span.reset(&g_w);
        Cbor.put_uint(&g_w, VALUES[i]);
        TEST_ASSERT_TRUE(span.ok(g_w));

        protocore_cspan r = produced();
        TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_UINT, Cbor.peek(&r));
        uint64_t v = 0;
        TEST_ASSERT_TRUE(Cbor.get_uint(&r, &v));
        TEST_ASSERT_EQUAL_UINT64(VALUES[i], v);
        TEST_ASSERT_TRUE(span.cok(r));
    }
}

// A negative integer round-trips through major 1's -1-n form at each argument width.
void test_negative_integers_round_trip(void)
{
    static const int64_t VALUES[] = {-1, -10, -24, -25, -100, -1000, -65536, -4294967296LL};
    for (size_t i = 0; i < sizeof(VALUES) / sizeof(VALUES[0]); i++)
    {
        span.reset(&g_w);
        Cbor.put_int(&g_w, VALUES[i]);

        protocore_cspan r = produced();
        TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_INT, Cbor.peek(&r));
        int64_t v = 0;
        TEST_ASSERT_TRUE(Cbor.get_int(&r, &v));
        TEST_ASSERT_EQUAL_INT64(VALUES[i], v);
    }
}

// peek names the next item without consuming it, one name per major type, and reports INVALID for
// the tags of major 6 and for an unassigned simple value this codec does not carry.
void test_peek_names_every_item(void)
{
    static const uint8_t RAW[2] = {1, 2};

    span.reset(&g_w);
    Cbor.put_uint(&g_w, 7);
    protocore_cspan r = produced();
    TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_UINT, Cbor.peek(&r));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_UINT, Cbor.peek(&r)); // peek does not consume

    span.reset(&g_w);
    Cbor.put_int(&g_w, -5);
    r = produced();
    TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_INT, Cbor.peek(&r));

    span.reset(&g_w);
    Cbor.put_bytes(&g_w, RAW, sizeof(RAW));
    r = produced();
    TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_BYTES, Cbor.peek(&r));

    span.reset(&g_w);
    Cbor.put_str(&g_w, "x");
    r = produced();
    TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_STR, Cbor.peek(&r));

    span.reset(&g_w);
    Cbor.put_array(&g_w, 1);
    r = produced();
    TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_ARRAY, Cbor.peek(&r));

    span.reset(&g_w);
    Cbor.put_map(&g_w, 1);
    r = produced();
    TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_MAP, Cbor.peek(&r));

    span.reset(&g_w);
    Cbor.put_bool(&g_w, PROTO_TRUE);
    r = produced();
    TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_BOOL, Cbor.peek(&r));

    span.reset(&g_w);
    Cbor.put_bool(&g_w, PROTO_FALSE);
    r = produced();
    TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_BOOL, Cbor.peek(&r));

    span.reset(&g_w);
    Cbor.put_null(&g_w);
    r = produced();
    TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_NULL, Cbor.peek(&r));

    span.reset(&g_w);
    Cbor.put_float(&g_w, 1.5f);
    r = produced();
    TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_FLOAT, Cbor.peek(&r));

    // 0xc0 is tag 0 (sec 3.4), 0xe0 is an unassigned simple value: neither is an item this carries.
    static const uint8_t TAG[] = {0xc0};
    r = span.cfrom(TAG, sizeof(TAG));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_INVALID, Cbor.peek(&r));

    static const uint8_t SIMPLE[] = {0xe0};
    r = span.cfrom(SIMPLE, sizeof(SIMPLE));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_INVALID, Cbor.peek(&r));

    // Nothing left to name.
    static const uint8_t NONE[] = {0x00};
    r = span.cfrom(NONE, 0);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_INVALID, Cbor.peek(&r));
}

// A whole map is written and read back item by item, and the reader lands exactly at the end.
void test_map_round_trips_item_by_item(void)
{
    Cbor.put_map(&g_w, 3);
    Cbor.put_str(&g_w, "heap");
    Cbor.put_uint(&g_w, 42000);
    Cbor.put_str(&g_w, "name");
    Cbor.put_str(&g_w, "esp");
    Cbor.put_str(&g_w, "on");
    Cbor.put_bool(&g_w, PROTO_TRUE);
    TEST_ASSERT_TRUE(span.ok(g_w));

    protocore_cspan r = produced();
    size_t n = 0;
    TEST_ASSERT_TRUE(Cbor.get_map(&r, &n));
    TEST_ASSERT_EQUAL_size_t(3, n);

    const char *k = NULL;
    size_t kl = 0;
    uint64_t u = 0;
    const char *s = NULL;
    size_t sl = 0;
    proto_bool b = PROTO_FALSE;

    TEST_ASSERT_TRUE(Cbor.get_str(&r, &k, &kl));
    TEST_ASSERT_EQUAL_size_t(4, kl);
    TEST_ASSERT_EQUAL_MEMORY("heap", k, 4);
    TEST_ASSERT_TRUE(Cbor.get_uint(&r, &u));
    TEST_ASSERT_EQUAL_UINT64(42000, u);

    TEST_ASSERT_TRUE(Cbor.get_str(&r, &k, &kl));
    TEST_ASSERT_EQUAL_MEMORY("name", k, 4);
    TEST_ASSERT_TRUE(Cbor.get_str(&r, &s, &sl));
    TEST_ASSERT_EQUAL_size_t(3, sl);
    TEST_ASSERT_EQUAL_MEMORY("esp", s, 3);

    TEST_ASSERT_TRUE(Cbor.get_str(&r, &k, &kl));
    TEST_ASSERT_EQUAL_MEMORY("on", k, 2);
    TEST_ASSERT_TRUE(Cbor.get_bool(&r, &b));
    TEST_ASSERT_TRUE(b);

    TEST_ASSERT_TRUE(span.cok(r));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_INVALID, Cbor.peek(&r)); // everything consumed
}

// A byte string carries arbitrary octets, including the NUL a text scan would stop at.
void test_byte_string_round_trips(void)
{
    static const uint8_t RAW[] = {0xde, 0x00, 0xad, 0xff};
    Cbor.put_bytes(&g_w, RAW, sizeof(RAW));

    protocore_cspan r = produced();
    const uint8_t *p = NULL;
    size_t n = 0;
    TEST_ASSERT_TRUE(Cbor.get_bytes(&r, &p, &n));
    TEST_ASSERT_EQUAL_size_t(sizeof(RAW), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RAW, p, n);
}

// sec 3.3: 0xfa carries an IEEE 754 single and 0xfb a double. A double is narrowed on the way in,
// so 2.5 (exact in both) comes back unchanged.
void test_float_forms_read_back(void)
{
    Cbor.put_float(&g_w, 3.5f);
    protocore_cspan r = produced();
    float f = 0.0f;
    TEST_ASSERT_TRUE(Cbor.get_float(&r, &f));
    TEST_ASSERT_EQUAL_FLOAT(3.5f, f);

    // 2.5 as an IEEE 754 double: sign 0, exponent 0x400, mantissa 0x4000000000000.
    static const uint8_t DOUBLE_2_5[] = {0xfb, 0x40, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    r = span.cfrom(DOUBLE_2_5, sizeof(DOUBLE_2_5));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_FLOAT, Cbor.peek(&r));
    TEST_ASSERT_TRUE(Cbor.get_float(&r, &f));
    TEST_ASSERT_EQUAL_FLOAT(2.5f, f);
}

// A write that does not fit sets the overflow flag and stops, while the cursor keeps counting what
// the payload would have needed, so a caller can size the buffer from one failed attempt.
void test_overflow_reports_the_size_it_needed(void)
{
    uint8_t small[2];
    protocore_span w = span.from(small, sizeof(small));
    Cbor.put_uint(&w, 1000000); // the 5-octet form
    TEST_ASSERT_FALSE(span.ok(w));
    TEST_ASSERT_EQUAL_size_t(5, span.len(w));
}

// sec 3: additional information 28 to 30 is reserved and 31 starts an indefinite-length item, and
// neither is an item this codec carries, so both are refused rather than misread.
void test_reserved_and_indefinite_heads_are_refused(void)
{
    static const uint8_t RESERVED[] = {0x1c}; // major 0, additional info 28
    protocore_cspan r = span.cfrom(RESERVED, sizeof(RESERVED));
    uint64_t v = 0;
    TEST_ASSERT_FALSE(Cbor.get_uint(&r, &v));
    TEST_ASSERT_FALSE(span.cok(r));

    // 0x9f is the indefinite-length array head of Table 6's "[_ ]" (0x9fff).
    static const uint8_t INDEFINITE[] = {0x9f, 0xff};
    r = span.cfrom(INDEFINITE, sizeof(INDEFINITE));
    size_t n = 0;
    TEST_ASSERT_FALSE(Cbor.get_array(&r, &n));
    TEST_ASSERT_FALSE(span.cok(r));
}

// Reading an item as the wrong type fails and marks the reader, so a decoder cannot walk on from a
// misread head.
void test_type_mismatch_fails_and_marks_the_reader(void)
{
    Cbor.put_str(&g_w, "x");
    protocore_cspan r = produced();
    uint64_t v = 0;
    TEST_ASSERT_FALSE(Cbor.get_uint(&r, &v));
    TEST_ASSERT_FALSE(span.cok(r));

    static const uint8_t UINT5[] = {0x05};
    r = span.cfrom(UINT5, sizeof(UINT5));
    size_t n = 0;
    TEST_ASSERT_FALSE(Cbor.get_map(&r, &n));
    TEST_ASSERT_FALSE(span.cok(r));

    r = span.cfrom(UINT5, sizeof(UINT5));
    TEST_ASSERT_FALSE(Cbor.get_array(&r, &n));

    static const uint8_t TRUE_ITEM[] = {0xf5};
    r = span.cfrom(TRUE_ITEM, sizeof(TRUE_ITEM));
    TEST_ASSERT_FALSE(Cbor.get_null(&r));

    static const uint8_t NULL_ITEM[] = {0xf6};
    r = span.cfrom(NULL_ITEM, sizeof(NULL_ITEM));
    proto_bool b = PROTO_FALSE;
    TEST_ASSERT_FALSE(Cbor.get_bool(&r, &b));

    static const uint8_t FALSE_ITEM[] = {0xf4};
    r = span.cfrom(FALSE_ITEM, sizeof(FALSE_ITEM));
    float f = 0.0f;
    TEST_ASSERT_FALSE(Cbor.get_float(&r, &f));
}

// A declared string length that runs past the buffer is refused: the head is not evidence that the
// octets are there.
void test_declared_length_past_the_end_is_refused(void)
{
    static const uint8_t TEXT5[] = {0x65, 'h', 'e', 'l', 'l'}; // text(5), four octets follow
    protocore_cspan r = span.cfrom(TEXT5, sizeof(TEXT5));
    const char *s = NULL;
    size_t n = 0;
    TEST_ASSERT_FALSE(Cbor.get_str(&r, &s, &n));
    TEST_ASSERT_FALSE(span.cok(r));

    static const uint8_t BYTES5[] = {0x45, 1, 2, 3}; // bytes(5), three octets follow
    r = span.cfrom(BYTES5, sizeof(BYTES5));
    const uint8_t *p = NULL;
    TEST_ASSERT_FALSE(Cbor.get_bytes(&r, &p, &n));
    TEST_ASSERT_FALSE(span.cok(r));

    static const uint8_t SHORT_FLOAT[] = {0xfa, 0x00, 0x00}; // a single needs four argument octets
    r = span.cfrom(SHORT_FLOAT, sizeof(SHORT_FLOAT));
    float f = 0.0f;
    TEST_ASSERT_FALSE(Cbor.get_float(&r, &f));

    static const uint8_t SHORT_DOUBLE[] = {0xfb, 0x00, 0x00}; // a double needs eight
    r = span.cfrom(SHORT_DOUBLE, sizeof(SHORT_DOUBLE));
    TEST_ASSERT_FALSE(Cbor.get_float(&r, &f));
}

// The error is sticky: once a read has failed, every later read fails on that flag alone rather than
// re-reading the buffer, so a caller that ignores one return cannot resynchronize by accident.
void test_the_read_error_is_sticky(void)
{
    static const uint8_t RESERVED[] = {0x1c};
    protocore_cspan r = span.cfrom(RESERVED, sizeof(RESERVED));

    uint64_t uv = 0;
    TEST_ASSERT_FALSE(Cbor.get_uint(&r, &uv));
    TEST_ASSERT_FALSE(span.cok(r));

    int64_t iv = 0;
    proto_bool bv = PROTO_FALSE;
    float fv = 0.0f;
    const char *s = NULL;
    const uint8_t *p = NULL;
    size_t n = 0;

    TEST_ASSERT_FALSE(Cbor.get_uint(&r, &uv));
    TEST_ASSERT_FALSE(Cbor.get_int(&r, &iv));
    TEST_ASSERT_FALSE(Cbor.get_bool(&r, &bv));
    TEST_ASSERT_FALSE(Cbor.get_null(&r));
    TEST_ASSERT_FALSE(Cbor.get_float(&r, &fv));
    TEST_ASSERT_FALSE(Cbor.get_str(&r, &s, &n));
    TEST_ASSERT_FALSE(Cbor.get_bytes(&r, &p, &n));
    TEST_ASSERT_FALSE(Cbor.get_array(&r, &n));
    TEST_ASSERT_FALSE(Cbor.get_map(&r, &n));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_CODEC_INVALID, Cbor.peek(&r));
}

// A buffer that ends mid-item is refused rather than completed from whatever follows it.
void test_truncated_item_is_refused(void)
{
    Cbor.put_uint(&g_w, 1000000); // five octets
    protocore_cspan r = span.cfrom(g_buf, 3);
    uint64_t v = 0;
    TEST_ASSERT_FALSE(Cbor.get_uint(&r, &v));
    TEST_ASSERT_FALSE(span.cok(r));
}

// Every reader on an empty region fails closed instead of reading the byte that is not there.
void test_every_reader_fails_closed_on_an_empty_region(void)
{
    static const uint8_t NONE[] = {0x00};
    uint64_t uv = 0;
    int64_t iv = 0;
    proto_bool bv = PROTO_FALSE;
    float fv = 0.0f;
    const char *s = NULL;
    const uint8_t *p = NULL;
    size_t n = 0;
    protocore_cspan r;

    r = span.cfrom(NONE, 0);
    TEST_ASSERT_FALSE(Cbor.get_uint(&r, &uv));
    r = span.cfrom(NONE, 0);
    TEST_ASSERT_FALSE(Cbor.get_int(&r, &iv));
    r = span.cfrom(NONE, 0);
    TEST_ASSERT_FALSE(Cbor.get_bool(&r, &bv));
    r = span.cfrom(NONE, 0);
    TEST_ASSERT_FALSE(Cbor.get_null(&r));
    r = span.cfrom(NONE, 0);
    TEST_ASSERT_FALSE(Cbor.get_float(&r, &fv));
    r = span.cfrom(NONE, 0);
    TEST_ASSERT_FALSE(Cbor.get_str(&r, &s, &n));
    r = span.cfrom(NONE, 0);
    TEST_ASSERT_FALSE(Cbor.get_bytes(&r, &p, &n));
    r = span.cfrom(NONE, 0);
    TEST_ASSERT_FALSE(Cbor.get_array(&r, &n));
    r = span.cfrom(NONE, 0);
    TEST_ASSERT_FALSE(Cbor.get_map(&r, &n));
}
