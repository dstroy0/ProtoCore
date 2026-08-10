// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the HPACK codec (network_drivers/presentation/http/http2/hpack) against RFC 7541.
//
// The Appendix C worked examples are carried byte for byte - the four representation forms (C.2),
// both request sequences (C.3, C.4) and both response sequences (C.5, C.6, which run with the
// table capped at 256 so eviction happens), each checked against the appendix's own dynamic-table
// size and entry count after every block. Appendix A's 61 static-table entries and Appendix B's
// 256 octet codes are enumerated in full. Those blocks never touch the encoder, so a symmetric
// codec fault has nowhere to hide. Around them: prefix-integer coding (C.1) and its overflow
// bound, dynamic-table size updates, and the server encoder.

#include "network_drivers/presentation/codec/hpack_prim/hpack_prim.h" // prefix-int + Huffman primitives
#include "network_drivers/presentation/http/http2/hpack.h"
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// Fixed collection of the emitted header list: the decoder hands each field back as a
// pointer + length, so each one is copied into its own bounded slot and NUL-terminated.
#define COLLECT_MAX 32
#define COLLECT_LEN 256
typedef struct
{
    char name[COLLECT_LEN];
    char value[COLLECT_LEN];
} CollectedField;
typedef struct
{
    CollectedField f[COLLECT_MAX];
    size_t n;
} Collected;
static proto_bool collect(void *ctx, const char *n, size_t nl, const char *v, size_t vl)
{
    Collected *c = (Collected *)ctx;
    if (c->n >= COLLECT_MAX || nl >= COLLECT_LEN || vl >= COLLECT_LEN)
    {
        return PROTO_FALSE;
    }
    memcpy(c->f[c->n].name, n, nl);
    c->f[c->n].name[nl] = 0;
    memcpy(c->f[c->n].value, v, vl);
    c->f[c->n].value[vl] = 0;
    c->n++;
    return PROTO_TRUE;
}

void test_int_coding()
{
    uint8_t b[8];
    size_t c;
    uint32_t v;
    // C.1.1: 10, prefix 5 -> 0x0a
    TEST_ASSERT_EQUAL_INT(1, (int)HpackPrim.encode_int(b, sizeof b, 5, 0, 10));
    TEST_ASSERT_EQUAL_HEX8(0x0a, b[0]);
    TEST_ASSERT_TRUE(HpackPrim.decode_int(b, 1, 5, &c, &v));
    TEST_ASSERT_EQUAL_UINT32(10, v);
    // C.1.2: 1337, prefix 5 -> 1f 9a 0a
    TEST_ASSERT_EQUAL_INT(3, (int)HpackPrim.encode_int(b, sizeof b, 5, 0, 1337));
    const uint8_t exp[3] = {0x1f, 0x9a, 0x0a};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, b, 3);
    TEST_ASSERT_TRUE(HpackPrim.decode_int(b, 3, 5, &c, &v));
    TEST_ASSERT_EQUAL_UINT32(1337, v);
    TEST_ASSERT_EQUAL_INT(3, (int)c);
    // C.1.3: 42, prefix 8 -> 0x2a
    TEST_ASSERT_EQUAL_INT(1, (int)HpackPrim.encode_int(b, sizeof b, 8, 0, 42));
    TEST_ASSERT_EQUAL_HEX8(0x2a, b[0]);
}

// RFC 7541 sec 5.1: "Integer encodings that exceed implementation limits - in value or octet length -
// MUST be treated as decoding errors." The continuation is bounded at m > 28, which admits m == 28,
// where only the low four bits survive a 32-bit shift. Anything wider silently wrapped: the encoding
// of 34091302943 came back TRUE as 4026531871.
void test_int_decode_rejects_overflowing_prefix_int()
{
    size_t c = 0;
    uint32_t v = 0;

    // 0x7f << 28 does not fit 32 bits.
    const uint8_t over[6] = {0x1f, 0x80, 0x80, 0x80, 0x80, 0x7f};
    TEST_ASSERT_FALSE(HpackPrim.decode_int(over, sizeof over, 5, &c, &v));

    // 0x10 << 28 is the first that does not fit; 0x0f << 28 is the last that does.
    const uint8_t just_over[6] = {0x1f, 0x80, 0x80, 0x80, 0x80, 0x10};
    TEST_ASSERT_FALSE(HpackPrim.decode_int(just_over, sizeof just_over, 5, &c, &v));

    const uint8_t largest[6] = {0x1f, 0x80, 0x80, 0x80, 0x80, 0x0f};
    TEST_ASSERT_TRUE(HpackPrim.decode_int(largest, sizeof largest, 5, &c, &v));
    TEST_ASSERT_EQUAL_UINT32(31u + (0x0fu << 28), v);
    TEST_ASSERT_EQUAL_INT(6, (int)c);

    // The octet-length bound still holds: a sixth continuation byte is past m == 28.
    const uint8_t too_long[7] = {0x1f, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00};
    TEST_ASSERT_FALSE(HpackPrim.decode_int(too_long, sizeof too_long, 5, &c, &v));
}

void test_huffman()
{
    const char *s = "www.example.com";
    size_t n = strlen(s);
    const uint8_t exp[12] = {0xf1, 0xe3, 0xc2, 0xe5, 0xf2, 0x3a, 0x6b, 0xa0, 0xab, 0x90, 0xf4, 0xff};
    TEST_ASSERT_EQUAL_INT(12, (int)HpackPrim.huff_len(s, n));
    uint8_t out[32];
    TEST_ASSERT_EQUAL_INT(12, (int)HpackPrim.huff_encode(out, sizeof out, s, n));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, out, 12);
    char dec[32];
    size_t dl;
    TEST_ASSERT_TRUE(HpackPrim.huff_decode(exp, 12, dec, sizeof dec, &dl));
    TEST_ASSERT_EQUAL_INT((int)n, (int)dl);
    TEST_ASSERT_EQUAL_MEMORY(s, dec, n);
}

void test_decode_c31_and_index()
{
    // RFC 7541 C.3.1: GET / with :authority www.example.com (no Huffman).
    const uint8_t block[] = {0x82, 0x86, 0x84, 0x41, 0x0f, 0x77, 0x77, 0x77, 0x2e, 0x65,
                             0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d};
    HpackDynTable t;
    pc_hpack_dyn_init(&t, 4096);
    Collected c = {0};
    char scratch[512];
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, block, sizeof block, scratch, sizeof scratch, collect, &c));
    TEST_ASSERT_EQUAL_INT(4, (int)c.n);
    TEST_ASSERT_EQUAL_STRING(":method", c.f[0].name);
    TEST_ASSERT_EQUAL_STRING("GET", c.f[0].value);
    TEST_ASSERT_EQUAL_STRING(":scheme", c.f[1].name);
    TEST_ASSERT_EQUAL_STRING("http", c.f[1].value);
    TEST_ASSERT_EQUAL_STRING(":path", c.f[2].name);
    TEST_ASSERT_EQUAL_STRING("/", c.f[2].value);
    TEST_ASSERT_EQUAL_STRING(":authority", c.f[3].name);
    TEST_ASSERT_EQUAL_STRING("www.example.com", c.f[3].value);
    // RFC: the dynamic table now holds one entry of size 57.
    TEST_ASSERT_EQUAL_UINT32(57, t.used);
    TEST_ASSERT_EQUAL_INT(1, (int)t.ecount);

    // An indexed reference to entry 62 (0x80|62 = 0xbe) resolves it from the dynamic table.
    const uint8_t idx62[] = {0xbe};
    Collected c2 = {0};
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, idx62, 1, scratch, sizeof scratch, collect, &c2));
    TEST_ASSERT_EQUAL_INT(1, (int)c2.n);
    TEST_ASSERT_EQUAL_STRING(":authority", c2.f[0].name);
    TEST_ASSERT_EQUAL_STRING("www.example.com", c2.f[0].value);
}

