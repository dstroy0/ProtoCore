// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for HTTP Basic Authentication (per-route).
//
// Tests verify that:
//   - Unprotected routes are not affected
//   - Protected routes return 401 when no Authorization header is present
//   - Protected routes return 401 on bad credentials
//   - Protected routes invoke the handler on valid credentials
//   - The WWW-Authenticate header includes the correct realm
//   - Credentials are checked exactly (no prefix match)

#include "crypto/hash/sha256.h" // recompute the Digest response test-side
#include "protocore.h"
#include <stdio.h>

#include <unity.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static proto_bool handler_called = PROTO_FALSE;
static uint8_t handler_slot = 0xFF;

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

static void handle_ok(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    handler_called = PROTO_TRUE;
    handler_slot = slot_id;
    send_text(slot_id, 200, "text/plain", "OK");
}

void setUp()
{
    pc_server_reset();
    handler_called = PROTO_FALSE;
    handler_slot = 0xFF;

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
}

// ---------------------------------------------------------------------------
// Helper: feed a complete HTTP request and drive handle()
// ---------------------------------------------------------------------------
static void feed_and_handle(uint8_t slot, const char *req_str)
{
    push_str(slot, req_str);
    http_parse(slot);
    handle();
}

// ====================================================================
// UNIT TESTS
// ====================================================================

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
    // base64("user:wrong") = "dXNlcjp3cm9uZw=="
    feed_and_handle(0, "GET /admin HTTP/1.1\r\n"
                       "Authorization: Basic dXNlcjp3cm9uZw==\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_TRUE(strstr(tcp_captured(), "401") != NULL);
}

