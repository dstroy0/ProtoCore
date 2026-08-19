// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "crypto/mac/hmac_sha256/hmac_sha256.h"
#include "network_drivers/presentation/security/dtls/dtls_handshake/dtls_handshake.h"
#include <stdint.h>
#include <string.h>

#include <unity.h>

static uint8_t tw[4096];

void setUp()
{
}
void tearDown()
{
}

 void test_hs_header_roundtrip(void)
{
    uint8_t frag[30];
    for (unsigned i = 0; i < sizeof(frag); i++)
    {
        frag[i] = (uint8_t)(0x40 + i);
    }

    uint8_t out[64];

    size_t n = DtlsHandshake.frag_build(1, 7, 100, 40, frag, sizeof(frag), out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_DTLS_HS_HDR_LEN + sizeof(frag), n);

    TEST_ASSERT_EQUAL_UINT8(1, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0x64, out[3]);
    TEST_ASSERT_EQUAL_UINT8(0x07, out[5]);
    TEST_ASSERT_EQUAL_UINT8(0x28, out[8]);
    TEST_ASSERT_EQUAL_UINT8(0x1E, out[11]);

    DtlsHsHeader h;
    size_t consumed = DtlsHandshake.header_parse(out, n, &h);
    TEST_ASSERT_EQUAL_size_t(n, consumed);
    TEST_ASSERT_EQUAL_UINT8(1, h.msg_type);
    TEST_ASSERT_EQUAL_UINT32(100, h.length);
    TEST_ASSERT_EQUAL_UINT16(7, h.msg_seq);
    TEST_ASSERT_EQUAL_UINT32(40, h.frag_offset);
    TEST_ASSERT_EQUAL_UINT32(30, h.frag_length);
    TEST_ASSERT_EQUAL_MEMORY(frag, h.fragment, sizeof(frag));
}

 void test_hs_header_parse_rejects(void)
{
    uint8_t buf[32];
    DtlsHsHeader h;

    TEST_ASSERT_EQUAL_size_t(0, DtlsHandshake.header_parse(buf, 11, &h));

    memset(buf, 0, sizeof(buf));
    buf[0] = 1;
    buf[3] = 10;
    buf[8] = 8;
    buf[11] = 5;
    TEST_ASSERT_EQUAL_size_t(0, DtlsHandshake.header_parse(buf, sizeof(buf), &h));

    memset(buf, 0, sizeof(buf));
    buf[0] = 1;
    buf[3] = 40;
    buf[11] = 20;
    TEST_ASSERT_EQUAL_size_t(0, DtlsHandshake.header_parse(buf, PROTOCORE_DTLS_HS_HDR_LEN + 4, &h));
}

static int feed(DtlsHsReasm *r, uint8_t msg_type, uint16_t msg_seq, uint32_t full_len, uint32_t off,
                const uint8_t *body, uint32_t flen)
{
    uint8_t rec[512];
    size_t n = DtlsHandshake.frag_build(msg_type, msg_seq, full_len, off, body + off, flen, rec, sizeof(rec));
    TEST_ASSERT_TRUE(n > 0);
    DtlsHsHeader h;
    TEST_ASSERT_EQUAL_size_t(n, DtlsHandshake.header_parse(rec, n, &h));
    return DtlsHandshake.reasm_add(r, &h);
}

