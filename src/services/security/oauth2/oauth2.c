// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
#include "mmgr/membuild/membuild.h" // Sb: the bounded builder the body is written with
#include "mmgr/protomem/protomem.h" // mem.cpy: the reply into the module's own buffer
#include "mmgr/secure/secure.h"     // the persistent end this module's key material is taken from
#include "shared/hex/hex.h"         // Hex.digit: the two digits of a percent-encoded octet

static uint8_t hex_work[16]; // the borrow an entry takes; Hex never reads it

#if PROTOCORE_ENABLE_OAUTH2

#include "network_drivers/presentation/codec/json/json.h"

static uint8_t json_work[16]; // the borrow an entry takes; Json never reads it

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

#if PROTOCORE_ENABLE_HTTP_CLIENT
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define OAUTH2_OFF_CTX 0u
static_assert(OAUTH2_OFF_CTX + sizeof(struct Oauth2Storage) <= PROTOCORE_OAUTH2_BORROW,
              "PROTOCORE_OAUTH2_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define OAUTH2_CTX(w) ((struct Oauth2Storage *)(void *)((w) + OAUTH2_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_OAUTH2_BORROW persistent bytes, or null while the pool was short
} Oauth2OwnCtx;
static Oauth2OwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_oauth2_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_secure_persist_span(PROTOCORE_OAUTH2_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

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
        Hex.digit(hex_work);
        Sb.ch(b, Hex.ch);
        Hex.args.nibble = (uint8_t)(c & 0x0Fu);
        Hex.digit(hex_work);
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
static void build_code_request(uint8_t *restrict work)
{
    (void)work;
    Oauth2.i32 = 0;
    if (!Oauth2.code_grant.code || !Oauth2.code_grant.redirect_uri || !Oauth2.client.client_id || !Oauth2.request.out ||
        Oauth2.request.cap == 0)
    {
        return;
    }
    protocore_sb b = {Oauth2.request.out, Oauth2.request.cap, 0, PROTO_TRUE};
    Sb.put(&b, "grant_type=authorization_code");
    put_param(&b, "code", Oauth2.code_grant.code);
    put_param(&b, "redirect_uri", Oauth2.code_grant.redirect_uri);
    put_param(&b, "client_id", Oauth2.client.client_id);
    if (Oauth2.client.client_secret)
    {
        put_param(&b, "client_secret", Oauth2.client.client_secret);
    }
    if (Oauth2.code_grant.code_verifier)
    {
        put_param(&b, "code_verifier", Oauth2.code_grant.code_verifier);
    }
    Oauth2.i32 = (int32_t)Sb.finish(&b);
}

// RFC 6749 sec 6: grant_type=refresh_token, refresh_token, client_id, and the sec 2.3.1 client
// password where the client authenticates. i32 takes the encoded length, or 0 when it did not fit.
static void build_refresh_request(uint8_t *restrict work)
{
    (void)work;
    Oauth2.i32 = 0;
    if (!Oauth2.refresh_grant.refresh_token || !Oauth2.client.client_id || !Oauth2.request.out ||
        Oauth2.request.cap == 0)
    {
        return;
    }
    protocore_sb b = {Oauth2.request.out, Oauth2.request.cap, 0, PROTO_TRUE};
    Sb.put(&b, "grant_type=refresh_token");
    put_param(&b, "refresh_token", Oauth2.refresh_grant.refresh_token);
    put_param(&b, "client_id", Oauth2.client.client_id);
    if (Oauth2.client.client_secret)
    {
        put_param(&b, "client_secret", Oauth2.client.client_secret);
    }
    Oauth2.i32 = (int32_t)Sb.finish(&b);
}

// RFC 6749 sec 5.1: read access_token, token_type, expires_in and refresh_token out of the reply,
// plus the OpenID Connect id_token where the provider sends one. ok stays false when access_token is
// absent, which is the shape of the sec 5.2 error object.
static void parse_token_response(uint8_t *restrict work)
{
    (void)work;
    Oauth2.ok = PROTO_FALSE;
    const char *json = Oauth2.response.json;
    Oauth2Tokens *t = Oauth2.response.tokens;
    if (!json || !t)
    {
        return;
    }
    t->access_token[0] = '\0';
    t->id_token[0] = '\0';
    t->refresh_token[0] = '\0';
    t->token_type[0] = '\0';
    t->expires_in = 0;

    Json.get_str_args.json = json;
    Json.get_str_args.key = "access_token";
    Json.get_str_args.out = t->access_token;
    Json.get_str_args.out_cap = sizeof(t->access_token);
    Json.get_str(json_work);
    if (!Json.ok)
    {
        return;
    }
    Json.get_str_args.json = json;
    Json.get_str_args.key = "id_token";
    Json.get_str_args.out = t->id_token;
    Json.get_str_args.out_cap = sizeof(t->id_token);
    Json.get_str(json_work);
    Json.get_str_args.json = json;
    Json.get_str_args.key = "refresh_token";
    Json.get_str_args.out = t->refresh_token;
    Json.get_str_args.out_cap = sizeof(t->refresh_token);
    Json.get_str(json_work);
    Json.get_str_args.json = json;
    Json.get_str_args.key = "token_type";
    Json.get_str_args.out = t->token_type;
    Json.get_str_args.out_cap = sizeof(t->token_type);
    Json.get_str(json_work);
    long e = 0;
    Json.get_int_args.json = json;
    Json.get_int_args.key = "expires_in";
    Json.get_int_args.out = &e;
    Json.get_int(json_work);
    if (Json.ok)
    {
        t->expires_in = e;
    }
    Oauth2.ok = PROTO_TRUE;
}

#if PROTOCORE_ENABLE_HTTP_CLIENT

// POST the body already built into the store to request.token_endpoint (RFC 6749 sec 3.2), copy the
// reply in, and parse it. i32 takes the HTTP status on a sec 5.1 response, the provider's 4xx on a
// sec 5.2 error object, and a negative Oauth2Result where the exchange never got that far. Without a
// net stack HttpClient.post reports HTTP_CLIENT_ERR_CONNECT, which lands on ERR_TRANSPORT.
static void post_and_parse(uint8_t *restrict work, int body_len)
{
    if (body_len <= 0)
    {
        Oauth2.i32 = (int32_t)PROTOCORE_OAUTH2_ERR_BUILD;
        return;
    }
    HttpClient.target.url = Oauth2.request.token_endpoint;
    HttpClient.request.content_type = "application/x-www-form-urlencoded";
    HttpClient.request.body = (const uint8_t *)OAUTH2_CTX(work)->body;
    HttpClient.request.body_len = (size_t)body_len;
#if PROTOCORE_HAS_NET_STACK
    HttpClient.post(protocore_http_client_span());
#else
    HttpClient.post(NULL);
#endif
    const int32_t st = HttpClient.status;
    if (st <= 0)
    {
        Oauth2.i32 = (int32_t)PROTOCORE_OAUTH2_ERR_TRANSPORT;
        return;
    }
    const size_t room = sizeof(OAUTH2_CTX(work)->resp) - 1;
    const size_t k = HttpClient.body_len < room ? HttpClient.body_len : room;
    if (HttpClient.body && k)
    {
        mem.cpy(OAUTH2_CTX(work)->resp, HttpClient.body, k);
    }
    OAUTH2_CTX(work)->resp[k] = '\0';
    Oauth2.response.json = OAUTH2_CTX(work)->resp;
    parse_token_response(work);
    Oauth2.i32 = Oauth2.ok ? st : (st >= 400 ? st : (int32_t)PROTOCORE_OAUTH2_ERR_RESPONSE);
}

// RFC 6749 sec 4.1.3 request, sec 4.1.4 response: the body is built into the store's own buffer, so
// the caller sets only the endpoint, the grant members and response.tokens.
static void exchange_code(uint8_t *restrict work)
{
    Oauth2.request.out = OAUTH2_CTX(work)->body;
    Oauth2.request.cap = sizeof(OAUTH2_CTX(work)->body);
    build_code_request(work);
    post_and_parse(work, (int)Oauth2.i32);
}

// RFC 6749 sec 6: the same exchange presenting refresh_grant.refresh_token.
static void refresh(uint8_t *restrict work)
{
    Oauth2.request.out = OAUTH2_CTX(work)->body;
    Oauth2.request.cap = sizeof(OAUTH2_CTX(work)->body);
    build_refresh_request(work);
    post_and_parse(work, (int)Oauth2.i32);
}

#endif // PROTOCORE_ENABLE_HTTP_CLIENT

// Designated, so a member's position in the struct does not decide what it binds to.
Oauth2Ns Oauth2 = {
    .build_code_request = build_code_request,
    .build_refresh_request = build_refresh_request,
    .parse_token_response = parse_token_response,
#if PROTOCORE_ENABLE_HTTP_CLIENT
    .exchange_code = exchange_code,
    .refresh = refresh,
#endif
};

#endif // PROTOCORE_ENABLE_OAUTH2
