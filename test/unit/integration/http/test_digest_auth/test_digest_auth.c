// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "crypto/hash/sha256.h"
#include "network_drivers/transport/tcp/common.h"
#include "protocore.h"
#include "server/clock/clock.h"
#include <stdio.h>
#include <string.h>

#include "network_drivers/transport/tcp/tcp.h"
#include "rx_feed.h"
#include <unity.h>

static uint8_t tw[4096];

static uint32_t g_fake_ms = 0;
static uint32_t fake_clock()
{
    return g_fake_ms;
}

static proto_bool g_called;

static const char *kUser = "admin";
static const char *kRealm = "secure area";
static const char *kPass = "s3cret";

static void h_secure(uint8_t slot, HttpReq *req)
{
    (void)req;
    g_called = PROTO_TRUE;
    send_text(slot, 200, "text/plain", "secret");
}

static void sha256_hex(const char *s, char out[65])
{
    uint8_t d[PROTOCORE_SHA256_DIGEST_LEN];
    protocore_sha256(tw, (const uint8_t *)s, strlen(s), d);
    static const char *hx = "0123456789abcdef";
    for (int i = 0; i < PROTOCORE_SHA256_DIGEST_LEN; i++)
    {
        out[i * 2] = hx[d[i] >> 4];
        out[i * 2 + 1] = hx[d[i] & 0x0f];
    }
    out[64] = '\0';
}

static proto_bool extract_nonce(const char *resp, char *out, size_t n)
{
    const char *p = strstr(resp, "nonce=\"");
    if (!p)
    {
        return PROTO_FALSE;
    }
    p += 7;
    const char *e = strchr(p, '"');
    if (!e)
    {
        return PROTO_FALSE;
    }
    size_t len = (size_t)(e - p);
    if (len > n - 1)
    {
        len = n - 1;
    }
    memcpy(out, p, len);
    out[len] = '\0';
    return PROTO_TRUE;
}

static void compute_response(const char *user, const char *realm, const char *pass, const char *method, const char *uri,
                             const char *nonce, const char *nc, const char *cnonce, char out[65])
{
    char buf[256], ha1[65], ha2[65];
    snprintf(buf, sizeof(buf), "%s:%s:%s", user, realm, pass);
    sha256_hex(buf, ha1);
    snprintf(buf, sizeof(buf), "%s:%s", method, uri);
    sha256_hex(buf, ha2);
    snprintf(buf, sizeof(buf), "%s:%s:%s:%s:%s:%s", ha1, nonce, nc, cnonce, "auth", ha2);
    sha256_hex(buf, out);
}

void setUp()
{
    protocore_server_reset();
    g_called = PROTO_FALSE;
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = (TcpConn){0};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].proto = PROTO_HTTP;
        conn_pool[i].pcb = protocore_net_host_pcb();
        HttpConn.slot = i;
        HttpConn.reset(HttpConn.internal);
    }
    Ws.init(Ws.internal);
    Sse.init(Sse.internal);
    tcp_capture_reset();
}

void tearDown()
{
    tcp_capture_disable();
    Clock.src.fn = NULL;
    Clock.src.ticks_per_second = 0;
    Clock.set_ms(Clock.internal);
}

static void rearm_slot(uint8_t slot)
{
    conn_pool[slot] = (TcpConn){0};
    conn_pool[slot].id = slot;
    conn_pool[slot].state = CONN_ACTIVE;
    conn_pool[slot].proto = PROTO_HTTP;
    conn_pool[slot].pcb = protocore_net_host_pcb();
    HttpConn.slot = slot;
    HttpConn.reset(HttpConn.internal);
    tcp_capture_reset();
}

static void feed_and_handle(uint8_t slot, const char *req_str)
{
    push_str(slot, req_str);
    HttpConn.slot = slot;
    HttpConn.parse(HttpConn.internal);
    handle();
}

