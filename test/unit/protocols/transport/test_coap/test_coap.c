// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/transport/udp/server/server.h"
#include "network_drivers/transport/udp/udp.h"
#include "server/clock/clock.h"
#include "services/iot/coap/coap.h"
#include <string.h>

#include <unity.h>

static void inject(uint16_t port, const char *src_ip, uint16_t src_port, const uint8_t *data, size_t len)
{
    protocore_net_host_udp_deliver(port, src_ip, src_port, (void *)(uintptr_t)data, (uint16_t)len);
    UdpListener.poll(UdpListener.internal);
}

static size_t sent_len(void)
{
    size_t n = protocore_net_host_udp_count();
    return n ? protocore_net_host_udp_at(n - 1)->len : 0;
}

static const uint8_t *sent_bytes(void)
{
    size_t n = protocore_net_host_udp_count();
    return n ? protocore_net_host_udp_at(n - 1)->data : NULL;
}

static void reset_udp(void)
{
    UdpListener.port = 5683;
    UdpListener.close(UdpListener.internal);
    (void)UdpListener.ok;
    protocore_net_host_udp_reset();
}

static CoapMethod g_method;
static char g_path[128];
static char g_query[128];
static CoapContentFormat g_cf;
static uint8_t g_payload[128];
static size_t g_payload_len;
static proto_bool g_called;

static void record(const CoapRequest *req)
{
    g_called = PROTO_TRUE;
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

static void h_resource(const CoapRequest *req, CoapResponse *resp)
{
    record(req);
    switch (req->method)
    {
    case COAP_GET:
        resp->code = (uint8_t)COAP_RSP_CONTENT;
        memcpy(resp->payload, "hi", 2);
        resp->payload_len = 2;
        resp->content_format = COAP_CF_TEXT;
        break;
    case COAP_POST:
        resp->code = (uint8_t)COAP_RSP_CREATED;
        resp->payload_len = 0;
        break;
    case COAP_PUT:
        resp->code = (uint8_t)COAP_RSP_CHANGED;
        resp->payload_len = 0;
        break;
    case COAP_DELETE:
        resp->code = (uint8_t)COAP_RSP_DELETED;
        resp->payload_len = 0;
        break;
    }
}

static const size_t BIG_LEN = 150;
static uint8_t big_expected(size_t i)
{
    return (uint8_t)('A' + (int)(i % 26));
}
static void h_big(const CoapRequest *req, CoapResponse *resp)
{
    record(req);
    resp->code = (uint8_t)COAP_RSP_CONTENT;
    resp->content_format = COAP_CF_TEXT;
    resp->payload_len = BIG_LEN;
    for (size_t i = 0; i < BIG_LEN; i++)
    {
        resp->payload[i] = big_expected(i);
    }
}

void setUp()
{
    g_called = PROTO_FALSE;
    g_method = (CoapMethod)0;
    g_path[0] = '\0';
    g_query[0] = '\0';
    g_cf = COAP_CF_NONE;
    g_payload_len = 0;

    protocore_coap_server_reset();
    protocore_coap_server_add_resource("/temp", COAP_ALLOW_GET | COAP_ALLOW_POST | COAP_ALLOW_PUT | COAP_ALLOW_DELETE,
                                       h_resource);
    protocore_coap_server_add_resource("/ro", COAP_ALLOW_GET, h_resource);
    protocore_coap_server_add_resource("/a/b", COAP_ALLOW_GET, h_resource);
    protocore_coap_server_add_resource("/longresourcename12345", COAP_ALLOW_GET, h_resource);
    protocore_coap_server_add_resource("/", COAP_ALLOW_GET, h_resource);
    protocore_coap_server_add_resource("/big", COAP_ALLOW_GET | COAP_ALLOW_POST | COAP_ALLOW_PUT, h_big);
}

void tearDown()
{
}

typedef struct
{
    uint8_t *buf;
    size_t len;
    uint32_t last_opt;
} CoapEnc;

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

typedef struct
{
    uint8_t ver, type, tkl, code;
    uint16_t mid;
    const uint8_t *token;
    CoapContentFormat content_format;
    int observe;
    int block1;
    int block2;
    const uint8_t *payload;
    size_t payload_len;
} CoapDec;

#define BLK_NUM(v) ((uint32_t)(v) >> 4)
#define BLK_M(v) (((uint32_t)(v) >> 3) & 1)
#define BLK_SZX(v) ((uint32_t)(v) & 7)

static proto_bool dec(const uint8_t *buf, size_t len, CoapDec *d)
{
    if (len < 4)
    {
        return PROTO_FALSE;
    }
    d->ver = buf[0] >> 6;
    d->type = (buf[0] >> 4) & 0x03;
    d->tkl = buf[0] & 0x0F;
    d->code = buf[1];
    d->mid = (uint16_t)((buf[2] << 8) | buf[3]);
    d->token = buf + 4;
    d->content_format = COAP_CF_NONE;
    d->observe = -1;
    d->block1 = -1;
    d->block2 = -1;
    d->payload = NULL;
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
    return PROTO_TRUE;
}

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

void test_get_content()
{
    const char *paths[] = {"temp"};
    uint8_t tok[] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t req[128], resp[128];
    size_t rl = build(req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, tok, 4, 0x1234, paths, 1, NULL, 0, -1, NULL, 0);
    size_t n = protocore_coap_server_process(req, rl, resp, sizeof(resp));
    TEST_ASSERT_GREATER_THAN_UINT(0, n);

    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT(1, d.ver);
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_TYPE_ACK, d.type);
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CONTENT, d.code);
    TEST_ASSERT_EQUAL_UINT(0x1234, d.mid);
    TEST_ASSERT_EQUAL_UINT(4, d.tkl);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tok, d.token, 4);
    TEST_ASSERT_EQUAL_UINT((uint16_t)COAP_CF_TEXT, d.content_format);
    TEST_ASSERT_EQUAL_UINT(2, d.payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("hi", d.payload, 2);

    TEST_ASSERT_TRUE(g_called);
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_GET, g_method);
    TEST_ASSERT_EQUAL_STRING("/temp", g_path);
}

void test_not_found()
{
    const char *paths[] = {"missing"};
    uint8_t req[128], resp[128];
    size_t rl = build(req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, NULL, 0, 0x0001, paths, 1, NULL, 0, -1, NULL, 0);
    size_t n = protocore_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_NOT_FOUND, d.code);
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_TYPE_ACK, d.type);
    TEST_ASSERT_FALSE(g_called);
}

void test_method_not_allowed()
{
    const char *paths[] = {"ro"};
    uint8_t req[128], resp[128];
    size_t rl = build(req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_PUT, NULL, 0, 0x0002, paths, 1, NULL, 0, -1, NULL, 0);
    size_t n = protocore_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_METHOD_NOT_ALLOWED, d.code);
    TEST_ASSERT_FALSE(g_called);
}