// ---------------------------------------------------------------------------
// RFC 7541 Appendix C worked examples, byte for byte.
//
// Every octet array below is the appendix's "Hex dump of encoded data" verbatim, so nothing here
// runs through the encoder: a symmetric codec fault has nowhere to hide. The table checkpoints
// (size and entry count) after each block are the appendix's own "Dynamic Table (after decoding)".
// ---------------------------------------------------------------------------

static void vec_decode(HpackDynTable *t, const uint8_t *blk, size_t n, Collected *c)
{
    char scratch[512];
    memset(c, 0, sizeof *c);
    TEST_ASSERT_TRUE(pc_hpack_decode(t, blk, n, scratch, sizeof scratch, collect, c));
}

static void vec_field(const Collected *c, size_t i, const char *name, const char *value)
{
    TEST_ASSERT_TRUE(i < c->n);
    TEST_ASSERT_EQUAL_STRING(name, c->f[i].name);
    TEST_ASSERT_EQUAL_STRING(value, c->f[i].value);
}

// The four values the C.5 / C.6 response sequences carry, spelled once.
#define C5_DATE21 "Mon, 21 Oct 2013 20:13:21 GMT"
#define C5_DATE22 "Mon, 21 Oct 2013 20:13:22 GMT"
#define C5_LOCATION "https://www.example.com"
#define C5_COOKIE "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1"

// C.2.1-C.2.4: the four representation forms, each decoded on a fresh table so the appendix's
// "Dynamic Table (after decoding)" is exactly what the table must hold.
void test_c2_representation_vectors()
{
    HpackDynTable t;
    Collected c;

    // C.2.1 literal with incremental indexing, literal name: the only one that indexes.
    const uint8_t c21[26] = {0x40, 0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65, 0x79, 0x0d,
                             0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x68, 0x65, 0x61, 0x64, 0x65, 0x72};
    pc_hpack_dyn_init(&t, 4096);
    vec_decode(&t, c21, sizeof c21, &c);
    TEST_ASSERT_EQUAL_INT(1, (int)c.n);
    vec_field(&c, 0, "custom-key", "custom-header");
    TEST_ASSERT_EQUAL_UINT32(55, t.used);
    TEST_ASSERT_EQUAL_INT(1, (int)t.ecount);

    // C.2.2 literal without indexing, indexed name (idx 4 = :path).
    const uint8_t c22[14] = {0x04, 0x0c, 0x2f, 0x73, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2f, 0x70, 0x61, 0x74, 0x68};
    pc_hpack_dyn_init(&t, 4096);
    vec_decode(&t, c22, sizeof c22, &c);
    TEST_ASSERT_EQUAL_INT(1, (int)c.n);
    vec_field(&c, 0, ":path", "/sample/path");
    TEST_ASSERT_EQUAL_UINT32(0, t.used);
    TEST_ASSERT_EQUAL_INT(0, (int)t.ecount);

    // C.2.3 literal never indexed (the 0001 pattern). Sec 6.2.3: it must not reach the table.
    const uint8_t c23[17] = {0x10, 0x08, 0x70, 0x61, 0x73, 0x73, 0x77, 0x6f, 0x72,
                             0x64, 0x06, 0x73, 0x65, 0x63, 0x72, 0x65, 0x74};
    pc_hpack_dyn_init(&t, 4096);
    vec_decode(&t, c23, sizeof c23, &c);
    TEST_ASSERT_EQUAL_INT(1, (int)c.n);
    vec_field(&c, 0, "password", "secret");
    TEST_ASSERT_EQUAL_UINT32(0, t.used);
    TEST_ASSERT_EQUAL_INT(0, (int)t.ecount);

    // The 0000 (without indexing) form of the same field: same header out, table still untouched.
    // Decoding is byte-compatible between the two patterns; what must hold for both is that
    // neither indexes.
    uint8_t c23_without[17];
    memcpy(c23_without, c23, sizeof c23);
    c23_without[0] = 0x00;
    pc_hpack_dyn_init(&t, 4096);
    vec_decode(&t, c23_without, sizeof c23_without, &c);
    TEST_ASSERT_EQUAL_INT(1, (int)c.n);
    vec_field(&c, 0, "password", "secret");
    TEST_ASSERT_EQUAL_UINT32(0, t.used);
    TEST_ASSERT_EQUAL_INT(0, (int)t.ecount);

    // C.2.4 indexed header field, static index 2.
    const uint8_t c24[1] = {0x82};
    pc_hpack_dyn_init(&t, 4096);
    vec_decode(&t, c24, sizeof c24, &c);
    TEST_ASSERT_EQUAL_INT(1, (int)c.n);
    vec_field(&c, 0, ":method", "GET");
    TEST_ASSERT_EQUAL_UINT32(0, t.used);
    TEST_ASSERT_EQUAL_INT(0, (int)t.ecount);
}

// C.3.1-C.3.3: three requests on one connection, no Huffman. The second and third resolve
// dynamic indices 62 and 63, so they only decode if the first two left the table exactly right.
void test_c3_request_sequence()
{
    const uint8_t c31[20] = {0x82, 0x86, 0x84, 0x41, 0x0f, 0x77, 0x77, 0x77, 0x2e, 0x65,
                             0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d};
    const uint8_t c32[14] = {0x82, 0x86, 0x84, 0xbe, 0x58, 0x08, 0x6e, 0x6f, 0x2d, 0x63, 0x61, 0x63, 0x68, 0x65};
    const uint8_t c33[29] = {0x82, 0x87, 0x85, 0xbf, 0x40, 0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65,
                             0x79, 0x0c, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x76, 0x61, 0x6c, 0x75, 0x65};
    HpackDynTable t;
    Collected c;
    pc_hpack_dyn_init(&t, 4096);

    vec_decode(&t, c31, sizeof c31, &c);
    TEST_ASSERT_EQUAL_INT(4, (int)c.n);
    vec_field(&c, 0, ":method", "GET");
    vec_field(&c, 1, ":scheme", "http");
    vec_field(&c, 2, ":path", "/");
    vec_field(&c, 3, ":authority", "www.example.com");
    TEST_ASSERT_EQUAL_UINT32(57, t.used);
    TEST_ASSERT_EQUAL_INT(1, (int)t.ecount);

    vec_decode(&t, c32, sizeof c32, &c);
    TEST_ASSERT_EQUAL_INT(5, (int)c.n);
    vec_field(&c, 3, ":authority", "www.example.com"); // 0xbe, out of the dynamic table
    vec_field(&c, 4, "cache-control", "no-cache");
    TEST_ASSERT_EQUAL_UINT32(110, t.used);
    TEST_ASSERT_EQUAL_INT(2, (int)t.ecount);

    vec_decode(&t, c33, sizeof c33, &c);
    TEST_ASSERT_EQUAL_INT(5, (int)c.n);
    vec_field(&c, 0, ":method", "GET");
    vec_field(&c, 1, ":scheme", "https");
    vec_field(&c, 2, ":path", "/index.html");
    vec_field(&c, 3, ":authority", "www.example.com"); // 0xbf, now index 63
    vec_field(&c, 4, "custom-key", "custom-value");
    TEST_ASSERT_EQUAL_UINT32(164, t.used);
    TEST_ASSERT_EQUAL_INT(3, (int)t.ecount);
}

