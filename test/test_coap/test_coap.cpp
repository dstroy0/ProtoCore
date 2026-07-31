// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the CoAP server core (pc_coap_server_process). Each test encodes a
// real RFC 7252 request datagram, runs it through the server, and decodes the
// response - no sockets, no heap.

#include "network_drivers/transport/udp.h" // pc_udp_inject / capture (host UDP mock)
#include "services/iot/coap/coap.h"
#include "services/system/clock.h" // pc_set_clock() to drive dedup freshness in tests
#include <string.h>
#include <string> // std::string (test code may use the full STL; only src/ is constrained)
#include <unity.h>

// ---------------------------------------------------------------------------
// Captured request state (what the handler saw on the last call)
// ---------------------------------------------------------------------------

static CoapMethod g_method;
static char g_path[128];
static char g_query[128];
static CoapContentFormat g_cf;
static uint8_t g_payload[128];
static size_t g_payload_len;
static bool g_called;

static void record(const CoapRequest *req)
{
    g_called = true;
    g_method = req->method;
    strncpy(g_path, req->path, sizeof(g_path) - 1);
    g_path[sizeof(g_path) - 1] = '\0';
    strncpy(g_query, req->query, sizeof(g_query) - 1);
    g_query[sizeof(g_query) - 1] = '\0';
    g_cf = req->content_format;
    g_payload_len = req->payload_len < sizeof(g_payload) ? req->payload_len : sizeof(g_payload);
    if (g_payload_len)
    {
        memcpy(g_payload, req->payload, g_payload_len);
    }
}

// A handler that records the request and replies per method.
static void h_resource(const CoapRequest *req, CoapResponse *resp)
{
    record(req);
    switch (req->method)
    {
    case CoapMethod::COAP_GET:
        resp->code = (uint8_t)CoapResponseCode::COAP_RSP_CONTENT;
        memcpy(resp->payload, "hi", 2);
        resp->payload_len = 2;
        resp->content_format = CoapContentFormat::COAP_CF_TEXT;
        break;
    case CoapMethod::COAP_POST:
        resp->code = (uint8_t)CoapResponseCode::COAP_RSP_CREATED;
        resp->payload_len = 0;
        break;
    case CoapMethod::COAP_PUT:
        resp->code = (uint8_t)CoapResponseCode::COAP_RSP_CHANGED;
        resp->payload_len = 0;
        break;
    case CoapMethod::COAP_DELETE:
        resp->code = (uint8_t)CoapResponseCode::COAP_RSP_DELETED;
        resp->payload_len = 0;
        break;
    }
}

// Deterministic 150-byte representation for block-wise (Block2) tests: big[i] =
// 'A' + (i % 26). Larger than the test env's 64-byte max block (SZX_MAX=2).
static const size_t BIG_LEN = 150;
static uint8_t big_expected(size_t i)
{
    return (uint8_t)('A' + (int)(i % 26));
}
static void h_big(const CoapRequest *req, CoapResponse *resp)
{
    record(req);
    resp->code = (uint8_t)CoapResponseCode::COAP_RSP_CONTENT;
    resp->content_format = CoapContentFormat::COAP_CF_TEXT;
    resp->payload_len = BIG_LEN;
    for (size_t i = 0; i < BIG_LEN; i++)
    {
        resp->payload[i] = big_expected(i);
    }
}

void setUp()
{
    g_called = false;
    g_method = (CoapMethod)0;
    g_path[0] = '\0';
    g_query[0] = '\0';
    g_cf = CoapContentFormat::COAP_CF_NONE;
    g_payload_len = 0;

    pc_coap_server_reset();
    pc_coap_server_add_resource("/temp",
                                CoapMethodMask::COAP_ALLOW_GET | CoapMethodMask::COAP_ALLOW_POST |
                                    CoapMethodMask::COAP_ALLOW_PUT | CoapMethodMask::COAP_ALLOW_DELETE,
                                h_resource);
    pc_coap_server_add_resource("/ro", CoapMethodMask::COAP_ALLOW_GET, h_resource);
    pc_coap_server_add_resource("/a/b", CoapMethodMask::COAP_ALLOW_GET, h_resource);
    pc_coap_server_add_resource("/longresourcename12345", CoapMethodMask::COAP_ALLOW_GET,
                                h_resource); // >12 chars: extended opt length
    pc_coap_server_add_resource("/", CoapMethodMask::COAP_ALLOW_GET, h_resource);
    pc_coap_server_add_resource(
        "/big", CoapMethodMask::COAP_ALLOW_GET | CoapMethodMask::COAP_ALLOW_POST | CoapMethodMask::COAP_ALLOW_PUT,
        h_big);
}

void tearDown()
{
}

// ---------------------------------------------------------------------------
// Request encoder
// ---------------------------------------------------------------------------

struct CoapEnc
{
    uint8_t *buf;
    size_t len;
    uint32_t last_opt;
};

static void enc_init(CoapEnc *e, uint8_t *buf, uint8_t type, uint8_t code, const uint8_t *token, uint8_t tkl,
                     uint16_t mid)
{
    e->buf = buf;
    e->len = 0;
    e->last_opt = 0;
    buf[e->len++] = (uint8_t)((1 << 6) | (type << 4) | tkl);
    buf[e->len++] = code;
    buf[e->len++] = (uint8_t)(mid >> 8);
    buf[e->len++] = (uint8_t)(mid & 0xFF);
    for (uint8_t i = 0; i < tkl; i++)
    {
        buf[e->len++] = token[i];
    }
}

static void enc_nibble(uint8_t *out_nib, uint8_t *ext, int *next, uint32_t v)
{
    if (v < 13)
    {
        *out_nib = (uint8_t)v;
        *next = 0;
    }
    else if (v < 269)
    {
        *out_nib = 13;
        ext[0] = (uint8_t)(v - 13);
        *next = 1;
    }
    else
    {
        *out_nib = 14;
        uint32_t x = v - 269;
        ext[0] = (uint8_t)(x >> 8);
        ext[1] = (uint8_t)(x & 0xFF);
        *next = 2;
    }
}

static void enc_option(CoapEnc *e, uint32_t num, const uint8_t *val, size_t vlen)
{
    uint32_t delta = num - e->last_opt;
    e->last_opt = num;
    uint8_t dn, ln, de[2], le[2];
    int nde, nle;
    enc_nibble(&dn, de, &nde, delta);
    enc_nibble(&ln, le, &nle, (uint32_t)vlen);
    e->buf[e->len++] = (uint8_t)((dn << 4) | ln);
    for (int i = 0; i < nde; i++)
    {
        e->buf[e->len++] = de[i];
    }
    for (int i = 0; i < nle; i++)
    {
        e->buf[e->len++] = le[i];
    }
    for (size_t i = 0; i < vlen; i++)
    {
        e->buf[e->len++] = val[i];
    }
}

static void enc_payload(CoapEnc *e, const uint8_t *pl, size_t n)
{
    e->buf[e->len++] = 0xFF;
    memcpy(e->buf + e->len, pl, n);
    e->len += n;
}

// Build a request with path segments, optional query segments, optional request
// Content-Format (req_cf < 0 = none) and optional payload. Options are emitted in
// ascending option-number order (Uri-Path 11, Content-Format 12, Uri-Query 15).
static size_t build(uint8_t *buf, uint8_t type, uint8_t code, const uint8_t *token, uint8_t tkl, uint16_t mid,
                    const char *const *paths, int npaths, const char *const *queries, int nq, int req_cf,
                    const uint8_t *payload, size_t plen)
{
    CoapEnc e;
    enc_init(&e, buf, type, code, token, tkl, mid);
    for (int i = 0; i < npaths; i++)
    {
        enc_option(&e, 11, (const uint8_t *)paths[i], strlen(paths[i]));
    }
    if (req_cf >= 0)
    {
        uint8_t cfv[2];
        int n = 0;
        if (req_cf > 0xFF)
        {
            cfv[n++] = (uint8_t)(req_cf >> 8);
            cfv[n++] = (uint8_t)(req_cf & 0xFF);
        }
        else if (req_cf > 0)
        {
            cfv[n++] = (uint8_t)req_cf;
        }
        enc_option(&e, 12, cfv, (size_t)n);
    }
    for (int i = 0; i < nq; i++)
    {
        enc_option(&e, 15, (const uint8_t *)queries[i], strlen(queries[i]));
    }
    if (payload && plen)
    {
        enc_payload(&e, payload, plen);
    }
    return e.len;
}

// ---------------------------------------------------------------------------
// Response decoder
// ---------------------------------------------------------------------------

struct CoapDec
{
    uint8_t ver, type, tkl, code;
    uint16_t mid;
    const uint8_t *token;
    CoapContentFormat content_format;
    int observe; // Observe option value (RFC 7641), or -1 if absent
    int block1;  // Block1 option value (RFC 7959), or -1 if absent
    int block2;  // Block2 option value, or -1 if absent
    const uint8_t *payload;
    size_t payload_len;
};

// Block option field accessors (RFC 7959 §2.2: value = (NUM<<4)|(M<<3)|SZX).
#define BLK_NUM(v) ((uint32_t)(v) >> 4)
#define BLK_M(v) (((uint32_t)(v) >> 3) & 1)
#define BLK_SZX(v) ((uint32_t)(v) & 7)