void test_non_request_type()
{
    const char *paths[] = {"temp"};
    uint8_t req[128], resp[128];
    size_t rl = build(req, (uint8_t)COAP_TYPE_NON, (uint8_t)COAP_GET, NULL, 0, 0x0003, paths, 1, NULL, 0, -1, NULL, 0);
    size_t n = protocore_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_TYPE_NON, d.type);
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CONTENT, d.code);
}

void test_put_with_payload()
{
    const char *paths[] = {"temp"};
    const uint8_t body[] = {'2', '5'};
    uint8_t req[128], resp[128];
    size_t rl = build(req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_PUT, NULL, 0, 0x0004, paths, 1, NULL, 0,
                      (uint16_t)COAP_CF_TEXT, body, 2);
    size_t n = protocore_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CHANGED, d.code);

    TEST_ASSERT_TRUE(g_called);
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_PUT, g_method);
    TEST_ASSERT_EQUAL_UINT(2, g_payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, g_payload, 2);
    TEST_ASSERT_EQUAL_UINT((uint16_t)COAP_CF_TEXT, g_cf);
}

void test_multi_segment_path()
{
    const char *paths[] = {"a", "b"};
    uint8_t req[128], resp[128];
    size_t rl = build(req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, NULL, 0, 0x0005, paths, 2, NULL, 0, -1, NULL, 0);
    size_t n = protocore_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CONTENT, d.code);
    TEST_ASSERT_EQUAL_STRING("/a/b", g_path);
}

void test_uri_query()
{
    const char *paths[] = {"temp"};
    const char *queries[] = {"x=1", "y=2"};
    uint8_t req[128], resp[128];
    size_t rl =
        build(req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, NULL, 0, 0x0006, paths, 1, queries, 2, -1, NULL, 0);
    size_t n = protocore_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CONTENT, d.code);
    TEST_ASSERT_EQUAL_STRING("x=1&y=2", g_query);
}

void test_empty_con_ping_rst()
{
    uint8_t req[8], resp[16];
    CoapEnc e;
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, 0, NULL, 0, 0x4242);
    size_t n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_TYPE_RST, d.type);
    TEST_ASSERT_EQUAL_UINT(0, d.code);
    TEST_ASSERT_EQUAL_UINT(0x4242, d.mid);
    TEST_ASSERT_EQUAL_UINT(0, d.tkl);
}

void test_bad_version_rst()
{
    uint8_t req[8] = {0};
    req[0] = (uint8_t)((2 << 6) | ((uint8_t)COAP_TYPE_CON << 4) | 0);
    req[1] = (uint8_t)COAP_GET;
    req[2] = 0x12;
    req[3] = 0x34;
    uint8_t resp[16];
    size_t n = protocore_coap_server_process(req, 4, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_TYPE_RST, d.type);
    TEST_ASSERT_EQUAL_UINT(0x1234, d.mid);
}

void test_delete()
{
    const char *paths[] = {"temp"};
    uint8_t req[128], resp[128];
    size_t rl =
        build(req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_DELETE, NULL, 0, 0x0007, paths, 1, NULL, 0, -1, NULL, 0);
    size_t n = protocore_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_DELETED, d.code);
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_DELETE, g_method);
}

void test_token_8_bytes()
{
    const char *paths[] = {"temp"};
    uint8_t tok[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t req[128], resp[128];
    size_t rl = build(req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, tok, 8, 0x0008, paths, 1, NULL, 0, -1, NULL, 0);
    size_t n = protocore_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT(8, d.tkl);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tok, d.token, 8);
}

void test_extended_option_length()
{
    const char *paths[] = {"longresourcename12345"};
    uint8_t req[128], resp[128];
    size_t rl = build(req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, NULL, 0, 0x0009, paths, 1, NULL, 0, -1, NULL, 0);
    size_t n = protocore_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CONTENT, d.code);
    TEST_ASSERT_EQUAL_STRING("/longresourcename12345", g_path);
}

void test_ack_ignored()
{
    uint8_t req[8];
    CoapEnc e;
    enc_init(&e, req, (uint8_t)COAP_TYPE_ACK, (uint8_t)COAP_RSP_CONTENT, NULL, 0, 0x00AA);
    uint8_t resp[16];
    size_t n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT(0, n);
}

void test_root_path()
{
    uint8_t req[16], resp[64];
    size_t rl = build(req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, NULL, 0, 0x000B, NULL, 0, NULL, 0, -1, NULL, 0);
    size_t n = protocore_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CONTENT, d.code);
    TEST_ASSERT_EQUAL_STRING("/", g_path);
}

void test_unknown_method_not_allowed()
{
    const char *paths[] = {"temp"};
    uint8_t req[128], resp[128];

    size_t rl = build(req, (uint8_t)COAP_TYPE_CON, COAP_CODE(0, 5), NULL, 0, 0x000C, paths, 1, NULL, 0, -1, NULL, 0);
    size_t n = protocore_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_METHOD_NOT_ALLOWED, d.code);
}

void test_unknown_critical_option_bad_option()
{
    uint8_t resp[128];

    uint8_t m[32];
    size_t k = 0;
    m[k++] = 0x40;
    m[k++] = (uint8_t)COAP_GET;
    m[k++] = 0x00;
    m[k++] = 0x0C;
    m[k++] = (uint8_t)(0xB0 | 4);
    m[k++] = 't';
    m[k++] = 'e';
    m[k++] = 'm';
    m[k++] = 'p';
    m[k++] = (uint8_t)((6 << 4) | 1);
    m[k++] = 0x00;
    size_t n = protocore_coap_server_process(m, k, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_BAD_OPTION, d.code);
}

void test_observe_option_in_response()
{
    const char *paths[] = {"ro"};
    uint8_t tok[] = {0x01, 0x02};
    uint8_t req[128], resp[128];
    size_t rl = build(req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, tok, 2, 0x2222, paths, 1, NULL, 0, -1, NULL, 0);
    size_t n = protocore_coap_server_process_ex(req, rl, resp, sizeof(resp), 5);
    TEST_ASSERT_GREATER_THAN_UINT(0, n);
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_TYPE_ACK, d.type);
    TEST_ASSERT_EQUAL_INT(5, d.observe);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)COAP_CF_TEXT, d.content_format);
    TEST_ASSERT_EQUAL_size_t(2, d.payload_len);
}

void test_response_option_overflows_buffer()
{
    const char *paths[] = {"ro"};
    uint8_t tok[] = {0x01, 0x02};
    uint8_t req[128], resp[8];
    size_t rl = build(req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, tok, 2, 0x2222, paths, 1, NULL, 0, -1, NULL, 0);

    size_t n = protocore_coap_server_process(req, rl, resp, 6);
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)COAP_CF_NONE, d.content_format);
}

void test_no_observe_option_when_seq_negative()
{
    const char *paths[] = {"ro"};
    uint8_t req[128], resp[128];
    size_t rl = build(req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, NULL, 0, 0x2223, paths, 1, NULL, 0, -1, NULL, 0);
    size_t n = protocore_coap_server_process_ex(req, rl, resp, sizeof(resp), -1);
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_INT(-1, d.observe);
}

