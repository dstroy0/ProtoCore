// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the OAuth 2.0 token-endpoint client (services/security/oauth2/oauth2.h).
//
// RFC 6749 sec 4.1.3 prints the Access Token Request body and sec 5.1 prints the token response, so
// both ends of the exchange come from the RFC verbatim. test_rfc6749_413_request_body is the
// load-bearing case: the parameter names, their order and the RFC 3986 sec 2.1 percent-encoding of
// the redirect_uri are all fixed by it, and its code and credentials are the running example's
// (sec 2.3.1 spells the Basic value czZCaGRSa3F0MzpnWDFmQmF0M2JW, which is s6BhdRkqt3:gX1fBat3bV).
// One deliberate difference from the RFC's printed body: RFC 3986 sec 2.3 puts "." in the unreserved
// set and sec 2.1 says a producer should not encode those, so this builder writes "client.example.com"
// where the RFC's display copy wrote "client%2Eexample%2Ecom". Both decode to the same URI.

#include "services/security/oauth2/oauth2.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static char g_body[512];

static int32_t build_code(const char *code, const char *redirect_uri, const char *client_id, const char *secret,
                          const char *verifier, size_t cap)
{
    memset(g_body, 0, sizeof(g_body));
    Oauth2.code_grant.code = code;
    Oauth2.code_grant.redirect_uri = redirect_uri;
    Oauth2.code_grant.code_verifier = verifier;
    Oauth2.client.client_id = client_id;
    Oauth2.client.client_secret = secret;
    Oauth2.request.out = g_body;
    Oauth2.request.cap = cap;
    Oauth2.build_code_request(Oauth2.internal);
    return Oauth2.i32;
}

// RFC 6749 sec 4.1.3: grant_type=authorization_code with code, redirect_uri and client_id, the
// authorization code and redirection URI taken from the section's own example request.
void test_rfc6749_413_request_body(void)
{
    static const char WANT[] = "grant_type=authorization_code"
                               "&code=SplxlOBeZQQYbYS6WxSbIA"
                               "&redirect_uri=https%3A%2F%2Fclient.example.com%2Fcb"
                               "&client_id=s6BhdRkqt3";
    const int32_t n = build_code("SplxlOBeZQQYbYS6WxSbIA", "https://client.example.com/cb", "s6BhdRkqt3", NULL, NULL,
                                 sizeof(g_body));
    TEST_ASSERT_EQUAL_STRING(WANT, g_body);
    TEST_ASSERT_EQUAL_INT32((int32_t)(sizeof(WANT) - 1), n);
}

// RFC 6749 sec 2.3.1 lets a confidential client present its password in the request body; the same
// section's example credentials are s6BhdRkqt3 / gX1fBat3bV.
void test_client_secret_is_appended_when_set(void)
{
    static const char WANT[] = "grant_type=authorization_code"
                               "&code=SplxlOBeZQQYbYS6WxSbIA"
                               "&redirect_uri=https%3A%2F%2Fclient.example.com%2Fcb"
                               "&client_id=s6BhdRkqt3"
                               "&client_secret=gX1fBat3bV";
    const int32_t n = build_code("SplxlOBeZQQYbYS6WxSbIA", "https://client.example.com/cb", "s6BhdRkqt3", "gX1fBat3bV",
                                 NULL, sizeof(g_body));
    TEST_ASSERT_EQUAL_STRING(WANT, g_body);
    TEST_ASSERT_EQUAL_INT32((int32_t)(sizeof(WANT) - 1), n);
}

// RFC 7636 sec 4.5: a public client sends the PKCE code_verifier to the token endpoint instead. Its
// syntax (sec 4.1) is unreserved characters only, so nothing in it is percent-encoded.
void test_pkce_code_verifier_is_appended_when_set(void)
{
    static const char VERIFIER[] = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
    static const char WANT[] = "grant_type=authorization_code"
                               "&code=SplxlOBeZQQYbYS6WxSbIA"
                               "&redirect_uri=https%3A%2F%2Fclient.example.com%2Fcb"
                               "&client_id=s6BhdRkqt3"
                               "&code_verifier=dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
    (void)build_code("SplxlOBeZQQYbYS6WxSbIA", "https://client.example.com/cb", "s6BhdRkqt3", NULL, VERIFIER,
                     sizeof(g_body));
    TEST_ASSERT_EQUAL_STRING(WANT, g_body);
}

