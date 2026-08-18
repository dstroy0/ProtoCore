// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the QPACK field-section codec
// (network_drivers/presentation/http/http3/qpack.h).
//
// RFC 9204 Appendix B.1 prints a complete encoded field section octet by octet - "0000 510b 2f69
// 6e64 6578 2e68 746d 6c" carrying :path=/index.html - and test_rfc9204_b1_worked_example decodes
// exactly those octets. It is the load-bearing case: it exercises the sec 4.5.1 prefix, the sec
// 4.5.4 literal-with-static-name-reference representation and the sec 3.1 static table in one pass,
// against bytes the RFC wrote rather than bytes this encoder produced. The encoder is not pinned to
// those octets because Huffman coding is the encoder's choice (sec 5), so it is checked by decoding
// its output and by the representation byte it selects.

#include "network_drivers/presentation/http/http3/qpack.h"
#include <string.h>

#include <unity.h>

static uint8_t qpack_work[16]; // the borrow an entry takes; Qpack never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

#define SINK_MAX 8
#define FIELD_MAX 96

typedef struct
{
    char name[FIELD_MAX];
    char value[FIELD_MAX];
} Field;

typedef struct
{
    Field f[SINK_MAX];
    size_t n;
} Sink;

static uint8_t g_out[256];
static char g_scratch[256];
static Sink g_sink;

// Copy each emitted field into its own slot, NUL-terminated, so a case can compare it as a string.
static proto_bool collect(void *ctx, const char *name, size_t name_len, const char *value, size_t value_len)
{
    Sink *s = (Sink *)ctx;
    if (s->n >= SINK_MAX || name_len >= FIELD_MAX || value_len >= FIELD_MAX)
    {
        return PROTO_FALSE;
    }
    memcpy(s->f[s->n].name, name, name_len);
    s->f[s->n].name[name_len] = '\0';
    memcpy(s->f[s->n].value, value, value_len);
    s->f[s->n].value[value_len] = '\0';
    s->n++;
    return PROTO_TRUE;
}

static proto_bool refuse(void *ctx, const char *name, size_t name_len, const char *value, size_t value_len)
{
    (void)ctx;
    (void)name;
    (void)name_len;
    (void)value;
    (void)value_len;
    return PROTO_FALSE;
}

static proto_bool decode(const uint8_t *block, size_t len)
{
    g_sink.n = 0;
    Qpack.decode_args.block = block;
    Qpack.decode_args.len = len;
    Qpack.decode_args.scratch = g_scratch;
    Qpack.decode_args.scratch_cap = sizeof(g_scratch);
    Qpack.decode_args.emit = collect;
    Qpack.decode_args.ctx = &g_sink;
    Qpack.decode(qpack_work);
    return Qpack.ok;
}

// Encode a prefix plus one field into g_out and return the total length.
static size_t encode_one(const char *name, const char *value)
{
    Qpack.encode_prefix_args.out = g_out;
    Qpack.encode_prefix_args.cap = sizeof(g_out);
    Qpack.encode_prefix(qpack_work);
    size_t p = Qpack.n;
    Qpack.encode_header_args.out = g_out + p;
    Qpack.encode_header_args.cap = sizeof(g_out) - p;
    Qpack.encode_header_args.name = name;
    Qpack.encode_header_args.name_len = strlen(name);
    Qpack.encode_header_args.value = value;
    Qpack.encode_header_args.value_len = strlen(value);
    Qpack.encode_header(qpack_work);
    size_t h = Qpack.n;
    return (p && h) ? p + h : 0;
}

// RFC 9204 Appendix B.1:
//   0000                | Required Insert Count = 0, Base = 0
//   510b 2f69 6e64 6578 | Literal Field Line with Name Reference
//   2e68 746d 6c        |  Static Table, Index=1
//                       |  (:path=/index.html)
void test_rfc9204_b1_worked_example(void)
{
    static const uint8_t B1[15] = {0x00, 0x00, 0x51, 0x0b, '/', 'i', 'n', 'd', 'e', 'x', '.', 'h', 't', 'm', 'l'};
    TEST_ASSERT_TRUE(decode(B1, sizeof(B1)));
    TEST_ASSERT_EQUAL_UINT(1u, g_sink.n);
    TEST_ASSERT_EQUAL_STRING(":path", g_sink.f[0].name);
    TEST_ASSERT_EQUAL_STRING("/index.html", g_sink.f[0].value);
}