void test_block2_explicit_paging()
{
    const uint8_t expect_more[] = {1, 1, 0};
    const size_t expect_len[] = {64, 64, BIG_LEN - 128};
    for (uint32_t num = 0; num < 3; num++)
    {
        uint8_t req[64], resp[256];
        CoapEnc e;
        enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, NULL, 0, (uint16_t)(0x3000 + num));
        enc_option(&e, 11, (const uint8_t *)"big", 3);
        enc_block(&e, 23, num, 0, 2);
        size_t n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
        TEST_ASSERT_GREATER_THAN_UINT(0, n);

        CoapDec d;
        TEST_ASSERT_TRUE(dec(resp, n, &d));
        TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CONTENT, d.code);
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

void test_block2_auto_when_large()
{
    uint8_t req[64], resp[256];
    CoapEnc e;
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, NULL, 0, 0x3100);
    enc_option(&e, 11, (const uint8_t *)"big", 3);
    size_t n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_TRUE(d.block2 >= 0);
    TEST_ASSERT_EQUAL_UINT(0, BLK_NUM(d.block2));
    TEST_ASSERT_EQUAL_UINT(1, BLK_M(d.block2));
    TEST_ASSERT_EQUAL_UINT(2, BLK_SZX(d.block2));
    TEST_ASSERT_EQUAL_size_t(64, d.payload_len);
}

void test_block2_szx_clamped()
{
    uint8_t req[64], resp[256];
    CoapEnc e;
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, NULL, 0, 0x3200);
    enc_option(&e, 11, (const uint8_t *)"big", 3);
    enc_block(&e, 23, 0, 0, 6);
    size_t n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT(2, BLK_SZX(d.block2));
    TEST_ASSERT_EQUAL_size_t(64, d.payload_len);
}

void test_block2_absent_for_small()
{
    const char *paths[] = {"temp"};
    uint8_t req[64], resp[128];
    size_t rl = build(req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, NULL, 0, 0x3300, paths, 1, NULL, 0, -1, NULL, 0);
    size_t n = protocore_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_INT(-1, d.block2);
    TEST_ASSERT_EQUAL_size_t(2, d.payload_len);
}

void test_block2_out_of_range()
{
    uint8_t req[64], resp[256];
    CoapEnc e;
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, NULL, 0, 0x3400);
    enc_option(&e, 11, (const uint8_t *)"big", 3);
    enc_block(&e, 23, 10, 0, 2);
    size_t n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_BAD_REQUEST, d.code);
}

void test_block2_reserved_szx()
{
    uint8_t req[64], resp[256];
    CoapEnc e;
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, NULL, 0, 0x3500);
    enc_option(&e, 11, (const uint8_t *)"big", 3);
    enc_block(&e, 23, 0, 0, 7);
    size_t n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_BAD_OPTION, d.code);
}

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

    uint8_t req[128], resp[256];
    CoapEnc e;
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_POST, NULL, 0, 0x3600);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_block(&e, 27, 0, 1, 2);
    enc_payload(&e, chunk0, 64);
    size_t n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CONTINUE, d.code);
    TEST_ASSERT_TRUE(d.block1 >= 0);
    TEST_ASSERT_EQUAL_UINT(0, BLK_NUM(d.block1));
    TEST_ASSERT_EQUAL_UINT(1, BLK_M(d.block1));
    TEST_ASSERT_FALSE(g_called);

    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_POST, NULL, 0, 0x3601);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_block(&e, 27, 1, 0, 2);
    enc_payload(&e, chunk1, 20);
    n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CREATED, d.code);
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

void test_block1_out_of_order()
{
    uint8_t chunk[64];
    for (int i = 0; i < 64; i++)
    {
        chunk[i] = (uint8_t)i;
    }
    uint8_t req[128], resp[256];
    CoapEnc e;
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_POST, NULL, 0, 0x3700);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_block(&e, 27, 0, 1, 2);
    enc_payload(&e, chunk, 64);
    protocore_coap_server_process(req, e.len, resp, sizeof(resp));

    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_POST, NULL, 0, 0x3701);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_block(&e, 27, 2, 0, 2);
    enc_payload(&e, chunk, 64);
    size_t n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_REQUEST_ENTITY_INCOMPLETE, d.code);
}

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
        enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_POST, NULL, 0, (uint16_t)(0x3800 + num));
        enc_option(&e, 11, (const uint8_t *)"temp", 4);
        enc_block(&e, 27, num, 1, 2);
        enc_payload(&e, chunk, 64);
        size_t n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
        TEST_ASSERT_TRUE(dec(resp, n, &d));
        if (num < 2)
        {
            TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CONTINUE, d.code);
        }
        else
        {
            TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_REQUEST_ENTITY_TOO_LARGE, d.code);
        }
    }
    TEST_ASSERT_FALSE(g_called);
}

void test_well_known_core_discovery()
{
    const char *paths[] = {".well-known", "core"};
    uint8_t req[160], resp[256];
    size_t rl = build(req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, NULL, 0, 0x0CDE, paths, 2, NULL, 0, -1, NULL, 0);
    size_t n = protocore_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CONTENT, d.code);
    TEST_ASSERT_EQUAL_UINT((uint16_t)COAP_CF_LINK, d.content_format);
    TEST_ASSERT_FALSE(g_called);

    char body[512];
    TEST_ASSERT_TRUE(d.payload_len < sizeof(body));
    memcpy(body, d.payload, d.payload_len);
    body[d.payload_len] = '\0';
    TEST_ASSERT_NOT_NULL(strstr(body, "</temp>"));
    TEST_ASSERT_NOT_NULL(strstr(body, "</ro>"));
    TEST_ASSERT_NOT_NULL(strstr(body, "</a/b>"));
}

void test_well_known_core_rejects_post()
{
    const char *paths[] = {".well-known", "core"};
    uint8_t req[160], resp[256];
    size_t rl = build(req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_POST, NULL, 0, 0x0CDF, paths, 2, NULL, 0, -1, NULL, 0);
    size_t n = protocore_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_METHOD_NOT_ALLOWED, d.code);
}

static void h_overflow(const CoapRequest *req, CoapResponse *resp)
{
    (void)req;
    resp->code = (uint8_t)COAP_RSP_CONTENT;
    resp->content_format = COAP_CF_TEXT;
    resp->payload_len = resp->payload_cap + 1000;
}

void test_add_resource_limits()
{
    TEST_ASSERT_FALSE(protocore_coap_server_add_resource(NULL, COAP_ALLOW_GET, h_resource));
    TEST_ASSERT_FALSE(protocore_coap_server_add_resource("/x", COAP_ALLOW_GET, NULL));
    int added = 0;
    while (protocore_coap_server_add_resource("/fill", COAP_ALLOW_GET, h_resource))
    {
        if (++added > 64)
        {
            break;
        }
    }
    TEST_ASSERT_LESS_THAN(64, added);
    TEST_ASSERT_FALSE(protocore_coap_server_add_resource("/nope", COAP_ALLOW_GET, h_resource));
}

