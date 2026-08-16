// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "crypto/hash/sha256.h"
#include "network_drivers/transport/tcp/common.h"
#include "protocore.h"
#include <stdio.h>
#include <string.h>

#include "network_drivers/transport/tcp/tcp.h"
#include "rx_feed.h"
#include <unity.h>

static uint8_t tw[4096];

static proto_bool handler_called = PROTO_FALSE;
static uint8_t handler_slot = 0xFF;

static void handle_ok(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    handler_called = PROTO_TRUE;
    handler_slot = slot_id;
    send_text(slot_id, 200, "text/plain", "OK");
}

void setUp()
{
    protocore_server_reset();
    handler_called = PROTO_FALSE;
    handler_slot = 0xFF;

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
}

static void feed_and_handle(uint8_t slot, const char *req_str)
{
    push_str(slot, req_str);
    HttpConn.slot = slot;
    HttpConn.parse(HttpConn.internal);
    handle();
}

void test_unprotected_route_fires_handler()
{
    on_http("/open", HTTP_GET, handle_ok);
    feed_and_handle(0, "GET /open HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(handler_called);
}

void test_protected_route_no_header_returns_401()
{
    on_http_auth("/admin", HTTP_GET, handle_ok, "Admin", "user", "pass", PROTO_FALSE);
    feed_and_handle(0, "GET /admin HTTP/1.1\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_TRUE(strstr(tcp_captured(), "401 Unauthorized") != NULL);
}

void test_protected_route_wrong_password_returns_401()
{
    on_http_auth("/admin", HTTP_GET, handle_ok, "Admin", "user", "pass", PROTO_FALSE);

    feed_and_handle(0, "GET /admin HTTP/1.1\r\n"
                       "Authorization: Basic dXNlcjp3cm9uZw==\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_TRUE(strstr(tcp_captured(), "401") != NULL);
}

void test_protected_route_wrong_username_returns_401()
{
    on_http_auth("/admin", HTTP_GET, handle_ok, "Admin", "user", "pass", PROTO_FALSE);

    feed_and_handle(0, "GET /admin HTTP/1.1\r\n"
                       "Authorization: Basic YWRtaW46cGFzcw==\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_TRUE(strstr(tcp_captured(), "401") != NULL);
}

void test_protected_route_valid_credentials_fires_handler()
{
    on_http_auth("/admin", HTTP_GET, handle_ok, "Admin", "user", "pass", PROTO_FALSE);

    feed_and_handle(0, "GET /admin HTTP/1.1\r\n"
                       "Authorization: Basic dXNlcjpwYXNz\r\n\r\n");
    TEST_ASSERT_TRUE(handler_called);
    TEST_ASSERT_TRUE(strstr(tcp_captured(), "200 OK") != NULL);
}

void test_401_includes_www_authenticate_header()
{
    on_http_auth("/secret", HTTP_GET, handle_ok, "MyRealm", "u", "p", PROTO_FALSE);
    feed_and_handle(0, "GET /secret HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "WWW-Authenticate: Basic realm=\"MyRealm\""));
}

void test_non_basic_scheme_returns_401()
{
    on_http_auth("/admin", HTTP_GET, handle_ok, "Admin", "user", "pass", PROTO_FALSE);
    feed_and_handle(0, "GET /admin HTTP/1.1\r\n"
                       "Authorization: Bearer some_token\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_TRUE(strstr(tcp_captured(), "401") != NULL);
}

void test_credentials_without_colon_returns_401()
{
    on_http_auth("/admin", HTTP_GET, handle_ok, "Admin", "user", "pass", PROTO_FALSE);

    feed_and_handle(0, "GET /admin HTTP/1.1\r\n"
                       "Authorization: Basic bm9jb2xvbg==\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_TRUE(strstr(tcp_captured(), "401") != NULL);
}

void test_protected_and_unprotected_routes_coexist()
{
    on_http("/public", HTTP_GET, handle_ok);
    on_http_auth("/private", HTTP_GET, handle_ok, "Priv", "u", "p", PROTO_FALSE);

    feed_and_handle(0, "GET /public HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(handler_called);
    handler_called = PROTO_FALSE;
    conn_pool[1].state = CONN_ACTIVE;
    conn_pool[1].proto = PROTO_HTTP;
    conn_pool[1].pcb = protocore_net_host_pcb();
    HttpConn.slot = 1;
    HttpConn.reset(HttpConn.internal);
    tcp_capture_reset();

    feed_and_handle(1, "GET /private HTTP/1.1\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_TRUE(strstr(tcp_captured(), "401") != NULL);
}

void test_auth_route_returns_404_for_wrong_path()
{
    on_http_auth("/admin", HTTP_GET, handle_ok, "Admin", "user", "pass", PROTO_FALSE);
    feed_and_handle(0, "GET /other HTTP/1.1\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_TRUE(strstr(tcp_captured(), "404") != NULL);
}

void test_auth_checked_per_method()
{

    on_http_auth("/upload", HTTP_POST, handle_ok, "Upload", "u", "p", PROTO_FALSE);
    feed_and_handle(0, "GET /upload HTTP/1.1\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "405"));
    TEST_ASSERT_NULL(strstr(tcp_captured(), "401"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Allow: POST"));
}

void stress_auth_50_valid_requests()
{
    on_http_auth("/s", HTTP_GET, handle_ok, "R", "u", "p", PROTO_FALSE);

    const char *req = "GET /s HTTP/1.1\r\n"
                      "Authorization: Basic dTpw\r\n\r\n";

    for (int i = 0; i < 50; i++)
    {
        uint8_t slot = (uint8_t)(i % MAX_CONNS);
        conn_pool[slot] = (TcpConn){0};
        conn_pool[slot].id = slot;
        conn_pool[slot].state = CONN_ACTIVE;
        conn_pool[slot].proto = PROTO_HTTP;
        conn_pool[slot].pcb = protocore_net_host_pcb();
        HttpConn.slot = slot;
        HttpConn.reset(HttpConn.internal);

        handler_called = PROTO_FALSE;
        push_str(slot, req);
        HttpConn.slot = slot;
        HttpConn.parse(HttpConn.internal);
        handle();
        TEST_ASSERT_TRUE_MESSAGE(handler_called, "handler not called with valid creds");
    }
}

void stress_auth_50_invalid_requests()
{
    on_http_auth("/s", HTTP_GET, handle_ok, "R", "u", "p", PROTO_FALSE);
    const char *req = "GET /s HTTP/1.1\r\n"
                      "Authorization: Basic d3Jvbmc6Y3JlZHM=\r\n\r\n";

    for (int i = 0; i < 50; i++)
    {
        uint8_t slot = (uint8_t)(i % MAX_CONNS);
        conn_pool[slot] = (TcpConn){0};
        conn_pool[slot].id = slot;
        conn_pool[slot].state = CONN_ACTIVE;
        conn_pool[slot].proto = PROTO_HTTP;
        conn_pool[slot].pcb = protocore_net_host_pcb();
        HttpConn.slot = slot;
        HttpConn.reset(HttpConn.internal);

        handler_called = PROTO_FALSE;
        push_str(slot, req);
        HttpConn.slot = slot;
        HttpConn.parse(HttpConn.internal);
        handle();
        TEST_ASSERT_FALSE_MESSAGE(handler_called, "handler called with bad creds");
    }
}

static void rearm(uint8_t slot)
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

void test_basic_auth_same_length_wrong_credentials()
{
    on_http_auth("/admin", HTTP_GET, handle_ok, "Admin", "user", "pass", PROTO_FALSE);

    feed_and_handle(0, "GET /admin HTTP/1.1\r\n"
                       "Authorization: Basic eHNlcjpwYXNz\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));

    rearm(0);
    feed_and_handle(0, "GET /admin HTTP/1.1\r\n"
                       "Authorization: Basic dXNlcjp4YXNz\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));
}

void test_basic_auth_invalid_base64_rejected()
{
    on_http_auth("/admin", HTTP_GET, handle_ok, "Admin", "user", "pass", PROTO_FALSE);
    feed_and_handle(0, "GET /admin HTTP/1.1\r\n"
                       "Authorization: Basic ****\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));
}

void test_unauth_challenge_cors_and_head()
{
    set_cors("*");
    on_http_auth("/admin", HTTP_GET, handle_ok, "Admin", "user", "pass", PROTO_FALSE);

    feed_and_handle(0, "GET /admin HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Access-Control-Allow-Origin: *\r\n"));

    rearm(0);
    feed_and_handle(0, "HEAD /admin HTTP/1.1\r\n\r\n");
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "401"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Content-Length: 12\r\n"));
    TEST_ASSERT_NULL(strstr(out, "\r\n\r\nUnauthorized"));
}

void test_unauth_challenge_on_dead_connection()
{
    on_http_auth("/admin", HTTP_GET, handle_ok, "Admin", "user", "pass", PROTO_FALSE);
    push_str(0, "GET /admin HTTP/1.1\r\n\r\n");
    HttpConn.slot = 0;
    HttpConn.parse(HttpConn.internal);
    conn_pool[0].pcb = NULL;
    tcp_capture_reset();
    handle();
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[0].parse_state);
}

static const char *kDUser = "admin";
static const char *kDRealm = "secure area";
static const char *kDPass = "s3cret";

static void sha256_hex_str(const char *s, char out[65])
{
    uint8_t d[PROTOCORE_SHA256_DIGEST_LEN];
    Sha256.hash_args.data = (const uint8_t *)s;
    Sha256.hash_args.len = strlen(s);
    Sha256.hash_args.out = d;
    Sha256.hash(tw);
    static const char *hx = "0123456789abcdef";
    for (int i = 0; i < PROTOCORE_SHA256_DIGEST_LEN; i++)
    {
        out[i * 2] = hx[d[i] >> 4];
        out[i * 2 + 1] = hx[d[i] & 0x0f];
    }
    out[64] = '\0';
}

static proto_bool grab_nonce(const char *resp, char *out, size_t n)
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

static void digest_response(const char *uri, const char *nonce, char out[65])
{
    char buf[256];
    char ha1[65];
    char ha2[65];
    snprintf(buf, sizeof(buf), "%s:%s:%s", kDUser, kDRealm, kDPass);
    sha256_hex_str(buf, ha1);
    snprintf(buf, sizeof(buf), "GET:%s", uri);
    sha256_hex_str(buf, ha2);
    snprintf(buf, sizeof(buf), "%s:%s:00000001:abc:auth:%s", ha1, nonce, ha2);
    sha256_hex_str(buf, out);
}

static void digest_challenge(char *nonce, size_t n)
{
    rearm(0);
    feed_and_handle(0, "GET /d HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(grab_nonce(tcp_captured(), nonce, n));
}

void test_digest_field_parser_boundaries()
{
    on_http_auth("/d", HTTP_GET, handle_ok, kDRealm, kDUser, kDPass, PROTO_TRUE);
    char nonce[48];
    digest_challenge(nonce, sizeof(nonce));

    char req[700];

    snprintf(req, sizeof(req),
             "GET /d HTTP/1.1\r\nHost: x\r\n"
             "Authorization: Digest username=\"%s\",realm=\"%s\",nonce=\"%s\",uri=\"/d\","
             "qop=auth,nc=00000001,cnonce=\"abc\",response=\"%s\"\r\n\r\n",
             kDUser, kDRealm, nonce, "00");
    rearm(0);
    feed_and_handle(0, req);
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));

    rearm(0);
    feed_and_handle(0, "GET /d HTTP/1.1\r\nHost: x\r\n"
                       "Authorization: Digest username =\"admin\", nonce=\"x\"\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));

    rearm(0);
    feed_and_handle(0, "GET /d HTTP/1.1\r\nHost: x\r\n"
                       "Authorization: Digest username=\"admin\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));
}

void test_digest_token_values_and_truncation()
{
    on_http_auth("/d", HTTP_GET, handle_ok, kDRealm, kDUser, kDPass, PROTO_TRUE);
    char nonce[48];
    digest_challenge(nonce, sizeof(nonce));

    char req[700];
    snprintf(req, sizeof(req),
             "GET /d HTTP/1.1\r\nHost: x\r\n"
             "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"/d\", "
             "qop=auth nc=00000001 cnonce=abc response=deadbeef\r\n\r\n",
             kDUser, kDRealm, nonce);
    rearm(0);
    feed_and_handle(0, req);
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));

    snprintf(req, sizeof(req),
             "GET /d HTTP/1.1\r\nHost: x\r\n"
             "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"/d\", "
             "qop=\"authauthauthauthauthauthauthauthauth\", nc=00000001, cnonce=\"abc\", "
             "response=\"00\"\r\n\r\n",
             kDUser, kDRealm, nonce);
    rearm(0);
    feed_and_handle(0, req);
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));
}

void test_digest_nonce_shape_and_mac()
{
    on_http_auth("/d", HTTP_GET, handle_ok, kDRealm, kDUser, kDPass, PROTO_TRUE);

    static const char *bad_nonces[] = {
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "zzzzzzzz.00112233445566778899aabbccddeeff",
        "00000000.00000000000000000000000000000000",
    };
    for (size_t i = 0; i < sizeof(bad_nonces) / sizeof(bad_nonces[0]); i++)
    {
        char req[700];
        snprintf(req, sizeof(req),
                 "GET /d HTTP/1.1\r\nHost: x\r\n"
                 "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"/d\", "
                 "qop=auth, nc=00000001, cnonce=\"abc\", response=\"00\"\r\n\r\n",
                 kDUser, kDRealm, bad_nonces[i]);
        rearm(0);
        feed_and_handle(0, req);
        TEST_ASSERT_FALSE_MESSAGE(handler_called, bad_nonces[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(tcp_captured(), "401"), bad_nonces[i]);
    }
}

void test_digest_missing_field_rejected()
{
    on_http_auth("/d", HTTP_GET, handle_ok, kDRealm, kDUser, kDPass, PROTO_TRUE);
    char nonce[48];
    digest_challenge(nonce, sizeof(nonce));

    static const char *omit[] = {"username", "nonce", "uri", "qop", "nc", "cnonce", "response"};
    for (size_t i = 0; i < sizeof(omit) / sizeof(omit[0]); i++)
    {
        char hdr[700];
        int n = snprintf(hdr, sizeof(hdr), "GET /d HTTP/1.1\r\nHost: x\r\nAuthorization: Digest ");
        const char *fields[7][2] = {{"username", kDUser}, {"nonce", nonce},  {"uri", "/d"},     {"qop", "auth"},
                                    {"nc", "00000001"},   {"cnonce", "abc"}, {"response", "00"}};
        proto_bool first = PROTO_TRUE;
        for (size_t f = 0; f < 7; f++)
        {
            if (strcmp(fields[f][0], omit[i]) == 0)
            {
                continue;
            }
            n += snprintf(hdr + n, sizeof(hdr) - (size_t)n, "%s%s=\"%s\"", first ? "" : ", ", fields[f][0],
                          fields[f][1]);
            first = PROTO_FALSE;
        }
        snprintf(hdr + n, sizeof(hdr) - (size_t)n, "\r\n\r\n");

        rearm(0);
        feed_and_handle(0, hdr);
        TEST_ASSERT_FALSE_MESSAGE(handler_called, omit[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(tcp_captured(), "401"), omit[i]);
    }
}

void test_digest_uri_includes_query_string()
{
    on_http_auth("/d", HTTP_GET, handle_ok, kDRealm, kDUser, kDPass, PROTO_TRUE);
    char nonce[48];
    rearm(0);
    feed_and_handle(0, "GET /d?a=1 HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(grab_nonce(tcp_captured(), nonce, sizeof(nonce)));

    char resp[65];
    char req[700];

    digest_response("/d", nonce, resp);
    snprintf(req, sizeof(req),
             "GET /d?a=1 HTTP/1.1\r\nHost: x\r\n"
             "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"/d\", "
             "qop=auth, nc=00000001, cnonce=\"abc\", response=\"%s\"\r\n\r\n",
             kDUser, kDRealm, nonce, resp);
    rearm(0);
    feed_and_handle(0, req);
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));

    digest_response("/d?a=1", nonce, resp);
    snprintf(req, sizeof(req),
             "GET /d?a=1 HTTP/1.1\r\nHost: x\r\n"
             "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"/d?a=1\", "
             "qop=auth, nc=00000001, cnonce=\"abc\", response=\"%s\"\r\n\r\n",
             kDUser, kDRealm, nonce, resp);
    rearm(0);
    feed_and_handle(0, req);
    TEST_ASSERT_TRUE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "200 OK"));
}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_unprotected_route_fires_handler);
    RUN_TEST(test_protected_route_no_header_returns_401);
    RUN_TEST(test_protected_route_wrong_password_returns_401);
    RUN_TEST(test_protected_route_wrong_username_returns_401);
    RUN_TEST(test_protected_route_valid_credentials_fires_handler);
    RUN_TEST(test_401_includes_www_authenticate_header);
    RUN_TEST(test_non_basic_scheme_returns_401);
    RUN_TEST(test_credentials_without_colon_returns_401);
    RUN_TEST(test_protected_and_unprotected_routes_coexist);
    RUN_TEST(test_auth_route_returns_404_for_wrong_path);
    RUN_TEST(test_auth_checked_per_method);

    RUN_TEST(test_basic_auth_same_length_wrong_credentials);
    RUN_TEST(test_basic_auth_invalid_base64_rejected);
    RUN_TEST(test_unauth_challenge_cors_and_head);
    RUN_TEST(test_unauth_challenge_on_dead_connection);

    RUN_TEST(test_digest_field_parser_boundaries);
    RUN_TEST(test_digest_token_values_and_truncation);
    RUN_TEST(test_digest_nonce_shape_and_mac);
    RUN_TEST(test_digest_missing_field_rejected);
    RUN_TEST(test_digest_uri_includes_query_string);

    RUN_TEST(stress_auth_50_valid_requests);
    RUN_TEST(stress_auth_50_invalid_requests);

    return UNITY_END();
}
