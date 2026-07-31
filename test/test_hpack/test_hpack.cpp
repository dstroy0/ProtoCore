// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the HPACK codec (network_drivers/presentation/http/http2/hpack) against the RFC 7541
// worked examples: prefix-integer coding (Appendix C.1), the Huffman string code (C.4.1), the
// first request decode with dynamic-table insertion (C.3.1), plus dynamic-table indexing +
// FIFO eviction + size updates, and the server encoder (static indexing + literal round-trip).

#include "network_drivers/presentation/codec/hpack_prim/hpack_prim.h" // prefix-int + Huffman primitives
#include "network_drivers/presentation/http/http2/hpack.h"
#include <string.h>
#include <string>
#include <unity.h>
#include <utility>
#include <vector>

void setUp()
{
}
void tearDown()
{
}

struct Collected
{
    std::vector<std::pair<std::string, std::string>> h;
};
static bool collect(void *ctx, const char *n, size_t nl, const char *v, size_t vl)
{
    ((Collected *)ctx)->h.emplace_back(std::string(n, nl), std::string(v, vl));
    return true;
}

void test_int_coding()
{
    uint8_t b[8];
    size_t c;
    uint32_t v;
    // C.1.1: 10, prefix 5 -> 0x0a
    TEST_ASSERT_EQUAL_INT(1, (int)pc_hpack_encode_int(b, sizeof b, 5, 0, 10));
    TEST_ASSERT_EQUAL_HEX8(0x0a, b[0]);
    TEST_ASSERT_TRUE(pc_hpack_decode_int(b, 1, 5, &c, &v));
    TEST_ASSERT_EQUAL_UINT32(10, v);
    // C.1.2: 1337, prefix 5 -> 1f 9a 0a
    TEST_ASSERT_EQUAL_INT(3, (int)pc_hpack_encode_int(b, sizeof b, 5, 0, 1337));
    const uint8_t exp[3] = {0x1f, 0x9a, 0x0a};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, b, 3);
    TEST_ASSERT_TRUE(pc_hpack_decode_int(b, 3, 5, &c, &v));
    TEST_ASSERT_EQUAL_UINT32(1337, v);
    TEST_ASSERT_EQUAL_INT(3, (int)c);
    // C.1.3: 42, prefix 8 -> 0x2a
    TEST_ASSERT_EQUAL_INT(1, (int)pc_hpack_encode_int(b, sizeof b, 8, 0, 42));
    TEST_ASSERT_EQUAL_HEX8(0x2a, b[0]);
}

void test_huffman()
{
    const char *s = "www.example.com";
    size_t n = strlen(s);
    const uint8_t exp[12] = {0xf1, 0xe3, 0xc2, 0xe5, 0xf2, 0x3a, 0x6b, 0xa0, 0xab, 0x90, 0xf4, 0xff};
    TEST_ASSERT_EQUAL_INT(12, (int)pc_hpack_huff_len(s, n));
    uint8_t out[32];
    TEST_ASSERT_EQUAL_INT(12, (int)pc_hpack_huff_encode(out, sizeof out, s, n));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, out, 12);
    char dec[32];
    size_t dl;
    TEST_ASSERT_TRUE(pc_hpack_huff_decode(exp, 12, dec, sizeof dec, &dl));
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
    Collected c;
    char scratch[512];
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, block, sizeof block, scratch, sizeof scratch, collect, &c));
    TEST_ASSERT_EQUAL_INT(4, (int)c.h.size());
    TEST_ASSERT_EQUAL_STRING(":method", c.h[0].first.c_str());
    TEST_ASSERT_EQUAL_STRING("GET", c.h[0].second.c_str());
    TEST_ASSERT_EQUAL_STRING(":scheme", c.h[1].first.c_str());
    TEST_ASSERT_EQUAL_STRING("http", c.h[1].second.c_str());
    TEST_ASSERT_EQUAL_STRING(":path", c.h[2].first.c_str());
    TEST_ASSERT_EQUAL_STRING("/", c.h[2].second.c_str());
    TEST_ASSERT_EQUAL_STRING(":authority", c.h[3].first.c_str());
    TEST_ASSERT_EQUAL_STRING("www.example.com", c.h[3].second.c_str());
    // RFC: the dynamic table now holds one entry of size 57.
    TEST_ASSERT_EQUAL_UINT32(57, t.used);
    TEST_ASSERT_EQUAL_INT(1, (int)t.ecount);

    // An indexed reference to entry 62 (0x80|62 = 0xbe) resolves it from the dynamic table.
    const uint8_t idx62[] = {0xbe};
    Collected c2;
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, idx62, 1, scratch, sizeof scratch, collect, &c2));
    TEST_ASSERT_EQUAL_INT(1, (int)c2.h.size());
    TEST_ASSERT_EQUAL_STRING(":authority", c2.h[0].first.c_str());
    TEST_ASSERT_EQUAL_STRING("www.example.com", c2.h[0].second.c_str());
}

