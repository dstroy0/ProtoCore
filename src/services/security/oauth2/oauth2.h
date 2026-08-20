// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file oauth2.h
 * @brief OAuth 2.0 client at the token endpoint (RFC 6749), PROTOCORE_ENABLE_OAUTH2.
 *
 * RFC 6749 sec 3.2 names the token endpoint; sec 4.1.3 is the Access Token Request that trades an
 * authorization code for tokens, and sec 6 is the same exchange presenting a refresh token. Both
 * requests are `application/x-www-form-urlencoded` bodies encoded per RFC 6749 Appendix B, whose
 * percent-encoding is RFC 3986 sec 2.1 over the unreserved set of RFC 3986 sec 2.3. The reply is the
 * JSON of RFC 6749 sec 5.1 on success, or the error object of sec 5.2, read here with the library's
 * zero-heap JSON reader. `token_type` is "Bearer" for the tokens of RFC 6750 sec 6.1.1, which
 * RFC 6750 sec 2.1 presents in the Authorization header.
 *
 * Two layers, one handle:
 *
 *  - Host-testable core: @ref Oauth2Ns::build_code_request, @ref Oauth2Ns::build_refresh_request and
 *    @ref Oauth2Ns::parse_token_response work on caller-owned buffers.
 *  - Transport (needs PROTOCORE_ENABLE_HTTP_CLIENT): @ref Oauth2Ns::exchange_code and
 *    @ref Oauth2Ns::refresh post the built body to @c request.token_endpoint over the platform's
 *    HTTP(S) client and parse what comes back.
 *
 * A confidential client sets @c client.client_secret; a public client sets
 * @c code_grant.code_verifier, the PKCE verifier of RFC 7636 sec 4.1 that sec 4.5 sends to the token
 * endpoint. The other stays NULL. No heap, no stdlib.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_OAUTH2_H
#define PROTOCORE_OAUTH2_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_OAUTH2

PROTOCORE_BEGIN_DECLS

#ifndef PROTOCORE_OAUTH2_TOKEN_TYPE_LEN
#define PROTOCORE_OAUTH2_TOKEN_TYPE_LEN 24 ///< the `token_type` value buffer, "Bearer" and any longer registered name.
#endif

/** @brief RFC 6749 sec 5.1 access token response parameters. An absent one reads empty or 0. */
typedef struct
{
    char access_token[PROTOCORE_OAUTH2_TOKEN_LEN]; ///< the issued access token, presented per RFC 6750 sec 2.1
    char id_token[PROTOCORE_OAUTH2_TOKEN_LEN];     ///< the OpenID Connect Core 1.0 ID Token, an OpenID Foundation
                                                   ///< specification and not an RFC 6749 sec 5.1 parameter; verify it
                                                   ///< with services/security/oidc
    char refresh_token[PROTOCORE_OAUTH2_RT_LEN];   ///< the refresh token a later sec 6 request presents
    char token_type[PROTOCORE_OAUTH2_TOKEN_TYPE_LEN]; ///< the token type, "Bearer" per RFC 6750 sec 6.1.1
    long expires_in;                                  ///< the access token lifetime in seconds, 0 when absent
} Oauth2Tokens;

/** @brief Negative outcomes of a transport call. An HTTP status code is positive. */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_OAUTH2_ERR_BUILD = -1,     ///< the encoded body did not fit @c request.cap
    PROTOCORE_OAUTH2_ERR_TRANSPORT = -2, ///< the HTTP client reached no endpoint
    PROTOCORE_OAUTH2_ERR_RESPONSE = -3,  ///< the body carried no access_token (RFC 6749 sec 5.2)
} Oauth2Result;

/**
 * @brief RFC 6749 sec 3.2.1: the client identity a token request carries.
 *
 * A set @c client_secret is emitted as the sec 2.3.1 client password in the request body, the form
 * that section marks NOT RECOMMENDED beside HTTP Basic.
 */
typedef struct
{
    const char *client_id;     ///< the client identifier, required when the client does not authenticate (sec 4.1.3)
    const char *client_secret; ///< the confidential client's secret (sec 2.3.1), or NULL for a public client
} Oauth2ClientArgs;

/** @brief RFC 6749 sec 4.1.3 authorization_code grant, plus the PKCE verifier RFC 7636 sec 4.5 sends. */
typedef struct
{
    const char *code;          ///< the authorization code received from the authorization server
    const char *redirect_uri;  ///< the redirection URI of the sec 4.1.1 authorization request, identical to it
    const char *code_verifier; ///< the PKCE code verifier (RFC 7636 sec 4.1), or NULL
} Oauth2CodeGrantArgs;

/** @brief RFC 6749 sec 6: what refreshing an access token presents. */
typedef struct
{
    const char *refresh_token; ///< the refresh token issued alongside the access token (sec 5.1)
} Oauth2RefreshArgs;