// C.4.1-C.4.3: the same three requests with Huffman-coded literals.
void test_c4_request_sequence_huffman()
{
    const uint8_t c41[17] = {0x82, 0x86, 0x84, 0x41, 0x8c, 0xf1, 0xe3, 0xc2, 0xe5,
                             0xf2, 0x3a, 0x6b, 0xa0, 0xab, 0x90, 0xf4, 0xff};
    const uint8_t c42[12] = {0x82, 0x86, 0x84, 0xbe, 0x58, 0x86, 0xa8, 0xeb, 0x10, 0x64, 0x9c, 0xbf};
    const uint8_t c43[24] = {0x82, 0x87, 0x85, 0xbf, 0x40, 0x88, 0x25, 0xa8, 0x49, 0xe9, 0x5b, 0xa9,
                             0x7d, 0x7f, 0x89, 0x25, 0xa8, 0x49, 0xe9, 0x5b, 0xb8, 0xe8, 0xb4, 0xbf};
    HpackDynTable t;
    Collected c;
    pc_hpack_dyn_init(&t, 4096);

    vec_decode(&t, c41, sizeof c41, &c);
    TEST_ASSERT_EQUAL_INT(4, (int)c.n);
    vec_field(&c, 3, ":authority", "www.example.com");
    TEST_ASSERT_EQUAL_UINT32(57, t.used);
    TEST_ASSERT_EQUAL_INT(1, (int)t.ecount);

    vec_decode(&t, c42, sizeof c42, &c);
    TEST_ASSERT_EQUAL_INT(5, (int)c.n);
    vec_field(&c, 4, "cache-control", "no-cache");
    TEST_ASSERT_EQUAL_UINT32(110, t.used);
    TEST_ASSERT_EQUAL_INT(2, (int)t.ecount);

    vec_decode(&t, c43, sizeof c43, &c);
    TEST_ASSERT_EQUAL_INT(5, (int)c.n);
    vec_field(&c, 2, ":path", "/index.html");
    vec_field(&c, 4, "custom-key", "custom-value");
    TEST_ASSERT_EQUAL_UINT32(164, t.used);
    TEST_ASSERT_EQUAL_INT(3, (int)t.ecount);
}

// C.5.1-C.5.3: three responses with the table capped at 256, which is what forces eviction.
// C.5.2 evicts one entry; C.5.3 evicts three inside a single block.
void test_c5_response_sequence_evicts()
{
    const uint8_t c51[70] = {0x48, 0x03, 0x33, 0x30, 0x32, 0x58, 0x07, 0x70, 0x72, 0x69, 0x76, 0x61, 0x74, 0x65,
                             0x61, 0x1d, 0x4d, 0x6f, 0x6e, 0x2c, 0x20, 0x32, 0x31, 0x20, 0x4f, 0x63, 0x74, 0x20,
                             0x32, 0x30, 0x31, 0x33, 0x20, 0x32, 0x30, 0x3a, 0x31, 0x33, 0x3a, 0x32, 0x31, 0x20,
                             0x47, 0x4d, 0x54, 0x6e, 0x17, 0x68, 0x74, 0x74, 0x70, 0x73, 0x3a, 0x2f, 0x2f, 0x77,
                             0x77, 0x77, 0x2e, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d};
    const uint8_t c52[8] = {0x48, 0x03, 0x33, 0x30, 0x37, 0xc1, 0xc0, 0xbf};
    const uint8_t c53[98] = {0x88, 0xc1, 0x61, 0x1d, 0x4d, 0x6f, 0x6e, 0x2c, 0x20, 0x32, 0x31, 0x20, 0x4f, 0x63,
                             0x74, 0x20, 0x32, 0x30, 0x31, 0x33, 0x20, 0x32, 0x30, 0x3a, 0x31, 0x33, 0x3a, 0x32,
                             0x32, 0x20, 0x47, 0x4d, 0x54, 0xc0, 0x5a, 0x04, 0x67, 0x7a, 0x69, 0x70, 0x77, 0x38,
                             0x66, 0x6f, 0x6f, 0x3d, 0x41, 0x53, 0x44, 0x4a, 0x4b, 0x48, 0x51, 0x4b, 0x42, 0x5a,
                             0x58, 0x4f, 0x51, 0x57, 0x45, 0x4f, 0x50, 0x49, 0x55, 0x41, 0x58, 0x51, 0x57, 0x45,
                             0x4f, 0x49, 0x55, 0x3b, 0x20, 0x6d, 0x61, 0x78, 0x2d, 0x61, 0x67, 0x65, 0x3d, 0x33,
                             0x36, 0x30, 0x30, 0x3b, 0x20, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e, 0x3d, 0x31};
    HpackDynTable t;
    Collected c;
    pc_hpack_dyn_init(&t, 256);

    vec_decode(&t, c51, sizeof c51, &c);
    TEST_ASSERT_EQUAL_INT(4, (int)c.n);
    vec_field(&c, 0, ":status", "302");
    vec_field(&c, 1, "cache-control", "private");
    vec_field(&c, 2, "date", C5_DATE21);
    vec_field(&c, 3, "location", C5_LOCATION);
    TEST_ASSERT_EQUAL_UINT32(222, t.used);
    TEST_ASSERT_EQUAL_INT(4, (int)t.ecount);

    // ":status: 302" is evicted to make room for ":status: 307"; the other three come back by index.
    vec_decode(&t, c52, sizeof c52, &c);
    TEST_ASSERT_EQUAL_INT(4, (int)c.n);
    vec_field(&c, 0, ":status", "307");
    vec_field(&c, 1, "cache-control", "private");
    vec_field(&c, 2, "date", C5_DATE21);
    vec_field(&c, 3, "location", C5_LOCATION);
    TEST_ASSERT_EQUAL_UINT32(222, t.used);
    TEST_ASSERT_EQUAL_INT(4, (int)t.ecount);

    // Three evictions inside this one block: cache-control, then the 20:13:21 date, then
    // location and ":status: 307" for the 98-byte set-cookie.
    vec_decode(&t, c53, sizeof c53, &c);
    TEST_ASSERT_EQUAL_INT(6, (int)c.n);
    vec_field(&c, 0, ":status", "200");
    vec_field(&c, 1, "cache-control", "private");
    vec_field(&c, 2, "date", C5_DATE22);
    vec_field(&c, 3, "location", C5_LOCATION);
    vec_field(&c, 4, "content-encoding", "gzip");
    vec_field(&c, 5, "set-cookie", C5_COOKIE);
    TEST_ASSERT_EQUAL_UINT32(215, t.used);
    TEST_ASSERT_EQUAL_INT(3, (int)t.ecount);
}