void test_protected_route_wrong_username_returns_401()
{
    on_http_auth("/admin", HTTP_GET, handle_ok, "Admin", "user", "pass", PROTO_FALSE);
    // base64("admin:pass") = "YWRtaW46cGFzcw=="
    feed_and_handle(0, "GET /admin HTTP/1.1\r\n"
                       "Authorization: Basic YWRtaW46cGFzcw==\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_TRUE(strstr(tcp_captured(), "401") != NULL);
}

void test_protected_route_valid_credentials_fires_handler()
{
    on_http_auth("/admin", HTTP_GET, handle_ok, "Admin", "user", "pass", PROTO_FALSE);
    // base64("user:pass") = "dXNlcjpwYXNz"
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
    // base64("nocolon") = "bm9jb2xvbg=="
    feed_and_handle(0, "GET /admin HTTP/1.1\r\n"
                       "Authorization: Basic bm9jb2xvbg==\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_TRUE(strstr(tcp_captured(), "401") != NULL);
}

void test_protected_and_unprotected_routes_coexist()
{
    on_http("/public", HTTP_GET, handle_ok);
    on_http_auth("/private", HTTP_GET, handle_ok, "Priv", "u", "p", PROTO_FALSE);

    // Hit public route -- handler fires
    feed_and_handle(0, "GET /public HTTP/1.1\r\n\r\n");
    TEST_ASSERT_TRUE(handler_called);
    handler_called = PROTO_FALSE;
    conn_pool[1].state = CONN_ACTIVE;
    conn_pool[1].proto = PROTO_HTTP; // dispatch requires an explicit protocol
    conn_pool[1].pcb = pc_net_host_pcb();
    http_reset(1);
    tcp_capture_reset();

    // Hit private without creds -- 401
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
    // Route only handles POST; a GET to that path is 405 Method Not Allowed
    // (RFC 7231 §6.5.5) - auth is never evaluated for the wrong method, so the
    // response must not be 401.
    on_http_auth("/upload", HTTP_POST, handle_ok, "Upload", "u", "p", PROTO_FALSE);
    feed_and_handle(0, "GET /upload HTTP/1.1\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "405"));
    TEST_ASSERT_NULL(strstr(tcp_captured(), "401"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Allow: POST"));
}

// ====================================================================
// STRESS TESTS
// ====================================================================

void stress_auth_50_valid_requests()
{
    on_http_auth("/s", HTTP_GET, handle_ok, "R", "u", "p", PROTO_FALSE);
    // base64("u:p") = "dTpw"
    const char *req = "GET /s HTTP/1.1\r\n"
                      "Authorization: Basic dTpw\r\n\r\n";

    for (int i = 0; i < 50; i++)
    {
        uint8_t slot = (uint8_t)(i % MAX_CONNS);
        conn_pool[slot] = (TcpConn){0};
        conn_pool[slot].id = slot;
        conn_pool[slot].state = CONN_ACTIVE;
        conn_pool[slot].proto = PROTO_HTTP; // dispatch requires an explicit protocol
        conn_pool[slot].pcb = pc_net_host_pcb();
        http_reset(slot);

        handler_called = PROTO_FALSE;
        push_str(slot, req);
        http_parse(slot);
        handle();
        TEST_ASSERT_TRUE_MESSAGE(handler_called, "handler not called with valid creds");
    }
}

void stress_auth_50_invalid_requests()
{
    on_http_auth("/s", HTTP_GET, handle_ok, "R", "u", "p", PROTO_FALSE);
    const char *req = "GET /s HTTP/1.1\r\n"
                      "Authorization: Basic d3Jvbmc6Y3JlZHM=\r\n\r\n"; // "wrong:creds"

    for (int i = 0; i < 50; i++)
    {
        uint8_t slot = (uint8_t)(i % MAX_CONNS);
        conn_pool[slot] = (TcpConn){0};
        conn_pool[slot].id = slot;
        conn_pool[slot].state = CONN_ACTIVE;
        conn_pool[slot].proto = PROTO_HTTP; // dispatch requires an explicit protocol
        conn_pool[slot].pcb = pc_net_host_pcb();
        http_reset(slot);

        handler_called = PROTO_FALSE;
        push_str(slot, req);
        http_parse(slot);
        handle();
        TEST_ASSERT_FALSE_MESSAGE(handler_called, "handler called with bad creds");
    }
}

// ====================================================================
// BASIC-AUTH CREDENTIAL COMPARE + CHALLENGE EDGES
// ====================================================================

static void rearm(uint8_t slot)
{
    conn_pool[slot] = (TcpConn){0};
    conn_pool[slot].id = slot;
    conn_pool[slot].state = CONN_ACTIVE;
    conn_pool[slot].proto = PROTO_HTTP;
    conn_pool[slot].pcb = pc_net_host_pcb();
    http_reset(slot);
    tcp_capture_reset();
}

// The credential compare is length-bounded AND constant-time, so a submitted value
// of the right length but the wrong bytes fails on content - for the username and
// for the password independently.
void test_basic_auth_same_length_wrong_credentials()
{
    on_http_auth("/admin", HTTP_GET, handle_ok, "Admin", "user", "pass", PROTO_FALSE);

    // base64("xser:pass") - username same length, different bytes.
    feed_and_handle(0, "GET /admin HTTP/1.1\r\n"
                       "Authorization: Basic eHNlcjpwYXNz\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));

    // base64("user:xass") - password same length, different bytes.
    rearm(0);
    feed_and_handle(0, "GET /admin HTTP/1.1\r\n"
                       "Authorization: Basic dXNlcjp4YXNz\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));
}

// A credential blob that is not valid base64 fails the decode outright (no partial
// decode is compared), so the request is unauthorized.
void test_basic_auth_invalid_base64_rejected()
{
    on_http_auth("/admin", HTTP_GET, handle_ok, "Admin", "user", "pass", PROTO_FALSE);
    feed_and_handle(0, "GET /admin HTTP/1.1\r\n"
                       "Authorization: Basic ****\r\n\r\n"); // not base64 alphabet
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));
}

// The 401 challenge carries the CORS block when CORS is enabled, and a HEAD request
// gets the challenge headers with no body.
void test_unauth_challenge_cors_and_head()
{
    set_cors("*");
    on_http_auth("/admin", HTTP_GET, handle_ok, "Admin", "user", "pass", PROTO_FALSE);

    feed_and_handle(0, "GET /admin HTTP/1.1\r\n\r\n");
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "Access-Control-Allow-Origin: *\r\n"));

    // HEAD is served by the GET route, so it is challenged too - headers only.
    rearm(0);
    feed_and_handle(0, "HEAD /admin HTTP/1.1\r\n\r\n");
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "401"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Content-Length: 12\r\n")); // length of the would-be body
    TEST_ASSERT_NULL(strstr(out, "\r\n\r\nUnauthorized"));       // but no body follows
}

// If the connection is gone by the time the challenge would be written, send_unauth
// writes nothing and resets the parser instead.
void test_unauth_challenge_on_dead_connection()
{
    on_http_auth("/admin", HTTP_GET, handle_ok, "Admin", "user", "pass", PROTO_FALSE);
    push_str(0, "GET /admin HTTP/1.1\r\n\r\n");
    http_parse(0);
    conn_pool[0].pcb = NULL; // peer vanished between parse and dispatch
    tcp_capture_reset();
    handle();
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
    TEST_ASSERT_EQUAL(PARSE_METHOD, http_pool[0].parse_state);
}

// ====================================================================
// DIGEST FIELD PARSING + NONCE VALIDATION EDGES
// ====================================================================

static const char *kDUser = "admin";
static const char *kDRealm = "secure area";
static const char *kDPass = "s3cret";

static void sha256_hex_str(const char *s, char out[65])
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

// Pull the nonce out of a WWW-Authenticate: Digest challenge.
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

// Challenge slot 0 on a Digest route and return the minted nonce.
static void digest_challenge(char *nonce, size_t n)
{
    rearm(0);
    feed_and_handle(0, "GET /d HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(grab_nonce(tcp_captured(), nonce, n));
}

// The Digest field parser only accepts a key sitting on a field boundary and
// immediately followed by '=': a key embedded in a longer token ("nc" inside
// "cnonce"), a comma-delimited field with no following space, a key separated from
// its '=' by a space, and an unterminated quoted value are each handled without
// mis-parsing. Every one of these headers is rejected or fails the credential check,
// so none of them authenticates.
void test_digest_field_parser_boundaries()
{
    on_http_auth("/d", HTTP_GET, handle_ok, kDRealm, kDUser, kDPass, PROTO_TRUE);
    char nonce[48];
    digest_challenge(nonce, sizeof(nonce));

    char req[700];
    // (a) Comma-delimited with no space after the comma, and "nc" also occurs inside
    //     "cnonce" - the boundary check must pick the real field.
    snprintf(req, sizeof(req),
             "GET /d HTTP/1.1\r\nHost: x\r\n"
             "Authorization: Digest username=\"%s\",realm=\"%s\",nonce=\"%s\",uri=\"/d\","
             "qop=auth,nc=00000001,cnonce=\"abc\",response=\"%s\"\r\n\r\n",
             kDUser, kDRealm, nonce, "00");
    rearm(0);
    feed_and_handle(0, req);
    TEST_ASSERT_FALSE(handler_called); // parsed fine, but the response digest is wrong
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));

    // (b) A key separated from its '=' by a space is not a field: "username" is never
    //     found in a usable position, so the header is rejected.
    rearm(0);
    feed_and_handle(0, "GET /d HTTP/1.1\r\nHost: x\r\n"
                       "Authorization: Digest username =\"admin\", nonce=\"x\"\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));

    // (c) An unterminated quoted value is rejected rather than read past.
    rearm(0);
    feed_and_handle(0, "GET /d HTTP/1.1\r\nHost: x\r\n"
                       "Authorization: Digest username=\"admin\r\n\r\n");
    TEST_ASSERT_FALSE(handler_called);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));
}

// Unquoted (token) field values terminate at a comma, at a space, or at the end of
// the header, and a value longer than its destination buffer is truncated rather
// than overflowed (a 40-char qop cannot equal "auth", so the request is refused).
void test_digest_token_values_and_truncation()
{
    on_http_auth("/d", HTTP_GET, handle_ok, kDRealm, kDUser, kDPass, PROTO_TRUE);
    char nonce[48];
    digest_challenge(nonce, sizeof(nonce));

    // Space-separated tokens; the final value runs to the end of the header.
    char req[700];
    snprintf(req, sizeof(req),
             "GET /d HTTP/1.1\r\nHost: x\r\n"
             "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"/d\", "
             "qop=auth nc=00000001 cnonce=abc response=deadbeef\r\n\r\n",
             kDUser, kDRealm, nonce);
    rearm(0);
    feed_and_handle(0, req);
    TEST_ASSERT_FALSE(handler_called); // fields parsed, digest does not match
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "401"));

    // A qop longer than its 16-byte buffer truncates; the truncation cannot equal "auth".
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

