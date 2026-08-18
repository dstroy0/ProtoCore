// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the oauth2 transport calls (services/security/oauth2/oauth2.h), the two that
// PROTOCORE_ENABLE_HTTP_CLIENT compiles. test_oauth2 covers the three calls that write the caller's
// buffers; these two write the module's own, so what they own is the subject here: the borrow the
// request body and the token-endpoint reply are carved out of, and the Oauth2Result each failure
// reports.
//
// The env builds no net stack, so HttpClient.post reports HTTP_CLIENT_ERR_CONNECT and no request
// leaves. That is RFC 6749 sec 4.1.3's Access Token Request never being made, which sec 5 has no
// response for: PROTOCORE_OAUTH2_ERR_TRANSPORT. The body is built either way and is asserted on
// through the same buffer the exchange posts from. test_oauth2_transport drives the arm that answers.

#include "services/security/oauth2/oauth2.c"

#include <string.h>

#include <unity.h>

void setUp(void)
{
    Oauth2.client.client_id = NULL;
    Oauth2.client.client_secret = NULL;
    Oauth2.code_grant.code = NULL;
    Oauth2.code_grant.redirect_uri = NULL;
    Oauth2.code_grant.code_verifier = NULL;
    Oauth2.refresh_grant.refresh_token = NULL;
    Oauth2.request.token_endpoint = NULL;
    Oauth2.response.json = NULL;
    Oauth2.response.tokens = NULL;
    Oauth2.ok = PROTO_FALSE;
    Oauth2.i32 = 0;
}
void tearDown(void)
{
}

// The RFC 6749 sec 4.1.3 running example's client and code, so a body built here is the one the
// section prints.
static void seat_code_grant(void)
{
    Oauth2.client.client_id = "s6BhdRkqt3";
    Oauth2.client.client_secret = "gX1fBat3bV";
    Oauth2.code_grant.code = "SplxlOBeZQQYbYS6WxSbIA";
    Oauth2.code_grant.redirect_uri = "https://client.example.com/cb";
    Oauth2.code_grant.code_verifier = NULL;
    Oauth2.request.token_endpoint = "http://10.0.0.5:8080/token";
}

// ---------------------------------------------------------------------------
// The borrow
// ---------------------------------------------------------------------------

// The span the entries take is the PROTOCORE_OAUTH2_BORROW bytes carved out of the secure end, and
// the two buffers sit inside it.
void test_the_span_carves_the_two_exchange_buffers(void)
{
    uint8_t *work = protocore_oauth2_span();
    TEST_ASSERT_NOT_NULL(work);
    TEST_ASSERT_EQUAL_PTR(work, (uint8_t *)OAUTH2_CTX(work));
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_OAUTH2_BODY_BUF, sizeof(OAUTH2_CTX(work)->body));
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_OAUTH2_RESP_BUF, sizeof(OAUTH2_CTX(work)->resp));
    TEST_ASSERT_TRUE(sizeof(struct Oauth2Storage) <= PROTOCORE_OAUTH2_BORROW);
}

// Taken once: a second call answers with the same bytes rather than taking the borrow again.
void test_the_span_is_taken_once(void)
{
    TEST_ASSERT_EQUAL_PTR(protocore_oauth2_span(), protocore_oauth2_span());
}

// NULL is what a short pool hands over, and an exchange that would write through it does nothing.
void test_an_exchange_refuses_a_null_borrow(void)
{
    seat_code_grant();
    Oauth2.i32 = 1234;
    Oauth2.exchange_code(NULL);
    TEST_ASSERT_EQUAL_INT32(1234, Oauth2.i32);

    Oauth2.refresh_grant.refresh_token = "tGzv3JOkF0XG5Qx2TlKWIA";
    Oauth2.refresh(NULL);
    TEST_ASSERT_EQUAL_INT32(1234, Oauth2.i32);
}

// ---------------------------------------------------------------------------
// What the exchange posts
// ---------------------------------------------------------------------------