static void fill(uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        b[i] = (uint8_t)(i * 3 + 1);
    }
}

 void test_hs_reasm_single_fragment(void)
{
    uint8_t body[80];
    fill(body, sizeof(body));
    uint8_t buf[80];
    DtlsHsReasm r;
    DtlsHandshake.reasm_init(&r, 2, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(1, feed(&r, 11, 2, sizeof(body), 0, body, sizeof(body)));
    TEST_ASSERT_EQUAL_UINT32(sizeof(body), r.length);
    TEST_ASSERT_EQUAL_UINT8(11, r.msg_type);
    TEST_ASSERT_EQUAL_MEMORY(body, buf, sizeof(body));
}

 void test_hs_reasm_in_order(void)
{
    uint8_t body[100];
    fill(body, sizeof(body));
    uint8_t buf[100];
    DtlsHsReasm r;
    DtlsHandshake.reasm_init(&r, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, feed(&r, 1, 0, 100, 0, body, 40));
    TEST_ASSERT_EQUAL_INT(0, feed(&r, 1, 0, 100, 40, body, 40));
    TEST_ASSERT_EQUAL_INT(1, feed(&r, 1, 0, 100, 80, body, 20));
    TEST_ASSERT_EQUAL_MEMORY(body, buf, sizeof(body));
}

 void test_hs_reasm_out_of_order(void)
{
    uint8_t body[100];
    fill(body, sizeof(body));
    uint8_t buf[100];
    DtlsHsReasm r;
    DtlsHandshake.reasm_init(&r, 4, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, feed(&r, 1, 4, 100, 80, body, 20));
    TEST_ASSERT_EQUAL_INT(0, feed(&r, 1, 4, 100, 0, body, 40));
    TEST_ASSERT_EQUAL_INT(1, feed(&r, 1, 4, 100, 40, body, 40));
    TEST_ASSERT_EQUAL_MEMORY(body, buf, sizeof(body));
}

 void test_hs_reasm_overlap_and_duplicate(void)
{
    uint8_t body[100];
    fill(body, sizeof(body));
    uint8_t buf[100];
    DtlsHsReasm r;
    DtlsHandshake.reasm_init(&r, 1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, feed(&r, 1, 1, 100, 0, body, 60));
    TEST_ASSERT_EQUAL_INT(0, feed(&r, 1, 1, 100, 0, body, 60));
    TEST_ASSERT_EQUAL_INT(0, feed(&r, 1, 1, 100, 30, body, 40));
    TEST_ASSERT_EQUAL_INT(1, feed(&r, 1, 1, 100, 55, body, 45));
    TEST_ASSERT_EQUAL_MEMORY(body, buf, sizeof(body));
}

 void test_hs_reasm_conflicting_overlap_aborts(void)
{
    uint8_t body[100];
    fill(body, sizeof(body));
    uint8_t other[100];
    fill(other, sizeof(other));
    other[40] ^= 0xFF;

    uint8_t buf[100];
    DtlsHsReasm r;
    DtlsHandshake.reasm_init(&r, 1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, feed(&r, 1, 1, 100, 0, body, 60));
    TEST_ASSERT_EQUAL_INT(-1, feed(&r, 1, 1, 100, 30, other, 40));

    DtlsHandshake.reasm_init(&r, 1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, feed(&r, 1, 1, 100, 0, body, 60));
    TEST_ASSERT_EQUAL_INT(0, feed(&r, 1, 1, 100, 41, other, 19));
}

 void test_hs_reasm_wrong_msg_seq_ignored(void)
{
    uint8_t body[40];
    fill(body, sizeof(body));
    uint8_t buf[40];
    DtlsHsReasm r;
    DtlsHandshake.reasm_init(&r, 5, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, feed(&r, 1, 6, 40, 0, body, 40));
    TEST_ASSERT_FALSE(r.active);
    TEST_ASSERT_EQUAL_INT(1, feed(&r, 1, 5, 40, 0, body, 40));
    TEST_ASSERT_EQUAL_MEMORY(body, buf, sizeof(body));
}

 void test_hs_reasm_empty_body(void)
{
    uint8_t buf[16];
    DtlsHsReasm r;
    DtlsHandshake.reasm_init(&r, 0, buf, sizeof(buf));

    TEST_ASSERT_EQUAL_INT(1, feed(&r, 22, 0, 0, 0, buf, 0));
    TEST_ASSERT_EQUAL_UINT32(0, r.length);
}

 void test_hs_reasm_rejects(void)
{
    uint8_t body[300];
    fill(body, sizeof(body));

    {
        uint8_t buf[32];
        DtlsHsReasm r;
        DtlsHandshake.reasm_init(&r, 0, buf, sizeof(buf));
        TEST_ASSERT_EQUAL_INT(-1, feed(&r, 1, 0, 100, 0, body, 32));
    }

    {
        uint8_t buf[256];
        DtlsHsReasm r;
        DtlsHandshake.reasm_init(&r, 0, buf, sizeof(buf));
        TEST_ASSERT_EQUAL_INT(0, feed(&r, 1, 0, 100, 0, body, 40));
        DtlsHsHeader h = {1, 90, 0, 40, 10, body + 40};
        TEST_ASSERT_EQUAL_INT(-1, DtlsHandshake.reasm_add(&r, &h));
    }

    {
        uint8_t buf[256];
        DtlsHsReasm r;
        DtlsHandshake.reasm_init(&r, 0, buf, sizeof(buf));
        int rc = 0;

        for (uint32_t off = 0; off <= 2u * PROTOCORE_DTLS_HS_REASM_MAX_RANGES; off += 2)
        {
            rc = feed(&r, 1, 0, 100, off, body, 1);
        }
        TEST_ASSERT_EQUAL_INT(-1, rc);
    }
}

 void test_ack_roundtrip(void)
{
    DtlsRecordNumber in[3] = {{2, 5}, {2, 6}, {3, 0x0102030405060708ull}};
    uint8_t out[64];
    size_t n = DtlsHandshake.ack_build(in, 3, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(2 + 3 * 16, n);
    TEST_ASSERT_EQUAL_UINT8(0x00, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0x30, out[1]);
    TEST_ASSERT_EQUAL_UINT8(0x02, out[9]);
    TEST_ASSERT_EQUAL_UINT8(0x05, out[17]);

    DtlsRecordNumber back[4];
    size_t count = 0;
    TEST_ASSERT_TRUE(DtlsHandshake.ack_parse(out, n, back, 4, &count));
    TEST_ASSERT_EQUAL_size_t(3, count);
    for (unsigned i = 0; i < 3; i++)
    {
        TEST_ASSERT_EQUAL_UINT64(in[i].epoch, back[i].epoch);
        TEST_ASSERT_EQUAL_UINT64(in[i].seq, back[i].seq);
    }

    n = DtlsHandshake.ack_build(NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_TRUE(DtlsHandshake.ack_parse(out, n, back, 4, &count));
    TEST_ASSERT_EQUAL_size_t(0, count);
}

 void test_ack_parse_rejects(void)
{
    DtlsRecordNumber out[4];
    size_t count = 0;
    uint8_t buf[64];

    TEST_ASSERT_FALSE(DtlsHandshake.ack_parse(buf, 1, out, 4, &count));

    buf[0] = 0x00;
    buf[1] = 0x08;
    TEST_ASSERT_FALSE(DtlsHandshake.ack_parse(buf, 10, out, 4, &count));

    buf[0] = 0x00;
    buf[1] = 0x10;
    TEST_ASSERT_FALSE(DtlsHandshake.ack_parse(buf, 10, out, 4, &count));

    DtlsRecordNumber many[3] = {{0, 1}, {0, 2}, {0, 3}};
    size_t n = DtlsHandshake.ack_build(many, 3, buf, sizeof(buf));
    TEST_ASSERT_FALSE(DtlsHandshake.ack_parse(buf, n, out, 2, &count));
}

static const uint8_t COOKIE_KEY[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                                       0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                                       0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
static const uint8_t COOKIE_ADDR[4] = {0xC0, 0xA8, 0x01, 0x32};
static const uint64_t COOKIE_TS = 0x1122334455667788ull;

static const uint8_t COOKIE_PAYLOAD[34] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
                                           0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                           0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21};

static const uint8_t COOKIE_WIRE[77] = {0x01, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x00, 0x22, 0x00, 0x01,
                                        0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
                                        0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b,
                                        0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0xe0, 0x4e, 0x06, 0x36, 0x1c, 0x71, 0xe3,
                                        0x64, 0x8f, 0x15, 0x4c, 0xc1, 0x4c, 0x8a, 0xaa, 0xbf, 0x1a, 0x2e, 0xa4, 0x06,
                                        0xca, 0x8f, 0xe2, 0x49, 0xcf, 0x1d, 0x4d, 0xa1, 0x65, 0xbc, 0x6e, 0x94};

 void test_cookie_kat(void)
{
    uint8_t out[PROTOCORE_DTLS_COOKIE_MAX];
    size_t n = DtlsHandshake.cookie_make(tw, COOKIE_KEY, COOKIE_TS, COOKIE_PAYLOAD, sizeof(COOKIE_PAYLOAD), COOKIE_ADDR,
                                         sizeof(COOKIE_ADDR), out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(sizeof(COOKIE_WIRE), n);
    TEST_ASSERT_EQUAL_MEMORY(COOKIE_WIRE, out, sizeof(COOKIE_WIRE));
}

 void test_cookie_verify_accept_and_payload(void)
{
    uint8_t payload[64];
    size_t plen = 0;

    TEST_ASSERT_TRUE(DtlsHandshake.cookie_verify(tw, COOKIE_KEY, 0, 0, COOKIE_ADDR, sizeof(COOKIE_ADDR), COOKIE_WIRE,
                                                 sizeof(COOKIE_WIRE), payload, sizeof(payload), &plen));
    TEST_ASSERT_EQUAL_size_t(sizeof(COOKIE_PAYLOAD), plen);
    TEST_ASSERT_EQUAL_MEMORY(COOKIE_PAYLOAD, payload, plen);
}

 void test_cookie_verify_rejects(void)
{
    uint8_t payload[64];
    size_t plen = 0;

    uint8_t other_addr[4] = {0xC0, 0xA8, 0x01, 0x33};
    TEST_ASSERT_FALSE(DtlsHandshake.cookie_verify(tw, COOKIE_KEY, 0, 0, other_addr, sizeof(other_addr), COOKIE_WIRE,
                                                  sizeof(COOKIE_WIRE), payload, sizeof(payload), &plen));

    uint8_t bad[77];
    memcpy(bad, COOKIE_WIRE, sizeof(bad));
    bad[20] ^= 0x01;
    TEST_ASSERT_FALSE(DtlsHandshake.cookie_verify(tw, COOKIE_KEY, 0, 0, COOKIE_ADDR, sizeof(COOKIE_ADDR), bad,
                                                  sizeof(bad), payload, sizeof(payload), &plen));

    TEST_ASSERT_FALSE(DtlsHandshake.cookie_verify(tw, COOKIE_KEY, 0, 0, COOKIE_ADDR, sizeof(COOKIE_ADDR), COOKIE_WIRE,
                                                  20, payload, sizeof(payload), &plen));
}

 void test_cookie_freshness(void)
{
    uint8_t cookie[PROTOCORE_DTLS_COOKIE_MAX];
    const uint8_t payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    size_t n = DtlsHandshake.cookie_make(tw, COOKIE_KEY, 1000, payload, sizeof(payload), COOKIE_ADDR,
                                         sizeof(COOKIE_ADDR), cookie, sizeof(cookie));
    TEST_ASSERT_TRUE(n > 0);

    uint8_t out[16];
    size_t plen = 0;

    TEST_ASSERT_TRUE(DtlsHandshake.cookie_verify(tw, COOKIE_KEY, 1005, 10, COOKIE_ADDR, sizeof(COOKIE_ADDR), cookie, n,
                                                 out, sizeof(out), &plen));

    TEST_ASSERT_FALSE(DtlsHandshake.cookie_verify(tw, COOKIE_KEY, 2000, 10, COOKIE_ADDR, sizeof(COOKIE_ADDR), cookie, n,
                                                  out, sizeof(out), &plen));

    TEST_ASSERT_FALSE(DtlsHandshake.cookie_verify(tw, COOKIE_KEY, 999, 10, COOKIE_ADDR, sizeof(COOKIE_ADDR), cookie, n,
                                                  out, sizeof(out), &plen));
}

 void test_hs_frag_build_rejects(void)
{
    uint8_t body[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t out[64];

    TEST_ASSERT_EQUAL_size_t(0, DtlsHandshake.frag_build(1, 0, 0x1000000, 0, body, 8, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, DtlsHandshake.frag_build(1, 0, 100, 0x1000000, body, 8, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, DtlsHandshake.frag_build(1, 0, 100, 0, body, 0x1000000, out, sizeof(out)));

    TEST_ASSERT_EQUAL_size_t(0, DtlsHandshake.frag_build(1, 0, 10, 8, body, 5, out, sizeof(out)));

    TEST_ASSERT_EQUAL_size_t(0, DtlsHandshake.frag_build(1, 0, 8, 0, body, 8, out, PROTOCORE_DTLS_HS_HDR_LEN + 7));

    TEST_ASSERT_EQUAL_size_t(PROTOCORE_DTLS_HS_HDR_LEN + 8,
                             DtlsHandshake.frag_build(1, 0, 8, 0, body, 8, out, PROTOCORE_DTLS_HS_HDR_LEN + 8));
}

 void test_hs_reasm_header_guards(void)
{
    uint8_t body[64];
    fill(body, sizeof(body));
    uint8_t buf[64];

    {
        DtlsHsReasm r;
        DtlsHandshake.reasm_init(&r, 0, buf, sizeof(buf));
        DtlsHsHeader h = {1, 40, 0, 30, 20, body};
        TEST_ASSERT_EQUAL_INT(-1, DtlsHandshake.reasm_add(&r, &h));
    }

    {
        DtlsHsReasm r;
        DtlsHandshake.reasm_init(&r, 0, buf, sizeof(buf));
        DtlsHsHeader empty = {1, 40, 0, 0, 0, body};
        TEST_ASSERT_EQUAL_INT(0, DtlsHandshake.reasm_add(&r, &empty));
        TEST_ASSERT_TRUE(r.active);
        TEST_ASSERT_EQUAL_INT(1, feed(&r, 1, 0, 40, 0, body, 40));
        TEST_ASSERT_EQUAL_MEMORY(body, buf, 40);
    }
}

 void test_ack_build_rejects(void)
{
    uint8_t out[64];

    TEST_ASSERT_EQUAL_size_t(0, DtlsHandshake.ack_build(NULL, 4096, out, sizeof(out)));

    DtlsRecordNumber rns[2] = {{2, 1}, {2, 2}};
    TEST_ASSERT_EQUAL_size_t(0, DtlsHandshake.ack_build(rns, 2, out, 2 + 2 * 16 - 1));
    TEST_ASSERT_EQUAL_size_t(2 + 2 * 16, DtlsHandshake.ack_build(rns, 2, out, 2 + 2 * 16));
}

 void test_cookie_make_rejects(void)
{
    uint8_t out[256];
    const uint8_t payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    TEST_ASSERT_EQUAL_size_t(0, DtlsHandshake.cookie_make(tw, COOKIE_KEY, 1, NULL, 0x10000, COOKIE_ADDR,
                                                          sizeof(COOKIE_ADDR), out, sizeof(out)));

    TEST_ASSERT_EQUAL_size_t(0, DtlsHandshake.cookie_make(tw, COOKIE_KEY, 1, payload, sizeof(payload), COOKIE_ADDR,
                                                          sizeof(COOKIE_ADDR), out, 20));

    uint8_t big[128];
    memset(big, 0x5A, sizeof(big));
    TEST_ASSERT_EQUAL_size_t(0, DtlsHandshake.cookie_make(tw, COOKIE_KEY, 1, big, sizeof(big), COOKIE_ADDR,
                                                          sizeof(COOKIE_ADDR), out, sizeof(out)));
}

 void test_cookie_empty_payload_roundtrip(void)
{
    uint8_t cookie[PROTOCORE_DTLS_COOKIE_MAX];
    size_t n = DtlsHandshake.cookie_make(tw, COOKIE_KEY, 4242, NULL, 0, COOKIE_ADDR, sizeof(COOKIE_ADDR), cookie,
                                         sizeof(cookie));
    TEST_ASSERT_EQUAL_size_t(1 + 8 + 2 + PROTOCORE_HMAC_SHA256_LEN, n);

    uint8_t payload[4];
    memset(payload, 0xEE, sizeof(payload));
    size_t plen = 123;
    TEST_ASSERT_TRUE(DtlsHandshake.cookie_verify(tw, COOKIE_KEY, 4242, 0, COOKIE_ADDR, sizeof(COOKIE_ADDR), cookie, n,
                                                 payload, sizeof(payload), &plen));
    TEST_ASSERT_EQUAL_size_t(0, plen);
    TEST_ASSERT_EQUAL_UINT8(0xEE, payload[0]);
}

 void test_cookie_verify_structural_rejects(void)
{
    uint8_t payload[64];
    size_t plen = 0;
    uint8_t bad[sizeof(COOKIE_WIRE)];

    memcpy(bad, COOKIE_WIRE, sizeof(bad));
    bad[0] = 2;
    TEST_ASSERT_FALSE(DtlsHandshake.cookie_verify(tw, COOKIE_KEY, 0, 0, COOKIE_ADDR, sizeof(COOKIE_ADDR), bad,
                                                  sizeof(bad), payload, sizeof(payload), &plen));

    memcpy(bad, COOKIE_WIRE, sizeof(bad));
    bad[10] = 0x21;
    TEST_ASSERT_FALSE(DtlsHandshake.cookie_verify(tw, COOKIE_KEY, 0, 0, COOKIE_ADDR, sizeof(COOKIE_ADDR), bad,
                                                  sizeof(bad), payload, sizeof(payload), &plen));

    uint8_t small[16];
    TEST_ASSERT_FALSE(DtlsHandshake.cookie_verify(tw, COOKIE_KEY, 0, 0, COOKIE_ADDR, sizeof(COOKIE_ADDR), COOKIE_WIRE,
                                                  sizeof(COOKIE_WIRE), small, sizeof(small), &plen));
}