void test_short_and_truncated_token()
{
    uint8_t resp[64];
    uint8_t too_short[3] = {0x40, (uint8_t)COAP_GET, 0x00};
    TEST_ASSERT_EQUAL_UINT(0, protocore_coap_server_process(too_short, 3, resp, sizeof(resp)));

    uint8_t bad_tkl[4] = {(uint8_t)((1 << 6) | ((uint8_t)COAP_TYPE_CON << 4) | 3), (uint8_t)COAP_GET, 0x12, 0x34};
    size_t n = protocore_coap_server_process(bad_tkl, 4, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT(4, n);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)COAP_TYPE_RST, (resp[0] >> 4) & 0x03);
    TEST_ASSERT_EQUAL_UINT8(0, resp[1]);
}

void test_malformed_options_bad_request()
{
    uint8_t resp[64];
    const uint8_t hdr[4] = {0x40, (uint8_t)COAP_GET, 0xAB, 0xCD};

    typedef struct
    {
        const char *name;
        uint8_t opt[4];
        size_t olen;
    } Case;
    const Case cases[] = {
        {"delta15_reserved", {0xF5}, 1},
        {"olen15_reserved", {0x0F}, 1},
        {"delta13_truncated", {0xD0}, 1},
        {"delta14_truncated", {0xE0, 0x01}, 2},
        {"olen13_truncated", {0x0D}, 1},
        {"olen14_truncated", {0x0E, 0x01}, 2},
        {"value_truncated", {0x03, 0x61, 0x62}, 3},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        uint8_t req[16];
        memcpy(req, hdr, 4);
        memcpy(req + 4, cases[i].opt, cases[i].olen);
        size_t n = protocore_coap_server_process(req, 4 + cases[i].olen, resp, sizeof(resp));
        TEST_ASSERT_TRUE(n > 0);
        TEST_ASSERT_EQUAL_UINT_MESSAGE((uint8_t)COAP_RSP_BAD_REQUEST, resp[1], cases[i].name);
    }
}

void test_extended_delta_and_length_ignored()
{
    uint8_t req[400], resp[512], tok[1] = {0};
    CoapEnc e;
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, tok, 0, 1);
    enc_option(&e, 300, NULL, 0);
    size_t n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CONTENT, resp[1]);

    uint8_t big[269];
    memset(big, 'x', sizeof(big));
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, tok, 0, 2);
    enc_option(&e, 60, big, sizeof(big));
    n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CONTENT, resp[1]);
}

void test_oversized_path_and_query()
{
    uint8_t req[256], resp[128], tok[1] = {0};
    uint8_t seg[80];
    CoapEnc e;

    memset(seg, 'p', sizeof(seg));
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, tok, 0, 1);
    enc_option(&e, 11, seg, sizeof(seg));
    TEST_ASSERT_TRUE(protocore_coap_server_process(req, e.len, resp, sizeof(resp)) > 0);
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_BAD_REQUEST, resp[1]);

    memset(seg, 'q', sizeof(seg));
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, tok, 0, 2);
    enc_option(&e, 15, seg, sizeof(seg));
    TEST_ASSERT_TRUE(protocore_coap_server_process(req, e.len, resp, sizeof(resp)) > 0);
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_BAD_REQUEST, resp[1]);
}

void test_block_option_too_wide()
{
    uint8_t req[64], resp[64], tok[1] = {0}, v4[4] = {0, 0, 0, 0};
    CoapEnc e;

    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_PUT, tok, 0, 1);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_option(&e, 27, v4, 4);
    TEST_ASSERT_TRUE(protocore_coap_server_process(req, e.len, resp, sizeof(resp)) > 0);
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_BAD_REQUEST, resp[1]);

    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, tok, 0, 2);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_option(&e, 23, v4, 4);
    TEST_ASSERT_TRUE(protocore_coap_server_process(req, e.len, resp, sizeof(resp)) > 0);
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_BAD_REQUEST, resp[1]);
}

void test_block1_reserved_szx()
{
    uint8_t req[64], resp[64], tok[1] = {0}, v[1] = {0x07};
    CoapEnc e;
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_POST, tok, 0, 1);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_option(&e, 27, v, 1);
    TEST_ASSERT_TRUE(protocore_coap_server_process(req, e.len, resp, sizeof(resp)) > 0);
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_BAD_OPTION, resp[1]);
}

void test_block1_continue_no_space()
{
    uint8_t req[64], resp[3], tok[1] = {0}, v[1] = {0x0A};
    uint8_t pl[16];
    memset(pl, 'z', sizeof(pl));
    CoapEnc e;
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_PUT, tok, 0, 1);
    enc_option(&e, 11, (const uint8_t *)"big", 3);
    enc_option(&e, 27, v, 1);
    enc_payload(&e, pl, sizeof(pl));
    TEST_ASSERT_EQUAL_UINT(0, protocore_coap_server_process(req, e.len, resp, sizeof(resp)));
}

void test_response_payload_clamped()
{
    TEST_ASSERT_TRUE(protocore_coap_server_add_resource("/of", COAP_ALLOW_GET, h_overflow));
    uint8_t req[32], resp[256], tok[1] = {0};
    CoapEnc e;
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, tok, 0, 1);
    enc_option(&e, 11, (const uint8_t *)"of", 2);
    size_t n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CONTENT, resp[1]);
}

void test_response_buffer_too_small()
{
    uint8_t req[32], resp[3], tok[1] = {0};
    CoapEnc e;
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, tok, 0, 1);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    TEST_ASSERT_EQUAL_UINT(0, protocore_coap_server_process(req, e.len, resp, sizeof(resp)));
}

static char g_longpaths[8][40];

void test_well_known_core_truncates()
{
    protocore_coap_server_reset();
    for (int i = 0; i < 8; i++)
    {
        memset(g_longpaths[i], 'a' + i, 34);
        g_longpaths[i][0] = '/';
        g_longpaths[i][34] = '\0';
        TEST_ASSERT_TRUE(protocore_coap_server_add_resource(g_longpaths[i], COAP_ALLOW_GET, h_resource));
    }
    uint8_t req[64], resp[512], tok[1] = {0};
    CoapEnc e;
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, tok, 0, 1);
    enc_option(&e, 11, (const uint8_t *)".well-known", 11);
    enc_option(&e, 11, (const uint8_t *)"core", 4);
    size_t n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CONTENT, resp[1]);
}

void test_observe_large_seq_encoding()
{
    uint8_t req[32], resp[64], tok[2] = {0xAA, 0xBB};
    CoapEnc e;
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, tok, 2, 1);
    enc_option(&e, 6, NULL, 0);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    TEST_ASSERT_TRUE(protocore_coap_server_process_ex(req, e.len, resp, sizeof(resp), 0x0102) > 0);
    TEST_ASSERT_TRUE(protocore_coap_server_process_ex(req, e.len, resp, sizeof(resp), 0x010203) > 0);
}

