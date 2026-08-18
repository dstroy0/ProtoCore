// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the oauth2 token exchange over a transport that answers.
//
// The env builds the TCP client, the resolver and http_client with a net stack, so
// Oauth2.exchange_code opens a connection, sends an RFC 9112 request message and reads the reply.
// The peer is the host pcb model: the request is captured on the wire and the armed response is
// handed to the connection's recv callback while the exchange waits, then a FIN, which is the
// close-delimited body of RFC 9112 sec 6.3 item 8.
//
// The endpoint is a dotted quad so the resolver's literal path answers inside open() and no DNS
// query is needed. RFC 6749 sec 4.1.3 and sec 5.1 supply the request body and the response.

#include "services/security/oauth2/oauth2.c"

#include "network_drivers/transport/tcp/client/client.h"

#include <stdio.h>
#include <string.h>

#include <unity.h>

// One HTTP response message, status line through body, as the origin server would write it.
static char g_reply[1024];

static void arm_reply(int status, const char *reason, const char *json)
{
    const int n = snprintf(g_reply, sizeof(g_reply),
                           "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: %u\r\n\r\n%s", status,
                           reason, (unsigned)strlen(json), json);
    TEST_ASSERT_TRUE(n > 0 && (size_t)n < sizeof(g_reply));
    protocore_net_host_arm_reply(g_reply, (size_t)n);
}

void setUp(void)
{
    set_millis(0);
    protocore_net_host_reply_reset();
    protocore_net_host_tx_len = 0;
    for (int i = 0; i < PROTOCORE_NET_HOST_PCBS; i++)
    {
        memset(&protocore_net_host_pcbs[i], 0, sizeof(protocore_pcb));
    }
    for (int i = 0; i < PROTOCORE_CLIENT_CONNS; i++)
    {
        TcpClient.cid = i;
        TcpClient.close(protocore_tcp_client_span());
    }
    Oauth2.client.client_id = NULL;
    Oauth2.client.client_secret = NULL;
    Oauth2.code_grant.code = NULL;
    Oauth2.code_grant.redirect_uri = NULL;
    Oauth2.code_grant.code_verifier = NULL;
    Oauth2.refresh_grant.refresh_token = NULL;
    Oauth2.response.json = NULL;
    Oauth2.response.tokens = NULL;
    Oauth2.ok = PROTO_FALSE;
    Oauth2.i32 = 0;
    Oauth2.request.token_endpoint = "http://10.0.0.5:8080/token";
}

void tearDown(void)
{
    protocore_net_host_reply_reset();
    for (int i = 0; i < PROTOCORE_CLIENT_CONNS; i++)
    {
        TcpClient.cid = i;
        TcpClient.close(protocore_tcp_client_span());
    }
}

// RFC 6749 sec 4.1.3's running example.
static void seat_code_grant(void)
{
    Oauth2.client.client_id = "s6BhdRkqt3";
    Oauth2.client.client_secret = "gX1fBat3bV";
    Oauth2.code_grant.code = "SplxlOBeZQQYbYS6WxSbIA";
    Oauth2.code_grant.redirect_uri = "https://client.example.com/cb";
    Oauth2.code_grant.code_verifier = NULL;
}

// The octets the exchange put on the wire, NUL terminated for a substring search.
static const char *sent(void)
{
    return tcp_captured();
}

// ---------------------------------------------------------------------------
// The request that goes out
// ---------------------------------------------------------------------------