// RFC 9204 sec 4.5.1: with Required Insert Count 0 and Base 0 the whole prefix is two zero octets.
// A non-zero Required Insert Count names an entry of a dynamic table this codec never populates.
void test_rfc9204_field_section_prefix(void)
{
    static const uint8_t EMPTY_SECTION[2] = {0x00, 0x00};
    Qpack.encode_prefix_args.out = g_out;
    Qpack.encode_prefix_args.cap = sizeof(g_out);
    Qpack.encode_prefix(qpack_work);
    TEST_ASSERT_EQUAL_UINT(2u, Qpack.n);
    TEST_ASSERT_EQUAL_MEMORY(EMPTY_SECTION, g_out, 2);
    Qpack.encode_prefix_args.out = g_out;
    Qpack.encode_prefix_args.cap = 1;
    Qpack.encode_prefix(qpack_work);
    TEST_ASSERT_EQUAL_UINT(0u, Qpack.n);

    TEST_ASSERT_TRUE(decode(EMPTY_SECTION, sizeof(EMPTY_SECTION)));
    TEST_ASSERT_EQUAL_UINT(0u, g_sink.n);

    static const uint8_t NONZERO_RIC[3] = {0x01, 0x00, 0xD1};
    TEST_ASSERT_FALSE(decode(NONZERO_RIC, sizeof(NONZERO_RIC)));
}

// RFC 9204 sec 4.5.2: an Indexed Field Line is "1" then T then a 6-bit index, T = 1 selecting the
// static table. The indices come from the Appendix A table, so 0xC0 | index is the whole line.
void test_rfc9204_indexed_field_line(void)
{
    struct
    {
        const char *name;
        const char *value;
        uint8_t first;
    } static const CASES[] = {
        {":authority", "", 0xC0},   // index 0
        {":path", "/", 0xC1},       // index 1
        {":method", "GET", 0xD1},   // index 17
        {":scheme", "https", 0xD7}, // index 23
        {":status", "200", 0xD9},   // index 25
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        size_t n = encode_one(CASES[i].name, CASES[i].value);
        TEST_ASSERT_EQUAL_UINT(3u, n); // 2 prefix octets + one indexed line
        TEST_ASSERT_EQUAL_HEX8(CASES[i].first, g_out[2]);
        TEST_ASSERT_TRUE(decode(g_out, n));
        TEST_ASSERT_EQUAL_UINT(1u, g_sink.n);
        TEST_ASSERT_EQUAL_STRING(CASES[i].name, g_sink.f[0].name);
        TEST_ASSERT_EQUAL_STRING(CASES[i].value, g_sink.f[0].value);
    }

    // Appendix A index 63 is :status 100, and 63 is the largest value a 6-bit prefix can hold, so
    // RFC 7541 sec 5.1 puts 0b111111 in the prefix and the remainder 0 in one continuation octet
    TEST_ASSERT_EQUAL_UINT(4u, encode_one(":status", "100"));
    TEST_ASSERT_EQUAL_HEX8(0xFF, g_out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[3]);
    TEST_ASSERT_TRUE(decode(g_out, 4));
    TEST_ASSERT_EQUAL_STRING(":status", g_sink.f[0].name);
    TEST_ASSERT_EQUAL_STRING("100", g_sink.f[0].value);
}