static bool dec(const uint8_t *buf, size_t len, CoapDec *d)
{
    if (len < 4)
    {
        return false;
    }
    d->ver = buf[0] >> 6;
    d->type = (buf[0] >> 4) & 0x03;
    d->tkl = buf[0] & 0x0F;
    d->code = buf[1];
    d->mid = (uint16_t)((buf[2] << 8) | buf[3]);
    d->token = buf + 4;
    d->content_format = CoapContentFormat::COAP_CF_NONE;
    d->observe = -1;
    d->block1 = -1;
    d->block2 = -1;
    d->payload = nullptr;
    d->payload_len = 0;
    size_t p = 4 + d->tkl;
    uint32_t opt = 0;
    while (p < len)
    {
        if (buf[p] == 0xFF)
        {
            d->payload = buf + p + 1;
            d->payload_len = len - p - 1;
            break;
        }
        uint8_t b = buf[p++];
        uint32_t delta = b >> 4, l = b & 0x0F;
        if (delta == 13)
        {
            delta = buf[p++] + 13;
        }
        else if (delta == 14)
        {
            delta = ((buf[p] << 8) | buf[p + 1]) + 269;
            p += 2;
        }
        if (l == 13)
        {
            l = buf[p++] + 13;
        }
        else if (l == 14)
        {
            l = ((buf[p] << 8) | buf[p + 1]) + 269;
            p += 2;
        }
        opt += delta;
        if (opt == 12 || opt == 6 || opt == 23 || opt == 27)
        {
            uint32_t v = 0;
            for (uint32_t k = 0; k < l; k++)
            {
                v = (v << 8) | buf[p + k];
            }
            if (opt == 12)
            {
                d->content_format = (CoapContentFormat)v;
            }
            else if (opt == 6)
            {
                d->observe = (int)v;
            }
            else if (opt == 23)
            {
                d->block2 = (int)v;
            }
            else
            {
                d->block1 = (int)v;
            }
        }
        p += l;
    }
    return true;
}

// Encode a Block option (RFC 7959) with the minimal-byte value into the request.
static void enc_block(CoapEnc *e, uint32_t optnum, uint32_t num, uint8_t m, uint8_t szx)
{
    uint32_t v = (num << 4) | ((uint32_t)(m & 1) << 3) | (szx & 7);
    uint8_t vb[3];
    uint8_t k = 0;
    if (v & 0xFF0000)
    {
        vb[k++] = (uint8_t)(v >> 16);
    }
    if (v & 0xFFFF00)
    {
        vb[k++] = (uint8_t)(v >> 8);
    }
    if (v)
    {
        vb[k++] = (uint8_t)v;
    }
    enc_option(e, optnum, vb, k);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_get_content()
{
    const char *paths[] = {"temp"};
    uint8_t tok[] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t req[128], resp[128];
    size_t rl = build(req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, tok, 4, 0x1234, paths, 1,
                      nullptr, 0, -1, nullptr, 0);
    size_t n = pc_coap_server_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_GREATER_THAN_UINT(0, n);

    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT(1, d.ver);
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapType::COAP_TYPE_ACK, d.type); // CON -> piggybacked ACK
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CONTENT, d.code);
    TEST_ASSERT_EQUAL_UINT(0x1234, d.mid);
    TEST_ASSERT_EQUAL_UINT(4, d.tkl);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tok, d.token, 4);
    TEST_ASSERT_EQUAL_UINT((uint16_t)CoapContentFormat::COAP_CF_TEXT, d.content_format);
    TEST_ASSERT_EQUAL_UINT(2, d.payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("hi", d.payload, 2);

    TEST_ASSERT_TRUE(g_called);
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapMethod::COAP_GET, g_method);
    TEST_ASSERT_EQUAL_STRING("/temp", g_path);
}

void test_not_found()
{
    const char *paths[] = {"missing"};
    uint8_t req[128], resp[128];
    size_t rl = build(req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, nullptr, 0, 0x0001, paths,
                      1, nullptr, 0, -1, nullptr, 0);
    size_t n = pc_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_NOT_FOUND, d.code);
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapType::COAP_TYPE_ACK, d.type);
    TEST_ASSERT_FALSE(g_called);
}

void test_method_not_allowed()
{
    const char *paths[] = {"ro"};
    uint8_t req[128], resp[128];
    size_t rl = build(req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_PUT, nullptr, 0, 0x0002, paths,
                      1, nullptr, 0, -1, nullptr, 0);
    size_t n = pc_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_METHOD_NOT_ALLOWED, d.code);
    TEST_ASSERT_FALSE(g_called);
}

void test_non_request_type()
{
    const char *paths[] = {"temp"};
    uint8_t req[128], resp[128];
    size_t rl = build(req, (uint8_t)CoapType::COAP_TYPE_NON, (uint8_t)CoapMethod::COAP_GET, nullptr, 0, 0x0003, paths,
                      1, nullptr, 0, -1, nullptr, 0);
    size_t n = pc_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapType::COAP_TYPE_NON, d.type); // NON request -> NON response
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CONTENT, d.code);
}

void test_put_with_payload()
{
    const char *paths[] = {"temp"};
    const uint8_t body[] = {'2', '5'};
    uint8_t req[128], resp[128];
    size_t rl = build(req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_PUT, nullptr, 0, 0x0004, paths,
                      1, nullptr, 0, (uint16_t)CoapContentFormat::COAP_CF_TEXT, body, 2);
    size_t n = pc_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CHANGED, d.code);

    TEST_ASSERT_TRUE(g_called);
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapMethod::COAP_PUT, g_method);
    TEST_ASSERT_EQUAL_UINT(2, g_payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, g_payload, 2);
    TEST_ASSERT_EQUAL_UINT((uint16_t)CoapContentFormat::COAP_CF_TEXT, g_cf);
}

void test_multi_segment_path()
{
    const char *paths[] = {"a", "b"};
    uint8_t req[128], resp[128];
    size_t rl = build(req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, nullptr, 0, 0x0005, paths,
                      2, nullptr, 0, -1, nullptr, 0);
    size_t n = pc_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CONTENT, d.code);
    TEST_ASSERT_EQUAL_STRING("/a/b", g_path);
}

void test_uri_query()
{
    const char *paths[] = {"temp"};
    const char *queries[] = {"x=1", "y=2"};
    uint8_t req[128], resp[128];
    size_t rl = build(req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, nullptr, 0, 0x0006, paths,
                      1, queries, 2, -1, nullptr, 0);
    size_t n = pc_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CONTENT, d.code);
    TEST_ASSERT_EQUAL_STRING("x=1&y=2", g_query);
}

void test_empty_con_ping_rst()
{
    uint8_t req[8], resp[16];
    CoapEnc e;
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, 0 /* empty */, nullptr, 0, 0x4242);
    size_t n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapType::COAP_TYPE_RST, d.type);
    TEST_ASSERT_EQUAL_UINT(0, d.code);
    TEST_ASSERT_EQUAL_UINT(0x4242, d.mid);
    TEST_ASSERT_EQUAL_UINT(0, d.tkl);
}

void test_bad_version_rst()
{
    uint8_t req[8] = {0};
    req[0] = (uint8_t)((2 << 6) | ((uint8_t)CoapType::COAP_TYPE_CON << 4) | 0); // Ver=2 (invalid)
    req[1] = (uint8_t)CoapMethod::COAP_GET;
    req[2] = 0x12;
    req[3] = 0x34;
    uint8_t resp[16];
    size_t n = pc_coap_server_process(req, 4, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapType::COAP_TYPE_RST, d.type);
    TEST_ASSERT_EQUAL_UINT(0x1234, d.mid);
}

void test_delete()
{
    const char *paths[] = {"temp"};
    uint8_t req[128], resp[128];
    size_t rl = build(req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_DELETE, nullptr, 0, 0x0007,
                      paths, 1, nullptr, 0, -1, nullptr, 0);
    size_t n = pc_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_DELETED, d.code);
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapMethod::COAP_DELETE, g_method);
}

void test_token_8_bytes()
{
    const char *paths[] = {"temp"};
    uint8_t tok[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t req[128], resp[128];
    size_t rl = build(req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, tok, 8, 0x0008, paths, 1,
                      nullptr, 0, -1, nullptr, 0);
    size_t n = pc_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT(8, d.tkl);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tok, d.token, 8);
}

void test_extended_option_length()
{
    const char *paths[] = {"longresourcename12345"}; // 21 bytes -> option length 13 (extended)
    uint8_t req[128], resp[128];
    size_t rl = build(req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, nullptr, 0, 0x0009, paths,
                      1, nullptr, 0, -1, nullptr, 0);
    size_t n = pc_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CONTENT, d.code);
    TEST_ASSERT_EQUAL_STRING("/longresourcename12345", g_path);
}

void test_ack_ignored()
{
    uint8_t req[8];
    CoapEnc e;
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_ACK, (uint8_t)CoapResponseCode::COAP_RSP_CONTENT, nullptr, 0,
             0x00AA);
    uint8_t resp[16];
    size_t n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT(0, n); // ACK is not a request
}

void test_root_path()
{
    uint8_t req[16], resp[64];
    size_t rl = build(req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, nullptr, 0, 0x000B, nullptr,
                      0, nullptr, 0, -1, nullptr, 0);
    size_t n = pc_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CONTENT, d.code);
    TEST_ASSERT_EQUAL_STRING("/", g_path);
}

void test_unknown_method_not_allowed()
{
    const char *paths[] = {"temp"};
    uint8_t req[128], resp[128];
    // Code 0.05 (FETCH) is a valid class-0 code we don't implement. RFC 7252 5.8:
    // an unrecognized/unsupported Method Code MUST get 4.05 Method Not Allowed.
    size_t rl = build(req, (uint8_t)CoapType::COAP_TYPE_CON, COAP_CODE(0, 5), nullptr, 0, 0x000C, paths, 1, nullptr, 0,
                      -1, nullptr, 0);
    size_t n = pc_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_METHOD_NOT_ALLOWED, d.code);
}

// RFC 7252 5.4.1: an unrecognized option of class "critical" (odd option number) in
// a CON request MUST cause a 4.02 (Bad Option). Option 17 (Accept) is critical and
// not implemented here, so it must be rejected rather than silently ignored.
void test_unknown_critical_option_bad_option()
{
    uint8_t resp[128];
    // Hand-build: ver1/CON/TKL0, GET, MID, Uri-Path "temp", then Accept(17) - a
    // critical option we do not implement (delta 11->17 = 6, len 1).
    uint8_t m[32];
    size_t k = 0;
    m[k++] = 0x40;                          // ver=1, type=CON, TKL=0
    m[k++] = (uint8_t)CoapMethod::COAP_GET; // 0.01
    m[k++] = 0x00;                          // MID hi
    m[k++] = 0x0C;                          // MID lo
    m[k++] = (uint8_t)(0xB0 | 4);           // option delta=11 (Uri-Path), len=4
    m[k++] = 't';
    m[k++] = 'e';
    m[k++] = 'm';
    m[k++] = 'p';
    m[k++] = (uint8_t)((6 << 4) | 1); // option delta=6 -> opt 17 (Accept, critical), len=1
    m[k++] = 0x00;                    // Accept value
    size_t n = pc_coap_server_process(m, k, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_BAD_OPTION, d.code);
}

