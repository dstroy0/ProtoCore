// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file oauth2.c
 * @brief OAuth 2.0 token-endpoint client (RFC 6749): the two grant bodies and the token response.
 *
 * The bodies are `application/x-www-form-urlencoded` per RFC 6749 Appendix B, percent-encoded per
 * RFC 3986 sec 2.1 over the unreserved set of sec 2.3. RFC 6749 sec 4.1.3 carries grant_type=
 * authorization_code, sec 6 carries grant_type=refresh_token, and the reply is read as the sec 5.1
 * parameters. No heap, no stdlib.
 */

#include "services/security/oauth2/oauth2.h"
#include "mmgr/membuild.h"  // Sb: the bounded builder the body is written with
#include "mmgr/protomem.h"  // mem.cpy: the reply into the module's own buffer
#include "shared/hex/hex.h" // Hex.digit: the two digits of a percent-encoded octet

#if PROTOCORE_ENABLE_OAUTH2

#include "network_drivers/presentation/codec/json/json.h"

#if PROTOCORE_ENABLE_HTTP_CLIENT
#include "services/net/http_client/http_client.h"

/**
 * @brief The exchange buffers this module owns, all BSS.
 *
 * @var Oauth2Storage::body  the RFC 6749 Appendix B body a request is built into
 * @var Oauth2Storage::resp  the token-endpoint reply, NUL-terminated for the JSON reader
 */
struct Oauth2Storage
{
    char body[PROTOCORE_OAUTH2_BODY_BUF];
    char resp[PROTOCORE_OAUTH2_RESP_BUF];
};
#endif // PROTOCORE_ENABLE_HTTP_CLIENT

/**
 * @brief The exchange state and the calls that reach it - what Oauth2Ns points at.
 *
 * @var Oauth2Internal::store  the request-body and reply buffers, only where a transport call exists
 * @var Oauth2Internal::ns     the handle a caller sets a call's members on
 */
struct Oauth2Internal
{
#if PROTOCORE_ENABLE_HTTP_CLIENT
    struct Oauth2Storage *store;
#endif
    Oauth2Ns *ns;
};

#if PROTOCORE_ENABLE_HTTP_CLIENT
static struct Oauth2Storage s_store;
static struct Oauth2Internal s_oauth2 = {.store = &s_store, .ns = &Oauth2};
#else
static struct Oauth2Internal s_oauth2 = {.ns = &Oauth2};
#endif

// The unreserved set of RFC 3986 sec 2.3: ALPHA / DIGIT / "-" / "." / "_" / "~".
static proto_bool unreserved(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
           c == '_' || c == '~';
}

// Append one form value: an unreserved octet passes, every other one becomes "%" HEXDIG HEXDIG in
// the uppercase digits RFC 3986 sec 2.1 asks a producer for.
static void put_form_value(protocore_sb *b, const char *s)
{
    for (; *s; s++)
    {
        const unsigned char c = (unsigned char)*s;
        if (unreserved((char)c))
        {
            Sb.ch(b, (char)c);
            continue;
        }
        Sb.ch(b, '%');
        Hex.args.upper = PROTO_TRUE;
        Hex.args.nibble = (uint8_t)(c >> 4);
        Hex.digit(Hex.internal);
        Sb.ch(b, Hex.ch);
        Hex.args.nibble = (uint8_t)(c & 0x0Fu);
        Hex.digit(Hex.internal);
        Sb.ch(b, Hex.ch);
    }
}

// Append "&<name>=<encoded value>": one more parameter of the form body.
static void put_param(protocore_sb *b, const char *name, const char *value)
{
    Sb.ch(b, '&');
    Sb.put(b, name);
    Sb.ch(b, '=');
    put_form_value(b, value);
}

// RFC 6749 sec 4.1.3: grant_type=authorization_code, code, redirect_uri, client_id, the sec 2.3.1
// client password where the client authenticates, and the RFC 7636 sec 4.5 code_verifier where it
// does not. i32 takes the encoded length, or 0 when the body did not fit.
static void build_code_request(struct Oauth2Internal *restrict ctx)
{
    ctx->ns->i32 = 0;
    if (!ctx->ns->code_grant.code || !ctx->ns->code_grant.redirect_uri || !ctx->ns->client.client_id ||
        !ctx->ns->request.out || ctx->ns->request.cap == 0)
    {
        return;
    }
    protocore_sb b = {ctx->ns->request.out, ctx->ns->request.cap, 0, PROTO_TRUE};
    Sb.put(&b, "grant_type=authorization_code");
    put_param(&b, "code", ctx->ns->code_grant.code);
    put_param(&b, "redirect_uri", ctx->ns->code_grant.redirect_uri);
    put_param(&b, "client_id", ctx->ns->client.client_id);
    if (ctx->ns->client.client_secret)
    {
        put_param(&b, "client_secret", ctx->ns->client.client_secret);
    }
    if (ctx->ns->code_grant.code_verifier)
    {
        put_param(&b, "code_verifier", ctx->ns->code_grant.code_verifier);
    }
    ctx->ns->i32 = (int32_t)Sb.finish(&b);
}