// C.6.1-C.6.3: the same three responses Huffman-coded. Eviction runs on the decoded lengths, so
// the table checkpoints are identical to C.5 even though every block is shorter on the wire.
void test_c6_response_sequence_huffman_evicts()
{
    const uint8_t c61[54] = {0x48, 0x82, 0x64, 0x02, 0x58, 0x85, 0xae, 0xc3, 0x77, 0x1a, 0x4b, 0x61, 0x96, 0xd0,
                             0x7a, 0xbe, 0x94, 0x10, 0x54, 0xd4, 0x44, 0xa8, 0x20, 0x05, 0x95, 0x04, 0x0b, 0x81,
                             0x66, 0xe0, 0x82, 0xa6, 0x2d, 0x1b, 0xff, 0x6e, 0x91, 0x9d, 0x29, 0xad, 0x17, 0x18,
                             0x63, 0xc7, 0x8f, 0x0b, 0x97, 0xc8, 0xe9, 0xae, 0x82, 0xae, 0x43, 0xd3};
    const uint8_t c62[8] = {0x48, 0x83, 0x64, 0x0e, 0xff, 0xc1, 0xc0, 0xbf};
    const uint8_t c63[79] = {0x88, 0xc1, 0x61, 0x96, 0xd0, 0x7a, 0xbe, 0x94, 0x10, 0x54, 0xd4, 0x44, 0xa8, 0x20,
                             0x05, 0x95, 0x04, 0x0b, 0x81, 0x66, 0xe0, 0x84, 0xa6, 0x2d, 0x1b, 0xff, 0xc0, 0x5a,
                             0x83, 0x9b, 0xd9, 0xab, 0x77, 0xad, 0x94, 0xe7, 0x82, 0x1d, 0xd7, 0xf2, 0xe6, 0xc7,
                             0xb3, 0x35, 0xdf, 0xdf, 0xcd, 0x5b, 0x39, 0x60, 0xd5, 0xaf, 0x27, 0x08, 0x7f, 0x36,
                             0x72, 0xc1, 0xab, 0x27, 0x0f, 0xb5, 0x29, 0x1f, 0x95, 0x87, 0x31, 0x60, 0x65, 0xc0,
                             0x03, 0xed, 0x4e, 0xe5, 0xb1, 0x06, 0x3d, 0x50, 0x07};
    HpackDynTable t;
    Collected c;
    pc_hpack_dyn_init(&t, 256);

    vec_decode(&t, c61, sizeof c61, &c);
    TEST_ASSERT_EQUAL_INT(4, (int)c.n);
    vec_field(&c, 0, ":status", "302");
    vec_field(&c, 2, "date", C5_DATE21);
    vec_field(&c, 3, "location", C5_LOCATION);
    TEST_ASSERT_EQUAL_UINT32(222, t.used);
    TEST_ASSERT_EQUAL_INT(4, (int)t.ecount);

    vec_decode(&t, c62, sizeof c62, &c);
    TEST_ASSERT_EQUAL_INT(4, (int)c.n);
    vec_field(&c, 0, ":status", "307");
    vec_field(&c, 3, "location", C5_LOCATION);
    TEST_ASSERT_EQUAL_UINT32(222, t.used);
    TEST_ASSERT_EQUAL_INT(4, (int)t.ecount);

    vec_decode(&t, c63, sizeof c63, &c);
    TEST_ASSERT_EQUAL_INT(6, (int)c.n);
    vec_field(&c, 0, ":status", "200");
    vec_field(&c, 2, "date", C5_DATE22);
    vec_field(&c, 4, "content-encoding", "gzip");
    vec_field(&c, 5, "set-cookie", C5_COOKIE);
    TEST_ASSERT_EQUAL_UINT32(215, t.used);
    TEST_ASSERT_EQUAL_INT(3, (int)t.ecount);
}

// RFC 7541 Appendix A, Table 1: the 61 static-table entries, name then value.
static const char *const RFC_STATIC[61][2] = {
    {":authority", ""},
    {":method", "GET"},
    {":method", "POST"},
    {":path", "/"},
    {":path", "/index.html"},
    {":scheme", "http"},
    {":scheme", "https"},
    {":status", "200"},
    {":status", "204"},
    {":status", "206"},
    {":status", "304"},
    {":status", "400"},
    {":status", "404"},
    {":status", "500"},
    {"accept-charset", ""},
    {"accept-encoding", "gzip, deflate"},
    {"accept-language", ""},
    {"accept-ranges", ""},
    {"accept", ""},
    {"access-control-allow-origin", ""},
    {"age", ""},
    {"allow", ""},
    {"authorization", ""},
    {"cache-control", ""},
    {"content-disposition", ""},
    {"content-encoding", ""},
    {"content-language", ""},
    {"content-length", ""},
    {"content-location", ""},
    {"content-range", ""},
    {"content-type", ""},
    {"cookie", ""},
    {"date", ""},
    {"etag", ""},
    {"expect", ""},
    {"expires", ""},
    {"from", ""},
    {"host", ""},
    {"if-match", ""},
    {"if-modified-since", ""},
    {"if-none-match", ""},
    {"if-range", ""},
    {"if-unmodified-since", ""},
    {"last-modified", ""},
    {"link", ""},
    {"location", ""},
    {"max-forwards", ""},
    {"proxy-authenticate", ""},
    {"proxy-authorization", ""},
    {"range", ""},
    {"referer", ""},
    {"refresh", ""},
    {"retry-after", ""},
    {"server", ""},
    {"set-cookie", ""},
    {"strict-transport-security", ""},
    {"transfer-encoding", ""},
    {"user-agent", ""},
    {"vary", ""},
    {"via", ""},
    {"www-authenticate", ""},
};