// RFC 7641: pc_coap_server_process_ex() includes an Observe option (6, before
// Content-Format 12) carrying the notification sequence on a 2.xx response.
void test_observe_option_in_response()
{
    const char *paths[] = {"ro"};
    uint8_t tok[] = {0x01, 0x02};
    uint8_t req[128], resp[128];
    size_t rl = build(req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, tok, 2, 0x2222, paths, 1,
                      nullptr, 0, -1, nullptr, 0);
    size_t n = pc_coap_server_process_ex(req, rl, resp, sizeof(resp), 5);
    TEST_ASSERT_GREATER_THAN_UINT(0, n);
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapType::COAP_TYPE_ACK, d.type);
    TEST_ASSERT_EQUAL_INT(5, d.observe); // Observe option present, seq 5
    TEST_ASSERT_EQUAL_UINT16((uint16_t)CoapContentFormat::COAP_CF_TEXT,
                             d.content_format); // still ordered/decoded after Observe
    TEST_ASSERT_EQUAL_size_t(2, d.payload_len);
}

// append_opt stops without writing when an option would overflow the response buffer:
// a GET whose response header fits but whose Content-Format option does not.
void test_response_option_overflows_buffer()
{
    const char *paths[] = {"ro"};
    uint8_t tok[] = {0x01, 0x02};
    uint8_t req[128], resp[8];
    size_t rl = build(req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, tok, 2, 0x2222, paths, 1,
                      nullptr, 0, -1, nullptr, 0);
    // resp holds the 4-byte header + 2-byte token (=6) but not the Content-Format option.
    size_t n = pc_coap_server_process(req, rl, resp, 6);
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)CoapContentFormat::COAP_CF_NONE,
                             d.content_format); // option dropped, response still well-formed
}

void test_no_observe_option_when_seq_negative()
{
    const char *paths[] = {"ro"};
    uint8_t req[128], resp[128];
    size_t rl = build(req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, nullptr, 0, 0x2223, paths,
                      1, nullptr, 0, -1, nullptr, 0);
    size_t n = pc_coap_server_process_ex(req, rl, resp, sizeof(resp), -1);
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_INT(-1, d.observe); // no Observe option
}

// ---------------------------------------------------------------------------
// Block-wise transfer (RFC 7959). The test env builds with a 64-byte max block
// (SZX_MAX=2) and a 128-byte Block1 reassembly buffer.
// ---------------------------------------------------------------------------

// Block2: a client paging a 150-byte resource in 64-byte blocks (SZX=2) gets the
// right block contents with the More bit set until the final (short) block.
void test_block2_explicit_paging()
{
    const uint8_t expect_more[] = {1, 1, 0};             // blocks 0,1 have More; block 2 is last
    const size_t expect_len[] = {64, 64, BIG_LEN - 128}; // 64, 64, 22
    for (uint32_t num = 0; num < 3; num++)
    {
        uint8_t req[64], resp[256];
        CoapEnc e;
        enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, nullptr, 0,
                 (uint16_t)(0x3000 + num));
        enc_option(&e, 11, (const uint8_t *)"big", 3);
        enc_block(&e, 23, num, 0, 2); // Block2: NUM, M=0, SZX=2 (64 bytes)
        size_t n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
        TEST_ASSERT_GREATER_THAN_UINT(0, n);

        CoapDec d;
        TEST_ASSERT_TRUE(dec(resp, n, &d));
        TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CONTENT, d.code);
        TEST_ASSERT_TRUE(d.block2 >= 0);
        TEST_ASSERT_EQUAL_UINT(num, BLK_NUM(d.block2));
        TEST_ASSERT_EQUAL_UINT(2, BLK_SZX(d.block2));
        TEST_ASSERT_EQUAL_UINT(expect_more[num], BLK_M(d.block2));
        TEST_ASSERT_EQUAL_size_t(expect_len[num], d.payload_len);
        for (size_t i = 0; i < d.payload_len; i++)
        {
            TEST_ASSERT_EQUAL_UINT8(big_expected(num * 64 + i), d.payload[i]);
        }
    }
}

// Block2: with no Block2 option, a representation larger than the server's max
// block size is served block-wise starting at block 0 (server max SZX = 2).
void test_block2_auto_when_large()
{
    uint8_t req[64], resp[256];
    CoapEnc e;
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, nullptr, 0, 0x3100);
    enc_option(&e, 11, (const uint8_t *)"big", 3);
    size_t n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_TRUE(d.block2 >= 0);
    TEST_ASSERT_EQUAL_UINT(0, BLK_NUM(d.block2));
    TEST_ASSERT_EQUAL_UINT(1, BLK_M(d.block2));
    TEST_ASSERT_EQUAL_UINT(2, BLK_SZX(d.block2)); // clamped to server max
    TEST_ASSERT_EQUAL_size_t(64, d.payload_len);
}

// Block2: a client requesting a block size larger than the server supports is
// clamped down to the server's max SZX (2).
void test_block2_szx_clamped()
{
    uint8_t req[64], resp[256];
    CoapEnc e;
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, nullptr, 0, 0x3200);
    enc_option(&e, 11, (const uint8_t *)"big", 3);
    enc_block(&e, 23, 0, 0, 6); // ask for 1024-byte blocks
    size_t n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT(2, BLK_SZX(d.block2)); // server clamps to SZX_MAX=2
    TEST_ASSERT_EQUAL_size_t(64, d.payload_len);
}

// Block2: a small representation (no Block2 requested) is returned whole, with no
// Block2 option in the response.
void test_block2_absent_for_small()
{
    const char *paths[] = {"temp"};
    uint8_t req[64], resp[128];
    size_t rl = build(req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, nullptr, 0, 0x3300, paths,
                      1, nullptr, 0, -1, nullptr, 0);
    size_t n = pc_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_INT(-1, d.block2);
    TEST_ASSERT_EQUAL_size_t(2, d.payload_len);
}

// Block2: a block number beyond the end of the representation is a bad request.
void test_block2_out_of_range()
{
    uint8_t req[64], resp[256];
    CoapEnc e;
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, nullptr, 0, 0x3400);
    enc_option(&e, 11, (const uint8_t *)"big", 3);
    enc_block(&e, 23, 10, 0, 2); // offset 640 > 150
    size_t n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_BAD_REQUEST, d.code);
}

// Block2: the reserved block-size exponent SZX=7 is rejected with 4.02 Bad Option.
void test_block2_reserved_szx()
{
    uint8_t req[64], resp[256];
    CoapEnc e;
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, nullptr, 0, 0x3500);
    enc_option(&e, 11, (const uint8_t *)"big", 3);
    enc_block(&e, 23, 0, 0, 7);
    size_t n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_BAD_OPTION, d.code);
}

// Block1: a two-block POST upload is acknowledged 2.31 Continue on the first
// block and dispatched (with the full reassembled payload) on the last.
void test_block1_upload_two_blocks()
{
    uint8_t chunk0[64], chunk1[20];
    for (int i = 0; i < 64; i++)
    {
        chunk0[i] = (uint8_t)i;
    }
    for (int i = 0; i < 20; i++)
    {
        chunk1[i] = (uint8_t)(100 + i);
    }

    // Block 0 (More=1): expect 2.31 Continue, no handler dispatch yet.
    uint8_t req[128], resp[256];
    CoapEnc e;
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_POST, nullptr, 0, 0x3600);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_block(&e, 27, 0, 1, 2); // Block1: NUM=0, M=1, SZX=2
    enc_payload(&e, chunk0, 64);
    size_t n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CONTINUE, d.code);
    TEST_ASSERT_TRUE(d.block1 >= 0);
    TEST_ASSERT_EQUAL_UINT(0, BLK_NUM(d.block1));
    TEST_ASSERT_EQUAL_UINT(1, BLK_M(d.block1));
    TEST_ASSERT_FALSE(g_called);

    // Block 1 (More=0): handler runs with the whole 84-byte payload.
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_POST, nullptr, 0, 0x3601);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_block(&e, 27, 1, 0, 2); // Block1: NUM=1, M=0
    enc_payload(&e, chunk1, 20);
    n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CREATED, d.code);
    TEST_ASSERT_TRUE(d.block1 >= 0);
    TEST_ASSERT_EQUAL_UINT(1, BLK_NUM(d.block1));
    TEST_ASSERT_EQUAL_UINT(0, BLK_M(d.block1));

    TEST_ASSERT_TRUE(g_called);
    TEST_ASSERT_EQUAL_UINT(84, g_payload_len);
    for (int i = 0; i < 64; i++)
    {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)i, g_payload[i]);
    }
    for (int i = 0; i < 20; i++)
    {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(100 + i), g_payload[64 + i]);
    }
}

// Block1: a gap in the block sequence (a lost block) yields 4.08 Incomplete.
void test_block1_out_of_order()
{
    uint8_t chunk[64];
    for (int i = 0; i < 64; i++)
    {
        chunk[i] = (uint8_t)i;
    }
    uint8_t req[128], resp[256];
    CoapEnc e;
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_POST, nullptr, 0, 0x3700);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_block(&e, 27, 0, 1, 2);
    enc_payload(&e, chunk, 64);
    pc_coap_server_process(req, e.len, resp, sizeof(resp)); // block 0 -> Continue

    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_POST, nullptr, 0, 0x3701);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_block(&e, 27, 2, 0, 2); // skip block 1
    enc_payload(&e, chunk, 64);
    size_t n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_REQUEST_INCOMPLETE, d.code);
}