// RFC 6749 sec 6: grant_type=refresh_token, refresh_token, client_id, and the sec 2.3.1 client
// password where the client authenticates. i32 takes the encoded length, or 0 when it did not fit.
static void build_refresh_request(struct Oauth2Internal *restrict ctx)
{
    ctx->ns->i32 = 0;
    if (!ctx->ns->refresh_grant.refresh_token || !ctx->ns->client.client_id || !ctx->ns->request.out ||
        ctx->ns->request.cap == 0)
    {
        return;
    }
    protocore_sb b = {ctx->ns->request.out, ctx->ns->request.cap, 0, PROTO_TRUE};
    Sb.put(&b, "grant_type=refresh_token");
    put_param(&b, "refresh_token", ctx->ns->refresh_grant.refresh_token);
    put_param(&b, "client_id", ctx->ns->client.client_id);
    if (ctx->ns->client.client_secret)
    {
        put_param(&b, "client_secret", ctx->ns->client.client_secret);
    }
    ctx->ns->i32 = (int32_t)Sb.finish(&b);
}

// RFC 6749 sec 5.1: read access_token, token_type, expires_in and refresh_token out of the reply,
// plus the OpenID Connect id_token where the provider sends one. ok stays false when access_token is
// absent, which is the shape of the sec 5.2 error object.
static void parse_token_response(struct Oauth2Internal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    const char *json = ctx->ns->response.json;
    Oauth2Tokens *t = ctx->ns->response.tokens;
    if (!json || !t)
    {
        return;
    }
    t->access_token[0] = '\0';
    t->id_token[0] = '\0';
    t->refresh_token[0] = '\0';
    t->token_type[0] = '\0';
    t->expires_in = 0;

    if (!Json.get_str(json, "access_token", t->access_token, sizeof(t->access_token)))
    {
        return;
    }
    Json.get_str(json, "id_token", t->id_token, sizeof(t->id_token));
    Json.get_str(json, "refresh_token", t->refresh_token, sizeof(t->refresh_token));
    Json.get_str(json, "token_type", t->token_type, sizeof(t->token_type));
    long e = 0;
    if (Json.get_int(json, "expires_in", &e))
    {
        t->expires_in = e;
    }
    ctx->ns->ok = PROTO_TRUE;
}

#if PROTOCORE_ENABLE_HTTP_CLIENT

// POST the body already built into the store to request.token_endpoint (RFC 6749 sec 3.2), copy the
// reply in, and parse it. i32 takes the HTTP status on a sec 5.1 response, the provider's 4xx on a
// sec 5.2 error object, and a negative Oauth2Result where the exchange never got that far.
static void post_and_parse(struct Oauth2Internal *restrict ctx, int body_len)
{
    if (body_len <= 0)
    {
        ctx->ns->i32 = (int32_t)PROTOCORE_OAUTH2_ERR_BUILD;
        return;
    }
    HttpClientResult r;
    int st = http_post(ctx->ns->request.token_endpoint, "application/x-www-form-urlencoded",
                       (const uint8_t *)ctx->store->body, (size_t)body_len, &r);
    if (st <= 0)
    {
        ctx->ns->i32 = (int32_t)PROTOCORE_OAUTH2_ERR_TRANSPORT;
        return;
    }
    size_t k = r.body_len < sizeof(ctx->store->resp) - 1 ? r.body_len : sizeof(ctx->store->resp) - 1;
    if (r.body && k)
    {
        mem.cpy(ctx->store->resp, r.body, k);
    }
    ctx->store->resp[k] = '\0';
    ctx->ns->response.json = ctx->store->resp;
    parse_token_response(ctx);
    ctx->ns->i32 = ctx->ns->ok ? (int32_t)st : (st >= 400 ? (int32_t)st : (int32_t)PROTOCORE_OAUTH2_ERR_RESPONSE);
}

// RFC 6749 sec 4.1.3 request, sec 4.1.4 response: the body is built into the store's own buffer, so
// the caller sets only the endpoint, the grant members and response.tokens.
static void exchange_code(struct Oauth2Internal *restrict ctx)
{
    ctx->ns->request.out = ctx->store->body;
    ctx->ns->request.cap = sizeof(ctx->store->body);
    build_code_request(ctx);
    post_and_parse(ctx, (int)ctx->ns->i32);
}

// RFC 6749 sec 6: the same exchange presenting refresh_grant.refresh_token.
static void refresh(struct Oauth2Internal *restrict ctx)
{
    ctx->ns->request.out = ctx->store->body;
    ctx->ns->request.cap = sizeof(ctx->store->body);
    build_refresh_request(ctx);
    post_and_parse(ctx, (int)ctx->ns->i32);
}

#endif // PROTOCORE_ENABLE_HTTP_CLIENT

// Designated, so a member's position in the struct does not decide what it binds to.
Oauth2Ns Oauth2 = {.build_code_request = build_code_request,
                   .build_refresh_request = build_refresh_request,
                   .parse_token_response = parse_token_response,
#if PROTOCORE_ENABLE_HTTP_CLIENT
                   .exchange_code = exchange_code,
                   .refresh = refresh,
#endif
                   .internal = &s_oauth2};

#endif // PROTOCORE_ENABLE_OAUTH2
