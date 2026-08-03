// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the QPACK codec (network_drivers/presentation/http/http3/qpack, RFC 9204): the
// Appendix B.1 worked example (literal field line with a static name reference), the encoder's
// exact bytes for an indexed / name-reference / literal-name field line, a round-trip through all
// three representations, and rejection of any dynamic-table reference (non-zero Required Insert
// Count, dynamic index/name, post-base representations). Static-table-only, pure host codec.

#include "network_drivers/presentation/http/http3/qpack.h"
#include <string.h>
#include <unity.h>

// Fixed collection of the emitted field lines: the decoder hands each field back as a pointer +
// length, so each one is copied into its own bounded slot and NUL-terminated.
#define COLLECT_MAX 8
#define COLLECT_LEN 128
typedef struct
{
    char name[COLLECT_LEN];
    char value[COLLECT_LEN];
} CollectedField;
typedef struct
{
    CollectedField f[COLLECT_MAX];
    size_t n;
} Sink;

static proto_bool fail_emit(void *ctx, const char *n, size_t nl, const char *v, size_t vl)
{
    (void)ctx;
    (void)n;
    (void)nl;
    (void)v;
    (void)vl;
    return PROTO_FALSE;
}

static proto_bool sink_emit(void *ctx, const char *n, size_t nl, const char *v, size_t vl)
{
    Sink *s = (Sink *)ctx;
    if (s->n >= COLLECT_MAX || nl >= COLLECT_LEN || vl >= COLLECT_LEN)
    {
        return PROTO_FALSE;
    }
    memcpy(s->f[s->n].name, n, nl);
    s->f[s->n].name[nl] = 0;
    memcpy(s->f[s->n].value, v, vl);
    s->f[s->n].value[vl] = 0;
    s->n++;
    return PROTO_TRUE;
}

static proto_bool decode_all(const uint8_t *block, size_t len, Sink *s)
{
    char scratch[512];
    return pc_qpack_decode(block, len, scratch, sizeof scratch, sink_emit, s);
}

void setUp(void)
{
}
void tearDown(void)
{
}

// RFC 9204 Appendix B.1: 0000 51 0b /index.html -> :path=/index.html (static name index 1).
void test_appendix_b1_decode(void)
{
    const uint8_t block[] = {0x00, 0x00, 0x51, 0x0b, '/', 'i', 'n', 'd', 'e', 'x', '.', 'h', 't', 'm', 'l'};
    Sink s = {0};
    TEST_ASSERT_TRUE(decode_all(block, sizeof block, &s));
    TEST_ASSERT_EQUAL_UINT(1, (unsigned)s.n);
    TEST_ASSERT_EQUAL_STRING(":path", s.f[0].name);
    TEST_ASSERT_EQUAL_STRING("/index.html", s.f[0].value);
}

// Full static match -> Indexed Field Line. :status=200 is static index 25 -> 0xC0|25 = 0xD9.
void test_encode_indexed(void)
{
    uint8_t out[8];
    size_t n = pc_qpack_encode_header(out, sizeof out, ":status", 7, "200", 3);
    TEST_ASSERT_EQUAL_INT(1, (int)n);
    TEST_ASSERT_EQUAL_HEX8(0xD9, out[0]);
    Sink s = {0};
    const uint8_t block[3] = {0x00, 0x00, out[0]};
    TEST_ASSERT_TRUE(decode_all(block, 3, &s));
    TEST_ASSERT_EQUAL_STRING(":status", s.f[0].name);
    TEST_ASSERT_EQUAL_STRING("200", s.f[0].value);
}

// Name-only static match -> Literal Field Line with Name Reference (static index 1 = :path -> 0x51).
void test_encode_nameref_roundtrip(void)
{
    uint8_t out[32];
    size_t n = pc_qpack_encode_header(out, sizeof out, ":path", 5, "/index.html", 11);
    TEST_ASSERT_TRUE(n > 1);
    TEST_ASSERT_EQUAL_HEX8(0x51, out[0]); // 01 N=0 T=1 index=1

    uint8_t block[34] = {0x00, 0x00};
    memcpy(block + 2, out, n);
    Sink s = {0};
    TEST_ASSERT_TRUE(decode_all(block, n + 2, &s));
    TEST_ASSERT_EQUAL_STRING(":path", s.f[0].name);
    TEST_ASSERT_EQUAL_STRING("/index.html", s.f[0].value);
}