// Block1: an upload exceeding the reassembly buffer (128 bytes here) yields 4.13.
void test_block1_too_large()
{
    uint8_t chunk[64];
    for (int i = 0; i < 64; i++)
    {
        chunk[i] = (uint8_t)i;
    }
    uint8_t req[128], resp[256];
    CoapDec d;
    for (uint32_t num = 0; num < 3; num++)
    {
        CoapEnc e;
        enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_POST, nullptr, 0,
                 (uint16_t)(0x3800 + num));
        enc_option(&e, 11, (const uint8_t *)"temp", 4);
        enc_block(&e, 27, num, 1, 2); // all More=1
        enc_payload(&e, chunk, 64);
        size_t n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
        TEST_ASSERT_TRUE(dec(resp, n, &d));
        if (num < 2)
        {
            TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CONTINUE, d.code); // 64, 128 bytes buffered
        }
        else
        {
            TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_REQUEST_TOO_LARGE, d.code); // 192 > 128
        }
    }
    TEST_ASSERT_FALSE(g_called); // handler never ran
}

// RFC 6690: GET /.well-known/core returns the CoRE Link Format listing of the
// registered resources, as application/link-format (CF 40), without invoking any
// handler. The body lists each resource as "<path>", comma-separated.
void test_well_known_core_discovery()
{
    const char *paths[] = {".well-known", "core"};
    uint8_t req[160], resp[256];
    size_t rl = build(req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, nullptr, 0, 0x0CDE, paths,
                      2, nullptr, 0, -1, nullptr, 0);
    size_t n = pc_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CONTENT, d.code);
    TEST_ASSERT_EQUAL_UINT((uint16_t)CoapContentFormat::COAP_CF_LINK, d.content_format);
    TEST_ASSERT_FALSE(g_called); // discovery is synthesized, not dispatched to a handler
    // The body must list the registered resources in Link Format.
    std::string body((const char *)d.payload, d.payload_len);
    TEST_ASSERT_TRUE(body.find("</temp>") != std::string::npos);
    TEST_ASSERT_TRUE(body.find("</ro>") != std::string::npos);
    TEST_ASSERT_TRUE(body.find("</a/b>") != std::string::npos);
}

// A non-GET to /.well-known/core is 4.05 Method Not Allowed (discovery is read-only).
void test_well_known_core_rejects_post()
{
    const char *paths[] = {".well-known", "core"};
    uint8_t req[160], resp[256];
    size_t rl = build(req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_POST, nullptr, 0, 0x0CDF, paths,
                      2, nullptr, 0, -1, nullptr, 0);
    size_t n = pc_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_METHOD_NOT_ALLOWED, d.code);
}

// ---------------------------------------------------------------------------
// Malformed-input / boundary coverage
// ---------------------------------------------------------------------------

// A handler that reports an over-capacity body, to exercise the defensive clamp.
static void h_overflow(const CoapRequest *req, CoapResponse *resp)
{
    (void)req;
    resp->code = (uint8_t)CoapResponseCode::COAP_RSP_CONTENT;
    resp->content_format = CoapContentFormat::COAP_CF_TEXT;
    resp->payload_len = resp->payload_cap + 1000; // absurd; the server must clamp
}

// pc_coap_server_add_resource rejects null args and a full table.
void test_add_resource_limits()
{
    TEST_ASSERT_FALSE(pc_coap_server_add_resource(nullptr, CoapMethodMask::COAP_ALLOW_GET, h_resource));
    TEST_ASSERT_FALSE(pc_coap_server_add_resource("/x", CoapMethodMask::COAP_ALLOW_GET, nullptr));
    int added = 0;
    while (pc_coap_server_add_resource("/fill", CoapMethodMask::COAP_ALLOW_GET, h_resource))
    {
        if (++added > 64)
        {
            break; // safety: the table is bounded, so this loop must terminate
        }
    }
    TEST_ASSERT_LESS_THAN(64, added);
    TEST_ASSERT_FALSE(pc_coap_server_add_resource("/nope", CoapMethodMask::COAP_ALLOW_GET, h_resource));
}

// A datagram too short for a header, and a token length that runs past the buffer.
void test_short_and_truncated_token()
{
    uint8_t resp[64];
    uint8_t too_short[3] = {0x40, (uint8_t)CoapMethod::COAP_GET, 0x00};
    TEST_ASSERT_EQUAL_UINT(0, pc_coap_server_process(too_short, 3, resp, sizeof(resp)));

    // CON with TKL=3 but no token bytes present (len == 4): RST with an empty token.
    uint8_t bad_tkl[4] = {(uint8_t)((1 << 6) | ((uint8_t)CoapType::COAP_TYPE_CON << 4) | 3),
                          (uint8_t)CoapMethod::COAP_GET, 0x12, 0x34};
    size_t n = pc_coap_server_process(bad_tkl, 4, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT(4, n);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)CoapType::COAP_TYPE_RST, (resp[0] >> 4) & 0x03);
    TEST_ASSERT_EQUAL_UINT8(0, resp[1]);
}

// Every malformed option encoding yields 4.00 Bad Request (piggybacked ACK). Each
// datagram is a CON GET header followed by one deliberately broken option.
void test_malformed_options_bad_request()
{
    uint8_t resp[64];
    const uint8_t hdr[4] = {0x40, (uint8_t)CoapMethod::COAP_GET, 0xAB, 0xCD}; // ver1, CON, tkl0, GET

    struct Case
    {
        const char *name;
        uint8_t opt[4];
        size_t olen;
    };
    const Case cases[] = {
        {"delta15_reserved", {0xF5}, 1},            // delta nibble 15 is reserved
        {"olen15_reserved", {0x0F}, 1},             // length nibble 15 is reserved
        {"delta13_truncated", {0xD0}, 1},           // delta=13 needs 1 ext byte; none present
        {"delta14_truncated", {0xE0, 0x01}, 2},     // delta=14 needs 2 ext bytes; only 1
        {"olen13_truncated", {0x0D}, 1},            // length=13 needs 1 ext byte; none present
        {"olen14_truncated", {0x0E, 0x01}, 2},      // length=14 needs 2 ext bytes; only 1
        {"value_truncated", {0x03, 0x61, 0x62}, 3}, // length=3 but only 2 value bytes present
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        uint8_t req[16];
        memcpy(req, hdr, 4);
        memcpy(req + 4, cases[i].opt, cases[i].olen);
        size_t n = pc_coap_server_process(req, 4 + cases[i].olen, resp, sizeof(resp));
        TEST_ASSERT_TRUE(n > 0);
        TEST_ASSERT_EQUAL_UINT_MESSAGE((uint8_t)CoapResponseCode::COAP_RSP_BAD_REQUEST, resp[1], cases[i].name);
    }
}

// A valid 2-byte extended option delta (>=269) and length (>=269) decode cleanly; an
// unknown even option is elective and ignored, so processing falls through to GET "/".
void test_extended_delta_and_length_ignored()
{
    uint8_t req[400], resp[512], tok[1] = {0};
    CoapEnc e;
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, tok, 0, 1);
    enc_option(&e, 300, nullptr, 0); // option 300: delta uses the 2-byte extension
    size_t n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CONTENT, resp[1]);

    uint8_t big[269];
    memset(big, 'x', sizeof(big));
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, tok, 0, 2);
    enc_option(&e, 60, big, sizeof(big)); // 269-byte value: length uses the 2-byte extension
    n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CONTENT, resp[1]);
}

// A single Uri-Path / Uri-Query segment larger than its reconstruction buffer fails
// closed as a bad request (seg_append overflow).
void test_oversized_path_and_query()
{
    uint8_t req[256], resp[128], tok[1] = {0};
    uint8_t seg[80]; // > PC_COAP_MAX_PATH / _QUERY (64)
    CoapEnc e;

    memset(seg, 'p', sizeof(seg));
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, tok, 0, 1);
    enc_option(&e, 11, seg, sizeof(seg)); // Uri-Path
    TEST_ASSERT_TRUE(pc_coap_server_process(req, e.len, resp, sizeof(resp)) > 0);
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_BAD_REQUEST, resp[1]);

    memset(seg, 'q', sizeof(seg));
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, tok, 0, 2);
    enc_option(&e, 15, seg, sizeof(seg)); // Uri-Query
    TEST_ASSERT_TRUE(pc_coap_server_process(req, e.len, resp, sizeof(resp)) > 0);
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_BAD_REQUEST, resp[1]);
}

// A Block1/Block2 option value wider than 3 bytes is malformed (RFC 7959 caps it at 3).
void test_block_option_too_wide()
{
    uint8_t req[64], resp[64], tok[1] = {0}, v4[4] = {0, 0, 0, 0};
    CoapEnc e;

    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_PUT, tok, 0, 1);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_option(&e, 27, v4, 4); // Block1, 4-byte value
    TEST_ASSERT_TRUE(pc_coap_server_process(req, e.len, resp, sizeof(resp)) > 0);
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_BAD_REQUEST, resp[1]);

    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, tok, 0, 2);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_option(&e, 23, v4, 4); // Block2, 4-byte value
    TEST_ASSERT_TRUE(pc_coap_server_process(req, e.len, resp, sizeof(resp)) > 0);
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_BAD_REQUEST, resp[1]);
}

// Block1 upload with the reserved block-size exponent (SZX=7) is a bad option.
void test_block1_reserved_szx()
{
    uint8_t req[64], resp[64], tok[1] = {0}, v[1] = {0x07}; // num=0, more=0, szx=7
    CoapEnc e;
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_POST, tok, 0, 1);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_option(&e, 27, v, 1);
    TEST_ASSERT_TRUE(pc_coap_server_process(req, e.len, resp, sizeof(resp)) > 0);
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_BAD_OPTION, resp[1]);
}

// A non-final Block1 upload whose 2.31 Continue reply cannot fit the buffer returns 0.
void test_block1_continue_no_space()
{
    uint8_t req[64], resp[3], tok[1] = {0}, v[1] = {0x0A}; // num=0, more=1, szx=2
    uint8_t pl[16];
    memset(pl, 'z', sizeof(pl));
    CoapEnc e;
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_PUT, tok, 0, 1);
    enc_option(&e, 11, (const uint8_t *)"big", 3);
    enc_option(&e, 27, v, 1);
    enc_payload(&e, pl, sizeof(pl));
    TEST_ASSERT_EQUAL_UINT(0, pc_coap_server_process(req, e.len, resp, sizeof(resp)));
}