// The Appendix A entries an Indexed Field Line names, read back through the decoder. Index 98 needs
// a continuation octet: 98 - 63 = 35.
void test_rfc9204_appendix_a_static_table(void)
{
    static const uint8_t BLOCK[9] = {0x00, 0x00, 0xC0, 0xC1, 0xD1, 0xD7, 0xD9, 0xFF, 0x23};
    TEST_ASSERT_TRUE(decode(BLOCK, sizeof(BLOCK)));
    TEST_ASSERT_EQUAL_UINT(6u, g_sink.n);
    TEST_ASSERT_EQUAL_STRING(":authority", g_sink.f[0].name);
    TEST_ASSERT_EQUAL_STRING("", g_sink.f[0].value);
    TEST_ASSERT_EQUAL_STRING(":path", g_sink.f[1].name);
    TEST_ASSERT_EQUAL_STRING("/", g_sink.f[1].value);
    TEST_ASSERT_EQUAL_STRING(":method", g_sink.f[2].name);
    TEST_ASSERT_EQUAL_STRING("GET", g_sink.f[2].value);
    TEST_ASSERT_EQUAL_STRING(":scheme", g_sink.f[3].name);
    TEST_ASSERT_EQUAL_STRING("https", g_sink.f[3].value);
    TEST_ASSERT_EQUAL_STRING(":status", g_sink.f[4].name);
    TEST_ASSERT_EQUAL_STRING("200", g_sink.f[4].value);
    TEST_ASSERT_EQUAL_STRING("x-frame-options", g_sink.f[5].name);
    TEST_ASSERT_EQUAL_STRING("sameorigin", g_sink.f[5].value);
}

// RFC 9204 sec 4.5.4: a Literal Field Line with Name Reference is "01" then N then T then a 4-bit
// index. Appendix A index 1 is :path, so the representation byte for a new :path value is
// 0b0101 0001 = 0x51 - the same octet B.1 prints.
void test_rfc9204_literal_with_name_reference(void)
{
    size_t n = encode_one(":path", "/index.html");
    TEST_ASSERT_TRUE(n > 4u);
    TEST_ASSERT_EQUAL_HEX8(0x51, g_out[2]);
    TEST_ASSERT_TRUE(decode(g_out, n));
    TEST_ASSERT_EQUAL_UINT(1u, g_sink.n);
    TEST_ASSERT_EQUAL_STRING(":path", g_sink.f[0].name);
    TEST_ASSERT_EQUAL_STRING("/index.html", g_sink.f[0].value);

    // Appendix A index 12 is location, so 0x50 | 12 = 0x5C
    n = encode_one("location", "https://example.com/x");
    TEST_ASSERT_EQUAL_HEX8(0x5C, g_out[2]);
    TEST_ASSERT_TRUE(decode(g_out, n));
    TEST_ASSERT_EQUAL_STRING("location", g_sink.f[0].name);
    TEST_ASSERT_EQUAL_STRING("https://example.com/x", g_sink.f[0].value);

    // an index past the 4-bit prefix takes a continuation octet: server is index 92, 92 - 15 = 77
    n = encode_one("server", "ProtoCore");
    TEST_ASSERT_EQUAL_HEX8(0x5F, g_out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x4D, g_out[3]);
    TEST_ASSERT_TRUE(decode(g_out, n));
    TEST_ASSERT_EQUAL_STRING("server", g_sink.f[0].name);
    TEST_ASSERT_EQUAL_STRING("ProtoCore", g_sink.f[0].value);
}

// RFC 9204 sec 4.5.6: a Literal Field Line with Literal Name is "001" then N then H then a 3-bit
// name length. A field name the static table does not hold takes this form.
void test_rfc9204_literal_with_literal_name(void)
{
    size_t n = encode_one("x-protocore-trace", "abc123");
    TEST_ASSERT_EQUAL_HEX8(0x20, (uint8_t)(g_out[2] & 0xE0));
    TEST_ASSERT_TRUE(decode(g_out, n));
    TEST_ASSERT_EQUAL_UINT(1u, g_sink.n);
    TEST_ASSERT_EQUAL_STRING("x-protocore-trace", g_sink.f[0].name);
    TEST_ASSERT_EQUAL_STRING("abc123", g_sink.f[0].value);

    // an empty value is a zero-length string literal, not an absent field
    n = encode_one("x-empty", "");
    TEST_ASSERT_TRUE(decode(g_out, n));
    TEST_ASSERT_EQUAL_STRING("x-empty", g_sink.f[0].name);
    TEST_ASSERT_EQUAL_STRING("", g_sink.f[0].value);
}