void test_response_option_capacity_stop()
{
    const char *paths[] = {"temp"};
    uint8_t req[64];
    size_t rl = build(req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, NULL, 0, 0x77, paths, 1, NULL, 0, -1, NULL, 0);
    uint8_t resp[64];
    size_t n = protocore_coap_server_process_ex(req, rl, resp, 5, -1);
    TEST_ASSERT_TRUE(n >= 4 && n <= 5);
}

void test_coap_udp_handler_basic()
{
    reset_udp();
    protocore_coap_server_begin(5683);

    const char *paths[] = {"temp"};
    uint8_t req[64];
    size_t rl = build(req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, NULL, 0, 0x9001, paths, 1, NULL, 0, -1, NULL, 0);
    protocore_net_host_udp_reset();
    inject(5683, "10.0.0.5", 5000, req, rl);
    TEST_ASSERT_TRUE(sent_len() > 0);

    uint8_t ack[8];
    CoapEnc e;
    enc_init(&e, ack, 2, 0, NULL, 0, 0x9001);
    protocore_net_host_udp_reset();
    inject(5683, "10.0.0.5", 5000, ack, e.len);
    TEST_ASSERT_EQUAL_UINT(0, sent_len());
}

void test_non_confirmable_malformed_is_silent()
{
    uint8_t resp[32];

    uint8_t bad_tkl_con[16] = {(uint8_t)((1 << 6) | ((uint8_t)COAP_TYPE_CON << 4) | 9), (uint8_t)COAP_GET, 0x01, 0x02};
    size_t n = protocore_coap_server_process(bad_tkl_con, sizeof(bad_tkl_con), resp, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT(4, n);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)COAP_TYPE_RST, (resp[0] >> 4) & 0x03);
    TEST_ASSERT_EQUAL_UINT8(0, resp[1]);

    uint8_t bad_ver_non[4] = {(uint8_t)((2 << 6) | ((uint8_t)COAP_TYPE_NON << 4) | 0), (uint8_t)COAP_GET, 0x01, 0x03};
    TEST_ASSERT_EQUAL_UINT(0, protocore_coap_server_process(bad_ver_non, 4, resp, sizeof(resp)));

    uint8_t bad_tkl_non[4] = {(uint8_t)((1 << 6) | ((uint8_t)COAP_TYPE_NON << 4) | 9), (uint8_t)COAP_GET, 0x01, 0x04};
    TEST_ASSERT_EQUAL_UINT(0, protocore_coap_server_process(bad_tkl_non, 4, resp, sizeof(resp)));

    uint8_t short_tok_non[4] = {(uint8_t)((1 << 6) | ((uint8_t)COAP_TYPE_NON << 4) | 3), (uint8_t)COAP_GET, 0x01, 0x05};
    TEST_ASSERT_EQUAL_UINT(0, protocore_coap_server_process(short_tok_non, 4, resp, sizeof(resp)));

    uint8_t empty_non[4] = {(uint8_t)((1 << 6) | ((uint8_t)COAP_TYPE_NON << 4) | 0), 0x00, 0x01, 0x06};
    TEST_ASSERT_EQUAL_UINT(0, protocore_coap_server_process(empty_non, 4, resp, sizeof(resp)));
}

void test_response_code_as_request_is_method_not_allowed()
{
    const char *paths[] = {"temp"};
    uint8_t req[64], resp[64];
    size_t rl = build(req, (uint8_t)COAP_TYPE_CON, COAP_CODE(2, 5), NULL, 0, 0x0D01, paths, 1, NULL, 0, -1, NULL, 0);
    size_t n = protocore_coap_server_process(req, rl, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_METHOD_NOT_ALLOWED, d.code);
    TEST_ASSERT_FALSE(g_called);
}

void test_block1_ignored_on_get()
{
    uint8_t req[64], resp[128], tok[1] = {0};
    CoapEnc e;
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, tok, 0, 0x0D02);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_block(&e, 27, 3, 1, 2);
    size_t n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CONTENT, d.code);
    TEST_ASSERT_EQUAL_INT(-1, d.block1);
    TEST_ASSERT_TRUE(g_called);
}

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
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_POST, NULL, 0, 0x3900);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_block(&e, 27, 0, 1, 2);
    enc_payload(&e, chunk, 64);
    size_t n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CONTINUE, d.code);

    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_POST, NULL, 0, 0x3901);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_block(&e, 27, 2, 0, 1);
    enc_payload(&e, chunk, 32);
    n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_REQUEST_ENTITY_INCOMPLETE, d.code);
    TEST_ASSERT_FALSE(g_called);
}

void test_block1_empty_intermediate_block()
{
    uint8_t req[64], resp[128];
    CoapDec d;
    CoapEnc e;
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_POST, NULL, 0, 0x3A00);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_block(&e, 27, 0, 1, 2);
    size_t n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CONTINUE, d.code);
    TEST_ASSERT_FALSE(g_called);

    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_POST, NULL, 0, 0x3A01);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_block(&e, 27, 1, 0, 2);
    n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_REQUEST_ENTITY_INCOMPLETE, d.code);
}

static void h_error(const CoapRequest *req, CoapResponse *resp)
{
    record(req);
    resp->code = (uint8_t)COAP_RSP_BAD_REQUEST;
    resp->content_format = COAP_CF_NONE;
    resp->payload_len = 0;
}

void test_error_response_carries_no_observe_or_block2()
{
    TEST_ASSERT_TRUE(protocore_coap_server_add_resource("/err", COAP_ALLOW_GET, h_error));
    uint8_t req[64], resp[128], tok[2] = {0x11, 0x22};
    CoapEnc e;
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, tok, 2, 0x0D03);
    enc_option(&e, 11, (const uint8_t *)"err", 3);
    enc_block(&e, 23, 0, 0, 2);
    size_t n = protocore_coap_server_process_ex(req, e.len, resp, sizeof(resp), 9);
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_BAD_REQUEST, d.code);
    TEST_ASSERT_EQUAL_INT(-1, d.observe);
    TEST_ASSERT_EQUAL_INT(-1, d.block2);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tok, d.token, 2);
}

static void h_exact_block(const CoapRequest *req, CoapResponse *resp)
{
    record(req);
    resp->code = (uint8_t)COAP_RSP_CONTENT;
    resp->content_format = COAP_CF_TEXT;
    resp->payload_len = 64;
    for (size_t i = 0; i < 64; i++)
    {
        resp->payload[i] = (uint8_t)('a' + (int)(i % 26));
    }
}

void test_block2_offset_at_end_of_representation()
{
    TEST_ASSERT_TRUE(protocore_coap_server_add_resource("/exact", COAP_ALLOW_GET, h_exact_block));
    uint8_t req[64], resp[256], tok[1] = {0};
    CoapDec d;
    CoapEnc e;

    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, tok, 0, 0x0D04);
    enc_option(&e, 11, (const uint8_t *)"exact", 5);
    enc_block(&e, 23, 0, 0, 2);
    size_t n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CONTENT, d.code);
    TEST_ASSERT_EQUAL_UINT(0, BLK_M(d.block2));
    TEST_ASSERT_EQUAL_UINT(64, d.payload_len);

    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, tok, 0, 0x0D05);
    enc_option(&e, 11, (const uint8_t *)"exact", 5);
    enc_block(&e, 23, 1, 0, 2);
    n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_BAD_REQUEST, d.code);
}