// RFC 3986 sec 2.3: the unreserved set is ALPHA / DIGIT / "-" / "." / "_" / "~", and sec 2.1 says
// every other octet is "%" HEXDIG HEXDIG with uppercase digits preferred by a producer.
void test_rfc3986_percent_encoding(void)
{
    (void)build_code("ABCXYZabcxyz0189-._~", "https://client.example.com/cb", "id", NULL, NULL, sizeof(g_body));
    TEST_ASSERT_NOT_NULL(strstr(g_body, "&code=ABCXYZabcxyz0189-._~")); // the unreserved set passes through

    // Space 0x20, "/" 0x2F, ":" 0x3A, "&" 0x26, "=" 0x3D, "+" 0x2B, "%" 0x25, and DEL 0x7F.
    (void)build_code("a b/c:d&e=f+g%h\x7f", "https://x/cb", "id", NULL, NULL, sizeof(g_body));
    TEST_ASSERT_NOT_NULL(strstr(g_body, "&code=a%20b%2Fc%3Ad%26e%3Df%2Bg%25h%7F"));

    // A non-ASCII octet is encoded as itself, not folded: U+00E9 in UTF-8 is C3 A9.
    (void)build_code("caf\xc3\xa9", "https://x/cb", "id", NULL, NULL, sizeof(g_body));
    TEST_ASSERT_NOT_NULL(strstr(g_body, "&code=caf%C3%A9"));
}

// RFC 6749 sec 6: grant_type=refresh_token with the refresh token the section's example issues.
void test_rfc6749_sec6_refresh_body(void)
{
    static const char WANT[] = "grant_type=refresh_token"
                               "&refresh_token=tGzv3JOkF0XG5Qx2TlKWIA"
                               "&client_id=s6BhdRkqt3"
                               "&client_secret=gX1fBat3bV";
    memset(g_body, 0, sizeof(g_body));
    Oauth2.refresh_grant.refresh_token = "tGzv3JOkF0XG5Qx2TlKWIA";
    Oauth2.client.client_id = "s6BhdRkqt3";
    Oauth2.client.client_secret = "gX1fBat3bV";
    Oauth2.request.out = g_body;
    Oauth2.request.cap = sizeof(g_body);
    Oauth2.build_refresh_request(Oauth2.internal);
    TEST_ASSERT_EQUAL_STRING(WANT, g_body);
    TEST_ASSERT_EQUAL_INT32((int32_t)(sizeof(WANT) - 1), Oauth2.i32);

    // A public client sends no secret.
    Oauth2.client.client_secret = NULL;
    Oauth2.build_refresh_request(Oauth2.internal);
    TEST_ASSERT_EQUAL_STRING("grant_type=refresh_token&refresh_token=tGzv3JOkF0XG5Qx2TlKWIA&client_id=s6BhdRkqt3",
                             g_body);
}