void test_dynamic_eviction()
{
    HpackDynTable t;
    pc_hpack_dyn_init(&t, 50); // room for one 36-byte entry, not two
    // Two literal-with-incremental-indexing inserts (name idx 0 + inline name/value), each size
    // 2+2+32 = 36. The second must evict the first.
    const uint8_t block[] = {0x40, 0x02, 'a', 'a', 0x02, 'b', 'b',  // insert (aa: bb)
                             0x40, 0x02, 'c', 'c', 0x02, 'd', 'd'}; // insert (cc: dd) -> evicts
    Collected c;
    char scratch[128];
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, block, sizeof block, scratch, sizeof scratch, collect, &c));
    TEST_ASSERT_EQUAL_INT(2, (int)c.h.size());
    TEST_ASSERT_EQUAL_INT(1, (int)t.ecount); // only the newest survives
    TEST_ASSERT_EQUAL_UINT32(36, t.used);
    // Index 62 is now (cc: dd); the evicted (aa: bb) would have been 63 (invalid now).
    const uint8_t idx62[] = {0xbe};
    Collected c2;
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, idx62, 1, scratch, sizeof scratch, collect, &c2));
    TEST_ASSERT_EQUAL_STRING("cc", c2.h[0].first.c_str());
    TEST_ASSERT_EQUAL_STRING("dd", c2.h[0].second.c_str());
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
    struct KV
    {
        const char *n;
        const char *v;
    };
    KV hs[] = {{":status", "200"},
               {"content-type", "text/html"},      // static name, literal value
               {"x-custom-header", "hello world"}, // fully literal
               {"server", "det/1"}};
    uint8_t block[512];
    size_t bo = 0;
    for (auto &kv : hs)
    {
        size_t w = pc_hpack_encode_header(block + bo, sizeof block - bo, kv.n, strlen(kv.n), kv.v, strlen(kv.v));
        TEST_ASSERT_TRUE(w > 0);
        bo += w;
    }
    HpackDynTable t;
    pc_hpack_dyn_init(&t, 4096);
    Collected c;
    char scratch[512];
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, block, bo, scratch, sizeof scratch, collect, &c));
    TEST_ASSERT_EQUAL_INT(4, (int)c.h.size());
    for (int i = 0; i < 4; i++)
    {
        TEST_ASSERT_EQUAL_STRING(hs[i].n, c.h[i].first.c_str());
        TEST_ASSERT_EQUAL_STRING(hs[i].v, c.h[i].second.c_str());
    }
    // The encoder never uses incremental indexing, so the decoder's table stays empty.
    TEST_ASSERT_EQUAL_INT(0, (int)t.ecount);
}