void test_block2_on_empty_success_body()
{
    uint8_t req[64], resp[128], tok[1] = {0};
    CoapEnc e;
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_POST, tok, 0, 0x0D06);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    enc_block(&e, 23, 0, 0, 2);
    size_t n = protocore_coap_server_process(req, e.len, resp, sizeof(resp));
    CoapDec d;
    TEST_ASSERT_TRUE(dec(resp, n, &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CREATED, d.code);
    TEST_ASSERT_TRUE(d.block2 >= 0);
    TEST_ASSERT_EQUAL_UINT(0, BLK_NUM(d.block2));
    TEST_ASSERT_EQUAL_UINT(0, BLK_M(d.block2));
    TEST_ASSERT_EQUAL_UINT(0, d.payload_len);
}

#if PROTOCORE_ENABLE_COAP_OBSERVE

static size_t build_observe_get(uint8_t *buf, const char *path, int observe, const uint8_t *token, uint8_t tkl,
                                uint16_t mid)
{
    CoapEnc e;
    enc_init(&e, buf, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, token, tkl, mid);
    uint8_t ov = (uint8_t)observe;
    enc_option(&e, 6, observe ? &ov : NULL, observe ? 1 : 0);
    enc_option(&e, 11, (const uint8_t *)path, strlen(path));
    return e.len;
}

void test_coap_observe_over_udp()
{
    reset_udp();
    protocore_coap_server_begin(5683);
    const uint8_t tok[2] = {0xAA, 0xBB};
    uint8_t req[64];

    protocore_net_host_udp_reset();
    size_t rl = build_observe_get(req, "temp", 0, tok, sizeof(tok), 0x0001);
    inject(5683, "10.0.0.9", 40000, req, rl);
    TEST_ASSERT_TRUE(sent_len() > 0);

    rl = build_observe_get(req, "temp", 0, tok, sizeof(tok), 0x0005);
    inject(5683, "10.0.0.9", 40000, req, rl);

    protocore_net_host_udp_reset();
    protocore_coap_notify("/temp");
    TEST_ASSERT_TRUE(sent_len() > 0);

    mock_udp_send_fail_after(0);
    protocore_coap_notify("/temp");
    mock_udp_send_fail_after(-1);
    protocore_net_host_udp_reset();
    protocore_coap_notify("/temp");
    TEST_ASSERT_EQUAL_UINT(0, sent_len());

    rl = build_observe_get(req, "temp", 0, tok, sizeof(tok), 0x0002);
    inject(5683, "10.0.0.9", 40000, req, rl);
    rl = build_observe_get(req, "temp", 1, tok, sizeof(tok), 0x0003);
    inject(5683, "10.0.0.9", 40000, req, rl);
    protocore_net_host_udp_reset();
    protocore_coap_notify("/temp");
    TEST_ASSERT_EQUAL_UINT(0, sent_len());

    rl = build_observe_get(req, "temp", 0, tok, sizeof(tok), 0x0004);
    inject(5683, "10.0.0.9", 40000, req, rl);
    uint8_t rst[8];
    CoapEnc re;
    enc_init(&re, rst, (uint8_t)COAP_TYPE_RST, 0, NULL, 0, 0x0004);
    inject(5683, "10.0.0.9", 40000, rst, re.len);
    protocore_net_host_udp_reset();
    protocore_coap_notify("/temp");
    TEST_ASSERT_EQUAL_UINT(0, sent_len());

    protocore_coap_notify("/no-such-resource");
}

void test_coap_observe_registry_full()
{
    reset_udp();
    protocore_coap_server_begin(5683);
    uint8_t req[64];

    for (int i = 0; i < PROTOCORE_COAP_MAX_OBSERVERS + 2; i++)
    {
        uint8_t tok[1] = {(uint8_t)i};
        size_t rl = build_observe_get(req, "temp", 0, tok, 1, (uint16_t)(0x100 + i));
        protocore_net_host_udp_reset();
        inject(5683, "10.0.0.9", 40000, req, rl);
        TEST_ASSERT_TRUE(sent_len() > 0);
    }
    protocore_coap_notify("/temp");
}

static int observe_seq_of_last_reply()
{
    CoapDec d;
    if (!sent_len() || !dec(sent_bytes(), sent_len(), &d))
    {
        return -2;
    }
    return d.observe;
}

void test_coap_observe_registry_key_fields()
{
    reset_udp();
    protocore_coap_server_begin(5683);
    const uint8_t tok[2] = {0xAA, 0xBB};
    uint8_t req[64];

    protocore_net_host_udp_reset();
    size_t rl = build_observe_get(req, "temp", 0, tok, sizeof(tok), 0x0201);
    inject(5683, "10.0.0.9", 40000, req, rl);
    TEST_ASSERT_EQUAL_INT(1, observe_seq_of_last_reply());

    protocore_coap_notify("/temp");

    protocore_net_host_udp_reset();
    rl = build_observe_get(req, "temp", 0, tok, sizeof(tok), 0x0202);
    inject(5683, "10.0.0.9", 40000, req, rl);
    TEST_ASSERT_EQUAL_INT(2, observe_seq_of_last_reply());

    protocore_net_host_udp_reset();
    rl = build_observe_get(req, "ro", 0, tok, sizeof(tok), 0x0203);
    inject(5683, "10.0.0.9", 40000, req, rl);
    TEST_ASSERT_EQUAL_INT(1, observe_seq_of_last_reply());

    protocore_net_host_udp_reset();
    rl = build_observe_get(req, "temp", 0, tok, sizeof(tok), 0x0204);
    inject(5683, "10.0.0.9", 40001, req, rl);
    TEST_ASSERT_EQUAL_INT(1, observe_seq_of_last_reply());

    protocore_net_host_udp_reset();
    rl = build_observe_get(req, "temp", 0, tok, sizeof(tok), 0x0205);
    inject(5683, "10.0.0.11", 40000, req, rl);
    TEST_ASSERT_EQUAL_INT(1, observe_seq_of_last_reply());

    protocore_net_host_udp_reset();
    protocore_coap_notify("/ro");
    TEST_ASSERT_TRUE(sent_len() > 0);
    protocore_net_host_udp_reset();
    protocore_coap_notify("/temp");
    TEST_ASSERT_TRUE(sent_len() > 0);
}