// A handler reporting an over-capacity body is clamped, not overflowed.
void test_response_payload_clamped()
{
    TEST_ASSERT_TRUE(pc_coap_server_add_resource("/of", CoapMethodMask::COAP_ALLOW_GET, h_overflow));
    uint8_t req[32], resp[256], tok[1] = {0};
    CoapEnc e;
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, tok, 0, 1);
    enc_option(&e, 11, (const uint8_t *)"of", 2);
    size_t n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CONTENT, resp[1]);
}

// A valid response whose header does not fit the output buffer returns 0.
void test_response_buffer_too_small()
{
    uint8_t req[32], resp[3], tok[1] = {0};
    CoapEnc e;
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, tok, 0, 1);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    TEST_ASSERT_EQUAL_UINT(0, pc_coap_server_process(req, e.len, resp, sizeof(resp)));
}

static char g_longpaths[8][40];

// A .well-known/core listing larger than the payload buffer truncates at a resource
// boundary rather than overflowing.
void test_well_known_core_truncates()
{
    pc_coap_server_reset();
    for (int i = 0; i < 8; i++)
    {
        memset(g_longpaths[i], 'a' + i, 34);
        g_longpaths[i][0] = '/';
        g_longpaths[i][34] = '\0'; // 8 * ("<path>" + ',') = 8*36-1 = 287 > MAX_PAYLOAD (256)
        TEST_ASSERT_TRUE(pc_coap_server_add_resource(g_longpaths[i], CoapMethodMask::COAP_ALLOW_GET, h_resource));
    }
    uint8_t req[64], resp[512], tok[1] = {0};
    CoapEnc e;
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, tok, 0, 1);
    enc_option(&e, 11, (const uint8_t *)".well-known", 11);
    enc_option(&e, 11, (const uint8_t *)"core", 4);
    size_t n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CONTENT, resp[1]);
}

// A large Observe sequence exercises the 2-byte and 3-byte minimal-uint option encodings.
void test_observe_large_seq_encoding()
{
    uint8_t req[32], resp[64], tok[2] = {0xAA, 0xBB};
    CoapEnc e;
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, tok, 2, 1);
    enc_option(&e, 6, nullptr, 0); // Observe (register)
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    TEST_ASSERT_TRUE(pc_coap_server_process_ex(req, e.len, resp, sizeof(resp), 0x0102) > 0);   // 2-byte seq
    TEST_ASSERT_TRUE(pc_coap_server_process_ex(req, e.len, resp, sizeof(resp), 0x010203) > 0); // 3-byte seq
}

// The response encoder ships what fits rather than overflowing a tight output
// buffer: with room for only the 4-byte header, the Content-Format option (and
// the payload) are dropped instead of running past the cap.
void test_response_option_capacity_stop()
{
    const char *paths[] = {"temp"};
    uint8_t req[64];
    size_t rl = build(req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, nullptr, 0, 0x77, paths, 1,
                      nullptr, 0, -1, nullptr, 0);
    uint8_t resp[64];
    size_t n = pc_coap_server_process_ex(req, rl, resp, 5, -1); // header (4) fits; the option does not
    TEST_ASSERT_TRUE(n >= 4 && n <= 5);
}

// The UDP transport handler (compiled in both the plain and the Observe build): a GET datagram is
// answered, and a received ACK (not a request) produces no reply.
void test_coap_udp_handler_basic()
{
    pc_udp_reset_listeners();
    pc_coap_server_begin(5683);
    pc_udp_capture_enable();

    const char *paths[] = {"temp"};
    uint8_t req[64];
    size_t rl = build(req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, nullptr, 0, 0x9001, paths,
                      1, nullptr, 0, -1, nullptr, 0);
    pc_udp_capture_reset();
    pc_udp_inject(5683, "10.0.0.5", 5000, req, rl);
    TEST_ASSERT_TRUE(pc_udp_captured_len() > 0); // 2.05 response served

    uint8_t ack[8];
    CoapEnc e;
    enc_init(&e, ack, 2 /* ACK */, 0, nullptr, 0, 0x9001);
    pc_udp_capture_reset();
    pc_udp_inject(5683, "10.0.0.5", 5000, ack, e.len);
    TEST_ASSERT_EQUAL_UINT(0, pc_udp_captured_len()); // an ACK is not a request -> nothing sent
}

// RFC 7252 4.2/4.3: a malformed CON is rejected with a Reset, but the identical malformation in a
// NON message is dropped silently - a non-confirmable message never earns a reply.
void test_non_confirmable_malformed_is_silent()
{
    uint8_t resp[32];

    // A reserved token length (9..15) in a CON is malformed: Reset, with an empty token.
    uint8_t bad_tkl_con[16] = {(uint8_t)((1 << 6) | ((uint8_t)CoapType::COAP_TYPE_CON << 4) | 9),
                               (uint8_t)CoapMethod::COAP_GET, 0x01, 0x02};
    size_t n = pc_coap_server_process(bad_tkl_con, sizeof(bad_tkl_con), resp, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT(4, n);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)CoapType::COAP_TYPE_RST, (resp[0] >> 4) & 0x03);
    TEST_ASSERT_EQUAL_UINT8(0, resp[1]);

    // The same four malformations in a NON: no bytes emitted at all.
    uint8_t bad_ver_non[4] = {(uint8_t)((2 << 6) | ((uint8_t)CoapType::COAP_TYPE_NON << 4) | 0),
                              (uint8_t)CoapMethod::COAP_GET, 0x01, 0x03};
    TEST_ASSERT_EQUAL_UINT(0, pc_coap_server_process(bad_ver_non, 4, resp, sizeof(resp)));

    uint8_t bad_tkl_non[4] = {(uint8_t)((1 << 6) | ((uint8_t)CoapType::COAP_TYPE_NON << 4) | 9),
                              (uint8_t)CoapMethod::COAP_GET, 0x01, 0x04};
    TEST_ASSERT_EQUAL_UINT(0, pc_coap_server_process(bad_tkl_non, 4, resp, sizeof(resp)));

    uint8_t short_tok_non[4] = {(uint8_t)((1 << 6) | ((uint8_t)CoapType::COAP_TYPE_NON << 4) | 3),
                                (uint8_t)CoapMethod::COAP_GET, 0x01, 0x05};
    TEST_ASSERT_EQUAL_UINT(0, pc_coap_server_process(short_tok_non, 4, resp, sizeof(resp)));

    uint8_t empty_non[4] = {(uint8_t)((1 << 6) | ((uint8_t)CoapType::COAP_TYPE_NON << 4) | 0), 0x00, 0x01, 0x06};
    TEST_ASSERT_EQUAL_UINT(0, pc_coap_server_process(empty_non, 4, resp, sizeof(resp)));
}

// A message whose Code is a *response* code (class 2) is not a request method at all: 4.05, and no
// handler runs.
void test_response_code_as_request_is_method_not_allowed()
{
    const char *paths[] = {"temp"};
    uint8_t req[64], resp[64];
    size_t rl = build(req, (uint8_t)CoapType::COAP_TYPE_CON, COAP_CODE(2, 5) /* 2.05 Content */, nullptr, 0, 0x0D01,
                      paths, 1, nullptr, 0, -1, nullptr, 0);
    size_t n = pc_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_METHOD_NOT_ALLOWED, d.code);
    TEST_ASSERT_FALSE(g_called);
}

// Block1 describes a request *upload*, so it applies only to POST/PUT. On a GET it is inert: the
// request is served normally instead of entering the reassembly state machine.
void test_block1_ignored_on_get()
{
    uint8_t req[64], resp[128], tok[1] = {0};
    CoapEnc e;
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, tok, 0, 0x0D02);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_block(&e, 27, 3, 1, 2); // Block1 NUM=3, M=1: a 4.08 offset gap if it were honoured
    size_t n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CONTENT, d.code);
    TEST_ASSERT_EQUAL_INT(-1, d.block1); // nothing to echo
    TEST_ASSERT_TRUE(g_called);
}

// RFC 7959: the block size is fixed for the duration of a Block1 transfer. A follow-on block that
// changes SZX cannot be placed even when its byte offset lines up, so reassembly fails with 4.08.
void test_block1_block_size_change_is_incomplete()
{
    uint8_t chunk[64];
    for (int i = 0; i < 64; i++)
    {
        chunk[i] = (uint8_t)i;
    }
    uint8_t req[128], resp[256];
    CoapDec d;
    CoapEnc e;
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_POST, nullptr, 0, 0x3900);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_block(&e, 27, 0, 1, 2); // SZX=2: 64-byte blocks
    enc_payload(&e, chunk, 64);
    size_t n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CONTINUE, d.code);

    // NUM=2 at SZX=1 is byte offset 2*32 = 64 - exactly where the transfer stands - so only the
    // size change makes this block unplaceable.
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_POST, nullptr, 0, 0x3901);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_block(&e, 27, 2, 0, 1);
    enc_payload(&e, chunk, 32);
    n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_REQUEST_INCOMPLETE, d.code);
    TEST_ASSERT_FALSE(g_called);
}

// A Block1 block that carries no payload at all is still a valid transfer step: 2.31 Continue, and
// it contributes nothing to the reassembly buffer (so the next expected offset is still 0).
void test_block1_empty_intermediate_block()
{
    uint8_t req[64], resp[128];
    CoapDec d;
    CoapEnc e;
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_POST, nullptr, 0, 0x3A00);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_block(&e, 27, 0, 1, 2); // NUM=0, M=1, no payload marker
    size_t n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CONTINUE, d.code);
    TEST_ASSERT_FALSE(g_called);

    // Nothing was buffered, so NUM=1 (offset 64) is now a gap rather than the next block.
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_POST, nullptr, 0, 0x3A01);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_block(&e, 27, 1, 0, 2);
    n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_REQUEST_INCOMPLETE, d.code);
}

// A handler that answers with an error code, for the "only a 2.xx body is observable/pageable" rule.
static void h_error(const CoapRequest *req, CoapResponse *resp)
{
    record(req);
    resp->code = (uint8_t)CoapResponseCode::COAP_RSP_BAD_REQUEST;
    resp->content_format = CoapContentFormat::COAP_CF_NONE;
    resp->payload_len = 0;
}

