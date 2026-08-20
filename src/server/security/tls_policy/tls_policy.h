// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tls_policy.h
 * @brief TLS version negotiation + pinned cipher-suite policy (PROTOCORE_ENABLE_TLS_POLICY).
 *
 * The transport TLS layer already runs the record and handshake and floors the version at TLS 1.2,
 * so both TLS 1.2 (RFC 5246) and TLS 1.3 (RFC 8446) are negotiated. What this adds on top is a
 * policy: pin the negotiated version to an audited [min,max] range and make the chosen version
 * observable, and pin the cipher suites to an audited allowlist selected by server preference
 * (AEAD-only for a hardened profile).
 *
 * The pure policy core: @ref TlsPolicyNs::negotiate picks the version the way a server does (the
 * highest it supports not above the client's), @ref TlsPolicyNs::name names it for a status
 * endpoint, @ref TlsPolicyNs::select picks a suite by server preference from the offered set, and
 * @ref TlsPolicyNs::is_aead classifies one. Host-testable; the app feeds the results to the TLS
 * config. No heap, no stdlib.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TLS_POLICY_H
#define PROTOCORE_TLS_POLICY_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_TLS_POLICY

PROTOCORE_BEGIN_DECLS

/** @brief TLS protocol version wire words. */
#define TLS_VERSION_1_2 0x0303
#define TLS_VERSION_1_3 0x0304

// This module holds nothing between calls, so it carves no borrow and states none. An entry takes
// one all the same, and never reads it, so every namespace in the tree is invoked the same way.

/** @brief The client's offer and the server's supported range. */
typedef struct
{
    uint16_t client_max; ///< the client's highest offered version
    uint16_t server_min; ///< the lowest version this server accepts
    uint16_t server_max; ///< the highest it supports
} TlsPolicyNegotiateArgs;

/** @brief The version word a name is asked for. */
typedef struct
{
    uint16_t version; ///< the wire word
} TlsPolicyNameArgs;

/** @brief The offered suites, and the pinned list that orders the choice. */
typedef struct
{
    const uint16_t *client_offered; ///< what the client sent
    size_t n_client;                ///< how many
    const uint16_t *server_pinned;  ///< the audited allowlist, in preference order
    size_t n_server;                ///< how many
} TlsPolicySelectArgs;

/** @brief The suite a classification is asked about. */
typedef struct
{
    uint16_t suite; ///< the wire id
} TlsPolicyAeadArgs;

/**
 * @brief TLS version and cipher-suite policy.
 *
 * A caller sets the members a call takes, invokes it through ::TlsPolicy with the bytes it runs out
 * of, and reads the outcome off the same handle.
 *
 *   TlsPolicy.negotiate_args.client_max = TLS_VERSION_1_3;
 *   TlsPolicy.negotiate_args.server_min = TLS_VERSION_1_2;
 *   TlsPolicy.negotiate_args.server_max = TLS_VERSION_1_3;
 *   TlsPolicy.negotiate(work);
 *   // TlsPolicy.version is the chosen word, 0 when the ranges do not overlap
 *
 * @var TlsPolicyNs::negotiate_args  the client's offer and the server's supported range
 * @var TlsPolicyNs::name_args       the version word a name is asked for
 * @var TlsPolicyNs::select_args     the offered suites, and the pinned list that orders the choice
 * @var TlsPolicyNs::aead_args       the suite a classification is asked about
 * @var TlsPolicyNs::ok              a call's true/false outcome
 * @var TlsPolicyNs::version         the negotiated version word, 0 when the ranges do not overlap
 * @var TlsPolicyNs::suite           the selected suite id, 0 when none of the pinned suites was offered
 * @var TlsPolicyNs::text            the version's human name: "TLS 1.2", "TLS 1.3", or "unknown"
 * @var TlsPolicyNs::aead            whether the suite is one of the modern AEAD suites
 * @var TlsPolicyNs::negotiate       the highest supported version not above the client's
 * @var TlsPolicyNs::name            name a version word for a status endpoint
 * @var TlsPolicyNs::select          the first pinned suite the client also offered (server preference)
 * @var TlsPolicyNs::is_aead         classify a suite as GCM / ChaCha20-Poly1305 or not
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing between
 * calls, so there is no state to keep and nothing to wipe. The parameter is there so a caller drives
 * every namespace the same way.
 *
 * No storage member and no context: a caller sets operands and reads @ref TlsPolicyNs::ok, and that
 * is all the surface there is.
 */
typedef struct
{
    TlsPolicyNegotiateArgs negotiate_args;
    TlsPolicyNameArgs name_args;
    TlsPolicySelectArgs select_args;
    TlsPolicyAeadArgs aead_args;
    proto_bool ok;
    uint16_t version;
    uint16_t suite;
    const char *text;
    proto_bool aead;
} TlsPolicyVars;

/** @brief The operands and the outcome. */
extern TlsPolicyVars TlsPolicyV;

/** @brief The entries. */
typedef struct
{
    void (*const negotiate)(uint8_t *restrict work);
    void (*const name)(uint8_t *restrict work);
    void (*const select)(uint8_t *restrict work);
    void (*const is_aead)(uint8_t *restrict work);
} TlsPolicyNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in TlsPolicyV or a region of the borrow at a fixed offset.
void protocore_tls_policy_negotiate(uint8_t *restrict work);
void protocore_tls_policy_name(uint8_t *restrict work);
void protocore_tls_policy_select(uint8_t *restrict work);
void protocore_tls_policy_is_aead(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `TlsPolicy.negotiate(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const TlsPolicyNs TlsPolicy __attribute__((unused)) = {
    .negotiate = protocore_tls_policy_negotiate,
    .name = protocore_tls_policy_name,
    .select = protocore_tls_policy_select,
    .is_aead = protocore_tls_policy_is_aead,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_TLS_POLICY

#endif // PROTOCORE_TLS_POLICY_H