void test_reject_malformed()
{
    HpackDynTable t;
    pc_hpack_dyn_init(&t, 4096);
    Collected c;
    char scratch[128];
    const uint8_t idx0[] = {0x80}; // indexed field, index 0 -> error
    TEST_ASSERT_FALSE(pc_hpack_decode(&t, idx0, 1, scratch, sizeof scratch, collect, &c));
    const uint8_t trunc[] = {0x41, 0x0f, 'w', 'w'}; // literal len 15 but only 2 bytes present
    TEST_ASSERT_FALSE(pc_hpack_decode(&t, trunc, sizeof trunc, scratch, sizeof scratch, collect, &c));
}

// 6.3 dynamic table size update: a huge value clamps to our storage; a zero evicts all.
void test_dyn_size_update()
{
    HpackDynTable t;
    pc_hpack_dyn_init(&t, 4096);
    char scratch[256];
    Collected c;
    const uint8_t ins[] = {0x40, 0x02, 'a', 'a', 0x02, 'b', 'b'}; // insert (aa: bb), size 36
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, ins, sizeof ins, scratch, sizeof scratch, collect, &c));
    TEST_ASSERT_EQUAL_INT(1, (int)t.ecount);
    uint8_t up[8]; // size update to 100000 -> clamped to the table storage (no eviction here)
    size_t un = pc_hpack_encode_int(up, sizeof up, 5, 0x20, 100000);
    Collected c2;
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, up, un, scratch, sizeof scratch, collect, &c2));
    TEST_ASSERT_EQUAL_INT(1, (int)t.ecount);
    const uint8_t z[] = {0x20}; // size update to 0 -> evicts everything
    Collected c3;
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, z, 1, scratch, sizeof scratch, collect, &c3));
    TEST_ASSERT_EQUAL_INT(0, (int)t.ecount);
}

// An entry larger than the max size clears the table without inserting (RFC 7541 sec 4.4).
void test_oversize_entry_clears()
{
    HpackDynTable t;
    pc_hpack_dyn_init(&t, 40); // (aaaaa: bbbbb) = 5+5+32 = 42 > 40
    char scratch[256];
    Collected c;
    const uint8_t ins[] = {0x40, 0x05, 'a', 'a', 'a', 'a', 'a', 0x05, 'b', 'b', 'b', 'b', 'b'};
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, ins, sizeof ins, scratch, sizeof scratch, collect, &c));
    TEST_ASSERT_EQUAL_INT(1, (int)c.h.size()); // still emitted
    TEST_ASSERT_EQUAL_INT(0, (int)t.ecount);   // but the table was cleared, nothing indexed
}

// Resolve a name from the dynamic table (literal indexed name) and index a whole dynamic entry.
void test_dynamic_name_and_index()
{
    HpackDynTable t;
    pc_hpack_dyn_init(&t, 4096);
    char scratch[256];
    Collected c;
    const uint8_t ins[] = {0x40, 0x06, 'm', 'y', 'n', 'a', 'm', 'e', 0x02, 'v', '1'}; // insert (myname: v1)
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, ins, sizeof ins, scratch, sizeof scratch, collect, &c));
    // literal (incremental) with name index 62 (the dynamic "myname") + value "v2"
    const uint8_t litname[] = {0x7e, 0x02, 'v', '2'};
    Collected c2;
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, litname, sizeof litname, scratch, sizeof scratch, collect, &c2));
    TEST_ASSERT_EQUAL_STRING("myname", c2.h[0].first.c_str()); // name came from the dynamic table
    TEST_ASSERT_EQUAL_STRING("v2", c2.h[0].second.c_str());
    // indexed reference (0x80|62) to the newest dynamic entry (myname: v2)
    const uint8_t idx[] = {0xbe};
    Collected c3;
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, idx, 1, scratch, sizeof scratch, collect, &c3));
    TEST_ASSERT_EQUAL_STRING("myname", c3.h[0].first.c_str());
    TEST_ASSERT_EQUAL_STRING("v2", c3.h[0].second.c_str());
}