// An error response carries neither an Observe option (RFC 7641: only a successful representation
// is a notification) nor Block2 paging, even when the caller supplied a sequence and the client
// asked for blocks.
void test_error_response_carries_no_observe_or_block2()
{
    TEST_ASSERT_TRUE(pc_coap_server_add_resource("/err", CoapMethodMask::COAP_ALLOW_GET, h_error));
    uint8_t req[64], resp[128], tok[2] = {0x11, 0x22};
    CoapEnc e;
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, tok, 2, 0x0D03);
    enc_option(&e, 11, (const uint8_t *)"err", 3);
    enc_block(&e, 23, 0, 0, 2); // Block2 requested
    size_t n = pc_coap_server_process_ex(req, e.len, resp, sizeof(resp), 9 /* Observe seq */);
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_BAD_REQUEST, d.code);
    TEST_ASSERT_EQUAL_INT(-1, d.observe);
    TEST_ASSERT_EQUAL_INT(-1, d.block2);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tok, d.token, 2); // the token still round-trips
}

// A resource whose representation is exactly one block long, for the Block2 end-of-body boundary.
static void h_exact_block(const CoapRequest *req, CoapResponse *resp)
{
    record(req);
    resp->code = (uint8_t)CoapResponseCode::COAP_RSP_CONTENT;
    resp->content_format = CoapContentFormat::COAP_CF_TEXT;
    resp->payload_len = 64; // == 2^(PC_COAP_BLOCK_SZX_MAX + 4)
    for (size_t i = 0; i < 64; i++)
    {
        resp->payload[i] = (uint8_t)('a' + (int)(i % 26));
    }
}

// RFC 7959 2.4: a body that is exactly one block long ends at block 0 with M=0; block 1 then starts
// at the end of the representation and is a bad request rather than an empty final block.
void test_block2_offset_at_end_of_representation()
{
    TEST_ASSERT_TRUE(pc_coap_server_add_resource("/exact", CoapMethodMask::COAP_ALLOW_GET, h_exact_block));
    uint8_t req[64], resp[256], tok[1] = {0};
    CoapDec d;
    CoapEnc e;

    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, tok, 0, 0x0D04);
    enc_option(&e, 11, (const uint8_t *)"exact", 5);
    enc_block(&e, 23, 0, 0, 2);
    size_t n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CONTENT, d.code);
    TEST_ASSERT_EQUAL_UINT(0, BLK_M(d.block2)); // the whole body fit in block 0
    TEST_ASSERT_EQUAL_UINT(64, d.payload_len);

    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, tok, 0, 0x0D05);
    enc_option(&e, 11, (const uint8_t *)"exact", 5);
    enc_block(&e, 23, 1, 0, 2); // offset 64 == payload_len
    n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_BAD_REQUEST, d.code);
}

// A successful response with an empty body (a POST that creates nothing to return) still answers a
// Block2 request: block 0 is the whole - empty - representation, not an out-of-range 4.00.
void test_block2_on_empty_success_body()
{
    uint8_t req[64], resp[128], tok[1] = {0};
    CoapEnc e;
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_POST, tok, 0, 0x0D06);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_block(&e, 23, 0, 0, 2);
    size_t n = pc_coap_server_process(req, e.len, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CREATED, d.code);
    TEST_ASSERT_TRUE(d.block2 >= 0);
    TEST_ASSERT_EQUAL_UINT(0, BLK_NUM(d.block2));
    TEST_ASSERT_EQUAL_UINT(0, BLK_M(d.block2));
    TEST_ASSERT_EQUAL_UINT(0, d.payload_len);
}

#if PC_ENABLE_COAP_OBSERVE
// Build a CON GET /<path> with an Observe option (0 = register, 1 = deregister). Options ascend:
// Observe (6) before Uri-Path (11).
static size_t build_observe_get(uint8_t *buf, const char *path, int observe, const uint8_t *token, uint8_t tkl,
                                uint16_t mid)
{
    CoapEnc e;
    enc_init(&e, buf, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, token, tkl, mid);
    uint8_t ov = (uint8_t)observe;
    enc_option(&e, 6, observe ? &ov : nullptr, observe ? 1 : 0); // Observe (register = empty value)
    enc_option(&e, 11, (const uint8_t *)path, strlen(path));
    return e.len;
}

// RFC 7641 Observe over the UDP transport, driven through the injectable host UDP mock: a GET with
// Observe:0 registers the client (the response carries an Observe option); pc_coap_notify() pushes a
// NON notification; an unreachable peer, an Observe:1, and a Reset each drop the observation.
void test_coap_observe_over_udp()
{
    pc_udp_reset_listeners();
    pc_coap_server_begin(5683);
    pc_udp_capture_enable();
    const uint8_t tok[2] = {0xAA, 0xBB};
    uint8_t req[64];

    // Register.
    pc_udp_capture_reset();
    size_t rl = build_observe_get(req, "temp", 0, tok, sizeof(tok), 0x0001);
    pc_udp_inject(5683, "10.0.0.9", 40000, req, rl);
    TEST_ASSERT_TRUE(pc_udp_captured_len() > 0); // registration answered

    // The identical GET again is a refresh of the same observation (dedup path).
    rl = build_observe_get(req, "temp", 0, tok, sizeof(tok), 0x0005);
    pc_udp_inject(5683, "10.0.0.9", 40000, req, rl);

    // Notify the registered observer.
    pc_udp_capture_reset();
    pc_coap_notify("/temp");
    TEST_ASSERT_TRUE(pc_udp_captured_len() > 0);

    // A notification to an unreachable peer drops the observer.
    pc_udp_set_listener_sendto_result(false);
    pc_coap_notify("/temp");
    pc_udp_set_listener_sendto_result(true);
    pc_udp_capture_reset();
    pc_coap_notify("/temp"); // observer gone -> nothing sent
    TEST_ASSERT_EQUAL_UINT(0, pc_udp_captured_len());

    // Re-register, then deregister with Observe:1.
    rl = build_observe_get(req, "temp", 0, tok, sizeof(tok), 0x0002);
    pc_udp_inject(5683, "10.0.0.9", 40000, req, rl);
    rl = build_observe_get(req, "temp", 1, tok, sizeof(tok), 0x0003);
    pc_udp_inject(5683, "10.0.0.9", 40000, req, rl);
    pc_udp_capture_reset();
    pc_coap_notify("/temp");
    TEST_ASSERT_EQUAL_UINT(0, pc_udp_captured_len());

    // Re-register, then a Reset from the peer drops all its observations.
    rl = build_observe_get(req, "temp", 0, tok, sizeof(tok), 0x0004);
    pc_udp_inject(5683, "10.0.0.9", 40000, req, rl);
    uint8_t rst[8];
    CoapEnc re;
    enc_init(&re, rst, (uint8_t)CoapType::COAP_TYPE_RST, 0, nullptr, 0, 0x0004);
    pc_udp_inject(5683, "10.0.0.9", 40000, rst, re.len);
    pc_udp_capture_reset();
    pc_coap_notify("/temp");
    TEST_ASSERT_EQUAL_UINT(0, pc_udp_captured_len());

    // A notification for an unknown resource path is a no-op.
    pc_coap_notify("/no-such-resource");
}

// A full observer registry declines further registrations; the resource is still served.
void test_coap_observe_registry_full()
{
    pc_udp_reset_listeners();
    pc_coap_server_begin(5683);
    pc_udp_capture_enable();
    uint8_t req[64];
    // Distinct tokens from one peer fill the PC_COAP_MAX_OBSERVERS slots; extras are declined
    // but still answered (RFC 7641: observation is best-effort, the GET response is unconditional).
    for (int i = 0; i < PC_COAP_MAX_OBSERVERS + 2; i++)
    {
        uint8_t tok[1] = {(uint8_t)i};
        size_t rl = build_observe_get(req, "temp", 0, tok, 1, (uint16_t)(0x100 + i));
        pc_udp_capture_reset();
        pc_udp_inject(5683, "10.0.0.9", 40000, req, rl);
        TEST_ASSERT_TRUE(pc_udp_captured_len() > 0);
    }
    pc_coap_notify("/temp"); // deliver to the registered observers
}

// The Observe value in the last captured reply: 1 for a brand-new registration, and the observer's
// current sequence when an existing observation was refreshed instead. -2 if nothing was captured.
static int observe_seq_of_last_reply()
{
    CoapDec d;
    if (!pc_udp_captured_len() || !dec(pc_udp_captured(), pc_udp_captured_len(), &d))
    {
        return -2;
    }
    return d.observe;
}

// The registry keys an observation on all four of (resource, peer IP, peer port, token): a request
// differing in any one of them is a new observation, not a refresh of an existing one. A refresh is
// visible as a reply carrying the observer's already-advanced sequence instead of a fresh 1.
void test_coap_observe_registry_key_fields()
{
    pc_udp_reset_listeners();
    pc_coap_server_begin(5683);
    pc_udp_capture_enable();
    const uint8_t tok[2] = {0xAA, 0xBB};
    uint8_t req[64];

    pc_udp_capture_reset();
    size_t rl = build_observe_get(req, "temp", 0, tok, sizeof(tok), 0x0201);
    pc_udp_inject(5683, "10.0.0.9", 40000, req, rl);
    TEST_ASSERT_EQUAL_INT(1, observe_seq_of_last_reply()); // new observation

    pc_coap_notify("/temp"); // advances that observer to sequence 2

    pc_udp_capture_reset();
    rl = build_observe_get(req, "temp", 0, tok, sizeof(tok), 0x0202);
    pc_udp_inject(5683, "10.0.0.9", 40000, req, rl);
    TEST_ASSERT_EQUAL_INT(2, observe_seq_of_last_reply()); // all four keys matched -> a refresh

    pc_udp_capture_reset(); // same peer + token, different resource
    rl = build_observe_get(req, "ro", 0, tok, sizeof(tok), 0x0203);
    pc_udp_inject(5683, "10.0.0.9", 40000, req, rl);
    TEST_ASSERT_EQUAL_INT(1, observe_seq_of_last_reply());

    pc_udp_capture_reset(); // same peer IP + token + resource, different source port
    rl = build_observe_get(req, "temp", 0, tok, sizeof(tok), 0x0204);
    pc_udp_inject(5683, "10.0.0.9", 40001, req, rl);
    TEST_ASSERT_EQUAL_INT(1, observe_seq_of_last_reply());

    pc_udp_capture_reset(); // same port + token + resource, different peer IP
    rl = build_observe_get(req, "temp", 0, tok, sizeof(tok), 0x0205);
    pc_udp_inject(5683, "10.0.0.11", 40000, req, rl);
    TEST_ASSERT_EQUAL_INT(1, observe_seq_of_last_reply());

    // Four distinct observations now exist, one of them on /ro: notifying /ro must not re-render
    // the /temp observers and vice versa.
    pc_udp_capture_reset();
    pc_coap_notify("/ro");
    TEST_ASSERT_TRUE(pc_udp_captured_len() > 0);
    pc_udp_capture_reset();
    pc_coap_notify("/temp");
    TEST_ASSERT_TRUE(pc_udp_captured_len() > 0);
}