// No static match -> Literal Field Line with Literal Name; plus a hand-built raw (H=0) name path.
void test_literal_name(void)
{
    uint8_t out[32];
    size_t n = pc_qpack_encode_header(out, sizeof out, "x-test", 6, "hi", 2);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_HEX8(0x20, out[0] & 0xE0); // 001 pattern
    uint8_t block[34] = {0x00, 0x00};
    memcpy(block + 2, out, n);
    Sink s = {0};
    TEST_ASSERT_TRUE(decode_all(block, n + 2, &s));
    TEST_ASSERT_EQUAL_STRING("x-test", s.f[0].name);
    TEST_ASSERT_EQUAL_STRING("hi", s.f[0].value);

    // Hand-built raw literal name (H=0, name len 6) + raw value "hi" (H=0, len 2).
    const uint8_t raw[] = {0x00, 0x00, 0x26, 'x', '-', 't', 'e', 's', 't', 0x02, 'h', 'i'};
    Sink s2 = {0};
    TEST_ASSERT_TRUE(decode_all(raw, sizeof raw, &s2));
    TEST_ASSERT_EQUAL_STRING("x-test", s2.f[0].name);
    TEST_ASSERT_EQUAL_STRING("hi", s2.f[0].value);
}

// A whole field section: prefix + several fields of each representation kind.
void test_full_section(void)
{
    uint8_t out[128];
    size_t o = pc_qpack_encode_prefix(out, sizeof out);
    TEST_ASSERT_EQUAL_INT(2, (int)o);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[1]);
    o += pc_qpack_encode_header(out + o, sizeof out - o, ":status", 7, "200", 3);       // indexed
    o += pc_qpack_encode_header(out + o, sizeof out - o, "content-type", 12, "x/y", 3); // name ref
    o += pc_qpack_encode_header(out + o, sizeof out - o, "x-test", 6, "hello", 5);      // literal name
    Sink s = {0};
    TEST_ASSERT_TRUE(decode_all(out, o, &s));
    TEST_ASSERT_EQUAL_UINT(3, (unsigned)s.n);
    TEST_ASSERT_EQUAL_STRING(":status", s.f[0].name);
    TEST_ASSERT_EQUAL_STRING("200", s.f[0].value);
    TEST_ASSERT_EQUAL_STRING("content-type", s.f[1].name);
    TEST_ASSERT_EQUAL_STRING("x/y", s.f[1].value);
    TEST_ASSERT_EQUAL_STRING("x-test", s.f[2].name);
    TEST_ASSERT_EQUAL_STRING("hello", s.f[2].value);
}

// Any dynamic-table reference or non-zero Required Insert Count is a decode error for us.
void test_reject_dynamic(void)
{
    Sink s = {0};
    const uint8_t ric_nonzero[] = {0x01, 0x00}; // Required Insert Count = 1
    TEST_ASSERT_FALSE(decode_all(ric_nonzero, 2, &s));
    const uint8_t indexed_dyn[] = {0x00, 0x00, 0x80}; // Indexed Field Line, T=0 (dynamic)
    TEST_ASSERT_FALSE(decode_all(indexed_dyn, 3, &s));
    const uint8_t nameref_dyn[] = {0x00, 0x00, 0x40, 0x00}; // Literal w/ Name Ref, T=0 (dynamic)
    TEST_ASSERT_FALSE(decode_all(nameref_dyn, 4, &s));
    const uint8_t postbase[] = {0x00, 0x00, 0x10}; // Indexed Post-Base (dynamic)
    TEST_ASSERT_FALSE(decode_all(postbase, 3, &s));
}

// Encoder overflow on each representation, and the Huffman literal-name path.
void test_encode_edges(void)
{
    uint8_t out[64];
    TEST_ASSERT_EQUAL_INT(0, (int)pc_qpack_encode_prefix(out, 1));                         // prefix needs 2
    TEST_ASSERT_EQUAL_INT(0, (int)pc_qpack_encode_header(out, 0, ":status", 7, "200", 3)); // indexed, no room
    TEST_ASSERT_EQUAL_INT(0, (int)pc_qpack_encode_header(out, 1, ":path", 5, "/x", 2));    // name-ref value overflow
    TEST_ASSERT_EQUAL_INT(0, (int)pc_qpack_encode_header(out, 1, "zzzz", 4, "v", 1));      // literal-name overflow

    // A name that Huffman-compresses (8x 'a' = 40 bits = 5 bytes < 8) takes the H-bit path.
    size_t n = pc_qpack_encode_header(out, sizeof out, "aaaaaaaa", 8, "v", 1);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE((out[0] & 0xE0) == 0x20 && (out[0] & 0x08)); // literal literal name, H set
    uint8_t blk[66] = {0x00, 0x00};
    memcpy(blk + 2, out, n);
    Sink s = {0};
    TEST_ASSERT_TRUE(decode_all(blk, n + 2, &s));
    TEST_ASSERT_EQUAL_STRING("aaaaaaaa", s.f[0].name);
    TEST_ASSERT_EQUAL_STRING("v", s.f[0].value);
}