void test_challenge_is_digest_sha256()
{
    on_http_auth("/secure", HTTP_GET, h_secure, kRealm, kUser, kPass, PROTO_TRUE);
    feed_and_handle(0, "GET /secure HTTP/1.1\r\nHost: x\r\n\r\n");
    const char *resp = tcp_captured();
    TEST_ASSERT_FALSE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(resp, "401"));
    TEST_ASSERT_NOT_NULL(strstr(resp, "WWW-Authenticate: Digest"));
    TEST_ASSERT_NOT_NULL(strstr(resp, "algorithm=SHA-256"));
    TEST_ASSERT_NOT_NULL(strstr(resp, "qop=\"auth\""));
    TEST_ASSERT_NOT_NULL(strstr(resp, "nonce=\""));
}

void test_valid_digest_authenticates()
{
    on_http_auth("/secure", HTTP_GET, h_secure, kRealm, kUser, kPass, PROTO_TRUE);

    feed_and_handle(0, "GET /secure HTTP/1.1\r\nHost: x\r\n\r\n");
    char nonce[48];
    TEST_ASSERT_TRUE(extract_nonce(tcp_captured(), nonce, sizeof(nonce)));

    char resp[65];
    compute_response(kUser, kRealm, kPass, "GET", "/secure", nonce, "00000001", "abc", resp);

    char authreq[640];
    snprintf(authreq, sizeof(authreq),
             "GET /secure HTTP/1.1\r\nHost: x\r\n"
             "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"/secure\", "
             "qop=auth, nc=00000001, cnonce=\"abc\", response=\"%s\"\r\n\r\n",
             kUser, kRealm, nonce, resp);

    rearm_slot(0);
    feed_and_handle(0, authreq);
    TEST_ASSERT_TRUE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "secret"));
}

void test_wrong_password_rejected()
{
    on_http_auth("/secure", HTTP_GET, h_secure, kRealm, kUser, kPass, PROTO_TRUE);

    feed_and_handle(0, "GET /secure HTTP/1.1\r\nHost: x\r\n\r\n");
    char nonce[48];
    TEST_ASSERT_TRUE(extract_nonce(tcp_captured(), nonce, sizeof(nonce)));

    char resp[65];
    compute_response(kUser, kRealm, "wrongpass", "GET", "/secure", nonce, "00000001", "abc", resp);

    char authreq[640];
    snprintf(authreq, sizeof(authreq),
             "GET /secure HTTP/1.1\r\nHost: x\r\n"
             "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"/secure\", "
             "qop=auth, nc=00000001, cnonce=\"abc\", response=\"%s\"\r\n\r\n",
             kUser, kRealm, nonce, resp);

    rearm_slot(0);
    feed_and_handle(0, authreq);
    TEST_ASSERT_FALSE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));
}

void test_bad_nonce_rejected()
{
    on_http_auth("/secure", HTTP_GET, h_secure, kRealm, kUser, kPass, PROTO_TRUE);

    feed_and_handle(0, "GET /secure HTTP/1.1\r\nHost: x\r\n\r\n");

    char resp[65];
    compute_response(kUser, kRealm, kPass, "GET", "/secure", "deadbeefdeadbeefdeadbeefdeadbeef", "00000001", "abc",
                     resp);

    char authreq[640];
    snprintf(authreq, sizeof(authreq),
             "GET /secure HTTP/1.1\r\nHost: x\r\n"
             "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"deadbeefdeadbeefdeadbeefdeadbeef\", "
             "uri=\"/secure\", qop=auth, nc=00000001, cnonce=\"abc\", response=\"%s\"\r\n\r\n",
             kUser, kRealm, resp);

    rearm_slot(0);
    feed_and_handle(0, authreq);
    TEST_ASSERT_FALSE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));
}