void test_coap_observe_zero_length_token()
{
    reset_udp();
    protocore_coap_server_begin(5683);
    const uint8_t tok[2] = {0xAA, 0xBB};
    uint8_t req[64];

    size_t rl = build_observe_get(req, "temp", 0, tok, sizeof(tok), 0x0301);
    inject(5683, "10.0.0.9", 40000, req, rl);

    protocore_net_host_udp_reset();
    rl = build_observe_get(req, "temp", 0, NULL, 0, 0x0302);
    inject(5683, "10.0.0.9", 40000, req, rl);
    TEST_ASSERT_EQUAL_INT(1, observe_seq_of_last_reply());

    protocore_coap_notify("/temp");

    protocore_net_host_udp_reset();
    rl = build_observe_get(req, "temp", 0, NULL, 0, 0x0303);
    inject(5683, "10.0.0.9", 40000, req, rl);
    TEST_ASSERT_EQUAL_INT(2, observe_seq_of_last_reply());
}

void test_coap_observe_targeted_removal()
{
    reset_udp();
    protocore_coap_server_begin(5683);
    const uint8_t tok_a[2] = {0xAA, 0xBB};
    const uint8_t tok_b[2] = {0xCC, 0xDD};
    uint8_t req[64], rst[8];

    size_t rl = build_observe_get(req, "temp", 0, tok_a, 2, 0x0401);
    inject(5683, "10.0.0.9", 40000, req, rl);
    rl = build_observe_get(req, "temp", 0, tok_a, 2, 0x0402);
    inject(5683, "10.0.0.20", 40000, req, rl);
    protocore_coap_notify("/temp");

    rl = build_observe_get(req, "temp", 1, tok_b, 2, 0x0403);
    inject(5683, "10.0.0.9", 40000, req, rl);
    protocore_net_host_udp_reset();
    rl = build_observe_get(req, "temp", 0, tok_a, 2, 0x0404);
    inject(5683, "10.0.0.9", 40000, req, rl);
    TEST_ASSERT_EQUAL_INT(2, observe_seq_of_last_reply());

    CoapEnc re;
    enc_init(&re, rst, (uint8_t)COAP_TYPE_RST, 0, NULL, 0, 0x0405);
    inject(5683, "10.0.0.9", 49999, rst, re.len);
    protocore_net_host_udp_reset();
    rl = build_observe_get(req, "temp", 0, tok_a, 2, 0x0406);
    inject(5683, "10.0.0.9", 40000, req, rl);
    TEST_ASSERT_EQUAL_INT(2, observe_seq_of_last_reply());

    enc_init(&re, rst, (uint8_t)COAP_TYPE_RST, 0, NULL, 0, 0x0407);
    inject(5683, "10.0.0.9", 40000, rst, re.len);
    protocore_net_host_udp_reset();
    rl = build_observe_get(req, "temp", 0, tok_a, 2, 0x0408);
    inject(5683, "10.0.0.9", 40000, req, rl);
    TEST_ASSERT_EQUAL_INT(1, observe_seq_of_last_reply());

    protocore_net_host_udp_reset();
    rl = build_observe_get(req, "temp", 0, tok_a, 2, 0x0409);
    inject(5683, "10.0.0.20", 40000, req, rl);
    TEST_ASSERT_EQUAL_INT(2, observe_seq_of_last_reply());
}

void test_coap_notify_clamps_oversized_body()
{
    reset_udp();
    TEST_ASSERT_TRUE(protocore_coap_server_add_resource("/of", COAP_ALLOW_GET, h_overflow));
    protocore_coap_server_begin(5683);
    const uint8_t tok[1] = {0x5A};
    uint8_t req[64];
    size_t rl = build_observe_get(req, "of", 0, tok, 1, 0x0501);
    inject(5683, "10.0.0.9", 40000, req, rl);

    protocore_net_host_udp_reset();
    protocore_coap_notify("/of");
    TEST_ASSERT_TRUE(sent_len() > 0);
    CoapDec d;
    TEST_ASSERT_TRUE(dec(sent_bytes(), sent_len(), &d));
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_COAP_MAX_PAYLOAD, d.payload_len);
}

void test_coap_observe_on_discovery_is_not_registered()
{
    reset_udp();
    protocore_coap_server_begin(5683);
    uint8_t req[64], tok[1] = {0x77};
    CoapEnc e;
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, tok, 1, 0x0601);
    enc_option(&e, 6, NULL, 0);
    enc_option(&e, 11, (const uint8_t *)".well-known", 11);
    enc_option(&e, 11, (const uint8_t *)"core", 4);
    protocore_net_host_udp_reset();
    inject(5683, "10.0.0.9", 40000, req, e.len);
    TEST_ASSERT_TRUE(sent_len() > 0);
    CoapDec d;
    TEST_ASSERT_TRUE(dec(sent_bytes(), sent_len(), &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CONTENT, d.code);
    TEST_ASSERT_EQUAL_INT(-1, d.observe);
}

void test_coap_udp_edge_datagrams()
{
    reset_udp();
    protocore_coap_server_begin(5683);

    uint8_t empty[1] = {0};
    protocore_net_host_udp_reset();
    inject(5683, "10.0.0.9", 40000, empty, 0);
    TEST_ASSERT_EQUAL_UINT(0, sent_len());

    uint8_t req[64];
    CoapEnc e;
    enc_init(&e, req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_POST, NULL, 0, 0x0701);
    enc_option(&e, 6, NULL, 0);
    enc_option(&e, 11, (const uint8_t *)"temp", 4);
    protocore_net_host_udp_reset();
    inject(5683, "10.0.0.9", 40000, req, e.len);
    TEST_ASSERT_TRUE(sent_len() > 0);
    CoapDec d;
    TEST_ASSERT_TRUE(dec(sent_bytes(), sent_len(), &d));
    TEST_ASSERT_EQUAL_UINT((uint8_t)COAP_RSP_CREATED, d.code);
    TEST_ASSERT_EQUAL_INT(-1, d.observe);

    protocore_net_host_udp_reset();
    protocore_coap_notify("/temp");
    TEST_ASSERT_EQUAL_UINT(0, sent_len());
}
#endif

#if PROTOCORE_COAP_DEDUP_ENTRIES > 0

static uint32_t g_now_ms = 0;
static uint32_t mock_clock()
{
    return g_now_ms;
}

void test_dedup_store_lookup_roundtrip()
{
    Clock.src.fn = mock_clock;
    Clock.src.ticks_per_second = 1000;
    Clock.set_ms(Clock.internal);
    g_now_ms = 1000;
    protocore_coap_server_reset();
    const uint8_t r[] = {0x62, 0x45, 0x12, 0x34, 0xAB, 0xCD};
    protocore_coap_dedup_store("192.168.1.10", 5683, 0x1234, r, sizeof(r));
    const uint8_t *c = NULL;
    size_t cl = 0;
    TEST_ASSERT_TRUE(protocore_coap_dedup_lookup("192.168.1.10", 5683, 0x1234, &c, &cl));
    TEST_ASSERT_EQUAL_size_t(sizeof(r), cl);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(r, c, cl);
    Clock.src.fn = NULL;
    Clock.src.ticks_per_second = 1000;
    Clock.set_ms(Clock.internal);
}