// A required parameter that is absent, a null buffer or a zero capacity produces no body at all.
void test_build_refuses_incomplete_requests(void)
{
    TEST_ASSERT_EQUAL_INT32(0, build_code(NULL, "https://x/cb", "id", NULL, NULL, sizeof(g_body)));
    TEST_ASSERT_EQUAL_INT32(0, build_code("c", NULL, "id", NULL, NULL, sizeof(g_body)));
    TEST_ASSERT_EQUAL_INT32(0, build_code("c", "https://x/cb", NULL, NULL, NULL, sizeof(g_body)));
    TEST_ASSERT_EQUAL_INT32(0, build_code("c", "https://x/cb", "id", NULL, NULL, 0));

    Oauth2.code_grant.code = "c";
    Oauth2.code_grant.redirect_uri = "https://x/cb";
    Oauth2.code_grant.code_verifier = NULL;
    Oauth2.client.client_id = "id";
    Oauth2.client.client_secret = NULL;
    Oauth2.request.out = NULL;
    Oauth2.request.cap = sizeof(g_body);
    Oauth2.build_code_request(Oauth2.internal);
    TEST_ASSERT_EQUAL_INT32(0, Oauth2.i32);

    Oauth2.refresh_grant.refresh_token = NULL;
    Oauth2.request.out = g_body;
    Oauth2.build_refresh_request(Oauth2.internal);
    TEST_ASSERT_EQUAL_INT32(0, Oauth2.i32);
}

// A buffer one octet short of the encoded body plus its terminator writes no body: half a form is a
// different request, and the endpoint would act on it.
void test_build_refuses_a_short_buffer(void)
{
    const int32_t exact = build_code("SplxlOBeZQQYbYS6WxSbIA", "https://client.example.com/cb", "s6BhdRkqt3", NULL,
                                     NULL, sizeof(g_body));
    TEST_ASSERT_TRUE(exact > 0);
    for (size_t cap = 1; cap <= (size_t)exact; cap++)
    {
        TEST_ASSERT_EQUAL_INT32(0, build_code("SplxlOBeZQQYbYS6WxSbIA", "https://client.example.com/cb", "s6BhdRkqt3",
                                              NULL, NULL, cap));
    }
    TEST_ASSERT_EQUAL_INT32(exact, build_code("SplxlOBeZQQYbYS6WxSbIA", "https://client.example.com/cb", "s6BhdRkqt3",
                                              NULL, NULL, (size_t)exact + 1));
}

// RFC 6749 sec 5.1, the section's own example response, with its unrecognized member the client
// MUST ignore.
void test_rfc6749_51_token_response(void)
{
    static const char JSON[] = "{\n"
                               "  \"access_token\":\"2YotnFZFEjr1zCsicMWpAA\",\n"
                               "  \"token_type\":\"example\",\n"
                               "  \"expires_in\":3600,\n"
                               "  \"refresh_token\":\"tGzv3JOkF0XG5Qx2TlKWIA\",\n"
                               "  \"example_parameter\":\"example_value\"\n"
                               "}";
    Oauth2Tokens t;
    Oauth2.response.json = JSON;
    Oauth2.response.tokens = &t;
    Oauth2.parse_token_response(Oauth2.internal);
    TEST_ASSERT_TRUE(Oauth2.ok);
    TEST_ASSERT_EQUAL_STRING("2YotnFZFEjr1zCsicMWpAA", t.access_token);
    TEST_ASSERT_EQUAL_STRING("example", t.token_type);
    TEST_ASSERT_EQUAL_INT32(3600L, t.expires_in);
    TEST_ASSERT_EQUAL_STRING("tGzv3JOkF0XG5Qx2TlKWIA", t.refresh_token);
    TEST_ASSERT_EQUAL_STRING("", t.id_token); // not an RFC 6749 sec 5.1 parameter, absent here
}

// RFC 6750 sec 6.1.1 registers the "Bearer" token type, and OpenID Connect Core adds id_token beside
// the sec 5.1 parameters.
void test_bearer_response_with_id_token(void)
{
    static const char JSON[] = "{\"access_token\":\"mF_9.B5f-4.1JqM\",\"token_type\":\"Bearer\","
                               "\"expires_in\":3600,\"id_token\":\"a.b.c\",\"scope\":\"openid email\"}";
    Oauth2Tokens t;
    Oauth2.response.json = JSON;
    Oauth2.response.tokens = &t;
    Oauth2.parse_token_response(Oauth2.internal);
    TEST_ASSERT_TRUE(Oauth2.ok);
    TEST_ASSERT_EQUAL_STRING("mF_9.B5f-4.1JqM", t.access_token); // RFC 6750 sec 1.2's example token
    TEST_ASSERT_EQUAL_STRING("Bearer", t.token_type);
    TEST_ASSERT_EQUAL_STRING("a.b.c", t.id_token);
    TEST_ASSERT_EQUAL_STRING("", t.refresh_token); // absent reads empty, not stale
}