void test_wrong_username_rejected()
{
    on_http_auth("/secure", HTTP_GET, h_secure, kRealm, kUser, kPass, PROTO_TRUE);
    feed_and_handle(0, "GET /secure HTTP/1.1\r\nHost: x\r\n\r\n");
    char nonce[48];
    TEST_ASSERT_TRUE(extract_nonce(tcp_captured(), nonce, sizeof(nonce)));

    char resp[65];
    compute_response("attacker", kRealm, kPass, "GET", "/secure", nonce, "00000001", "abc", resp);
    char authreq[640];
    snprintf(authreq, sizeof(authreq),
             "GET /secure HTTP/1.1\r\nHost: x\r\n"
             "Authorization: Digest username=\"attacker\", realm=\"%s\", nonce=\"%s\", uri=\"/secure\", "
             "qop=auth, nc=00000001, cnonce=\"abc\", response=\"%s\"\r\n\r\n",
             kRealm, nonce, resp);
    rearm_slot(0);
    feed_and_handle(0, authreq);
    TEST_ASSERT_FALSE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));
}

void test_wrong_qop_rejected()
{
    on_http_auth("/secure", HTTP_GET, h_secure, kRealm, kUser, kPass, PROTO_TRUE);
    feed_and_handle(0, "GET /secure HTTP/1.1\r\nHost: x\r\n\r\n");
    char nonce[48];
    TEST_ASSERT_TRUE(extract_nonce(tcp_captured(), nonce, sizeof(nonce)));

    char resp[65];
    compute_response(kUser, kRealm, kPass, "GET", "/secure", nonce, "00000001", "abc", resp);
    char authreq[640];
    snprintf(authreq, sizeof(authreq),
             "GET /secure HTTP/1.1\r\nHost: x\r\n"
             "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"/secure\", "
             "qop=auth-int, nc=00000001, cnonce=\"abc\", response=\"%s\"\r\n\r\n",
             kUser, kRealm, nonce, resp);
    rearm_slot(0);
    feed_and_handle(0, authreq);
    TEST_ASSERT_FALSE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));
}

void test_missing_response_field_rejected()
{
    on_http_auth("/secure", HTTP_GET, h_secure, kRealm, kUser, kPass, PROTO_TRUE);
    feed_and_handle(0, "GET /secure HTTP/1.1\r\nHost: x\r\n\r\n");
    char nonce[48];
    TEST_ASSERT_TRUE(extract_nonce(tcp_captured(), nonce, sizeof(nonce)));

    char authreq[640];
    snprintf(authreq, sizeof(authreq),
             "GET /secure HTTP/1.1\r\nHost: x\r\n"
             "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"/secure\", "
             "qop=auth, nc=00000001, cnonce=\"abc\"\r\n\r\n",
             kUser, kRealm, nonce);
    rearm_slot(0);
    feed_and_handle(0, authreq);
    TEST_ASSERT_FALSE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));
}

void test_basic_scheme_on_digest_route_rejected()
{
    on_http_auth("/secure", HTTP_GET, h_secure, kRealm, kUser, kPass, PROTO_TRUE);

    char authreq[256];
    snprintf(authreq, sizeof(authreq),
             "GET /secure HTTP/1.1\r\nHost: x\r\nAuthorization: Basic YWRtaW46czNjcmV0\r\n\r\n");
    feed_and_handle(0, authreq);
    TEST_ASSERT_FALSE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));
}

void test_uri_mismatch_rejected()
{
    on_http_auth("/secure", HTTP_GET, h_secure, kRealm, kUser, kPass, PROTO_TRUE);
    feed_and_handle(0, "GET /secure HTTP/1.1\r\nHost: x\r\n\r\n");
    char nonce[48];
    TEST_ASSERT_TRUE(extract_nonce(tcp_captured(), nonce, sizeof(nonce)));

    char resp[65];
    compute_response(kUser, kRealm, kPass, "GET", "/other", nonce, "00000001", "abc", resp);
    char authreq[640];
    snprintf(authreq, sizeof(authreq),
             "GET /secure HTTP/1.1\r\nHost: x\r\n"
             "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"/other\", "
             "qop=auth, nc=00000001, cnonce=\"abc\", response=\"%s\"\r\n\r\n",
             kUser, kRealm, nonce, resp);
    rearm_slot(0);
    feed_and_handle(0, authreq);
    TEST_ASSERT_FALSE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));
}