void test_dedup_full_address_keying()
{
    Clock.src.fn = mock_clock;
    Clock.src.ticks_per_second = 1000;
    Clock.set_ms(Clock.internal);
    g_now_ms = 1000;
    protocore_coap_server_reset();
    const uint8_t r[] = {1, 2, 3};
    protocore_coap_dedup_store("192.168.1.10", 5683, 0x1234, r, sizeof(r));
    const uint8_t *c = NULL;
    size_t cl = 0;
    TEST_ASSERT_FALSE(protocore_coap_dedup_lookup("192.168.1.11", 5683, 0x1234, &c, &cl));
    TEST_ASSERT_FALSE(protocore_coap_dedup_lookup("192.168.1.10", 5684, 0x1234, &c, &cl));
    TEST_ASSERT_FALSE(protocore_coap_dedup_lookup("192.168.1.10", 5683, 0x1235, &c, &cl));
    TEST_ASSERT_TRUE(protocore_coap_dedup_lookup("192.168.1.10", 5683, 0x1234, &c, &cl));
    Clock.src.fn = NULL;
    Clock.src.ticks_per_second = 1000;
    Clock.set_ms(Clock.internal);
}

void test_dedup_expiry()
{
    Clock.src.fn = mock_clock;
    Clock.src.ticks_per_second = 1000;
    Clock.set_ms(Clock.internal);
    g_now_ms = 1000;
    protocore_coap_server_reset();
    const uint8_t r[] = {1, 2, 3};
    protocore_coap_dedup_store("10.0.0.1", 5683, 0x0001, r, sizeof(r));
    const uint8_t *c = NULL;
    size_t cl = 0;
    g_now_ms = 1000 + PROTOCORE_COAP_DEDUP_LIFETIME_MS - 1;
    TEST_ASSERT_TRUE(protocore_coap_dedup_lookup("10.0.0.1", 5683, 0x0001, &c, &cl));
    g_now_ms = 1000 + PROTOCORE_COAP_DEDUP_LIFETIME_MS;
    TEST_ASSERT_FALSE(protocore_coap_dedup_lookup("10.0.0.1", 5683, 0x0001, &c, &cl));
    Clock.src.fn = NULL;
    Clock.src.ticks_per_second = 1000;
    Clock.set_ms(Clock.internal);
}

void test_dedup_too_large_not_cached()
{
    Clock.src.fn = mock_clock;
    Clock.src.ticks_per_second = 1000;
    Clock.set_ms(Clock.internal);
    g_now_ms = 1000;
    protocore_coap_server_reset();
    static uint8_t big[PROTOCORE_COAP_DEDUP_RESP_MAX + 1];
    memset(big, 0xAA, sizeof(big));
    protocore_coap_dedup_store("10.0.0.2", 5683, 0x0002, big, sizeof(big));
    const uint8_t *c = NULL;
    size_t cl = 0;
    TEST_ASSERT_FALSE(protocore_coap_dedup_lookup("10.0.0.2", 5683, 0x0002, &c, &cl));
    Clock.src.fn = NULL;
    Clock.src.ticks_per_second = 1000;
    Clock.set_ms(Clock.internal);
}

void test_dedup_eviction_and_update()
{
    Clock.src.fn = mock_clock;
    Clock.src.ticks_per_second = 1000;
    Clock.set_ms(Clock.internal);
    protocore_coap_server_reset();
    const uint8_t r[] = {9};
    for (int i = 0; i < PROTOCORE_COAP_DEDUP_ENTRIES; i++)
    {
        g_now_ms = 1000 + (uint32_t)i;
        char ip[16];
        snprintf(ip, sizeof(ip), "10.0.1.%d", i);
        protocore_coap_dedup_store(ip, 5683, (uint16_t)(0x100 + i), r, sizeof(r));
    }
    g_now_ms = 2000;
    protocore_coap_dedup_store("10.0.1.99", 5683, 0x999, r, sizeof(r));
    const uint8_t *c = NULL;
    size_t cl = 0;
    TEST_ASSERT_FALSE(protocore_coap_dedup_lookup("10.0.1.0", 5683, 0x100, &c, &cl));
    TEST_ASSERT_TRUE(protocore_coap_dedup_lookup("10.0.1.99", 5683, 0x999, &c, &cl));

    const uint8_t r2[] = {7, 7, 7, 7};
    protocore_coap_dedup_store("10.0.1.99", 5683, 0x999, r2, sizeof(r2));
    TEST_ASSERT_TRUE(protocore_coap_dedup_lookup("10.0.1.99", 5683, 0x999, &c, &cl));
    TEST_ASSERT_EQUAL_size_t(sizeof(r2), cl);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(r2, c, cl);
    Clock.src.fn = NULL;
    Clock.src.ticks_per_second = 1000;
    Clock.set_ms(Clock.internal);
}

void test_dedup_handler_replays_without_rerunning()
{
    Clock.src.fn = mock_clock;
    Clock.src.ticks_per_second = 1000;
    Clock.set_ms(Clock.internal);
    g_now_ms = 5000;
    reset_udp();
    protocore_coap_server_begin(5683);

    const char *paths[] = {"temp"};
    uint8_t req[64];
    size_t rl = build(req, (uint8_t)COAP_TYPE_CON, (uint8_t)COAP_GET, NULL, 0, 0x4242, paths, 1, NULL, 0, -1, NULL, 0);

    g_called = PROTO_FALSE;
    protocore_net_host_udp_reset();
    inject(5683, "10.0.0.9", 5555, req, rl);
    TEST_ASSERT_TRUE(g_called);
    size_t n1 = sent_len();
    TEST_ASSERT_TRUE(n1 > 0);
    uint8_t saved[64];
    memcpy(saved, sent_bytes(), n1);

    g_called = PROTO_FALSE;
    protocore_net_host_udp_reset();
    inject(5683, "10.0.0.9", 5555, req, rl);
    TEST_ASSERT_FALSE(g_called);
    TEST_ASSERT_EQUAL_size_t(n1, sent_len());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(saved, sent_bytes(), n1);

    g_called = PROTO_FALSE;
    protocore_net_host_udp_reset();
    inject(5683, "10.0.0.10", 5555, req, rl);
    TEST_ASSERT_TRUE(g_called);

    Clock.src.fn = NULL;
    Clock.src.ticks_per_second = 1000;
    Clock.set_ms(Clock.internal);
}
#endif

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_response_option_capacity_stop);
    RUN_TEST(test_coap_udp_handler_basic);
#if PROTOCORE_ENABLE_COAP_OBSERVE
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
#if PROTOCORE_COAP_DEDUP_ENTRIES > 0
    RUN_TEST(test_dedup_store_lookup_roundtrip);
    RUN_TEST(test_dedup_full_address_keying);
    RUN_TEST(test_dedup_expiry);
    RUN_TEST(test_dedup_too_large_not_cached);
    RUN_TEST(test_dedup_eviction_and_update);
    RUN_TEST(test_dedup_handler_replays_without_rerunning);
#endif
    return UNITY_END();
}