/** @brief The endpoint a request goes to (RFC 6749 sec 3.2) and the form body it is built into. */
typedef struct
{
    const char *token_endpoint; ///< the token endpoint URI a transport call posts to
    char *out;                  ///< where a build writes the Appendix B encoded body
    size_t cap;                 ///< how much room that has, the NUL included
} Oauth2RequestArgs;

/** @brief The token-endpoint reply (RFC 6749 sec 5.1 / 5.2) and where its parameters land. */
typedef struct
{
    const char *json;     ///< the response body a parse reads
    Oauth2Tokens *tokens; ///< where the parsed parameters are written
} Oauth2ResponseArgs;

/**
 * @brief The token-endpoint client: two grants out, one token response back.
 *
 * A caller sets the members a call takes, invokes it through ::Oauth2, and reads the outcome off the
 * same handle.
 *
 * @var Oauth2Ns::client       the client identity a request carries (RFC 6749 sec 3.2.1)
 * @var Oauth2Ns::code_grant   the authorization_code grant's parameters (sec 4.1.3, RFC 7636 sec 4.5)
 * @var Oauth2Ns::refresh_grant  the refresh token a sec 6 request presents
 * @var Oauth2Ns::request      the token endpoint (sec 3.2) and the buffer a build encodes into
 * @var Oauth2Ns::response     the reply text a parse reads and the tokens it fills (sec 5.1)
 * @var Oauth2Ns::ok           the parse found an access_token, so the reply is a sec 5.1 response
 * @var Oauth2Ns::i32          bytes a build wrote excluding the NUL and 0 when it did not fit, or a
 *                             transport call's HTTP status and a negative ::Oauth2Result below it
 * @var Oauth2Ns::build_code_request     encode the sec 4.1.3 Access Token Request body
 * @var Oauth2Ns::build_refresh_request  encode the sec 6 refresh request body
 * @var Oauth2Ns::parse_token_response   read the sec 5.1 parameters out of @c response.json
 * @var Oauth2Ns::exchange_code  post the sec 4.1.3 body to the endpoint and parse the sec 4.1.4 reply
 * @var Oauth2Ns::refresh        post the sec 6 body to the endpoint and parse the reply
 *
 * A transport call points @c request.out and @c request.cap at the module's own body buffer and
 * @c response.json at its own response buffer, so a caller sets only @c request.token_endpoint and
 * @c response.tokens for those two.
 *
 * No storage without PROTOCORE_ENABLE_HTTP_CLIENT: the three core calls write the caller's buffers
 * and keep nothing, and the body and reply buffers exist only for the two transport calls.
 */
typedef struct
{
    Oauth2ClientArgs client;         ///< who the client is
    Oauth2CodeGrantArgs code_grant;  ///< the authorization_code grant
    Oauth2RefreshArgs refresh_grant; ///< the refresh_token grant
    Oauth2RequestArgs request;       ///< where the request goes and where its body is built
    Oauth2ResponseArgs response;     ///< the reply and the tokens read out of it
    proto_bool ok;
    int32_t i32;
#if PROTOCORE_ENABLE_HTTP_CLIENT
#endif
} Oauth2Vars;

/** @brief The operands and the outcome. */
extern Oauth2Vars Oauth2V;

/** @brief The entries. */
typedef struct
{
    void (*const build_code_request)(uint8_t *restrict work);
    void (*const build_refresh_request)(uint8_t *restrict work);
    void (*const parse_token_response)(uint8_t *restrict work);
    void (*const exchange_code)(uint8_t *restrict work);
    void (*const refresh)(uint8_t *restrict work);
} Oauth2Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Oauth2V or a region of the borrow at a fixed offset.
void protocore_oauth2_build_code_request(uint8_t *restrict work);
void protocore_oauth2_build_refresh_request(uint8_t *restrict work);
void protocore_oauth2_parse_token_response(uint8_t *restrict work);
void protocore_oauth2_exchange_code(uint8_t *restrict work);
void protocore_oauth2_refresh(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Oauth2.build_code_request(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Oauth2Ns Oauth2 __attribute__((unused)) = {
    .build_code_request = protocore_oauth2_build_code_request,
    .build_refresh_request = protocore_oauth2_build_refresh_request,
    .parse_token_response = protocore_oauth2_parse_token_response,
    .exchange_code = protocore_oauth2_exchange_code,
    .refresh = protocore_oauth2_refresh,
};

/**
 * @brief The PROTOCORE_OAUTH2_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
#if PROTOCORE_ENABLE_HTTP_CLIENT
uint8_t *protocore_oauth2_span(void);
#endif // PROTOCORE_ENABLE_HTTP_CLIENT

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_OAUTH2

#endif // PROTOCORE_OAUTH2_H
