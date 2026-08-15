// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file auth.h
 * @brief HTTP authentication: Basic (RFC 7617) and stateless Digest (RFC 7616, SHA-256, qop=auth).
 *
 * Digest carries no per-nonce server state. A nonce is `<issue_ms_hex>.<mac_hex>` where the MAC is
 * SHA-256(secret || issue_ms) truncated to 128 bits, so a returned nonce is authenticated by
 * recomputing it and aged by reading the issue time back out of it. That is what makes the scheme
 * safe under the shared-nothing worker model: the keying secret is set once at begin() and is
 * read-only afterwards, and no worker owns a nonce table another worker has to see.
 *
 * The module exports one symbol, @ref Auth. Everything in auth.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_AUTH_H
#define PROTOCORE_AUTH_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_AUTH

PROTOCORE_BEGIN_DECLS

// Named, not defined: the request is the parser's and the route is the table's, and this module only
// reads them. Declaring them here rather than including their headers is what keeps auth.h includable
// from protocore.h, which is where both of those types come from.
struct HttpReq;

/** @brief The id a route carries when it needs no credentials. */
#define PROTOCORE_AUTH_NONE 0xFFu

/**
 * @brief The authentication module.
 *
 * @var AuthNs::add
 * Record one credential set and return the id that names it, or @ref PROTOCORE_AUTH_NONE when the table is
 * full. A route stores that id; it never stores the credential, so no route slot carries key
 * material and the same set can serve several routes.
 *
 * @var AuthNs::check
 * True when the request carries credentials matching the set @c id names. Which scheme applies is
 * the set's own property, so the caller does not choose between Basic and Digest.
 *
 * For Basic the decoded credential is split on its first colon and BOTH halves are compared in
 * constant time at their stated lengths - never as strings, because an embedded NUL must not
 * truncate a submitted password into a shorter one that happens to match.
 *
 * For Digest, @c stale comes back true
 * for credentials that verify against a nonce this server minted but whose issue time falls outside
 * the nonce lifetime: that is a reissue, not a rejection, so the caller re-challenges with
 * `stale=true` and the client retries without prompting the user again (RFC 7616 3.3). It is left
 * untouched on a credential mismatch or a forged nonce, where the only correct answer is no.
 *
 * @var AuthNs::challenge
 * Write the 401 and its `WWW-Authenticate` header for the set @c id names, Basic or Digest as that
 * set says. @c stale marks the transparent-retry case above.
 *
 * @var AuthNs::rekey
 * (Re)seed the Digest keying secret from the CSPRNG. Runs once per begin(); every nonce the server
 * mints afterwards is keyed by it, and it never leaves this module.
 *
 * @var AuthNs::mint_nonce
 * Mint a fresh nonce into @c out, which needs a capacity of at least 48.
 *
 * @var AuthNs::verify_nonce
 * True when @c nonce carries a MAC this server could have produced. @c expired is set when the MAC
 * is authentic but the issue time falls outside the nonce lifetime - authentic and fresh are
 * separate answers, and only the pair of them justifies trusting the credential that arrived with it.
 *
 * @var AuthNs::reset
 * Empty the credential table. An id names a row by index and a route holds that id, so the table
 * empties with the routes it is indexed from: protocore_server_reset() calls both. A table that kept its
 * rows across a reset would reach @ref PROTOCORE_AUTH_NONE after MAX_ROUTES registrations and hand every
 * later route an id that guards nothing.
 *
 * The keying secret is the module's own storage and is not a member: it is written at
 * @ref AuthNs::rekey and read by nothing outside auth.c, so exposing a handle to it would widen the
 * surface without giving any caller something it can use.
 */
/** @brief RFC 7616 sec 3.2.1 / RFC 7617: one credential row's realm and secret. */
typedef struct
{
    const char *realm; ///< the realm a row is added under
    const char *user;  ///< its username
    const char *pass;  ///< its password
    proto_bool digest; ///< the row is Digest rather than Basic
} AuthCredArgs;

/** @brief RFC 7616 sec 3.3: the nonce a challenge carries, and where a mint writes one. */
typedef struct
{
    proto_bool stale;  ///< in: the challenge marks a transparent retry; out: the check found one
    const char *nonce; ///< the nonce a verify judges
    char *out;         ///< where a mint writes
    size_t cap;        ///< how much room it has; at least 48
} AuthNonceArgs;

/** @brief The credential table's own state and the calls that reach it, described only in auth.c. */
struct AuthInternal;

/**
 * A caller sets the members a call takes, invokes it through ::Auth, and reads the outcome off the
 * same handle.
 *
 * @var AuthNs::work     the caller's scratch region a call borrows
 * @var AuthNs::slot     the connection a challenge or a check acts on
 * @var AuthNs::req      the parsed request a check reads its credential from
 * @var AuthNs::id       the credential set a call names
 * @var AuthNs::cred       one credential row: realm, user, pass, and whether it is Digest
 * @var AuthNs::nonce_args the nonce a mint writes and a verify judges, and the stale flag
 * @var AuthNs::ok       a call's true/false outcome
 * @var AuthNs::expired  the MAC is authentic but the issue time falls outside the nonce lifetime
 * @var AuthNs::u8       the id an add reports, or ::PROTOCORE_AUTH_NONE when the table is full
 * @var AuthNs::internal the table's state and the calls that reach it
 */
typedef struct
{
    uint8_t *work;       ///< the caller's scratch region a call borrows
    uint8_t slot;        ///< the connection a challenge or a check acts on
    struct HttpReq *req; ///< the parsed request a check reads its credential from
    uint8_t id;          ///< the credential set a call names

    AuthCredArgs cred;        ///< one credential row
    AuthNonceArgs nonce_args; ///< the nonce a mint writes and a verify judges

    proto_bool ok;
    proto_bool expired;
    uint8_t u8;

    void (*add)(struct AuthInternal *ctx);
    void (*check)(struct AuthInternal *ctx);
    void (*challenge)(struct AuthInternal *ctx);
    void (*rekey)(struct AuthInternal *ctx);
    void (*mint_nonce)(struct AuthInternal *ctx);
    void (*verify_nonce)(struct AuthInternal *ctx);
    void (*reset)(struct AuthInternal *ctx);

    struct AuthInternal *internal;
} AuthNs;

/** @brief The one symbol this module exports. */
extern AuthNs Auth;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_AUTH

#endif // PROTOCORE_AUTH_H
