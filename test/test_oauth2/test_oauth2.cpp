// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the OAuth2 token-endpoint client core (services/security/oauth2): building
// the form-encoded authorization_code / refresh_token request bodies (with proper
// percent-encoding) and parsing the JSON token response. The HTTP exchange is
// ESP32-only and HW-verified.

#include "services/security/oauth2/oauth2.h"
#include <string.h>
#include <unity.h>

static char out[512];

void setUp()
{
    memset(out, 0, sizeof(out));
}
void tearDown()
{
}

void test_build_code_request_minimal()
{
    int n = pc_oauth2_build_code_request("auth-code-123", "https://app.example/cb", "client-42", nullptr, nullptr, out,
                                         sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("grant_type=authorization_code&code=auth-code-123"
                             "&redirect_uri=https%3A%2F%2Fapp.example%2Fcb&client_id=client-42",
                             out);
}

void test_build_code_request_with_secret_encodes_specials()
{
    int n = pc_oauth2_build_code_request("c d", "u", "id", "s3cr!t", nullptr, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    // space -> %20, '!' -> %21
    TEST_ASSERT_EQUAL_STRING("grant_type=authorization_code&code=c%20d&redirect_uri=u"
                             "&client_id=id&client_secret=s3cr%21t",
                             out);
}

void test_build_code_request_pkce()
{
    pc_oauth2_build_code_request("abc", "u", "id", nullptr, "verifier_xyz-123", out, sizeof(out));
    TEST_ASSERT_NOT_NULL(strstr(out, "&code_verifier=verifier_xyz-123"));
    TEST_ASSERT_NULL(strstr(out, "client_secret")); // PKCE public client: no secret
}

void test_build_refresh_request()
{
    int n = pc_oauth2_build_refresh_request("rt-token", "client-42", "secret", out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("grant_type=refresh_token&refresh_token=rt-token"
                             "&client_id=client-42&client_secret=secret",
                             out);
}

void test_build_overflows_fail_closed()
{
    char tiny[10];
    TEST_ASSERT_EQUAL_INT(0, pc_oauth2_build_code_request("code", "uri", "id", nullptr, nullptr, tiny, sizeof(tiny)));
}

void test_parse_token_response()
{
    const char *json = "{\"access_token\":\"AT123\",\"token_type\":\"Bearer\",\"expires_in\":3600,"
                       "\"refresh_token\":\"RT456\",\"id_token\":\"eyJ.x.y\"}";
    pc_o_auth2_tokens t;
    TEST_ASSERT_TRUE(pc_oauth2_parse_token_response(json, &t));
    TEST_ASSERT_EQUAL_STRING("AT123", t.access_token);
    TEST_ASSERT_EQUAL_STRING("Bearer", t.token_type);
    TEST_ASSERT_EQUAL_INT32(3600, t.expires_in);
    TEST_ASSERT_EQUAL_STRING("RT456", t.refresh_token);
    TEST_ASSERT_EQUAL_STRING("eyJ.x.y", t.id_token);
}

void test_parse_minimal_response()
{
    // Only access_token present: still valid; optional fields stay empty/0.
    pc_o_auth2_tokens t;
    TEST_ASSERT_TRUE(pc_oauth2_parse_token_response("{\"access_token\":\"only\"}", &t));
    TEST_ASSERT_EQUAL_STRING("only", t.access_token);
    TEST_ASSERT_EQUAL_STRING("", t.refresh_token);
    TEST_ASSERT_EQUAL_INT32(0, t.expires_in);
}

void test_parse_error_response_fails()
{
    const char *err = "{\"error\":\"invalid_grant\",\"error_description\":\"bad code\"}";
    pc_o_auth2_tokens t;
    TEST_ASSERT_FALSE(pc_oauth2_parse_token_response(err, &t));
}

void test_oauth2_build_parse_guards()
{
    char out[256];
    TEST_ASSERT_EQUAL_INT(
        0, pc_oauth2_build_code_request(nullptr, "uri", "cid", "sec", "ver", out, sizeof(out)));        // null code
    TEST_ASSERT_EQUAL_INT(0, pc_oauth2_build_refresh_request(nullptr, "cid", "sec", out, sizeof(out))); // null refresh
    pc_o_auth2_tokens tok;
    TEST_ASSERT_FALSE(pc_oauth2_parse_token_response(nullptr, &tok)); // null json
    // A value needing percent-encoding into a tiny buffer overflows (b.ok=false).
    TEST_ASSERT_EQUAL_INT(0, pc_oauth2_build_code_request("a b&c", "uri", "cid", "sec", "ver", out, 8));
}

void test_unreserved_uppercase_and_tilde_pass_through()
{
    // Uppercase letters and '~' are both in the unreserved set (RFC 3986) and must
    // be copied verbatim, not percent-encoded.
    int n = pc_oauth2_build_code_request("AB~cd", "u", "id", nullptr, nullptr, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "code=AB~cd"));
}

void test_build_code_request_individual_null_guards()
{
    char buf[64];
    TEST_ASSERT_EQUAL_INT(0, pc_oauth2_build_code_request("code", nullptr, "id", nullptr, nullptr, buf,
                                                          sizeof(buf))); // null redirect_uri
    TEST_ASSERT_EQUAL_INT(
        0, pc_oauth2_build_code_request("code", "uri", nullptr, nullptr, nullptr, buf, sizeof(buf))); // null client_id
    TEST_ASSERT_EQUAL_INT(
        0, pc_oauth2_build_code_request("code", "uri", "id", nullptr, nullptr, nullptr, sizeof(buf)));     // null out
    TEST_ASSERT_EQUAL_INT(0, pc_oauth2_build_code_request("code", "uri", "id", nullptr, nullptr, buf, 0)); // cap == 0
}

void test_build_refresh_request_individual_null_guards()
{
    char buf[64];
    TEST_ASSERT_EQUAL_INT(0,
                          pc_oauth2_build_refresh_request("rt", nullptr, nullptr, buf, sizeof(buf))); // null client_id
    TEST_ASSERT_EQUAL_INT(0, pc_oauth2_build_refresh_request("rt", "id", nullptr, nullptr, sizeof(buf))); // null out
    TEST_ASSERT_EQUAL_INT(0, pc_oauth2_build_refresh_request("rt", "id", nullptr, buf, 0));               // cap == 0
}

void test_build_refresh_request_without_secret()
{
    int n = pc_oauth2_build_refresh_request("rt-token", "client-42", nullptr, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("grant_type=refresh_token&refresh_token=rt-token&client_id=client-42", out);
    TEST_ASSERT_NULL(strstr(out, "client_secret"));
}

void test_refresh_request_percent_encode_overflow()
{
    // Prefix "grant_type=refresh_token&refresh_token=" is exactly 39 chars. With a
    // 41-byte buffer only 2 bytes remain once the prefix is written, which is enough
    // for put_enc's plain-byte guard but not its 3-byte %XX escape guard, so encoding
    // the first (non-unreserved) char of the token trips put_enc's overflow path.
    char tiny[41];
    TEST_ASSERT_EQUAL_INT(0, pc_oauth2_build_refresh_request("!x", "cid", nullptr, tiny, sizeof(tiny)));
}

void test_parse_token_response_null_out()
{
    TEST_ASSERT_FALSE(pc_oauth2_parse_token_response("{\"access_token\":\"x\"}", nullptr));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_build_code_request_minimal);
    RUN_TEST(test_build_code_request_with_secret_encodes_specials);
    RUN_TEST(test_build_code_request_pkce);
    RUN_TEST(test_build_refresh_request);
    RUN_TEST(test_build_overflows_fail_closed);
    RUN_TEST(test_parse_token_response);
    RUN_TEST(test_parse_minimal_response);
    RUN_TEST(test_parse_error_response_fails);
    RUN_TEST(test_oauth2_build_parse_guards);
    RUN_TEST(test_unreserved_uppercase_and_tilde_pass_through);
    RUN_TEST(test_build_code_request_individual_null_guards);
    RUN_TEST(test_build_refresh_request_individual_null_guards);
    RUN_TEST(test_build_refresh_request_without_secret);
    RUN_TEST(test_refresh_request_percent_encode_overflow);
    RUN_TEST(test_parse_token_response_null_out);
    return UNITY_END();
}