// RFC 7541 Appendix B: the Huffman code for each of the 256 octets (EOS, 256, excluded:
// it never encodes a symbol). Index is the octet, value is the code right-aligned.
static const uint32_t RFC_HUFF_CODE[256] = {
    0x00001ff8u, 0x007fffd8u, 0x0fffffe2u, 0x0fffffe3u, 0x0fffffe4u, 0x0fffffe5u, 0x0fffffe6u, 0x0fffffe7u, 0x0fffffe8u,
    0x00ffffeau, 0x3ffffffcu, 0x0fffffe9u, 0x0fffffeau, 0x3ffffffdu, 0x0fffffebu, 0x0fffffecu, 0x0fffffedu, 0x0fffffeeu,
    0x0fffffefu, 0x0ffffff0u, 0x0ffffff1u, 0x0ffffff2u, 0x3ffffffeu, 0x0ffffff3u, 0x0ffffff4u, 0x0ffffff5u, 0x0ffffff6u,
    0x0ffffff7u, 0x0ffffff8u, 0x0ffffff9u, 0x0ffffffau, 0x0ffffffbu, 0x00000014u, 0x000003f8u, 0x000003f9u, 0x00000ffau,
    0x00001ff9u, 0x00000015u, 0x000000f8u, 0x000007fau, 0x000003fau, 0x000003fbu, 0x000000f9u, 0x000007fbu, 0x000000fau,
    0x00000016u, 0x00000017u, 0x00000018u, 0x00000000u, 0x00000001u, 0x00000002u, 0x00000019u, 0x0000001au, 0x0000001bu,
    0x0000001cu, 0x0000001du, 0x0000001eu, 0x0000001fu, 0x0000005cu, 0x000000fbu, 0x00007ffcu, 0x00000020u, 0x00000ffbu,
    0x000003fcu, 0x00001ffau, 0x00000021u, 0x0000005du, 0x0000005eu, 0x0000005fu, 0x00000060u, 0x00000061u, 0x00000062u,
    0x00000063u, 0x00000064u, 0x00000065u, 0x00000066u, 0x00000067u, 0x00000068u, 0x00000069u, 0x0000006au, 0x0000006bu,
    0x0000006cu, 0x0000006du, 0x0000006eu, 0x0000006fu, 0x00000070u, 0x00000071u, 0x00000072u, 0x000000fcu, 0x00000073u,
    0x000000fdu, 0x00001ffbu, 0x0007fff0u, 0x00001ffcu, 0x00003ffcu, 0x00000022u, 0x00007ffdu, 0x00000003u, 0x00000023u,
    0x00000004u, 0x00000024u, 0x00000005u, 0x00000025u, 0x00000026u, 0x00000027u, 0x00000006u, 0x00000074u, 0x00000075u,
    0x00000028u, 0x00000029u, 0x0000002au, 0x00000007u, 0x0000002bu, 0x00000076u, 0x0000002cu, 0x00000008u, 0x00000009u,
    0x0000002du, 0x00000077u, 0x00000078u, 0x00000079u, 0x0000007au, 0x0000007bu, 0x00007ffeu, 0x000007fcu, 0x00003ffdu,
    0x00001ffdu, 0x0ffffffcu, 0x000fffe6u, 0x003fffd2u, 0x000fffe7u, 0x000fffe8u, 0x003fffd3u, 0x003fffd4u, 0x003fffd5u,
    0x007fffd9u, 0x003fffd6u, 0x007fffdau, 0x007fffdbu, 0x007fffdcu, 0x007fffddu, 0x007fffdeu, 0x00ffffebu, 0x007fffdfu,
    0x00ffffecu, 0x00ffffedu, 0x003fffd7u, 0x007fffe0u, 0x00ffffeeu, 0x007fffe1u, 0x007fffe2u, 0x007fffe3u, 0x007fffe4u,
    0x001fffdcu, 0x003fffd8u, 0x007fffe5u, 0x003fffd9u, 0x007fffe6u, 0x007fffe7u, 0x00ffffefu, 0x003fffdau, 0x001fffddu,
    0x000fffe9u, 0x003fffdbu, 0x003fffdcu, 0x007fffe8u, 0x007fffe9u, 0x001fffdeu, 0x007fffeau, 0x003fffddu, 0x003fffdeu,
    0x00fffff0u, 0x001fffdfu, 0x003fffdfu, 0x007fffebu, 0x007fffecu, 0x001fffe0u, 0x001fffe1u, 0x003fffe0u, 0x001fffe2u,
    0x007fffedu, 0x003fffe1u, 0x007fffeeu, 0x007fffefu, 0x000fffeau, 0x003fffe2u, 0x003fffe3u, 0x003fffe4u, 0x007ffff0u,
    0x003fffe5u, 0x003fffe6u, 0x007ffff1u, 0x03ffffe0u, 0x03ffffe1u, 0x000fffebu, 0x0007fff1u, 0x003fffe7u, 0x007ffff2u,
    0x003fffe8u, 0x01ffffecu, 0x03ffffe2u, 0x03ffffe3u, 0x03ffffe4u, 0x07ffffdeu, 0x07ffffdfu, 0x03ffffe5u, 0x00fffff1u,
    0x01ffffedu, 0x0007fff2u, 0x001fffe3u, 0x03ffffe6u, 0x07ffffe0u, 0x07ffffe1u, 0x03ffffe7u, 0x07ffffe2u, 0x00fffff2u,
    0x001fffe4u, 0x001fffe5u, 0x03ffffe8u, 0x03ffffe9u, 0x0ffffffdu, 0x07ffffe3u, 0x07ffffe4u, 0x07ffffe5u, 0x000fffecu,
    0x00fffff3u, 0x000fffedu, 0x001fffe6u, 0x003fffe9u, 0x001fffe7u, 0x001fffe8u, 0x007ffff3u, 0x003fffeau, 0x003fffebu,
    0x01ffffeeu, 0x01ffffefu, 0x00fffff4u, 0x00fffff5u, 0x03ffffeau, 0x007ffff4u, 0x03ffffebu, 0x07ffffe6u, 0x03ffffecu,
    0x03ffffedu, 0x07ffffe7u, 0x07ffffe8u, 0x07ffffe9u, 0x07ffffeau, 0x07ffffebu, 0x0ffffffeu, 0x07ffffecu, 0x07ffffedu,
    0x07ffffeeu, 0x07ffffefu, 0x07fffff0u, 0x03ffffeeu,
};
static const uint8_t RFC_HUFF_LEN[256] = {
    13, 23, 28, 28, 28, 28, 28, 28, 28, 24, 30, 28, 28, 30, 28, 28, 28, 28, 28, 28, 28, 28, 30, 28, 28, 28, 28, 28, 28,
    28, 28, 28, 6,  10, 10, 12, 13, 6,  8,  11, 10, 10, 8,  11, 8,  6,  6,  6,  5,  5,  5,  6,  6,  6,  6,  6,  6,  6,
    7,  8,  15, 6,  12, 10, 13, 6,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,
    7,  8,  7,  8,  13, 19, 13, 14, 6,  15, 5,  6,  5,  6,  5,  6,  6,  6,  5,  7,  7,  6,  6,  6,  5,  6,  7,  6,  5,
    5,  6,  7,  7,  7,  7,  7,  15, 11, 14, 13, 28, 20, 22, 20, 20, 22, 22, 22, 23, 22, 23, 23, 23, 23, 23, 24, 23, 24,
    24, 22, 23, 24, 23, 23, 23, 23, 21, 22, 23, 22, 23, 23, 24, 22, 21, 20, 22, 22, 23, 23, 21, 23, 22, 22, 24, 21, 22,
    23, 23, 21, 21, 22, 21, 23, 22, 23, 23, 20, 22, 22, 22, 23, 22, 22, 23, 26, 26, 20, 19, 22, 23, 22, 25, 26, 26, 26,
    27, 27, 26, 24, 25, 19, 21, 26, 27, 27, 26, 27, 24, 21, 21, 26, 26, 28, 27, 27, 27, 20, 24, 20, 21, 22, 21, 21, 23,
    22, 22, 25, 25, 24, 24, 26, 23, 26, 27, 26, 26, 27, 27, 27, 27, 27, 28, 27, 27, 27, 27, 27, 26,
};

// RFC 7541 Appendix A: every one of the 61 static-table entries, resolved through the indexed
// representation. The suite previously exercised a handful by hand, so a wrong row anywhere else
// in the table went unseen.
void test_static_table_matches_appendix_a()
{
    HpackDynTable t;
    Collected c;
    for (int i = 1; i <= 61; i++)
    {
        const uint8_t indexed[1] = {(uint8_t)(0x80 | i)};
        pc_hpack_dyn_init(&t, 4096);
        vec_decode(&t, indexed, 1, &c);
        TEST_ASSERT_EQUAL_INT(1, (int)c.n);
        TEST_ASSERT_EQUAL_STRING(RFC_STATIC[i - 1][0], c.f[0].name);
        TEST_ASSERT_EQUAL_STRING(RFC_STATIC[i - 1][1], c.f[0].value);
    }
}

