// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file oauth2.h
 * @brief OAuth2 token-endpoint client - authorization-code + refresh (PC_ENABLE_OAUTH2).
 *
 * The other half of the OAuth/OIDC story (services/security/oidc verifies the ID token;
 * this obtains the tokens). A relying party exchanges an authorization code for
 * tokens at the provider's token endpoint, or refreshes them, per RFC 6749 §4.1.3
 * / §6:
 *
 *  - **Pure core (host-tested):** build the `application/x-www-form-urlencoded`
 *    request body (proper percent-encoding) for the `authorization_code` and
 *    `refresh_token` grants, and parse the JSON token response (reusing the
 *    library's zero-heap JSON reader) into ::pc_o_auth2_tokens.
 *  - **ESP32 convenience (needs PC_ENABLE_HTTP_CLIENT):**
 *    pc_oauth2_exchange_code() / _refresh() POST to the token URL over the
 *    HTTP(S) client and parse the result.
 *
 * Supports a confidential client (client_secret) or a public client with PKCE
 * (code_verifier, RFC 7636) - pass whichever applies, nullptr for the other. No
 * heap, no stdlib.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_OAUTH2_H
#define PROTOCORE_OAUTH2_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_OAUTH2

/** @brief Tokens parsed from a token-endpoint response. Absent fields are empty / 0. */
struct pc_o_auth2_tokens
{
    char access_token[PC_OAUTH2_TOKEN_LEN];
    char id_token[PC_OAUTH2_TOKEN_LEN]; ///< OIDC ID token (verify with services/security/oidc).
    char refresh_token[PC_OAUTH2_RT_LEN];
    char token_type[24]; ///< usually "Bearer".
    long expires_in;     ///< access-token lifetime in seconds (0 if absent).
};

/** @brief oauth2 result codes (HTTP status codes are positive on success). */
enum class pc_o_auth2_result : int32_t
{
    PC_OAUTH2_ERR_BUILD = -1,     ///< request body did not fit @p cap.
    PC_OAUTH2_ERR_TRANSPORT = -2, ///< HTTP client error (no DNS / TLS / connection).
    PC_OAUTH2_ERR_RESPONSE = -3,  ///< response was not a valid token JSON (no access_token).
};

// ---------------------------------------------------------------------------
// Pure core (host-testable)
// ---------------------------------------------------------------------------

/**
 * @brief Build the form body for the authorization_code grant.
 *
 * @param code,redirect_uri,client_id  required parameters.
 * @param client_secret  confidential-client secret, or nullptr.
 * @param code_verifier  PKCE verifier (RFC 7636) for a public client, or nullptr.
 * @return bytes written (excluding NUL), or 0 if it does not fit.
 */
int pc_oauth2_build_code_request(const char *code, const char *redirect_uri, const char *client_id,
                                 const char *client_secret, const char *code_verifier, char *out, size_t cap);

/**
 * @brief Build the form body for the refresh_token grant.
 * @return bytes written (excluding NUL), or 0 if it does not fit.
 */
int pc_oauth2_build_refresh_request(const char *refresh_token, const char *client_id, const char *client_secret,
                                    char *out, size_t cap);

/**
 * @brief Parse a token-endpoint JSON response into @p out.
 * @return true if an access_token is present (a usable token response).
 */
bool pc_oauth2_parse_token_response(const char *json, pc_o_auth2_tokens *out);

#if PC_ENABLE_HTTP_CLIENT
// ---------------------------------------------------------------------------
// ESP32 convenience (over the HTTP(S) client)
// ---------------------------------------------------------------------------

/**
 * @brief Exchange an authorization code at @p token_url for tokens.
 * @return the HTTP status (200 on success) or a negative ::pc_o_auth2_result.
 */
int pc_oauth2_exchange_code(const char *token_url, const char *code, const char *redirect_uri, const char *client_id,
                            const char *client_secret, const char *code_verifier, pc_o_auth2_tokens *out);

/**
 * @brief Refresh tokens at @p token_url using a refresh token.
 * @return the HTTP status (200 on success) or a negative ::pc_o_auth2_result.
 */
int pc_oauth2_refresh(const char *token_url, const char *refresh_token, const char *client_id,
                      const char *client_secret, pc_o_auth2_tokens *out);
#endif

#endif // PC_ENABLE_OAUTH2
#endif // PROTOCORE_OAUTH2_H