// RFC 6749 sec 3.2: the token request is a POST to the token endpoint, and sec 4.1.3's body is the
// content it encloses. The request-line, the Host field and the media type all come off the wire.
void test_the_exchange_posts_the_sec413_body_to_the_endpoint(void)
{
    static const char WANT[] = "grant_type=authorization_code&code=SplxlOBeZQQYbYS6WxSbIA"
                               "&redirect_uri=https%3A%2F%2Fclient.example.com%2Fcb"
                               "&client_id=s6BhdRkqt3&client_secret=gX1fBat3bV";
    Oauth2Tokens t;
    seat_code_grant();
    Oauth2.response.tokens = &t;
    arm_reply(200, "OK", "{\"access_token\":\"x\",\"token_type\":\"Bearer\"}");
    Oauth2.exchange_code(protocore_oauth2_span());

    TEST_ASSERT_NOT_NULL(strstr(sent(), "POST /token HTTP/1.1\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(sent(), "Host: 10.0.0.5:8080\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(sent(), "Content-Type: application/x-www-form-urlencoded\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(sent(), WANT));
}

// RFC 6749 sec 6 presents the refresh token to the same endpoint.
void test_the_refresh_posts_the_sec6_body(void)
{
    Oauth2Tokens t;
    Oauth2.client.client_id = "s6BhdRkqt3";
    Oauth2.client.client_secret = "gX1fBat3bV";
    Oauth2.refresh_grant.refresh_token = "tGzv3JOkF0XG5Qx2TlKWIA";
    Oauth2.response.tokens = &t;
    arm_reply(200, "OK", "{\"access_token\":\"x\",\"token_type\":\"Bearer\"}");
    Oauth2.refresh(protocore_oauth2_span());

    TEST_ASSERT_NOT_NULL(strstr(sent(), "grant_type=refresh_token&refresh_token=tGzv3JOkF0XG5Qx2TlKWIA"
                                        "&client_id=s6BhdRkqt3"));
}

// ---------------------------------------------------------------------------
// The reply that comes back
// ---------------------------------------------------------------------------

// RFC 6749 sec 5.1 prints this response body. Its parameters land in the caller's tokens, and the
// text they were read out of is the module's own reply buffer rather than the transport's.
void test_a_sec51_response_fills_the_tokens(void)
{
    static const char JSON[] = "{\"access_token\":\"2YotnFZFEjr1zCsicMWpAA\",\"token_type\":\"Bearer\","
                               "\"expires_in\":3600,\"refresh_token\":\"tGzv3JOkF0XG5Qx2TlKWIA\"}";
    uint8_t *work = protocore_oauth2_span();
    Oauth2Tokens t;
    seat_code_grant();
    Oauth2.response.tokens = &t;
    arm_reply(200, "OK", JSON);
    Oauth2.exchange_code(work);

    TEST_ASSERT_TRUE(Oauth2.ok);
    TEST_ASSERT_EQUAL_INT32(200, Oauth2.i32);
    TEST_ASSERT_EQUAL_PTR(OAUTH2_CTX(work)->resp, Oauth2.response.json);
    TEST_ASSERT_EQUAL_STRING(JSON, OAUTH2_CTX(work)->resp);
    TEST_ASSERT_EQUAL_STRING("2YotnFZFEjr1zCsicMWpAA", t.access_token);
    TEST_ASSERT_EQUAL_STRING("Bearer", t.token_type);
    TEST_ASSERT_EQUAL_STRING("tGzv3JOkF0XG5Qx2TlKWIA", t.refresh_token);
    TEST_ASSERT_EQUAL_INT32(3600, (int32_t)t.expires_in);
}

// RFC 6749 sec 5.2: the error object arrives with a 4xx the caller needs, so i32 carries the status
// and not an Oauth2Result.
void test_a_sec52_error_object_keeps_its_status(void)
{
    uint8_t *work = protocore_oauth2_span();
    Oauth2Tokens t;
    seat_code_grant();
    Oauth2.response.tokens = &t;
    arm_reply(400, "Bad Request", "{\"error\":\"invalid_grant\"}");
    Oauth2.exchange_code(work);

    TEST_ASSERT_FALSE(Oauth2.ok);
    TEST_ASSERT_EQUAL_INT32(400, Oauth2.i32);
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"invalid_grant\"}", OAUTH2_CTX(work)->resp);
    TEST_ASSERT_EQUAL_STRING("", t.access_token);
}

// A 2xx carrying no access_token is neither the sec 5.1 response nor an error status.
void test_a_2xx_without_an_access_token_reports_err_response(void)
{
    Oauth2Tokens t;
    seat_code_grant();
    Oauth2.response.tokens = &t;
    arm_reply(200, "OK", "{\"scope\":\"read\"}");
    Oauth2.exchange_code(protocore_oauth2_span());

    TEST_ASSERT_FALSE(Oauth2.ok);
    TEST_ASSERT_EQUAL_INT32((int32_t)PROTOCORE_OAUTH2_ERR_RESPONSE, Oauth2.i32);
}

// Nothing answers, so the read runs to its deadline and the exchange has no response at all.
void test_an_endpoint_that_never_answers_reports_err_transport(void)
{
    Oauth2Tokens t;
    seat_code_grant();
    Oauth2.response.tokens = &t;
    Oauth2.exchange_code(protocore_oauth2_span());

    TEST_ASSERT_FALSE(Oauth2.ok);
    TEST_ASSERT_EQUAL_INT32((int32_t)PROTOCORE_OAUTH2_ERR_TRANSPORT, Oauth2.i32);
}

// A grant with a required parameter missing builds no body, and the exchange reports that rather
// than opening a connection.
void test_an_unbuildable_grant_sends_nothing(void)
{
    Oauth2Tokens t;
    Oauth2.client.client_id = "s6BhdRkqt3";
    Oauth2.code_grant.code = NULL;
    Oauth2.code_grant.redirect_uri = "https://client.example.com/cb";
    Oauth2.response.tokens = &t;
    arm_reply(200, "OK", "{\"access_token\":\"x\"}");
    Oauth2.exchange_code(protocore_oauth2_span());

    TEST_ASSERT_EQUAL_INT32((int32_t)PROTOCORE_OAUTH2_ERR_BUILD, Oauth2.i32);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
}