void test_nonce_is_stateless_timestamped()
{
    on_http_auth("/secure", HTTP_GET, h_secure, kRealm, kUser, kPass, PROTO_TRUE);
    feed_and_handle(0, "GET /secure HTTP/1.1\r\nHost: x\r\n\r\n");
    char nonce[48];
    TEST_ASSERT_TRUE(extract_nonce(tcp_captured(), nonce, sizeof(nonce)));
    TEST_ASSERT_EQUAL_UINT(41, (unsigned)strlen(nonce));
    TEST_ASSERT_EQUAL_CHAR('.', nonce[8]);
    for (int i = 0; i < 41; i++)
    {
        if (i == 8)
        {
            continue;
        }
        char ch = nonce[i];
        proto_bool is_hex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
        TEST_ASSERT_TRUE(is_hex);
    }
}

void test_stale_nonce_triggers_transparent_retry()
{
    Clock.src.fn = fake_clock;
    Clock.src.ticks_per_second = 1000;
    Clock.set_ms(Clock.internal);
    g_fake_ms = 0;
    protocore_server_reset();
    on_http_auth("/secure", HTTP_GET, h_secure, kRealm, kUser, kPass, PROTO_TRUE);

    feed_and_handle(0, "GET /secure HTTP/1.1\r\nHost: x\r\n\r\n");
    char nonce[48];
    TEST_ASSERT_TRUE(extract_nonce(tcp_captured(), nonce, sizeof(nonce)));

    g_fake_ms = PROTOCORE_DIGEST_NONCE_LIFETIME_MS + 1;
    char resp[65];
    compute_response(kUser, kRealm, kPass, "GET", "/secure", nonce, "00000001", "abc", resp);
    char authreq[640];
    snprintf(authreq, sizeof(authreq),
             "GET /secure HTTP/1.1\r\nHost: x\r\n"
             "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"/secure\", "
             "qop=auth, nc=00000001, cnonce=\"abc\", response=\"%s\"\r\n\r\n",
             kUser, kRealm, nonce, resp);
    rearm_slot(0);
    conn_pool[0].last_activity_ms = g_fake_ms;
    feed_and_handle(0, authreq);
    TEST_ASSERT_FALSE(g_called);
    const char *chal = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(chal, "401"));
    TEST_ASSERT_NOT_NULL(strstr(chal, "stale=true"));

    char nonce2[48];
    TEST_ASSERT_TRUE(extract_nonce(chal, nonce2, sizeof(nonce2)));
    compute_response(kUser, kRealm, kPass, "GET", "/secure", nonce2, "00000001", "abc", resp);
    snprintf(authreq, sizeof(authreq),
             "GET /secure HTTP/1.1\r\nHost: x\r\n"
             "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"/secure\", "
             "qop=auth, nc=00000001, cnonce=\"abc\", response=\"%s\"\r\n\r\n",
             kUser, kRealm, nonce2, resp);
    rearm_slot(0);
    conn_pool[0].last_activity_ms = g_fake_ms;
    feed_and_handle(0, authreq);
    TEST_ASSERT_TRUE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_challenge_is_digest_sha256);
    RUN_TEST(test_valid_digest_authenticates);
    RUN_TEST(test_wrong_password_rejected);
    RUN_TEST(test_bad_nonce_rejected);
    RUN_TEST(test_wrong_username_rejected);
    RUN_TEST(test_wrong_qop_rejected);
    RUN_TEST(test_missing_response_field_rejected);
    RUN_TEST(test_basic_scheme_on_digest_route_rejected);
    RUN_TEST(test_uri_mismatch_rejected);
    RUN_TEST(test_nonce_is_stateless_timestamped);
    RUN_TEST(test_stale_nonce_triggers_transparent_retry);
    return UNITY_END();
}