// Decoder error paths: truncated value, out-of-range static index, scratch overflow.
void test_decode_errors(void)
{
    Sink s = {0};
    const uint8_t novalue[3] = {0x00, 0x00, 0x51}; // name ref, no value string
    TEST_ASSERT_FALSE(decode_all(novalue, 3, &s));
    const uint8_t badidx[4] = {0x00, 0x00, 0xFF, 0x25}; // indexed static index 100 (>= 99)
    TEST_ASSERT_FALSE(decode_all(badidx, 4, &s));

    // A decoded name that does not fit the caller's scratch is rejected.
    char tiny[4];
    const uint8_t nameref[5] = {0x00, 0x00, 0x51, 0x01, 'x'}; // :path (5 bytes) into a 4-byte scratch
    TEST_ASSERT_FALSE(pc_qpack_decode(nameref, 5, tiny, sizeof tiny, sink_emit, &s));
    char tiny2[2];
    const uint8_t litname[8] = {0x00, 0x00, 0x23, 'a', 'b', 'c', 0x01, 'v'}; // name "abc" into a 2-byte scratch
    TEST_ASSERT_FALSE(pc_qpack_decode(litname, 8, tiny2, sizeof tiny2, sink_emit, &s));
    // A truncated field-section prefix (no Delta Base byte).
    const uint8_t prefix_only[1] = {0x00};
    TEST_ASSERT_FALSE(decode_all(prefix_only, 1, &s));
}

// The value-string (str7) decode paths off a name-reference: bad Huffman, truncation, overflow.
void test_value_string_paths(void)
{
    Sink s = {0};
    // Value marked Huffman (0x81 = H, len 1) but 0xFF is not a valid single-byte code.
    const uint8_t bad_huff[5] = {0x00, 0x00, 0x51, 0x81, 0xFF};
    TEST_ASSERT_FALSE(decode_all(bad_huff, 5, &s));
    // Value length says 10 but only 1 value byte present.
    const uint8_t val_trunc[5] = {0x00, 0x00, 0x51, 0x0a, 'x'};
    TEST_ASSERT_FALSE(decode_all(val_trunc, 5, &s));
    // A raw value that does not fit the remaining scratch after the name is copied in.
    char scratch[6]; // :path (5) fits, leaving 1 byte for the value
    const uint8_t val_over[9] = {0x00, 0x00, 0x51, 0x05, 'a', 'b', 'c', 'd', 'e'};
    TEST_ASSERT_FALSE(pc_qpack_decode(val_over, 9, scratch, sizeof scratch, sink_emit, &s));
    // A well-formed Huffman value round-trips (decode_str7 Huffman path): "aaaa" -> H, 3 bytes.
    uint8_t enc[16];
    size_t n = pc_qpack_encode_header(enc, sizeof enc, ":path", 5, "aaaaaaaa", 8);
    uint8_t blk[18] = {0x00, 0x00};
    memcpy(blk + 2, enc, n);
    TEST_ASSERT_TRUE(decode_all(blk, n + 2, &s));
    TEST_ASSERT_EQUAL_STRING("aaaaaaaa", s.f[s.n - 1].value);
}