// A zero-length token is a legal Observe registration: the registry stores it, matches it on a
// refresh, and never confuses it with an observation whose token is a different length.
void test_coap_observe_zero_length_token()
{
    pc_udp_reset_listeners();
    pc_coap_server_begin(5683);
    pc_udp_capture_enable();
    const uint8_t tok[2] = {0xAA, 0xBB};
    uint8_t req[64];

    // A 2-byte-token observation first, so the empty-token registration below has to reject it on
    // token *length* rather than on token content.
    size_t rl = build_observe_get(req, "temp", 0, tok, sizeof(tok), 0x0301);
    pc_udp_inject(5683, "10.0.0.9", 40000, req, rl);

    pc_udp_capture_reset();
    rl = build_observe_get(req, "temp", 0, nullptr, 0, 0x0302);
    pc_udp_inject(5683, "10.0.0.9", 40000, req, rl);
    TEST_ASSERT_EQUAL_INT(1, observe_seq_of_last_reply()); // a second observation, not the first

    pc_coap_notify("/temp"); // both observers advance to sequence 2

    pc_udp_capture_reset();
    rl = build_observe_get(req, "temp", 0, nullptr, 0, 0x0303);
    pc_udp_inject(5683, "10.0.0.9", 40000, req, rl);
    TEST_ASSERT_EQUAL_INT(2, observe_seq_of_last_reply()); // refreshed the empty-token observation
}

// Deregistration is targeted. An Observe:1 whose token matches nothing, and a Reset from the right
// IP but the wrong port, both leave the registry untouched; only an exact peer match drops it.
void test_coap_observe_targeted_removal()
{
    pc_udp_reset_listeners();
    pc_coap_server_begin(5683);
    pc_udp_capture_enable();
    const uint8_t tok_a[2] = {0xAA, 0xBB};
    const uint8_t tok_b[2] = {0xCC, 0xDD};
    uint8_t req[64], rst[8];

    size_t rl = build_observe_get(req, "temp", 0, tok_a, 2, 0x0401);
    pc_udp_inject(5683, "10.0.0.9", 40000, req, rl); // peer A
    rl = build_observe_get(req, "temp", 0, tok_a, 2, 0x0402);
    pc_udp_inject(5683, "10.0.0.20", 40000, req, rl); // peer B: same port + token, other IP
    pc_coap_notify("/temp");                          // both advance to sequence 2

    // Observe:1 from peer A carrying an unknown token removes nothing.
    rl = build_observe_get(req, "temp", 1, tok_b, 2, 0x0403);
    pc_udp_inject(5683, "10.0.0.9", 40000, req, rl);
    pc_udp_capture_reset();
    rl = build_observe_get(req, "temp", 0, tok_a, 2, 0x0404);
    pc_udp_inject(5683, "10.0.0.9", 40000, req, rl);
    TEST_ASSERT_EQUAL_INT(2, observe_seq_of_last_reply()); // A's observation survived

    // A Reset from A's IP but a different source port removes nothing either.
    CoapEnc re;
    enc_init(&re, rst, (uint8_t)CoapType::COAP_TYPE_RST, 0, nullptr, 0, 0x0405);
    pc_udp_inject(5683, "10.0.0.9", 49999, rst, re.len);
    pc_udp_capture_reset();
    rl = build_observe_get(req, "temp", 0, tok_a, 2, 0x0406);
    pc_udp_inject(5683, "10.0.0.9", 40000, req, rl);
    TEST_ASSERT_EQUAL_INT(2, observe_seq_of_last_reply());

    // A Reset from A's exact address does drop it: the next registration starts over at 1.
    enc_init(&re, rst, (uint8_t)CoapType::COAP_TYPE_RST, 0, nullptr, 0, 0x0407);
    pc_udp_inject(5683, "10.0.0.9", 40000, rst, re.len);
    pc_udp_capture_reset();
    rl = build_observe_get(req, "temp", 0, tok_a, 2, 0x0408);
    pc_udp_inject(5683, "10.0.0.9", 40000, req, rl);
    TEST_ASSERT_EQUAL_INT(1, observe_seq_of_last_reply());

    // Peer B was never addressed by any of that and is still observing at sequence 2.
    pc_udp_capture_reset();
    rl = build_observe_get(req, "temp", 0, tok_a, 2, 0x0409);
    pc_udp_inject(5683, "10.0.0.20", 40000, req, rl);
    TEST_ASSERT_EQUAL_INT(2, observe_seq_of_last_reply());
}

// pc_coap_notify() applies the same defensive body clamp as the request path: a handler that
// reports more than the payload buffer holds is truncated rather than allowed to overrun it.
void test_coap_notify_clamps_oversized_body()
{
    pc_udp_reset_listeners();
    TEST_ASSERT_TRUE(pc_coap_server_add_resource("/of", CoapMethodMask::COAP_ALLOW_GET, h_overflow));
    pc_coap_server_begin(5683);
    pc_udp_capture_enable();
    const uint8_t tok[1] = {0x5A};
    uint8_t req[64];
    size_t rl = build_observe_get(req, "of", 0, tok, 1, 0x0501);
    pc_udp_inject(5683, "10.0.0.9", 40000, req, rl);

    pc_udp_capture_reset();
    pc_coap_notify("/of");
    TEST_ASSERT_TRUE(pc_udp_captured_len() > 0);
    CoapDec d;
    TEST_ASSERT_TRUE(dec(pc_udp_captured(), pc_udp_captured_len(), &d));
    TEST_ASSERT_EQUAL_UINT(PC_COAP_MAX_PAYLOAD, d.payload_len); // clamped, not payload_cap + 1000
}

// /.well-known/core is synthesized, not a registered resource, so an Observe on it has nothing to
// register against: the listing is still served, but the reply carries no Observe option.
void test_coap_observe_on_discovery_is_not_registered()
{
    pc_udp_reset_listeners();
    pc_coap_server_begin(5683);
    pc_udp_capture_enable();
    uint8_t req[64], tok[1] = {0x77};
    CoapEnc e;
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, tok, 1, 0x0601);
    enc_option(&e, 6, nullptr, 0); // Observe: register
    enc_option(&e, 11, (const uint8_t *)".well-known", 11);
    enc_option(&e, 11, (const uint8_t *)"core", 4);
    pc_udp_capture_reset();
    pc_udp_inject(5683, "10.0.0.9", 40000, req, e.len);
    TEST_ASSERT_TRUE(pc_udp_captured_len() > 0);
    CoapDec d;
    TEST_ASSERT_TRUE(dec(pc_udp_captured(), pc_udp_captured_len(), &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CONTENT, d.code);
    TEST_ASSERT_EQUAL_INT(-1, d.observe);
}

// Transport edge cases: a zero-length datagram is neither a Reset nor a request, and an Observe
// option on a POST is not a registration (only a GET establishes an observation).
void test_coap_udp_edge_datagrams()
{
    pc_udp_reset_listeners();
    pc_coap_server_begin(5683);
    pc_udp_capture_enable();

    uint8_t empty[1] = {0};
    pc_udp_capture_reset();
    pc_udp_inject(5683, "10.0.0.9", 40000, empty, 0);
    TEST_ASSERT_EQUAL_UINT(0, pc_udp_captured_len()); // dropped, nothing emitted

    uint8_t req[64];
    CoapEnc e;
    enc_init(&e, req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_POST, nullptr, 0, 0x0701);
    enc_option(&e, 6, nullptr, 0); // Observe on a POST
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    pc_udp_capture_reset();
    pc_udp_inject(5683, "10.0.0.9", 40000, req, e.len);
    TEST_ASSERT_TRUE(pc_udp_captured_len() > 0);
    CoapDec d;
    TEST_ASSERT_TRUE(dec(pc_udp_captured(), pc_udp_captured_len(), &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)CoapResponseCode::COAP_RSP_CREATED, d.code);
    TEST_ASSERT_EQUAL_INT(-1, d.observe); // no observation established

    // ...and no observation was created, so a notify on /temp reaches nobody.
    pc_udp_capture_reset();
    pc_coap_notify("/temp");
    TEST_ASSERT_EQUAL_UINT(0, pc_udp_captured_len());
}
#endif // PC_ENABLE_COAP_OBSERVE

#if PC_COAP_DEDUP_ENTRIES > 0
// ---------------------------------------------------------------------------
// Message de-duplication (RFC 7252 §4.5)
// ---------------------------------------------------------------------------
static uint32_t g_now_ms = 0;
static uint32_t mock_clock()
{
    return g_now_ms;
}

// store then lookup returns the cached response byte-exact.
void test_dedup_store_lookup_roundtrip()
{
    pc_set_clock(mock_clock, 1000);
    g_now_ms = 1000;
    pc_coap_server_reset();
    const uint8_t r[] = {0x62, 0x45, 0x12, 0x34, 0xAB, 0xCD};
    pc_coap_dedup_store("192.168.1.10", 5683, 0x1234, r, sizeof(r));
    const uint8_t *c = nullptr;
    size_t cl = 0;
    TEST_ASSERT_TRUE(pc_coap_dedup_lookup("192.168.1.10", 5683, 0x1234, &c, &cl));
    TEST_ASSERT_EQUAL_size_t(sizeof(r), cl);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(r, c, cl);
    pc_set_clock(nullptr, 1000);
}