// A nonce is only accepted in the exact "<8 hex>.<32 hex>" shape this server mints
// and only when its MAC recomputes: the right length with the separator in the wrong
// place, a non-hex issue field, and a well-formed nonce this server never issued are
// all refused.
void test_digest_nonce_shape_and_mac()
{
    on_http_auth("/d", HTTP_GET, handle_ok, kDRealm, kDUser, kDPass, PROTO_TRUE);

    static const char *bad_nonces[] = {
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", // 41 chars, no '.' at index 8
        "zzzzzzzz.00112233445566778899aabbccddeeff", // '.' in place, issue field not hex
        "00000000.00000000000000000000000000000000", // right shape, MAC never minted here
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

// Every field the RFC 7616 qop=auth computation needs is mandatory: dropping any one
// of them aborts the check before any hashing.
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
                continue; // this is the field under test
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

// RFC 7616 3.4: the "uri" parameter is compared against the full request target, so a
// request carrying a query string must present path+query and authenticates when it
// does (and is refused when it presents the path alone).
void test_digest_uri_includes_query_string()
{
    on_http_auth("/d", HTTP_GET, handle_ok, kDRealm, kDUser, kDPass, PROTO_TRUE);
    char nonce[48];
    rearm(0);
    feed_and_handle(0, "GET /d?a=1 HTTP/1.1\r\nHost: x\r\n\r\n");
    TEST_ASSERT_TRUE(grab_nonce(tcp_captured(), nonce, sizeof(nonce)));

    char resp[65];
    char req[700];
    // Path only: does not match the "/d?a=1" target -> refused.
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

    // Full target (path + query): authenticates.
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

    // Basic-auth credential compare + challenge edges
    RUN_TEST(test_basic_auth_same_length_wrong_credentials);
    RUN_TEST(test_basic_auth_invalid_base64_rejected);
    RUN_TEST(test_unauth_challenge_cors_and_head);
    RUN_TEST(test_unauth_challenge_on_dead_connection);

    // Digest field parsing + nonce validation edges
    RUN_TEST(test_digest_field_parser_boundaries);
    RUN_TEST(test_digest_token_values_and_truncation);
    RUN_TEST(test_digest_nonce_shape_and_mac);
    RUN_TEST(test_digest_missing_field_rejected);
    RUN_TEST(test_digest_uri_includes_query_string);

    RUN_TEST(stress_auth_50_valid_requests);
    RUN_TEST(stress_auth_50_invalid_requests);

    return UNITY_END();
}
