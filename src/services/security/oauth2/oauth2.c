// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file oauth2.c
 * @brief OAuth2 token-endpoint client - implementation (no heap, no stdlib).
 */

#include "services/security/oauth2/oauth2.h"
#include "shared_primitives/hex.h"

#if PC_ENABLE_OAUTH2

#include "network_drivers/presentation/codec/json/json.h"

#if PC_ENABLE_HTTP_CLIENT
#include "services/net/http_client/http_client.h"
#endif
// Bounded form-body builder.
typedef struct
{
    char *o;
    size_t cap;
    size_t n;
    proto_bool ok;
} Buf;

static void put_raw(Buf *b, const char *s)
{
    for (; *s; s++)
    {
        if (b->n + 1 >= b->cap)
        {
            b->ok = PROTO_FALSE;
            return;
        }
        b->o[b->n++] = *s;
    }
}

static proto_bool unreserved(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
           c == '_' || c == '~';
}

// Percent-encode a value (application/x-www-form-urlencoded; unreserved pass).
static void put_enc(Buf *b, const char *s)
{
    for (; *s; s++)
    {
        unsigned char c = (unsigned char)*s;
        if (unreserved((char)c))
        {
            if (b->n + 1 >= b->cap)
            {
                b->ok = PROTO_FALSE;
                return;
            }
            b->o[b->n++] = (char)c;
        }
        else
        {
            if (b->n + 3 >= b->cap)
            {
                b->ok = PROTO_FALSE;
                return;
            }
            b->o[b->n++] = '%';
            b->o[b->n++] = pc_hex_digit((c >> 4) & 0xF, PROTO_TRUE);
            b->o[b->n++] = pc_hex_digit(c & 0xF, PROTO_TRUE);
        }
    }
}

// Append "&key=<encoded value>".
static void put_param(Buf *b, const char *key, const char *val)
{
    put_raw(b, "&");
    put_raw(b, key);
    put_raw(b, "=");
    put_enc(b, val);
}

static int finish(Buf *b)
{
    if (!b->ok || b->n >= b->cap)
    {
        return 0; // public builders reject cap==0 before constructing Buf, so b->n=0 < b->cap
                  // holds at construction, and every put_raw/put_enc write only commits
                  // after checking b->n+1 (or +3) < b->cap, so b->n < b->cap stays invariant
                  // for as long as b->ok remains true.
    }
    b->o[b->n] = '\0';
    return (int)b->n;
}

int pc_oauth2_build_code_request(const char *code, const char *redirect_uri, const char *client_id,
                                 const char *client_secret, const char *code_verifier, char *out, size_t cap)
{
    if (!code || !redirect_uri || !client_id || !out || cap == 0)
    {
        return 0;
    }
    Buf b = {out, cap, 0, PROTO_TRUE};
    put_raw(&b, "grant_type=authorization_code");
    put_param(&b, "code", code);
    put_param(&b, "redirect_uri", redirect_uri);
    put_param(&b, "client_id", client_id);
    if (client_secret)
    {
        put_param(&b, "client_secret", client_secret);
    }
    if (code_verifier)
    {
        put_param(&b, "code_verifier", code_verifier);
    }
    return finish(&b);
}

int pc_oauth2_build_refresh_request(const char *refresh_token, const char *client_id, const char *client_secret,
                                    char *out, size_t cap)
{
    if (!refresh_token || !client_id || !out || cap == 0)
    {
        return 0;
    }
    Buf b = {out, cap, 0, PROTO_TRUE};
    put_raw(&b, "grant_type=refresh_token");
    put_param(&b, "refresh_token", refresh_token);
    put_param(&b, "client_id", client_id);
    if (client_secret)
    {
        put_param(&b, "client_secret", client_secret);
    }
    return finish(&b);
}

proto_bool pc_oauth2_parse_token_response(const char *json, pc_o_auth2_tokens *out)
{
    if (!json || !out)
    {
        return PROTO_FALSE;
    }
    out->access_token[0] = '\0';
    out->id_token[0] = '\0';
    out->refresh_token[0] = '\0';
    out->token_type[0] = '\0';
    out->expires_in = 0;

    if (!Json.get_str(json, "access_token", out->access_token, sizeof(out->access_token)))
    {
        return PROTO_FALSE; // an error response (e.g. {"error":"invalid_grant"}) has no access_token
    }
    Json.get_str(json, "id_token", out->id_token, sizeof(out->id_token));
    Json.get_str(json, "refresh_token", out->refresh_token, sizeof(out->refresh_token));
    Json.get_str(json, "token_type", out->token_type, sizeof(out->token_type));
    long e = 0;
    if (Json.get_int(json, "expires_in", &e))
    {
        out->expires_in = e;
    }
    return PROTO_TRUE;
}

#if PC_ENABLE_HTTP_CLIENT

// All OAuth2 exchange scratch, owned by one instance (internal linkage): the request-body
// and response buffers (kept off the caller's stack), grouped so it is one named owner,
// unreachable cross-TU.
typedef struct
{
    char body[PC_OAUTH2_BODY_BUF];
    char resp[PC_OAUTH2_RESP_BUF];
} Oauth2Ctx;
static Oauth2Ctx s_oauth;

static int post_and_parse(Oauth2Ctx *c, const char *token_url, int body_len, pc_o_auth2_tokens *out)
{
    if (body_len <= 0)
    {
        return (int)PC_OAUTH2_ERR_BUILD;
    }
    HttpClientResult r;
    int st = http_post(token_url, "application/x-www-form-urlencoded", (const uint8_t *)c->body, (size_t)body_len, &r);
    if (st <= 0)
    {
        return (int)PC_OAUTH2_ERR_TRANSPORT;
    }
    size_t k = r.body_len < sizeof(c->resp) - 1 ? r.body_len : sizeof(c->resp) - 1;
    if (r.body && k)
    {
        memcpy(c->resp, r.body, k);
    }
    c->resp[k] = '\0';
    if (!pc_oauth2_parse_token_response(c->resp, out))
    {
        return st >= 400 ? st : (int)PC_OAUTH2_ERR_RESPONSE; // surface the provider's 4xx, else generic
    }
    return st;
}

int pc_oauth2_exchange_code(const char *token_url, const char *code, const char *redirect_uri, const char *client_id,
                            const char *client_secret, const char *code_verifier, pc_o_auth2_tokens *out)
{
    int n = pc_oauth2_build_code_request(code, redirect_uri, client_id, client_secret, code_verifier, s_oauth.body,
                                         sizeof(s_oauth.body));
    return post_and_parse(&s_oauth, token_url, n, out);
}

int pc_oauth2_refresh(const char *token_url, const char *refresh_token, const char *client_id,
                      const char *client_secret, pc_o_auth2_tokens *out)
{
    int n =
        pc_oauth2_build_refresh_request(refresh_token, client_id, client_secret, s_oauth.body, sizeof(s_oauth.body));
    return post_and_parse(&s_oauth, token_url, n, out);
}

#endif // PC_ENABLE_HTTP_CLIENT

#endif // PC_ENABLE_OAUTH2