// The full source address keys the entry: a different ip / port / mid never false-hits.
void test_dedup_full_address_keying()
{
    pc_set_clock(mock_clock, 1000);
    g_now_ms = 1000;
    pc_coap_server_reset();
    const uint8_t r[] = {1, 2, 3};
    pc_coap_dedup_store("192.168.1.10", 5683, 0x1234, r, sizeof(r));
    const uint8_t *c = nullptr;
    size_t cl = 0;
    TEST_ASSERT_FALSE(pc_coap_dedup_lookup("192.168.1.11", 5683, 0x1234, &c, &cl)); // other ip
    TEST_ASSERT_FALSE(pc_coap_dedup_lookup("192.168.1.10", 5684, 0x1234, &c, &cl)); // other port
    TEST_ASSERT_FALSE(pc_coap_dedup_lookup("192.168.1.10", 5683, 0x1235, &c, &cl)); // other mid
    TEST_ASSERT_TRUE(pc_coap_dedup_lookup("192.168.1.10", 5683, 0x1234, &c, &cl));  // exact match
    pc_set_clock(nullptr, 1000);
}

// An entry older than PC_COAP_DEDUP_LIFETIME_MS is treated as a new exchange.
void test_dedup_expiry()
{
    pc_set_clock(mock_clock, 1000);
    g_now_ms = 1000;
    pc_coap_server_reset();
    const uint8_t r[] = {1, 2, 3};
    pc_coap_dedup_store("10.0.0.1", 5683, 0x0001, r, sizeof(r));
    const uint8_t *c = nullptr;
    size_t cl = 0;
    g_now_ms = 1000 + PC_COAP_DEDUP_LIFETIME_MS - 1;
    TEST_ASSERT_TRUE(pc_coap_dedup_lookup("10.0.0.1", 5683, 0x0001, &c, &cl)); // still fresh
    g_now_ms = 1000 + PC_COAP_DEDUP_LIFETIME_MS;
    TEST_ASSERT_FALSE(pc_coap_dedup_lookup("10.0.0.1", 5683, 0x0001, &c, &cl)); // expired
    pc_set_clock(nullptr, 1000);
}

// A response larger than PC_COAP_DEDUP_RESP_MAX is not cached (a retransmit re-processes it).
void test_dedup_too_large_not_cached()
{
    pc_set_clock(mock_clock, 1000);
    g_now_ms = 1000;
    pc_coap_server_reset();
    static uint8_t big[PC_COAP_DEDUP_RESP_MAX + 1];
    memset(big, 0xAA, sizeof(big));
    pc_coap_dedup_store("10.0.0.2", 5683, 0x0002, big, sizeof(big));
    const uint8_t *c = nullptr;
    size_t cl = 0;
    TEST_ASSERT_FALSE(pc_coap_dedup_lookup("10.0.0.2", 5683, 0x0002, &c, &cl));
    pc_set_clock(nullptr, 1000);
}

// When the cache is full, the oldest entry is evicted; storing the same key updates in place.
void test_dedup_eviction_and_update()
{
    pc_set_clock(mock_clock, 1000);
    pc_coap_server_reset();
    const uint8_t r[] = {9};
    for (int i = 0; i < PC_COAP_DEDUP_ENTRIES; i++)
    {
        g_now_ms = 1000 + (uint32_t)i;
        char ip[16];
        snprintf(ip, sizeof(ip), "10.0.1.%d", i);
        pc_coap_dedup_store(ip, 5683, (uint16_t)(0x100 + i), r, sizeof(r));
    }
    g_now_ms = 2000;
    pc_coap_dedup_store("10.0.1.99", 5683, 0x999, r, sizeof(r)); // evicts the oldest (entry 0)
    const uint8_t *c = nullptr;
    size_t cl = 0;
    TEST_ASSERT_FALSE(pc_coap_dedup_lookup("10.0.1.0", 5683, 0x100, &c, &cl)); // evicted
    TEST_ASSERT_TRUE(pc_coap_dedup_lookup("10.0.1.99", 5683, 0x999, &c, &cl)); // newest present

    // Storing an existing key updates its response rather than consuming another slot.
    const uint8_t r2[] = {7, 7, 7, 7};
    pc_coap_dedup_store("10.0.1.99", 5683, 0x999, r2, sizeof(r2));
    TEST_ASSERT_TRUE(pc_coap_dedup_lookup("10.0.1.99", 5683, 0x999, &c, &cl));
    TEST_ASSERT_EQUAL_size_t(sizeof(r2), cl);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(r2, c, cl);
    pc_set_clock(nullptr, 1000);
}

// End-to-end through the UDP handler: a retransmitted CON is answered from the cache and its handler
// does NOT run a second time, but a different source with the same Message-ID is a new exchange.
void test_dedup_handler_replays_without_rerunning()
{
    pc_set_clock(mock_clock, 1000);
    g_now_ms = 5000;
    pc_udp_reset_listeners();
    pc_coap_server_begin(5683);
    pc_udp_capture_enable();

    const char *paths[] = {"temp"};
    uint8_t req[64];
    size_t rl = build(req, (uint8_t)CoapType::COAP_TYPE_CON, (uint8_t)CoapMethod::COAP_GET, nullptr, 0, 0x4242, paths,
                      1, nullptr, 0, -1, nullptr, 0);

    g_called = false;
    pc_udp_capture_reset();
    pc_udp_inject(5683, "10.0.0.9", 5555, req, rl);
    TEST_ASSERT_TRUE(g_called); // handler ran
    size_t n1 = pc_udp_captured_len();
    TEST_ASSERT_TRUE(n1 > 0);
    uint8_t saved[64];
    memcpy(saved, pc_udp_captured(), n1);

    g_called = false;
    pc_udp_capture_reset();
    pc_udp_inject(5683, "10.0.0.9", 5555, req, rl); // duplicate CON (same mid + source)
    TEST_ASSERT_FALSE(g_called);                    // deduplicated: handler skipped
    TEST_ASSERT_EQUAL_size_t(n1, pc_udp_captured_len());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(saved, pc_udp_captured(), n1); // same cached response resent

    g_called = false;
    pc_udp_capture_reset();
    pc_udp_inject(5683, "10.0.0.10", 5555, req, rl); // different source, same mid -> new exchange
    TEST_ASSERT_TRUE(g_called);

    pc_set_clock(nullptr, 1000);
}
#endif // PC_COAP_DEDUP_ENTRIES > 0

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_response_option_capacity_stop);
    RUN_TEST(test_coap_udp_handler_basic);
#if PC_ENABLE_COAP_OBSERVE
    RUN_TEST(test_coap_observe_over_udp);
    RUN_TEST(test_coap_observe_registry_full);
    RUN_TEST(test_coap_observe_registry_key_fields);
    RUN_TEST(test_coap_observe_zero_length_token);
    RUN_TEST(test_coap_observe_targeted_removal);
    RUN_TEST(test_coap_notify_clamps_oversized_body);
    RUN_TEST(test_coap_observe_on_discovery_is_not_registered);
    RUN_TEST(test_coap_udp_edge_datagrams);
#endif
    RUN_TEST(test_non_confirmable_malformed_is_silent);
    RUN_TEST(test_response_code_as_request_is_method_not_allowed);
    RUN_TEST(test_block1_ignored_on_get);
    RUN_TEST(test_block1_block_size_change_is_incomplete);
    RUN_TEST(test_block1_empty_intermediate_block);
    RUN_TEST(test_error_response_carries_no_observe_or_block2);
    RUN_TEST(test_block2_offset_at_end_of_representation);
    RUN_TEST(test_block2_on_empty_success_body);
    RUN_TEST(test_add_resource_limits);
    RUN_TEST(test_short_and_truncated_token);
    RUN_TEST(test_malformed_options_bad_request);
    RUN_TEST(test_extended_delta_and_length_ignored);
    RUN_TEST(test_oversized_path_and_query);
    RUN_TEST(test_block_option_too_wide);
    RUN_TEST(test_block1_reserved_szx);
    RUN_TEST(test_block1_continue_no_space);
    RUN_TEST(test_response_payload_clamped);
    RUN_TEST(test_response_buffer_too_small);
    RUN_TEST(test_well_known_core_truncates);
    RUN_TEST(test_observe_large_seq_encoding);
    RUN_TEST(test_block2_explicit_paging);
    RUN_TEST(test_block2_auto_when_large);
    RUN_TEST(test_block2_szx_clamped);
    RUN_TEST(test_block2_absent_for_small);
    RUN_TEST(test_block2_out_of_range);
    RUN_TEST(test_block2_reserved_szx);
    RUN_TEST(test_block1_upload_two_blocks);
    RUN_TEST(test_block1_out_of_order);
    RUN_TEST(test_block1_too_large);
    RUN_TEST(test_observe_option_in_response);
    RUN_TEST(test_response_option_overflows_buffer);
    RUN_TEST(test_no_observe_option_when_seq_negative);
    RUN_TEST(test_get_content);
    RUN_TEST(test_not_found);
    RUN_TEST(test_method_not_allowed);
    RUN_TEST(test_non_request_type);
    RUN_TEST(test_put_with_payload);
    RUN_TEST(test_multi_segment_path);
    RUN_TEST(test_uri_query);
    RUN_TEST(test_empty_con_ping_rst);
    RUN_TEST(test_bad_version_rst);
    RUN_TEST(test_delete);
    RUN_TEST(test_token_8_bytes);
    RUN_TEST(test_extended_option_length);
    RUN_TEST(test_ack_ignored);
    RUN_TEST(test_root_path);
    RUN_TEST(test_unknown_method_not_allowed);
    RUN_TEST(test_unknown_critical_option_bad_option);
    RUN_TEST(test_well_known_core_discovery);
    RUN_TEST(test_well_known_core_rejects_post);
#if PC_COAP_DEDUP_ENTRIES > 0
    RUN_TEST(test_dedup_store_lookup_roundtrip);
    RUN_TEST(test_dedup_full_address_keying);
    RUN_TEST(test_dedup_expiry);
    RUN_TEST(test_dedup_too_large_not_cached);
    RUN_TEST(test_dedup_eviction_and_update);
    RUN_TEST(test_dedup_handler_replays_without_rerunning);
#endif
    return UNITY_END();
}