// RFC 7541 Appendix B: every one of the 256 octet codes. A one-symbol string encodes to that
// symbol's code left-aligned, padded to the octet with the leading bits of EOS, which are all
// ones (sec 5.2). Both directions are checked against the RFC's own bits, not against each other.
void test_huffman_table_matches_appendix_b()
{
    for (int sym = 0; sym < 256; sym++)
    {
        const char in = (char)sym;
        const unsigned bits = RFC_HUFF_LEN[sym];
        const unsigned bytes = (bits + 7u) / 8u;
        const unsigned pad = bytes * 8u - bits;

        uint8_t expect[5];
        uint64_t padded = ((uint64_t)RFC_HUFF_CODE[sym] << pad) | ((1ull << pad) - 1ull);
        for (unsigned k = 0; k < bytes; k++)
        {
            expect[k] = (uint8_t)(padded >> (8u * (bytes - 1u - k)));
        }

        uint8_t out[8];
        TEST_ASSERT_EQUAL_UINT32(bytes, (uint32_t)HpackPrim.huff_len(&in, 1));
        TEST_ASSERT_EQUAL_UINT32(bytes, (uint32_t)HpackPrim.huff_encode(out, sizeof out, &in, 1));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, out, bytes);

        char back[8];
        size_t bl = 0;
        TEST_ASSERT_TRUE(HpackPrim.huff_decode(expect, bytes, back, sizeof back, &bl));
        TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)bl);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)sym, (uint8_t)back[0]);
    }
}

void test_dynamic_eviction()
{
    HpackDynTable t;
    pc_hpack_dyn_init(&t, 50); // room for one 36-byte entry, not two
    // Two literal-with-incremental-indexing inserts (name idx 0 + inline name/value), each size
    // 2+2+32 = 36. The second must evict the first.
    const uint8_t block[] = {0x40, 0x02, 'a', 'a', 0x02, 'b', 'b',  // insert (aa: bb)
                             0x40, 0x02, 'c', 'c', 0x02, 'd', 'd'}; // insert (cc: dd) -> evicts
    Collected c = {0};
    char scratch[128];
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, block, sizeof block, scratch, sizeof scratch, collect, &c));
    TEST_ASSERT_EQUAL_INT(2, (int)c.n);
    TEST_ASSERT_EQUAL_INT(1, (int)t.ecount); // only the newest survives
    TEST_ASSERT_EQUAL_UINT32(36, t.used);
    // Index 62 is now (cc: dd); the evicted (aa: bb) would have been 63 (invalid now).
    const uint8_t idx62[] = {0xbe};
    Collected c2 = {0};
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, idx62, 1, scratch, sizeof scratch, collect, &c2));
    TEST_ASSERT_EQUAL_STRING("cc", c2.f[0].name);
    TEST_ASSERT_EQUAL_STRING("dd", c2.f[0].value);
}

void test_encode_static()
{
    uint8_t out[64];
    TEST_ASSERT_EQUAL_INT(1, (int)pc_hpack_encode_header(out, sizeof out, ":method", 7, "GET", 3));
    TEST_ASSERT_EQUAL_HEX8(0x82, out[0]); // static index 2
    TEST_ASSERT_EQUAL_INT(1, (int)pc_hpack_encode_header(out, sizeof out, ":path", 5, "/", 1));
    TEST_ASSERT_EQUAL_HEX8(0x84, out[0]); // static index 4
    TEST_ASSERT_EQUAL_INT(1, (int)pc_hpack_encode_header(out, sizeof out, ":status", 7, "200", 3));
    TEST_ASSERT_EQUAL_HEX8(0x88, out[0]); // static index 8
}

void test_encode_decode_roundtrip()
{
    typedef struct
    {
        const char *n;
        const char *v;
    } KV;
    KV hs[] = {{":status", "200"},
               {"content-type", "text/html"},      // static name, literal value
               {"x-custom-header", "hello world"}, // fully literal
               {"server", "det/1"}};
    uint8_t block[512];
    size_t bo = 0;
    for (size_t k = 0; k < sizeof(hs) / sizeof(hs[0]); k++)
    {
        const KV *kv = &hs[k];
        size_t w = pc_hpack_encode_header(block + bo, sizeof block - bo, kv->n, strlen(kv->n), kv->v, strlen(kv->v));
        TEST_ASSERT_TRUE(w > 0);
        bo += w;
    }
    HpackDynTable t;
    pc_hpack_dyn_init(&t, 4096);
    Collected c = {0};
    char scratch[512];
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, block, bo, scratch, sizeof scratch, collect, &c));
    TEST_ASSERT_EQUAL_INT(4, (int)c.n);
    for (int i = 0; i < 4; i++)
    {
        TEST_ASSERT_EQUAL_STRING(hs[i].n, c.f[i].name);
        TEST_ASSERT_EQUAL_STRING(hs[i].v, c.f[i].value);
    }
    // The encoder never uses incremental indexing, so the decoder's table stays empty.
    TEST_ASSERT_EQUAL_INT(0, (int)t.ecount);
}

void test_reject_malformed()
{
    HpackDynTable t;
    pc_hpack_dyn_init(&t, 4096);
    Collected c = {0};
    char scratch[128];
    const uint8_t idx0[] = {0x80}; // indexed field, index 0 -> error
    TEST_ASSERT_FALSE(pc_hpack_decode(&t, idx0, 1, scratch, sizeof scratch, collect, &c));
    const uint8_t trunc[] = {0x41, 0x0f, 'w', 'w'}; // literal len 15 but only 2 bytes present
    TEST_ASSERT_FALSE(pc_hpack_decode(&t, trunc, sizeof trunc, scratch, sizeof scratch, collect, &c));
}

// RFC 7541 sec 6.3: a size update at or below the protocol's limit is applied, and one above it is a
// decoding error (RFC 9113 sec 4.3 then makes it a connection error). We advertise no
// SETTINGS_HEADER_TABLE_SIZE, so the limit is the RFC 9113 sec 6.5.2 default of 4096. A zero evicts.
void test_dyn_size_update()
{
    HpackDynTable t;
    pc_hpack_dyn_init(&t, 4096);
    char scratch[256];
    Collected c = {0};
    const uint8_t ins[] = {0x40, 0x02, 'a', 'a', 0x02, 'b', 'b'}; // insert (aa: bb), size 36
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, ins, sizeof ins, scratch, sizeof scratch, collect, &c));
    TEST_ASSERT_EQUAL_INT(1, (int)t.ecount);

    uint8_t up[8];
    Collected c2 = {0};
    size_t un = HpackPrim.encode_int(up, sizeof up, 5, 0x20, 100000); // over the limit -> refused
    TEST_ASSERT_FALSE(pc_hpack_decode(&t, up, un, scratch, sizeof scratch, collect, &c2));

    un = HpackPrim.encode_int(up, sizeof up, 5, 0x20, 4097); // one over -> refused
    TEST_ASSERT_FALSE(pc_hpack_decode(&t, up, un, scratch, sizeof scratch, collect, &c2));

    un = HpackPrim.encode_int(up, sizeof up, 5, 0x20, 4096); // exactly the limit -> applied
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, up, un, scratch, sizeof scratch, collect, &c2));
    TEST_ASSERT_EQUAL_INT(1, (int)t.ecount);

    const uint8_t z[] = {0x20}; // size update to 0 -> evicts everything
    Collected c3 = {0};
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, z, 1, scratch, sizeof scratch, collect, &c3));
    TEST_ASSERT_EQUAL_INT(0, (int)t.ecount);
}

// An entry larger than the max size clears the table without inserting (RFC 7541 sec 4.4).
void test_oversize_entry_clears()
{
    HpackDynTable t;
    pc_hpack_dyn_init(&t, 40); // (aaaaa: bbbbb) = 5+5+32 = 42 > 40
    char scratch[256];
    Collected c = {0};
    const uint8_t ins[] = {0x40, 0x05, 'a', 'a', 'a', 'a', 'a', 0x05, 'b', 'b', 'b', 'b', 'b'};
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, ins, sizeof ins, scratch, sizeof scratch, collect, &c));
    TEST_ASSERT_EQUAL_INT(1, (int)c.n);      // still emitted
    TEST_ASSERT_EQUAL_INT(0, (int)t.ecount); // but the table was cleared, nothing indexed
}

