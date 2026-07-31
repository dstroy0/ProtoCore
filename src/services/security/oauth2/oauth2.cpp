// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file oauth2.cpp
 * @brief OAuth2 token-endpoint client - implementation (no heap, no stdlib).
 */

#include "services/security/oauth2/oauth2.h"
#include "shared_primitives/hex.h"

#if PC_ENABLE_OAUTH2

#include "network_drivers/presentation/codec/json/json.h"
#include <string.h>

#if PC_ENABLE_HTTP_CLIENT
#include "services/net/http_client/http_client.h"
#endif
namespace
{
// Bounded form-body builder.
struct Buf
{
    char *o;
    size_t cap;
    size_t n;
    bool ok;
};

void put_raw(Buf &b, const char *s)
{
    for (; *s; s++)
    {
        if (b.n + 1 >= b.cap)
        {
            b.ok = false;
            return;
        }
        b.o[b.n++] = *s;
    }
}

bool unreserved(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
           c == '_' || c == '~';
}

// Percent-encode a value (application/x-www-form-urlencoded; unreserved pass).
void put_enc(Buf &b, const char *s)
{
    for (; *s; s++)
    {
        unsigned char c = (unsigned char)*s;
        if (unreserved((char)c))
        {
            if (b.n + 1 >= b.cap)
            {
                b.ok = false;
                return;
            }
            b.o[b.n++] = (char)c;
        }
        else
        {
            if (b.n + 3 >= b.cap)
            {
                b.ok = false;
                return;
            }
            b.o[b.n++] = '%';
            b.o[b.n++] = pc_hex_digit((c >> 4) & 0xF, true);
            b.o[b.n++] = pc_hex_digit(c & 0xF, true);
        }
    }
}

// Append "&key=<encoded value>".
void put_param(Buf &b, const char *key, const char *val)
{
    put_raw(b, "&");
    put_raw(b, key);
    put_raw(b, "=");
    put_enc(b, val);
}

int finish(Buf &b)
{
    if (!b.ok || b.n >= b.cap) // GCOVR_EXCL_BR_LINE  b.n>=b.cap while b.ok is true is unreachable: both
    {
        return 0; // public builders reject cap==0 before constructing Buf, so b.n=0 < b.cap
                  // holds at construction, and every put_raw/put_enc write only commits
                  // after checking b.n+1 (or +3) < b.cap, so b.n < b.cap stays invariant
                  // for as long as b.ok remains true.
    }
    b.o[b.n] = '\0';
    return (int)b.n;
}
} // namespace

int pc_oauth2_build_code_request(const char *code, const char *redirect_uri, const char *client_id,
                                 const char *client_secret, const char *code_verifier, char *out, size_t cap)
{
    if (!code || !redirect_uri || !client_id || !out || cap == 0)
    {
        return 0;
    }
    Buf b = {out, cap, 0, true};
    put_raw(b, "grant_type=authorization_code");
    put_param(b, "code", code);
    put_param(b, "redirect_uri", redirect_uri);
    put_param(b, "client_id", client_id);
    if (client_secret)
    {
        put_param(b, "client_secret", client_secret);
    }
    if (code_verifier)
    {
        put_param(b, "code_verifier", code_verifier);
    }
    return finish(b);
}

int pc_oauth2_build_refresh_request(const char *refresh_token, const char *client_id, const char *client_secret,
                                    char *out, size_t cap)
{
    if (!refresh_token || !client_id || !out || cap == 0)
    {
        return 0;
    }
    Buf b = {out, cap, 0, true};
    put_raw(b, "grant_type=refresh_token");
    put_param(b, "refresh_token", refresh_token);
    put_param(b, "client_id", client_id);
    if (client_secret)
    {
        put_param(b, "client_secret", client_secret);
    }
    return finish(b);
}

bool pc_oauth2_parse_token_response(const char *json, pc_o_auth2_tokens *out)
{
    if (!json || !out)
    {
        return false;
    }
    out->access_token[0] = '\0';
    out->id_token[0] = '\0';
    out->refresh_token[0] = '\0';
    out->token_type[0] = '\0';
    out->expires_in = 0;

    if (!json_get_str(json, "access_token", out->access_token, sizeof(out->access_token)))
    {
        return false; // an error response (e.g. {"error":"invalid_grant"}) has no access_token
    }
    json_get_str(json, "id_token", out->id_token, sizeof(out->id_token));
    json_get_str(json, "refresh_token", out->refresh_token, sizeof(out->refresh_token));
    json_get_str(json, "token_type", out->token_type, sizeof(out->token_type));
    long e = 0;
    if (json_get_int(json, "expires_in", &e))
    {
        out->expires_in = e;
    }
    return true;
}

#if PC_ENABLE_HTTP_CLIENT

namespace
{
// All OAuth2 exchange scratch, owned by one instance (internal linkage): the request-body
// and response buffers (kept off the caller's stack), grouped so it is one named owner,
// unreachable cross-TU.
struct Oauth2Ctx
{
    char body[PC_OAUTH2_BODY_BUF];
    char resp[PC_OAUTH2_RESP_BUF];
};
Oauth2Ctx s_oauth;

int post_and_parse(Oauth2Ctx &c, const char *token_url, int body_len, pc_o_auth2_tokens *out)
{
    if (body_len <= 0)
    {
        return (int)pc_o_auth2_result::PC_OAUTH2_ERR_BUILD;
    }
    HttpClientResult r;
    int st = http_post(token_url, "application/x-www-form-urlencoded", (const uint8_t *)c.body, (size_t)body_len, &r);
    if (st <= 0)
    {
        return (int)pc_o_auth2_result::PC_OAUTH2_ERR_TRANSPORT;
    }
    size_t k = r.body_len < sizeof(c.resp) - 1 ? r.body_len : sizeof(c.resp) - 1;
    if (r.body && k)
    {
        memcpy(c.resp, r.body, k);
    }
    c.resp[k] = '\0';
    if (!pc_oauth2_parse_token_response(c.resp, out))
    {
        return st >= 400 ? st
                         : (int)pc_o_auth2_result::PC_OAUTH2_ERR_RESPONSE; // surface the provider's 4xx, else generic
    }
    return st;
}
} // namespace

int pc_oauth2_exchange_code(const char *token_url, const char *code, const char *redirect_uri, const char *client_id,
                            const char *client_secret, const char *code_verifier, pc_o_auth2_tokens *out)
{
    int n = pc_oauth2_build_code_request(code, redirect_uri, client_id, client_secret, code_verifier, s_oauth.body,
                                         sizeof(s_oauth.body));
    return post_and_parse(s_oauth, token_url, n, out);
}

int pc_oauth2_refresh(const char *token_url, const char *refresh_token, const char *client_id,
                      const char *client_secret, pc_o_auth2_tokens *out)
{
    int n =
        pc_oauth2_build_refresh_request(refresh_token, client_id, client_secret, s_oauth.body, sizeof(s_oauth.body));
    return post_and_parse(s_oauth, token_url, n, out);
}

#endif // PC_ENABLE_HTTP_CLIENT

#endif // PC_ENABLE_OAUTH2
