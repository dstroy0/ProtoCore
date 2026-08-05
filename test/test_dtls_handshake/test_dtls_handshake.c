// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// DTLS 1.3 handshake framing + reliability tests (RFC 9147 §5, §7): the 12-byte handshake header,
// overlap-tolerant message reassembly, the ACK message, and the stateless HelloRetryRequest cookie.
// The cookie wire format is pinned byte-for-byte to an INDEPENDENT HMAC-SHA256 (Python stdlib
// hmac/hashlib) so the field layout and the address binding are proven, not just self-consistent.

#include "crypto/mac/hmac_sha256.h" // PC_HMAC_SHA256_LEN (cookie MAC size)
#include "network_drivers/presentation/security/dtls/dtls_handshake.h"
#include <stdint.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// ---------------------------------------------------------------------------
// Handshake message header (§5.2)
// ---------------------------------------------------------------------------

static void test_hs_header_roundtrip(void)
{
    uint8_t frag[30];
    for (unsigned i = 0; i < sizeof(frag); i++)
    {
        frag[i] = (uint8_t)(0x40 + i);
    }

    uint8_t out[64];
    // msg_type=1 (client_hello), msg_seq=7, full length 100, this fragment covers [40,70).
    size_t n = pc_dtls_hs_frag_build(1, 7, 100, 40, frag, sizeof(frag), out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(PC_DTLS_HS_HDR_LEN + sizeof(frag), n);

    // Explicit big-endian layout: type | uint24 length | uint16 msg_seq | uint24 off | uint24 len.
    TEST_ASSERT_EQUAL_UINT8(1, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0x64, out[3]);  // length low byte = 100
    TEST_ASSERT_EQUAL_UINT8(0x07, out[5]);  // msg_seq low byte = 7
    TEST_ASSERT_EQUAL_UINT8(0x28, out[8]);  // fragment_offset low byte = 40
    TEST_ASSERT_EQUAL_UINT8(0x1E, out[11]); // fragment_length low byte = 30

    DtlsHsHeader h;
    size_t consumed = pc_dtls_hs_header_parse(out, n, &h);
    TEST_ASSERT_EQUAL_size_t(n, consumed);
    TEST_ASSERT_EQUAL_UINT8(1, h.msg_type);
    TEST_ASSERT_EQUAL_UINT32(100, h.length);
    TEST_ASSERT_EQUAL_UINT16(7, h.msg_seq);
    TEST_ASSERT_EQUAL_UINT32(40, h.frag_offset);
    TEST_ASSERT_EQUAL_UINT32(30, h.frag_length);
    TEST_ASSERT_EQUAL_MEMORY(frag, h.fragment, sizeof(frag));
}

static void test_hs_header_parse_rejects(void)
{
    uint8_t buf[32];
    DtlsHsHeader h;

    // Shorter than the 12-byte header.
    TEST_ASSERT_EQUAL_size_t(0, pc_dtls_hs_header_parse(buf, 11, &h));

    // fragment_offset + fragment_length runs past the declared message length.
    memset(buf, 0, sizeof(buf));
    buf[0] = 1;
    buf[3] = 10; // length = 10
    buf[8] = 8;  // fragment_offset = 8
    buf[11] = 5; // fragment_length = 5 -> 8+5 > 10
    TEST_ASSERT_EQUAL_size_t(0, pc_dtls_hs_header_parse(buf, sizeof(buf), &h));

    // fragment_length claims more bytes than are present.
    memset(buf, 0, sizeof(buf));
    buf[0] = 1;
    buf[3] = 40;  // length = 40
    buf[11] = 20; // fragment_length = 20 but only 4 bytes follow the header
    TEST_ASSERT_EQUAL_size_t(0, pc_dtls_hs_header_parse(buf, PC_DTLS_HS_HDR_LEN + 4, &h));
}

// ---------------------------------------------------------------------------
// Reassembly (§5.4)
// ---------------------------------------------------------------------------

// Frame one fragment, parse it, and feed it into the reassembler.
static int feed(DtlsHsReasm *r, uint8_t msg_type, uint16_t msg_seq, uint32_t full_len, uint32_t off,
                const uint8_t *body, uint32_t flen)
{
    uint8_t rec[512];
    size_t n = pc_dtls_hs_frag_build(msg_type, msg_seq, full_len, off, body + off, flen, rec, sizeof(rec));
    TEST_ASSERT_TRUE(n > 0);
    DtlsHsHeader h;
    TEST_ASSERT_EQUAL_size_t(n, pc_dtls_hs_header_parse(rec, n, &h));
    return pc_dtls_hs_reasm_add(r, &h);
}

static void fill(uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        b[i] = (uint8_t)(i * 3 + 1);
    }
}