// Resolve a name from the dynamic table (literal indexed name) and index a whole dynamic entry.
void test_dynamic_name_and_index()
{
    HpackDynTable t;
    pc_hpack_dyn_init(&t, 4096);
    char scratch[256];
    Collected c = {0};
    const uint8_t ins[] = {0x40, 0x06, 'm', 'y', 'n', 'a', 'm', 'e', 0x02, 'v', '1'}; // insert (myname: v1)
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, ins, sizeof ins, scratch, sizeof scratch, collect, &c));
    // literal (incremental) with name index 62 (the dynamic "myname") + value "v2"
    const uint8_t litname[] = {0x7e, 0x02, 'v', '2'};
    Collected c2 = {0};
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, litname, sizeof litname, scratch, sizeof scratch, collect, &c2));
    TEST_ASSERT_EQUAL_STRING("myname", c2.f[0].name); // name came from the dynamic table
    TEST_ASSERT_EQUAL_STRING("v2", c2.f[0].value);
    // indexed reference (0x80|62) to the newest dynamic entry (myname: v2)
    const uint8_t idx[] = {0xbe};
    Collected c3 = {0};
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, idx, 1, scratch, sizeof scratch, collect, &c3));
    TEST_ASSERT_EQUAL_STRING("myname", c3.f[0].name);
    TEST_ASSERT_EQUAL_STRING("v2", c3.f[0].value);
}

// Decoder fail-closed paths: bad dynamic index, missing/oversized strings, bad name index.
void test_hpack_decode_errors()
{
    HpackDynTable t;
    char scratch[256];
    Collected c = {0};
    pc_hpack_dyn_init(&t, 4096); // indexed ref 62 into an empty dynamic table -> dyn_entry null
    const uint8_t idx62[] = {0xbe};
    TEST_ASSERT_FALSE(pc_hpack_decode(&t, idx62, 1, scratch, sizeof scratch, collect, &c));
    pc_hpack_dyn_init(&t, 4096); // literal, inline name, but the block ends before the name string
    const uint8_t noname[] = {0x40};
    TEST_ASSERT_FALSE(pc_hpack_decode(&t, noname, 1, scratch, sizeof scratch, collect, &c));
    pc_hpack_dyn_init(&t, 4096); // literal with name index 62 but the dynamic table is empty
    const uint8_t badname[] = {0x7e, 0x00};
    TEST_ASSERT_FALSE(pc_hpack_decode(&t, badname, sizeof badname, scratch, sizeof scratch, collect, &c));
    pc_hpack_dyn_init(&t, 4096); // literal-without-indexing (0x00), then truncated -> decode_literal fails
    const uint8_t litni[] = {0x00};
    TEST_ASSERT_FALSE(pc_hpack_decode(&t, litni, 1, scratch, sizeof scratch, collect, &c));
    pc_hpack_dyn_init(&t, 4096);           // a length varint that never terminates -> decode_int fails
    const uint8_t badint[] = {0x40, 0xff}; // name string byte 0xff (huff + 7-bit len 0x7f, no continuation)
    TEST_ASSERT_FALSE(pc_hpack_decode(&t, badint, sizeof badint, scratch, sizeof scratch, collect, &c));
}

// Scratch/output too small: the resolve, emit, and decode-string bounds all fail closed.
void test_hpack_buffer_bounds()
{
    HpackDynTable t;
    Collected c = {0};
    char tiny[4];
    pc_hpack_dyn_init(&t, 4096);
    // indexed static entry 2 (:method GET, 10 bytes) into a 4-byte scratch -> emit_indexed too big
    const uint8_t idx2[] = {0x82};
    TEST_ASSERT_FALSE(pc_hpack_decode(&t, idx2, 1, tiny, sizeof tiny, collect, &c));
    pc_hpack_dyn_init(&t, 4096); // literal name index 1 (:authority, 10 bytes) into a 4-byte scratch
    const uint8_t litstatic[] = {0x41, 0x00};
    TEST_ASSERT_FALSE(pc_hpack_decode(&t, litstatic, sizeof litstatic, tiny, sizeof tiny, collect, &c));
    pc_hpack_dyn_init(&t, 4096); // literal "" name + a 10-byte value into a 4-byte scratch
    const uint8_t bigval[] = {0x40, 0x00, 0x0a, '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_FALSE(pc_hpack_decode(&t, bigval, sizeof bigval, tiny, sizeof tiny, collect, &c));
    // indexed dynamic entry into a tiny scratch -> emit_indexed dynamic too big
    pc_hpack_dyn_init(&t, 4096);
    char scratch[256];
    const uint8_t ins[] = {0x40, 0x06, 'm', 'y', 'n', 'a', 'm', 'e', 0x02, 'v', '1'};
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, ins, sizeof ins, scratch, sizeof scratch, collect, &c));
    const uint8_t idxd[] = {0xbe};
    TEST_ASSERT_FALSE(pc_hpack_decode(&t, idxd, 1, tiny, sizeof tiny, collect, &c));
}

// A literal whose name comes from the dynamic table (name index 62), but the entry's name is
// longer than the caller's scratch: resolve_name's dynamic branch must fail closed (distinct from
// the "index not found" case above -- here the entry exists, it's just too big to copy out).
void test_hpack_resolve_dynamic_name_too_big(void)
{
    HpackDynTable t;
    pc_hpack_dyn_init(&t, 4096);
    char scratch[64];
    Collected c = {0};
    // insert (longname: v1); "longname" is 8 bytes
    const uint8_t ins[] = {0x40, 0x08, 'l', 'o', 'n', 'g', 'n', 'a', 'm', 'e', 0x02, 'v', '1'};
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, ins, sizeof ins, scratch, sizeof scratch, collect, &c));
    // literal (incremental), name index 62 ("longname"), value "v2" -- but scratch is only 4 bytes,
    // too small for the 8-byte dynamic name.
    char tiny[4];
    Collected c2 = {0};
    const uint8_t litname[] = {0x7e, 0x02, 'v', '2'};
    TEST_ASSERT_FALSE(pc_hpack_decode(&t, litname, sizeof litname, tiny, sizeof tiny, collect, &c2));
}

// Encoder: the non-Huffman string path, output-overflow fail-closed, and the size clamp on init.
void test_hpack_encode_paths()
{
    // pc_hpack_dyn_init clamps a too-large max to the table storage.
    HpackDynTable t;
    pc_hpack_dyn_init(&t, 0xffffffffu);
    TEST_ASSERT_TRUE(t.max_size <= PC_HPACK_TABLE_BYTES);

    // A value whose Huffman form is not shorter takes the literal (non-Huffman) string path.
    uint8_t out[64];
    const char nul[1] = {0}; // 0x00 has a 13-bit Huffman code: 1 byte plain < huffman
    size_t w = pc_hpack_encode_header(out, sizeof out, "x", 1, nul, 1);
    TEST_ASSERT_TRUE(w > 0);
    // the same non-Huffman value, but the buffer runs out during its literal body
    TEST_ASSERT_EQUAL_INT(0, (int)pc_hpack_encode_header(out, 4, "x", 1, nul, 1));

    // Output buffer too small at each stage -> encode returns 0 (fails closed).
    TEST_ASSERT_EQUAL_INT(0, (int)pc_hpack_encode_header(out, 0, "x-custom", 8, "value", 5)); // prefix int
    TEST_ASSERT_EQUAL_INT(0, (int)pc_hpack_encode_header(out, 2, "x-custom", 8, "value", 5)); // name string
    TEST_ASSERT_EQUAL_INT(0, (int)pc_hpack_encode_header(out, 8, "x-custom", 8, "value", 5)); // value string
}

