// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for HTTP Digest authentication (RFC 7616, SHA-256, qop=auth).
// Each test performs the real handshake: an unauthenticated request yields a
// 401 challenge, the test computes the digest response with the same SHA-256
// the server uses, then re-requests with an Authorization: Digest header.

#include "crypto/hash/sha256.h"
#include "protocore.h"
#include "server/clock/clock.h"
#include <stdio.h>
#include <string.h>
#include <unity.h>

// A test-controllable monotonic clock (ms) so the stale-nonce path can be exercised
// deterministically: tests advance g_fake_ms and the library reads it via pc_millis().
static uint32_t g_fake_ms = 0;
static uint32_t fake_clock()
{
    return g_fake_ms;
}

static proto_bool g_called;

static const char *kUser = "admin";
static const char *kRealm = "secure area";
static const char *kPass = "s3cret";

static void push_str(uint8_t slot, const char *s)
{
    TcpConn *c = &conn_pool[slot];
    for (size_t i = 0; s[i]; i++)
    {
        size_t next = (c->rx_head + 1) % RX_BUF_SIZE;
        c->rx_buffer[c->rx_head] = (uint8_t)s[i];
        c->rx_head = next;
    }
}

static void h_secure(uint8_t slot, HttpReq *req)
{
    (void)req;
    g_called = PROTO_TRUE;
    send_text(slot, 200, "text/plain", "secret");
}

static void sha256_hex(const char *s, char out[65])
{
    uint8_t d[PC_SHA256_DIGEST_LEN];
    pc_sha256((const uint8_t *)s, strlen(s), d);
    static const char *hx = "0123456789abcdef";
    for (int i = 0; i < PC_SHA256_DIGEST_LEN; i++)
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

// Compute response = SHA256(HA1:nonce:nc:cnonce:qop:HA2) for GET <uri>.
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
    pc_server_reset();
    g_called = PROTO_FALSE;
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = (TcpConn){0};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].proto = PROTO_HTTP; // dispatch requires an explicit protocol
        conn_pool[i].pcb = pc_net_host_pcb();
        http_reset(i);
    }
    ws_init();
    pc_sse_init();
    tcp_capture_reset();
}

void tearDown()
{
    tcp_capture_disable();
    pc_set_clock(NULL, 0); // revert any injected clock so tests stay independent
}

static void rearm_slot(uint8_t slot)
{
    conn_pool[slot] = (TcpConn){0};
    conn_pool[slot].id = slot;
    conn_pool[slot].state = CONN_ACTIVE;
    conn_pool[slot].proto = PROTO_HTTP; // dispatch requires an explicit protocol
    conn_pool[slot].pcb = pc_net_host_pcb();
    http_reset(slot);
    tcp_capture_reset();
}

static void feed_and_handle(uint8_t slot, const char *req_str)
{
    push_str(slot, req_str);
    http_parse(slot);
    handle();
}

// ====================================================================
// TESTS
// ====================================================================

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
    // Compute a response against a forged nonce the server never issued.
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

    // A different username than the route is configured for: rejected before hashing.
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

    // Only qop=auth is accepted; qop=auth-int must be rejected (it also changes the
    // HA2 computation, which this server does not implement).
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

    // No response= field at all: a required field is missing -> rejected.
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
    // A Basic Authorization header on a Digest-protected route must not authenticate.
    char authreq[256];
    snprintf(authreq, sizeof(authreq),
             "GET /secure HTTP/1.1\r\nHost: x\r\nAuthorization: Basic YWRtaW46czNjcmV0\r\n\r\n");
    feed_and_handle(0, authreq);
    TEST_ASSERT_FALSE(g_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));
}

// RFC 7616 3.4: the "uri" parameter must match the request target. A response
// computed for /other must not authenticate a request to /secure (replay defense).
void test_uri_mismatch_rejected()
{
    on_http_auth("/secure", HTTP_GET, h_secure, kRealm, kUser, kPass, PROTO_TRUE);
    feed_and_handle(0, "GET /secure HTTP/1.1\r\nHost: x\r\n\r\n");
    char nonce[48];
    TEST_ASSERT_TRUE(extract_nonce(tcp_captured(), nonce, sizeof(nonce)));

    // Compute a fully valid response for uri "/other", then present it on /secure.
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

// The stateless nonce is "<8 hex issue ms>.<32 hex MAC>": exactly 41 lowercase
// hex/'.' characters with the separating dot at index 8.
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

// RFC 7616 3.3: once the nonce ages past its lifetime, a request with otherwise
// valid credentials is answered with 401 + stale=true (a transparent retry), and
// the fresh nonce from that response authenticates at the same (advanced) time.
void test_stale_nonce_triggers_transparent_retry()
{
    pc_set_clock(fake_clock, 1000); // 1000 ticks/sec -> 1 tick == 1 ms
    g_fake_ms = 0;
    pc_server_reset(); // re-seed the keying secret under the injected clock
    on_http_auth("/secure", HTTP_GET, h_secure, kRealm, kUser, kPass, PROTO_TRUE);

    // Mint a nonce at t=0.
    feed_and_handle(0, "GET /secure HTTP/1.1\r\nHost: x\r\n\r\n");
    char nonce[48];
    TEST_ASSERT_TRUE(extract_nonce(tcp_captured(), nonce, sizeof(nonce)));

    // Jump past the nonce lifetime, then present a valid response with the old nonce.
    g_fake_ms = PC_DIGEST_NONCE_LIFETIME_MS + 1;
    char resp[65];
    compute_response(kUser, kRealm, kPass, "GET", "/secure", nonce, "00000001", "abc", resp);
    char authreq[640];
    snprintf(authreq, sizeof(authreq),
             "GET /secure HTTP/1.1\r\nHost: x\r\n"
             "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"/secure\", "
             "qop=auth, nc=00000001, cnonce=\"abc\", response=\"%s\"\r\n\r\n",
             kUser, kRealm, nonce, resp);
    rearm_slot(0);
    conn_pool[0].last_activity_ms = g_fake_ms; // a real RX stamps arrival; do so under the jumped clock
    feed_and_handle(0, authreq);
    TEST_ASSERT_FALSE(g_called); // not served - the nonce is stale
    const char *chal = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(chal, "401"));
    TEST_ASSERT_NOT_NULL(strstr(chal, "stale=true"));

    // The retry: use the fresh nonce from the stale challenge -> authenticates now.
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