// The caller sets the endpoint and the grant only: the exchange points request.out and request.cap
// at its own body buffer, so the RFC 6749 sec 4.1.3 body is built there.
void test_exchange_code_builds_the_sec413_body_into_its_own_buffer(void)
{
    static const char WANT[] = "grant_type=authorization_code&code=SplxlOBeZQQYbYS6WxSbIA"
                               "&redirect_uri=https%3A%2F%2Fclient.example.com%2Fcb"
                               "&client_id=s6BhdRkqt3&client_secret=gX1fBat3bV";
    uint8_t *work = protocore_oauth2_span();
    seat_code_grant();
    Oauth2.request.out = NULL;
    Oauth2.request.cap = 0;
    Oauth2.exchange_code(work);
    TEST_ASSERT_EQUAL_PTR(OAUTH2_CTX(work)->body, Oauth2.request.out);
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_OAUTH2_BODY_BUF, Oauth2.request.cap);
    TEST_ASSERT_EQUAL_STRING(WANT, OAUTH2_CTX(work)->body);
}

// RFC 6749 sec 6 through the same buffer.
void test_refresh_builds_the_sec6_body_into_its_own_buffer(void)
{
    static const char WANT[] = "grant_type=refresh_token&refresh_token=tGzv3JOkF0XG5Qx2TlKWIA"
                               "&client_id=s6BhdRkqt3&client_secret=gX1fBat3bV";
    uint8_t *work = protocore_oauth2_span();
    Oauth2.client.client_id = "s6BhdRkqt3";
    Oauth2.client.client_secret = "gX1fBat3bV";
    Oauth2.refresh_grant.refresh_token = "tGzv3JOkF0XG5Qx2TlKWIA";
    Oauth2.request.token_endpoint = "http://10.0.0.5:8080/token";
    Oauth2.refresh(work);
    TEST_ASSERT_EQUAL_STRING(WANT, OAUTH2_CTX(work)->body);
}

// ---------------------------------------------------------------------------
// What it reports
// ---------------------------------------------------------------------------

// A grant missing a required parameter builds nothing, and nothing is not a request: the exchange
// reports the build rather than the transport.
void test_an_unbuildable_grant_reports_err_build(void)
{
    uint8_t *work = protocore_oauth2_span();
    Oauth2.client.client_id = "s6BhdRkqt3";
    Oauth2.code_grant.code = NULL;
    Oauth2.code_grant.redirect_uri = "https://client.example.com/cb";
    Oauth2.request.token_endpoint = "http://10.0.0.5:8080/token";
    Oauth2.exchange_code(work);
    TEST_ASSERT_EQUAL_INT32((int32_t)PROTOCORE_OAUTH2_ERR_BUILD, Oauth2.i32);

    Oauth2.refresh_grant.refresh_token = NULL;
    Oauth2.refresh(work);
    TEST_ASSERT_EQUAL_INT32((int32_t)PROTOCORE_OAUTH2_ERR_BUILD, Oauth2.i32);
}

// The body was built and posted, and no endpoint answered. RFC 6749 sec 5 describes what a token
// endpoint returns, so no return at all is neither sec 5.1 nor sec 5.2.
void test_an_exchange_that_reaches_no_endpoint_reports_err_transport(void)
{
    uint8_t *work = protocore_oauth2_span();
    seat_code_grant();
    Oauth2.exchange_code(work);
    TEST_ASSERT_EQUAL_INT32((int32_t)PROTOCORE_OAUTH2_ERR_TRANSPORT, Oauth2.i32);
    TEST_ASSERT_FALSE(Oauth2.ok);

    Oauth2.refresh_grant.refresh_token = "tGzv3JOkF0XG5Qx2TlKWIA";
    Oauth2.refresh(work);
    TEST_ASSERT_EQUAL_INT32((int32_t)PROTOCORE_OAUTH2_ERR_TRANSPORT, Oauth2.i32);
    TEST_ASSERT_FALSE(Oauth2.ok);
}

// The exchange posts what it built: the octets HttpClient was handed are the body buffer's, and the
// media type is the one RFC 6749 Appendix B encodes for.
void test_the_exchange_posts_the_body_it_built(void)
{
    uint8_t *work = protocore_oauth2_span();
    seat_code_grant();
    Oauth2.exchange_code(work);
    TEST_ASSERT_EQUAL_PTR(OAUTH2_CTX(work)->body, (const char *)HttpClient.request.body);
    TEST_ASSERT_EQUAL_size_t(strlen(OAUTH2_CTX(work)->body), HttpClient.request.body_len);
    TEST_ASSERT_EQUAL_STRING("application/x-www-form-urlencoded", HttpClient.request.content_type);
    TEST_ASSERT_EQUAL_STRING("http://10.0.0.5:8080/token", HttpClient.target.url);
}