// Malformed prefix-integers in the literal name-index and the size-update, and a
// Huffman string that decodes with invalid padding.
void test_hpack_more_errors()
{
    HpackDynTable t;
    char scratch[128];
    Collected c = {0};
    pc_hpack_dyn_init(&t, 4096); // literal, name-index prefix-6 = 63 -> needs a continuation byte, none
    const uint8_t badnameidx[] = {0x7f};
    TEST_ASSERT_FALSE(pc_hpack_decode(&t, badnameidx, 1, scratch, sizeof scratch, collect, &c));
    pc_hpack_dyn_init(&t, 4096); // size update, prefix-5 = 31 -> needs a continuation byte, none
    const uint8_t badupdate[] = {0x3f};
    TEST_ASSERT_FALSE(pc_hpack_decode(&t, badupdate, 1, scratch, sizeof scratch, collect, &c));
    pc_hpack_dyn_init(&t, 4096); // literal, inline name = Huffman "0" (00000) padded with 0s, not 1s
    const uint8_t badhuff[] = {0x00, 0x81, 0x00};
    TEST_ASSERT_FALSE(pc_hpack_decode(&t, badhuff, sizeof badhuff, scratch, sizeof scratch, collect, &c));
}

// pc_hpack_dyn_init's max_bytes=0 sentinel means "default to the full table storage".
void test_hpack_dyn_init_default_size(void)
{
    HpackDynTable t;
    pc_hpack_dyn_init(&t, 0);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)PC_HPACK_TABLE_BYTES, t.max_size);
}

// Indexed Header Field (top bit set) whose prefix-7 integer itself is truncated: the decode_int
// failure must be caught by pc_hpack_decode's own "!decode_int(...) || idx == 0" guard, not just
// the idx==0 half of it.
void test_hpack_indexed_field_truncated_int(void)
{
    HpackDynTable t;
    pc_hpack_dyn_init(&t, 4096);
    Collected c = {0};
    char scratch[128];
    const uint8_t trunc[] = {0xff}; // prefix-7 all-ones (127) demands a continuation byte, none given
    TEST_ASSERT_FALSE(pc_hpack_decode(&t, trunc, sizeof trunc, scratch, sizeof scratch, collect, &c));
}

// Encoder: a name that matches several static entries (":status" appears 7 times) but whose value
// never fully matches any of them. The loop must keep the *first* name match (index 8) without
// letting a later same-name entry overwrite it, and fall through to the literal-with-name-index path.
void test_hpack_encode_repeated_static_name(void)
{
    uint8_t out[64];
    size_t w = pc_hpack_encode_header(out, sizeof out, ":status", 7, "999", 3);
    TEST_ASSERT_TRUE(w > 0);
    TEST_ASSERT_EQUAL_HEX8(0x08, out[0]); // literal, name index 8 (the first ":status"), prefix 4

    HpackDynTable t;
    pc_hpack_dyn_init(&t, 4096);
    Collected c = {0};
    char scratch[64];
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, out, w, scratch, sizeof scratch, collect, &c));
    TEST_ASSERT_EQUAL_INT(1, (int)c.n);
    TEST_ASSERT_EQUAL_STRING(":status", c.f[0].name);
    TEST_ASSERT_EQUAL_STRING("999", c.f[0].value);
}

// Low-level pc_hpack_prim edge guards called directly: integer-encode buffer overflow (in the
// continuation loop and on the final byte), decode of a zero-length input, Huffman encode
// with no room for the trailing partial byte, a decoded EOS symbol, output overflow, and
// over-a-byte trailing padding.
void test_hpack_prim_edge_guards()
{
    uint8_t b[8];
    TEST_ASSERT_EQUAL_INT(0, (int)HpackPrim.encode_int(b, 1, 7, 0, 20000)); // overflow mid-continuation
    TEST_ASSERT_EQUAL_INT(0, (int)HpackPrim.encode_int(b, 1, 7, 0, 200));   // overflow on the final byte

    size_t c;
    uint32_t v;
    TEST_ASSERT_FALSE(HpackPrim.decode_int(b, 0, 5, &c, &v)); // empty input

    uint8_t enc[8];
    TEST_ASSERT_EQUAL_INT(0, (int)HpackPrim.huff_encode(enc, 0, "a", 1)); // no room for the trailing byte

    char out[32];
    size_t ol;
    const uint8_t eos[4] = {0xff, 0xff, 0xff, 0xff}; // 30 one-bits resolve to the EOS symbol
    TEST_ASSERT_FALSE(HpackPrim.huff_decode(eos, sizeof eos, out, sizeof out, &ol));

    size_t el = HpackPrim.huff_encode(enc, sizeof enc, "00", 2); // two symbols
    TEST_ASSERT_TRUE(el > 0);
    TEST_ASSERT_FALSE(HpackPrim.huff_decode(enc, el, out, 1, &ol)); // second symbol overflows the output

    const uint8_t pad[1] = {0xff}; // 8 unmatched bits -> more than a byte of padding
    TEST_ASSERT_FALSE(HpackPrim.huff_decode(pad, 1, out, sizeof out, &ol));

    // A continuation that would push the accumulated shift past a 32-bit result: 0x1f opens a
    // prefix-5 varint at max, then five 0x80 continuation bytes carry the shift m to 35 (> 28)
    // while bytes remain, so decode_int's "m > 28" guard rejects it (line 135) rather than i >= len.
    const uint8_t overlong[8] = {0x1f, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00};
    TEST_ASSERT_FALSE(HpackPrim.decode_int(overlong, sizeof overlong, 5, &c, &v));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_hpack_dyn_init_default_size);
    RUN_TEST(test_hpack_indexed_field_truncated_int);
    RUN_TEST(test_hpack_encode_repeated_static_name);
    RUN_TEST(test_hpack_prim_edge_guards);
    RUN_TEST(test_hpack_more_errors);
    RUN_TEST(test_dyn_size_update);
    RUN_TEST(test_oversize_entry_clears);
    RUN_TEST(test_dynamic_name_and_index);
    RUN_TEST(test_hpack_decode_errors);
    RUN_TEST(test_hpack_buffer_bounds);
    RUN_TEST(test_hpack_resolve_dynamic_name_too_big);
    RUN_TEST(test_hpack_encode_paths);
    RUN_TEST(test_int_coding);
    RUN_TEST(test_int_decode_rejects_overflowing_prefix_int);
    RUN_TEST(test_huffman);
    RUN_TEST(test_decode_c31_and_index);
    RUN_TEST(test_c2_representation_vectors);
    RUN_TEST(test_c3_request_sequence);
    RUN_TEST(test_c4_request_sequence_huffman);
    RUN_TEST(test_c5_response_sequence_evicts);
    RUN_TEST(test_c6_response_sequence_huffman_evicts);
    RUN_TEST(test_static_table_matches_appendix_a);
    RUN_TEST(test_huffman_table_matches_appendix_b);
    RUN_TEST(test_dynamic_eviction);
    RUN_TEST(test_encode_static);
    RUN_TEST(test_encode_decode_roundtrip);
    RUN_TEST(test_reject_malformed);
    return UNITY_END();
}