// Decoder fail-closed paths: bad dynamic index, missing/oversized strings, bad name index.
void test_hpack_decode_errors()
{
    HpackDynTable t;
    char scratch[256];
    Collected c;
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
    Collected c;
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
    Collected c;
    // insert (longname: v1); "longname" is 8 bytes
    const uint8_t ins[] = {0x40, 0x08, 'l', 'o', 'n', 'g', 'n', 'a', 'm', 'e', 0x02, 'v', '1'};
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, ins, sizeof ins, scratch, sizeof scratch, collect, &c));
    // literal (incremental), name index 62 ("longname"), value "v2" -- but scratch is only 4 bytes,
    // too small for the 8-byte dynamic name.
    char tiny[4];
    Collected c2;
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
    Collected c;
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
    Collected c;
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
    Collected c;
    char scratch[64];
    TEST_ASSERT_TRUE(pc_hpack_decode(&t, out, w, scratch, sizeof scratch, collect, &c));
    TEST_ASSERT_EQUAL_INT(1, (int)c.h.size());
    TEST_ASSERT_EQUAL_STRING(":status", c.h[0].first.c_str());
    TEST_ASSERT_EQUAL_STRING("999", c.h[0].second.c_str());
}

// Low-level pc_hpack_prim edge guards called directly: integer-encode buffer overflow (in the
// continuation loop and on the final byte), decode of a zero-length input, Huffman encode
// with no room for the trailing partial byte, a decoded EOS symbol, output overflow, and
// over-a-byte trailing padding.
void test_hpack_prim_edge_guards()
{
    uint8_t b[8];
    TEST_ASSERT_EQUAL_INT(0, (int)pc_hpack_encode_int(b, 1, 7, 0, 20000)); // overflow mid-continuation
    TEST_ASSERT_EQUAL_INT(0, (int)pc_hpack_encode_int(b, 1, 7, 0, 200));   // overflow on the final byte

    size_t c;
    uint32_t v;
    TEST_ASSERT_FALSE(pc_hpack_decode_int(b, 0, 5, &c, &v)); // empty input

    uint8_t enc[8];
    TEST_ASSERT_EQUAL_INT(0, (int)pc_hpack_huff_encode(enc, 0, "a", 1)); // no room for the trailing byte

    char out[32];
    size_t ol;
    const uint8_t eos[4] = {0xff, 0xff, 0xff, 0xff}; // 30 one-bits resolve to the EOS symbol
    TEST_ASSERT_FALSE(pc_hpack_huff_decode(eos, sizeof eos, out, sizeof out, &ol));

    size_t el = pc_hpack_huff_encode(enc, sizeof enc, "00", 2); // two symbols
    TEST_ASSERT_TRUE(el > 0);
    TEST_ASSERT_FALSE(pc_hpack_huff_decode(enc, el, out, 1, &ol)); // second symbol overflows the output

    const uint8_t pad[1] = {0xff}; // 8 unmatched bits -> more than a byte of padding
    TEST_ASSERT_FALSE(pc_hpack_huff_decode(pad, 1, out, sizeof out, &ol));

    // A continuation that would push the accumulated shift past a 32-bit result: 0x1f opens a
    // prefix-5 varint at max, then five 0x80 continuation bytes carry the shift m to 35 (> 28)
    // while bytes remain, so decode_int's "m > 28" guard rejects it (line 135) rather than i >= len.
    const uint8_t overlong[8] = {0x1f, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00};
    TEST_ASSERT_FALSE(pc_hpack_decode_int(overlong, sizeof overlong, 5, &c, &v));
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
    RUN_TEST(test_huffman);
    RUN_TEST(test_decode_c31_and_index);
    RUN_TEST(test_dynamic_eviction);
    RUN_TEST(test_encode_static);
    RUN_TEST(test_encode_decode_roundtrip);
    RUN_TEST(test_reject_malformed);
    return UNITY_END();
}