// RFC 6749 sec 5.2: an error object carries no access_token, so it is not a successful response.
void test_rfc6749_52_error_object_is_not_a_success(void)
{
    static const char *const ERRORS[] = {
        "{\"error\":\"invalid_request\"}",
        "{\"error\":\"invalid_client\",\"error_description\":\"bad credentials\"}",
        "{\"error\":\"invalid_grant\",\"error_uri\":\"https://example.com/e\"}",
        "{\"error\":\"unauthorized_client\"}",
        "{\"error\":\"unsupported_grant_type\"}",
        "{\"error\":\"invalid_scope\"}",
    };
    for (size_t i = 0; i < sizeof(ERRORS) / sizeof(ERRORS[0]); i++)
    {
        Oauth2Tokens t;
        memset(&t, 0xAA, sizeof(t));
        Oauth2.response.json = ERRORS[i];
        Oauth2.response.tokens = &t;
        Oauth2.parse_token_response(Oauth2.internal);
        TEST_ASSERT_FALSE_MESSAGE(Oauth2.ok, ERRORS[i]);
        TEST_ASSERT_EQUAL_STRING("", t.access_token); // cleared, never left holding old octets
        TEST_ASSERT_EQUAL_STRING("", t.refresh_token);
        TEST_ASSERT_EQUAL_INT32(0L, t.expires_in);
    }
}

// Nothing to parse, or nowhere to put it, is reported rather than written through.
void test_parse_null_arguments(void)
{
    Oauth2Tokens t;
    Oauth2.response.json = NULL;
    Oauth2.response.tokens = &t;
    Oauth2.parse_token_response(Oauth2.internal);
    TEST_ASSERT_FALSE(Oauth2.ok);

    Oauth2.response.json = "{\"access_token\":\"x\"}";
    Oauth2.response.tokens = NULL;
    Oauth2.parse_token_response(Oauth2.internal);
    TEST_ASSERT_FALSE(Oauth2.ok);

    Oauth2.response.json = "not json at all";
    Oauth2.response.tokens = &t;
    Oauth2.parse_token_response(Oauth2.internal);
    TEST_ASSERT_FALSE(Oauth2.ok);
}

// The two builders and the parser compose: a body is built, the endpoint's reply is read, and the
// refresh token that comes back is what the next request presents (RFC 6749 sec 6).
void test_code_exchange_then_refresh(void)
{
    (void)build_code("SplxlOBeZQQYbYS6WxSbIA", "https://client.example.com/cb", "s6BhdRkqt3", "gX1fBat3bV", NULL,
                     sizeof(g_body));
    TEST_ASSERT_NOT_NULL(strstr(g_body, "grant_type=authorization_code"));

    static const char JSON[] = "{\"access_token\":\"2YotnFZFEjr1zCsicMWpAA\",\"token_type\":\"Bearer\","
                               "\"expires_in\":3600,\"refresh_token\":\"tGzv3JOkF0XG5Qx2TlKWIA\"}";
    Oauth2Tokens t;
    Oauth2.response.json = JSON;
    Oauth2.response.tokens = &t;
    Oauth2.parse_token_response(Oauth2.internal);
    TEST_ASSERT_TRUE(Oauth2.ok);

    Oauth2.refresh_grant.refresh_token = t.refresh_token;
    Oauth2.request.out = g_body;
    Oauth2.request.cap = sizeof(g_body);
    Oauth2.build_refresh_request(Oauth2.internal);
    TEST_ASSERT_EQUAL_STRING("grant_type=refresh_token&refresh_token=tGzv3JOkF0XG5Qx2TlKWIA"
                             "&client_id=s6BhdRkqt3&client_secret=gX1fBat3bV",
                             g_body);
}