// A whole field section: the prefix once, then one representation per field, decoded back in the
// order it was written. The three representations of sec 4.5.2 / 4.5.4 / 4.5.6 appear together.
void test_field_section_round_trip(void)
{
    struct
    {
        const char *name;
        const char *value;
    } static const FIELDS[4] = {
        {":status", "200"},
        {"content-type", "text/html;charset=utf-8"},
        {"server", "ProtoCore"},
        {"x-protocore-trace", "7f3a"},
    };
    Qpack.encode_prefix_args.out = g_out;
    Qpack.encode_prefix_args.cap = sizeof(g_out);
    Qpack.encode_prefix(qpack_work);
    size_t o = Qpack.n;
    TEST_ASSERT_EQUAL_UINT(2u, o);
    for (size_t i = 0; i < 4; i++)
    {
        Qpack.encode_header_args.out = g_out + o;
        Qpack.encode_header_args.cap = sizeof(g_out) - o;
        Qpack.encode_header_args.name = FIELDS[i].name;
        Qpack.encode_header_args.name_len = strlen(FIELDS[i].name);
        Qpack.encode_header_args.value = FIELDS[i].value;
        Qpack.encode_header_args.value_len = strlen(FIELDS[i].value);
        Qpack.encode_header(qpack_work);
        size_t h = Qpack.n;
        TEST_ASSERT_TRUE(h > 0);
        o += h;
    }
    TEST_ASSERT_TRUE(decode(g_out, o));
    TEST_ASSERT_EQUAL_UINT(4u, g_sink.n);
    for (size_t i = 0; i < 4; i++)
    {
        TEST_ASSERT_EQUAL_STRING(FIELDS[i].name, g_sink.f[i].name);
        TEST_ASSERT_EQUAL_STRING(FIELDS[i].value, g_sink.f[i].value);
    }
}

// This codec advertises SETTINGS_QPACK_MAX_TABLE_CAPACITY = 0 (RFC 9204 sec 5), so no
// representation may reference the dynamic table. Each form that does is refused: T = 0 on an
// Indexed Field Line (sec 4.5.2) or on a Literal with Name Reference (sec 4.5.4), and the two
// post-base forms of sec 4.5.3 and sec 4.5.5, whose leading bits are 0001 and 0000.
void test_dynamic_table_references_are_refused(void)
{
    static const uint8_t INDEXED_DYNAMIC[3] = {0x00, 0x00, 0x81};
    static const uint8_t NAMEREF_DYNAMIC[5] = {0x00, 0x00, 0x41, 0x01, 'v'};
    static const uint8_t INDEXED_POST_BASE[3] = {0x00, 0x00, 0x11};
    static const uint8_t NAMEREF_POST_BASE[3] = {0x00, 0x00, 0x01};
    TEST_ASSERT_FALSE(decode(INDEXED_DYNAMIC, sizeof(INDEXED_DYNAMIC)));
    TEST_ASSERT_FALSE(decode(NAMEREF_DYNAMIC, sizeof(NAMEREF_DYNAMIC)));
    TEST_ASSERT_FALSE(decode(INDEXED_POST_BASE, sizeof(INDEXED_POST_BASE)));
    TEST_ASSERT_FALSE(decode(NAMEREF_POST_BASE, sizeof(NAMEREF_POST_BASE)));
}

// The static table has 99 entries (Appendix A, indices 0..98), so index 99 names nothing.
void test_static_index_out_of_range_is_refused(void)
{
    // 6-bit prefix: 63 in the prefix plus 36 in the continuation octet
    static const uint8_t INDEXED_99[4] = {0x00, 0x00, 0xFF, 0x24};
    // 4-bit prefix: 15 in the prefix plus 84 in the continuation octet
    static const uint8_t NAMEREF_99[6] = {0x00, 0x00, 0x5F, 0x54, 0x01, 'v'};
    TEST_ASSERT_FALSE(decode(INDEXED_99, sizeof(INDEXED_99)));
    TEST_ASSERT_FALSE(decode(NAMEREF_99, sizeof(NAMEREF_99)));
}