void test_qpack_more_encode_decode_paths(void)
{
    uint8_t out[64];
    Sink s = {0};
    // A short literal name that does not Huffman-compress takes the raw memcpy path.
    size_t n = pc_qpack_encode_header(out, sizeof out, "q", 1, "v", 1);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE((out[0] & 0xE0) == 0x20 && !(out[0] & 0x08)); // literal name, H clear
    // A value that Huffman-compresses exercises encode_str7's H-bit path.
    TEST_ASSERT_TRUE(pc_qpack_encode_header(out, sizeof out, ":path", 5, "aaaaaaaa", 8) > 0);
    // Huffman value with too little room fails at the length header / body.
    for (size_t cap = 1; cap <= 5; cap++)
    {
        (void)pc_qpack_encode_header(out, cap, ":path", 5, "aaaaaaaa", 8);
    }
    // cap=0 fails at the name-ref index and at the literal-name index.
    TEST_ASSERT_EQUAL_INT(0, (int)pc_qpack_encode_header(out, 0, ":path", 5, "/x", 2));
    TEST_ASSERT_EQUAL_INT(0, (int)pc_qpack_encode_header(out, 0, "zzzz", 4, "v", 1));
    // Decode error paths.
    const uint8_t bad_ric[1] = {0xFF}; // RIC varint needs a continuation byte
    TEST_ASSERT_FALSE(decode_all(bad_ric, 1, &s));
    const uint8_t idx_dyn[3] = {0x00, 0x00, 0x80}; // Indexed Field Line, T=0 (dynamic)
    TEST_ASSERT_FALSE(decode_all(idx_dyn, 3, &s));
    const uint8_t nameref_dyn[3] = {0x00, 0x00, 0x40}; // Literal w/ Name Ref, dynamic
    TEST_ASSERT_FALSE(decode_all(nameref_dyn, 3, &s));
    const uint8_t litname_trunc[3] = {0x00, 0x00, 0x27}; // literal name, NameLen=7 but no bytes
    TEST_ASSERT_FALSE(decode_all(litname_trunc, 3, &s));
    const uint8_t litname_badhuff[5] = {0x00, 0x00, 0x2A, 0xFF, 0xFF}; // literal name, H set, bad Huffman
    TEST_ASSERT_FALSE(decode_all(litname_badhuff, 5, &s));
    const uint8_t litname_novalue[4] = {0x00, 0x00, 0x21, 'q'}; // literal name "q", then no value
    TEST_ASSERT_FALSE(decode_all(litname_novalue, 4, &s));
    // A value length that is a truncated continuation varint fails the str7 decode.
    const uint8_t nameref_badvlen[4] = {0x00, 0x00, 0x51, 0xFF}; // :path name-ref, value length varint truncated
    TEST_ASSERT_FALSE(decode_all(nameref_badvlen, 4, &s));
    // An emit callback that rejects a header aborts the decode (indexed + literal-name field lines).
    char sc[128];
    const uint8_t indexed[3] = {0x00, 0x00, 0xC0 | 17}; // Indexed Field Line, static index 17
    TEST_ASSERT_FALSE(pc_qpack_decode(indexed, 3, sc, sizeof sc, fail_emit, NULL));
    const uint8_t litname[6] = {0x00, 0x00, 0x21, 'q', 0x01, 'v'}; // literal name "q" value "v"
    TEST_ASSERT_FALSE(pc_qpack_decode(litname, 6, sc, sizeof sc, fail_emit, NULL));
    // Literal-name encode running out of room mid-Huffman-name and at the value.
    for (size_t cap = 2; cap <= 7; cap++)
    {
        (void)pc_qpack_encode_header(out, cap, "aaaaaaaa", 8, "v", 1); // Huffman name body overflow
        (void)pc_qpack_encode_header(out, cap, "q", 1, "value", 5);    // literal name fits, value overflow
    }
}

void test_qpack_emit_fail_and_namelen_past(void)
{
    char sc[128];
    // Literal Field Line with Name Reference + a valid value, but the emit callback rejects it.
    const uint8_t nameref[5] = {0x00, 0x00, 0x51, 0x01, 'v'}; // :path name-ref, value "v"
    TEST_ASSERT_FALSE(pc_qpack_decode(nameref, 5, sc, sizeof sc, fail_emit, NULL));
    // Literal Field Line with Literal Name whose NameLen (6, not the 3-bit escape 7) runs past the block.
    Sink s = {0};
    const uint8_t namelen_past[3] = {0x00, 0x00, 0x26}; // NameLen 6, no name octets follow
    TEST_ASSERT_FALSE(decode_all(namelen_past, 3, &s));
}

// Field-line integer decode failures that the || guards must catch, not just the range checks.
void test_qpack_field_int_truncation(void)
{
    Sink s = {0};
    // Indexed Field Line (T=1 static), prefix-6 integer 63 (all-ones) with no continuation byte:
    // pc_hpack_decode_int fails, so line 220's first || arm rejects it (before the idx >= 99 check).
    const uint8_t idx_trunc[3] = {0x00, 0x00, 0xFF}; // 1 1 111111 = indexed, static, prefix 63, truncated
    TEST_ASSERT_FALSE(decode_all(idx_trunc, 3, &s));
    // Literal Field Line with Name Reference (T=1 static), prefix-4 integer 15 with no continuation:
    // decode_int fails at line 232.
    const uint8_t nameref_trunc[3] = {0x00, 0x00, 0x5F}; // 01 0 1 1111 = name-ref, static, prefix 15, truncated
    TEST_ASSERT_FALSE(decode_all(nameref_trunc, 3, &s));
    // Literal Field Line with Name Reference (T=1 static) whose static index resolves to 99 (>= 99):
    // decode_int succeeds, so line 235's idx >= 99 arm rejects it.
    const uint8_t nameref_badidx[4] = {0x00, 0x00, 0x5F, 0x54}; // 15 + 84 = 99
    TEST_ASSERT_FALSE(decode_all(nameref_badidx, 4, &s));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_qpack_field_int_truncation);
    RUN_TEST(test_appendix_b1_decode);
    RUN_TEST(test_encode_indexed);
    RUN_TEST(test_encode_nameref_roundtrip);
    RUN_TEST(test_literal_name);
    RUN_TEST(test_full_section);
    RUN_TEST(test_reject_dynamic);
    RUN_TEST(test_encode_edges);
    RUN_TEST(test_decode_errors);
    RUN_TEST(test_value_string_paths);
    RUN_TEST(test_qpack_more_encode_decode_paths);
    RUN_TEST(test_qpack_emit_fail_and_namelen_past);
    return UNITY_END();
}