static void test_hs_reasm_single_fragment(void)
{
    uint8_t body[80];
    fill(body, sizeof(body));
    uint8_t buf[80];
    DtlsHsReasm r;
    pc_dtls_hs_reasm_init(&r, 2, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(1, feed(&r, 11 /*certificate*/, 2, sizeof(body), 0, body, sizeof(body)));
    TEST_ASSERT_EQUAL_UINT32(sizeof(body), r.length);
    TEST_ASSERT_EQUAL_UINT8(11, r.msg_type);
    TEST_ASSERT_EQUAL_MEMORY(body, buf, sizeof(body));
}

static void test_hs_reasm_in_order(void)
{
    uint8_t body[100];
    fill(body, sizeof(body));
    uint8_t buf[100];
    DtlsHsReasm r;
    pc_dtls_hs_reasm_init(&r, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, feed(&r, 1, 0, 100, 0, body, 40));
    TEST_ASSERT_EQUAL_INT(0, feed(&r, 1, 0, 100, 40, body, 40));
    TEST_ASSERT_EQUAL_INT(1, feed(&r, 1, 0, 100, 80, body, 20));
    TEST_ASSERT_EQUAL_MEMORY(body, buf, sizeof(body));
}

static void test_hs_reasm_out_of_order(void)
{
    uint8_t body[100];
    fill(body, sizeof(body));
    uint8_t buf[100];
    DtlsHsReasm r;
    pc_dtls_hs_reasm_init(&r, 4, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, feed(&r, 1, 4, 100, 80, body, 20)); // last fragment first
    TEST_ASSERT_EQUAL_INT(0, feed(&r, 1, 4, 100, 0, body, 40));
    TEST_ASSERT_EQUAL_INT(1, feed(&r, 1, 4, 100, 40, body, 40)); // closes the middle gap
    TEST_ASSERT_EQUAL_MEMORY(body, buf, sizeof(body));
}

static void test_hs_reasm_overlap_and_duplicate(void)
{
    uint8_t body[100];
    fill(body, sizeof(body));
    uint8_t buf[100];
    DtlsHsReasm r;
    pc_dtls_hs_reasm_init(&r, 1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, feed(&r, 1, 1, 100, 0, body, 60));
    TEST_ASSERT_EQUAL_INT(0, feed(&r, 1, 1, 100, 0, body, 60));  // exact duplicate
    TEST_ASSERT_EQUAL_INT(0, feed(&r, 1, 1, 100, 30, body, 40)); // overlaps [30,70)
    TEST_ASSERT_EQUAL_INT(1, feed(&r, 1, 1, 100, 55, body, 45)); // overlaps and completes to 100
    TEST_ASSERT_EQUAL_MEMORY(body, buf, sizeof(body));
}

static void test_hs_reasm_wrong_msg_seq_ignored(void)
{
    uint8_t body[40];
    fill(body, sizeof(body));
    uint8_t buf[40];
    DtlsHsReasm r;
    pc_dtls_hs_reasm_init(&r, 5, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, feed(&r, 1, 6, 40, 0, body, 40)); // wrong msg_seq -> ignored
    TEST_ASSERT_FALSE(r.active);
    TEST_ASSERT_EQUAL_INT(1, feed(&r, 1, 5, 40, 0, body, 40)); // correct msg_seq completes
    TEST_ASSERT_EQUAL_MEMORY(body, buf, sizeof(body));
}

static void test_hs_reasm_empty_body(void)
{
    uint8_t buf[16];
    DtlsHsReasm r;
    pc_dtls_hs_reasm_init(&r, 0, buf, sizeof(buf));
    // A zero-length body (e.g. a bodiless message) is complete as soon as the header arrives.
    TEST_ASSERT_EQUAL_INT(1, feed(&r, 22, 0, 0, 0, buf, 0));
    TEST_ASSERT_EQUAL_UINT32(0, r.length);
}

static void test_hs_reasm_rejects(void)
{
    uint8_t body[300];
    fill(body, sizeof(body));

    // Message larger than the reassembly buffer.
    {
        uint8_t buf[32];
        DtlsHsReasm r;
        pc_dtls_hs_reasm_init(&r, 0, buf, sizeof(buf));
        TEST_ASSERT_EQUAL_INT(-1, feed(&r, 1, 0, 100, 0, body, 32));
    }
    // Fragments disagree on the total length.
    {
        uint8_t buf[256];
        DtlsHsReasm r;
        pc_dtls_hs_reasm_init(&r, 0, buf, sizeof(buf));
        TEST_ASSERT_EQUAL_INT(0, feed(&r, 1, 0, 100, 0, body, 40));
        DtlsHsHeader h = {1, 90 /*!=100*/, 0, 40, 10, body + 40};
        TEST_ASSERT_EQUAL_INT(-1, pc_dtls_hs_reasm_add(&r, &h));
    }
    // Too many disjoint ranges for the bounded interval list.
    {
        uint8_t buf[256];
        DtlsHsReasm r;
        pc_dtls_hs_reasm_init(&r, 0, buf, sizeof(buf));
        int rc = 0;
        // Single-byte fragments at even offsets stay disjoint (a gap at each odd byte).
        for (uint32_t off = 0; off <= 2u * PC_DTLS_HS_REASM_MAX_RANGES; off += 2)
        {
            rc = feed(&r, 1, 0, 100, off, body, 1);
        }
        TEST_ASSERT_EQUAL_INT(-1, rc); // the (MAX_RANGES+1)-th disjoint range overflows
    }
}

// ---------------------------------------------------------------------------
// ACK message (§7)
// ---------------------------------------------------------------------------

static void test_ack_roundtrip(void)
{
    DtlsRecordNumber in[3] = {{2, 5}, {2, 6}, {3, 0x0102030405060708ull}};
    uint8_t out[64];
    size_t n = pc_dtls_ack_build(in, 3, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(2 + 3 * 16, n);
    TEST_ASSERT_EQUAL_UINT8(0x00, out[0]); // list length prefix = 48
    TEST_ASSERT_EQUAL_UINT8(0x30, out[1]);
    TEST_ASSERT_EQUAL_UINT8(0x02, out[9]);  // first record: epoch low byte
    TEST_ASSERT_EQUAL_UINT8(0x05, out[17]); // first record: seq low byte

    DtlsRecordNumber back[4];
    size_t count = 0;
    TEST_ASSERT_TRUE(pc_dtls_ack_parse(out, n, back, 4, &count));
    TEST_ASSERT_EQUAL_size_t(3, count);
    for (unsigned i = 0; i < 3; i++)
    {
        TEST_ASSERT_EQUAL_UINT64(in[i].epoch, back[i].epoch);
        TEST_ASSERT_EQUAL_UINT64(in[i].seq, back[i].seq);
    }

    // An empty ACK is a valid "I have nothing outstanding to report" message.
    n = pc_dtls_ack_build(NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_TRUE(pc_dtls_ack_parse(out, n, back, 4, &count));
    TEST_ASSERT_EQUAL_size_t(0, count);
}

static void test_ack_parse_rejects(void)
{
    DtlsRecordNumber out[4];
    size_t count = 0;
    uint8_t buf[64];

    TEST_ASSERT_FALSE(pc_dtls_ack_parse(buf, 1, out, 4, &count)); // shorter than the length prefix

    buf[0] = 0x00;
    buf[1] = 0x08; // list length 8 is not a multiple of 16
    TEST_ASSERT_FALSE(pc_dtls_ack_parse(buf, 10, out, 4, &count));

    buf[0] = 0x00;
    buf[1] = 0x10;                                                 // claims 16 bytes...
    TEST_ASSERT_FALSE(pc_dtls_ack_parse(buf, 10, out, 4, &count)); // ...but only 8 follow

    DtlsRecordNumber many[3] = {{0, 1}, {0, 2}, {0, 3}};
    size_t n = pc_dtls_ack_build(many, 3, buf, sizeof(buf));
    TEST_ASSERT_FALSE(pc_dtls_ack_parse(buf, n, out, 2, &count)); // 3 records, capacity 2
}

// ---------------------------------------------------------------------------
// HelloRetryRequest cookie (§5.1)
// ---------------------------------------------------------------------------

static const uint8_t COOKIE_KEY[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                                       0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                                       0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
static const uint8_t COOKIE_ADDR[4] = {0xC0, 0xA8, 0x01, 0x32}; // 192.168.1.50
static const uint64_t COOKIE_TS = 0x1122334455667788ull;
// payload = 0x00..0x21 (34 bytes: room for a 32-byte transcript hash + a 2-byte selected group).
static const uint8_t COOKIE_PAYLOAD[34] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
                                           0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                           0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21};
// Independent Python-stdlib HMAC-SHA256 over version||ts||addr||payload_len||payload, then the full cookie.
static const uint8_t COOKIE_WIRE[77] = {0x01, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x00, 0x22, 0x00, 0x01,
                                        0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
                                        0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b,
                                        0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0xe0, 0x4e, 0x06, 0x36, 0x1c, 0x71, 0xe3,
                                        0x64, 0x8f, 0x15, 0x4c, 0xc1, 0x4c, 0x8a, 0xaa, 0xbf, 0x1a, 0x2e, 0xa4, 0x06,
                                        0xca, 0x8f, 0xe2, 0x49, 0xcf, 0x1d, 0x4d, 0xa1, 0x65, 0xbc, 0x6e, 0x94};

static void test_cookie_kat(void)
{
    uint8_t out[PC_DTLS_COOKIE_MAX];
    size_t n = pc_dtls_cookie_make(COOKIE_KEY, COOKIE_TS, COOKIE_PAYLOAD, sizeof(COOKIE_PAYLOAD), COOKIE_ADDR,
                                   sizeof(COOKIE_ADDR), out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(sizeof(COOKIE_WIRE), n);
    TEST_ASSERT_EQUAL_MEMORY(COOKIE_WIRE, out, sizeof(COOKIE_WIRE));
}

static void test_cookie_verify_accept_and_payload(void)
{
    uint8_t payload[64];
    size_t plen = 0;
    // max_age = 0 disables the freshness check, isolating the MAC + payload recovery.
    TEST_ASSERT_TRUE(pc_dtls_cookie_verify(COOKIE_KEY, 0, 0, COOKIE_ADDR, sizeof(COOKIE_ADDR), COOKIE_WIRE,
                                           sizeof(COOKIE_WIRE), payload, sizeof(payload), &plen));
    TEST_ASSERT_EQUAL_size_t(sizeof(COOKIE_PAYLOAD), plen);
    TEST_ASSERT_EQUAL_MEMORY(COOKIE_PAYLOAD, payload, plen);
}

static void test_cookie_verify_rejects(void)
{
    uint8_t payload[64];
    size_t plen = 0;

    // A different client address fails the MAC (the address is authenticated, not stored).
    uint8_t other_addr[4] = {0xC0, 0xA8, 0x01, 0x33};
    TEST_ASSERT_FALSE(pc_dtls_cookie_verify(COOKIE_KEY, 0, 0, other_addr, sizeof(other_addr), COOKIE_WIRE,
                                            sizeof(COOKIE_WIRE), payload, sizeof(payload), &plen));

    // A tampered payload byte fails the MAC.
    uint8_t bad[77];
    memcpy(bad, COOKIE_WIRE, sizeof(bad));
    bad[20] ^= 0x01;
    TEST_ASSERT_FALSE(pc_dtls_cookie_verify(COOKIE_KEY, 0, 0, COOKIE_ADDR, sizeof(COOKIE_ADDR), bad, sizeof(bad),
                                            payload, sizeof(payload), &plen));

    // A truncated cookie is rejected.
    TEST_ASSERT_FALSE(pc_dtls_cookie_verify(COOKIE_KEY, 0, 0, COOKIE_ADDR, sizeof(COOKIE_ADDR), COOKIE_WIRE, 20,
                                            payload, sizeof(payload), &plen));
}

static void test_cookie_freshness(void)
{
    uint8_t cookie[PC_DTLS_COOKIE_MAX];
    const uint8_t payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    size_t n = pc_dtls_cookie_make(COOKIE_KEY, 1000, payload, sizeof(payload), COOKIE_ADDR, sizeof(COOKIE_ADDR), cookie,
                                   sizeof(cookie));
    TEST_ASSERT_TRUE(n > 0);

    uint8_t out[16];
    size_t plen = 0;
    // Within max_age -> accepted.
    TEST_ASSERT_TRUE(pc_dtls_cookie_verify(COOKIE_KEY, 1005, 10, COOKIE_ADDR, sizeof(COOKIE_ADDR), cookie, n, out,
                                           sizeof(out), &plen));
    // Older than max_age -> stale.
    TEST_ASSERT_FALSE(pc_dtls_cookie_verify(COOKIE_KEY, 2000, 10, COOKIE_ADDR, sizeof(COOKIE_ADDR), cookie, n, out,
                                            sizeof(out), &plen));
    // Timestamp in the future relative to now -> rejected.
    TEST_ASSERT_FALSE(pc_dtls_cookie_verify(COOKIE_KEY, 999, 10, COOKIE_ADDR, sizeof(COOKIE_ADDR), cookie, n, out,
                                            sizeof(out), &plen));
}

// pc_dtls_hs_frag_build's range guards: each uint24 field overflowing, a fragment that falls
// outside the declared message, and an output buffer too small for header + fragment.
static void test_hs_frag_build_rejects(void)
{
    uint8_t body[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t out[64];

    // full_len / frag_offset / frag_length each above the 24-bit wire field.
    TEST_ASSERT_EQUAL_size_t(0, pc_dtls_hs_frag_build(1, 0, 0x1000000, 0, body, 8, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_dtls_hs_frag_build(1, 0, 100, 0x1000000, body, 8, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_dtls_hs_frag_build(1, 0, 100, 0, body, 0x1000000, out, sizeof(out)));

    // fragment_offset + fragment_length runs past the declared message length.
    TEST_ASSERT_EQUAL_size_t(0, pc_dtls_hs_frag_build(1, 0, 10, 8, body, 5, out, sizeof(out)));

    // The 12-byte header plus the fragment does not fit the output buffer.
    TEST_ASSERT_EQUAL_size_t(0, pc_dtls_hs_frag_build(1, 0, 8, 0, body, 8, out, PC_DTLS_HS_HDR_LEN + 7));
    // One more byte of room is enough.
    TEST_ASSERT_EQUAL_size_t(PC_DTLS_HS_HDR_LEN + 8,
                             pc_dtls_hs_frag_build(1, 0, 8, 0, body, 8, out, PC_DTLS_HS_HDR_LEN + 8));
}

// Reassembly guards reached only with a hand-built header (pc_dtls_hs_header_parse would have
// rejected the first): a fragment running past the message end, and an empty fragment of a
// non-empty message (which contributes nothing and leaves the reassembler incomplete).
static void test_hs_reasm_header_guards(void)
{
    uint8_t body[64];
    fill(body, sizeof(body));
    uint8_t buf[64];

    // frag_offset + frag_length > length -> refused.
    {
        DtlsHsReasm r;
        pc_dtls_hs_reasm_init(&r, 0, buf, sizeof(buf));
        DtlsHsHeader h = {1, 40, 0, 30, 20, body}; // 30 + 20 > 40
        TEST_ASSERT_EQUAL_INT(-1, pc_dtls_hs_reasm_add(&r, &h));
    }
    // A zero-length fragment of a non-empty message: accepted but incomplete, and the real
    // fragment that follows still completes the message.
    {
        DtlsHsReasm r;
        pc_dtls_hs_reasm_init(&r, 0, buf, sizeof(buf));
        DtlsHsHeader empty = {1, 40, 0, 0, 0, body};
        TEST_ASSERT_EQUAL_INT(0, pc_dtls_hs_reasm_add(&r, &empty));
        TEST_ASSERT_TRUE(r.active);
        TEST_ASSERT_EQUAL_INT(1, feed(&r, 1, 0, 40, 0, body, 40));
        TEST_ASSERT_EQUAL_MEMORY(body, buf, 40);
    }
}

// pc_dtls_ack_build's bounds: a record-number list longer than the 16-bit length prefix, and an
// output buffer too small for the list it was asked to write.
static void test_ack_build_rejects(void)
{
    uint8_t out[64];
    // 4096 * 16 = 65536 does not fit the uint16 list-length prefix (the list is never read).
    TEST_ASSERT_EQUAL_size_t(0, pc_dtls_ack_build(NULL, 4096, out, sizeof(out)));

    DtlsRecordNumber rns[2] = {{2, 1}, {2, 2}};
    TEST_ASSERT_EQUAL_size_t(0, pc_dtls_ack_build(rns, 2, out, 2 + 2 * 16 - 1)); // one byte short
    TEST_ASSERT_EQUAL_size_t(2 + 2 * 16, pc_dtls_ack_build(rns, 2, out, 2 + 2 * 16));
}

// pc_dtls_cookie_make's bounds: a payload larger than the 16-bit length field, a cookie that does
// not fit the caller's buffer, and one that would exceed PC_DTLS_COOKIE_MAX even with room to spare.
static void test_cookie_make_rejects(void)
{
    uint8_t out[256];
    const uint8_t payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    // payload_len above the 16-bit payload-length field (the payload is never read).
    TEST_ASSERT_EQUAL_size_t(
        0, pc_dtls_cookie_make(COOKIE_KEY, 1, NULL, 0x10000, COOKIE_ADDR, sizeof(COOKIE_ADDR), out, sizeof(out)));
    // total = 11 + payload + 32 = 51 > out_cap.
    TEST_ASSERT_EQUAL_size_t(
        0, pc_dtls_cookie_make(COOKIE_KEY, 1, payload, sizeof(payload), COOKIE_ADDR, sizeof(COOKIE_ADDR), out, 20));
    // Room in the caller's buffer, but the cookie would exceed PC_DTLS_COOKIE_MAX.
    uint8_t big[128];
    memset(big, 0x5A, sizeof(big));
    TEST_ASSERT_EQUAL_size_t(
        0, pc_dtls_cookie_make(COOKIE_KEY, 1, big, sizeof(big), COOKIE_ADDR, sizeof(COOKIE_ADDR), out, sizeof(out)));
}

// A payload-free cookie (what the DTLS handshake actually mints) round-trips: it is the minimum
// 43-byte form and verify reports a zero-length payload without touching the payload buffer.
static void test_cookie_empty_payload_roundtrip(void)
{
    uint8_t cookie[PC_DTLS_COOKIE_MAX];
    size_t n = pc_dtls_cookie_make(COOKIE_KEY, 4242, NULL, 0, COOKIE_ADDR, sizeof(COOKIE_ADDR), cookie, sizeof(cookie));
    TEST_ASSERT_EQUAL_size_t(1 + 8 + 2 + PC_HMAC_SHA256_LEN, n);

    uint8_t payload[4];
    memset(payload, 0xEE, sizeof(payload));
    size_t plen = 123;
    TEST_ASSERT_TRUE(pc_dtls_cookie_verify(COOKIE_KEY, 4242, 0, COOKIE_ADDR, sizeof(COOKIE_ADDR), cookie, n, payload,
                                           sizeof(payload), &plen));
    TEST_ASSERT_EQUAL_size_t(0, plen);
    TEST_ASSERT_EQUAL_UINT8(0xEE, payload[0]); // nothing was written
}

// pc_dtls_cookie_verify's structural rejects: an unknown format version, a declared payload length
// that disagrees with the cookie length, and a payload larger than the caller's buffer.
static void test_cookie_verify_structural_rejects(void)
{
    uint8_t payload[64];
    size_t plen = 0;
    uint8_t bad[sizeof(COOKIE_WIRE)];

    // Version byte other than 1.
    memcpy(bad, COOKIE_WIRE, sizeof(bad));
    bad[0] = 2;
    TEST_ASSERT_FALSE(pc_dtls_cookie_verify(COOKIE_KEY, 0, 0, COOKIE_ADDR, sizeof(COOKIE_ADDR), bad, sizeof(bad),
                                            payload, sizeof(payload), &plen));

    // Declared payload length no longer matches the cookie's actual length.
    memcpy(bad, COOKIE_WIRE, sizeof(bad));
    bad[10] = 0x21; // 33 instead of 34
    TEST_ASSERT_FALSE(pc_dtls_cookie_verify(COOKIE_KEY, 0, 0, COOKIE_ADDR, sizeof(COOKIE_ADDR), bad, sizeof(bad),
                                            payload, sizeof(payload), &plen));

    // The 34-byte payload does not fit the caller's buffer: refused before the MAC is even checked.
    uint8_t small[16];
    TEST_ASSERT_FALSE(pc_dtls_cookie_verify(COOKIE_KEY, 0, 0, COOKIE_ADDR, sizeof(COOKIE_ADDR), COOKIE_WIRE,
                                            sizeof(COOKIE_WIRE), small, sizeof(small), &plen));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_hs_header_roundtrip);
    RUN_TEST(test_hs_frag_build_rejects);
    RUN_TEST(test_hs_reasm_header_guards);
    RUN_TEST(test_ack_build_rejects);
    RUN_TEST(test_cookie_make_rejects);
    RUN_TEST(test_cookie_empty_payload_roundtrip);
    RUN_TEST(test_cookie_verify_structural_rejects);
    RUN_TEST(test_hs_header_parse_rejects);
    RUN_TEST(test_hs_reasm_single_fragment);
    RUN_TEST(test_hs_reasm_in_order);
    RUN_TEST(test_hs_reasm_out_of_order);
    RUN_TEST(test_hs_reasm_overlap_and_duplicate);
    RUN_TEST(test_hs_reasm_wrong_msg_seq_ignored);
    RUN_TEST(test_hs_reasm_empty_body);
    RUN_TEST(test_hs_reasm_rejects);
    RUN_TEST(test_ack_roundtrip);
    RUN_TEST(test_ack_parse_rejects);
    RUN_TEST(test_cookie_kat);
    RUN_TEST(test_cookie_verify_accept_and_payload);
    RUN_TEST(test_cookie_verify_rejects);
    RUN_TEST(test_cookie_freshness);
    return UNITY_END();
}