// A block that ends inside a representation is refused rather than emitting a half-read field.
void test_truncated_block_is_refused(void)
{
    static const uint8_t B1[15] = {0x00, 0x00, 0x51, 0x0b, '/', 'i', 'n', 'd', 'e', 'x', '.', 'h', 't', 'm', 'l'};
    TEST_ASSERT_FALSE(decode(B1, 0));  // no prefix at all
    TEST_ASSERT_FALSE(decode(B1, 1));  // half a prefix
    TEST_ASSERT_FALSE(decode(B1, 4));  // a value length with no value
    TEST_ASSERT_FALSE(decode(B1, 14)); // a value one octet short of its length
}

// A scratch buffer that cannot hold one field's name and value is a refusal, not an overrun.
void test_scratch_bound_is_respected(void)
{
    static const uint8_t B1[15] = {0x00, 0x00, 0x51, 0x0b, '/', 'i', 'n', 'd', 'e', 'x', '.', 'h', 't', 'm', 'l'};
    char tiny[4];
    g_sink.n = 0;
    Qpack.decode_args.block = B1;
    Qpack.decode_args.len = sizeof(B1);
    Qpack.decode_args.scratch = tiny;
    Qpack.decode_args.scratch_cap = sizeof(tiny);
    Qpack.decode_args.emit = collect;
    Qpack.decode_args.ctx = &g_sink;
    Qpack.decode(qpack_work);
    TEST_ASSERT_FALSE(Qpack.ok);
}

// An emit callback that returns false stops the decode and is reported as a failure, so a caller
// that runs out of room for headers is not told the section decoded cleanly.
void test_emit_refusal_aborts_the_decode(void)
{
    static const uint8_t B1[15] = {0x00, 0x00, 0x51, 0x0b, '/', 'i', 'n', 'd', 'e', 'x', '.', 'h', 't', 'm', 'l'};
    Qpack.decode_args.block = B1;
    Qpack.decode_args.len = sizeof(B1);
    Qpack.decode_args.scratch = g_scratch;
    Qpack.decode_args.scratch_cap = sizeof(g_scratch);
    Qpack.decode_args.emit = refuse;
    Qpack.decode_args.ctx = NULL;
    Qpack.decode(qpack_work);
    TEST_ASSERT_FALSE(Qpack.ok);
}

// A destination that cannot hold the whole representation yields 0, so a caller never ships a field
// line that stops in the middle of a string.
void test_encoder_refuses_a_short_destination(void)
{
    Qpack.encode_header_args.out = g_out;
    Qpack.encode_header_args.cap = 0;
    Qpack.encode_header_args.name = ":method";
    Qpack.encode_header_args.name_len = 7;
    Qpack.encode_header_args.value = "GET";
    Qpack.encode_header_args.value_len = 3;
    Qpack.encode_header(qpack_work);
    TEST_ASSERT_EQUAL_UINT(0u, Qpack.n);
    Qpack.encode_header_args.out = g_out;
    Qpack.encode_header_args.cap = 1;
    Qpack.encode_header_args.name = ":path";
    Qpack.encode_header_args.name_len = 5;
    Qpack.encode_header_args.value = "/index.html";
    Qpack.encode_header_args.value_len = 11;
    Qpack.encode_header(qpack_work);
    TEST_ASSERT_EQUAL_UINT(0u, Qpack.n);
    Qpack.encode_header_args.out = g_out;
    Qpack.encode_header_args.cap = 2;
    Qpack.encode_header_args.name = "x-protocore-trace";
    Qpack.encode_header_args.name_len = 17;
    Qpack.encode_header_args.value = "abc123";
    Qpack.encode_header_args.value_len = 6;
    Qpack.encode_header(qpack_work);
    TEST_ASSERT_EQUAL_UINT(0u, Qpack.n);
}
