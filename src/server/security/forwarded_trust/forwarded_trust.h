// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file forwarded_trust.h
 * @brief Trusted-reverse-proxy resolution of a forwarded client address (PROTOCORE_ENABLE_FORWARDED_TRUST).
 *
 * A `Forwarded` (RFC 7239) / `X-Forwarded-For` header is CLIENT-SPOOFABLE, so the client address it
 * carries may only be believed when the connection's real TCP peer is a reverse proxy the operator
 * trusts. This keeps a fixed BSS table of trusted-upstream CIDRs and resolves the effective client
 * address for the abuse-prevention layer: when the TCP peer matches a trusted CIDR and the forwarded
 * token is a valid, specified address, the forwarded client is used; otherwise the real TCP peer is
 * used. Fail-safe by construction - an empty table trusts no header, and a malformed / obfuscated /
 * unspecified token falls back to the TCP peer, so a direct (untrusted) client cannot spoof its way
 * out of, or another peer into, the auth lockout. Pure (no sockets, no heap), host-tested.
 *
 * The table is a single owned instance reached only through this API (mirrors the source-IP allowlist
 * and the auth-lockout table). Register upstreams with protocore_forwarded_trust_add_cidr("10.0.0.0/8").
 */

#ifndef PROTOCORE_FORWARDED_TRUST_H
#define PROTOCORE_FORWARDED_TRUST_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_FORWARDED_TRUST

PROTOCORE_BEGIN_DECLS

// PROTOCORE_FORWARDED_TRUST_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums it
// into its arena. A caller takes them once and passes the pointer to every call. How they are
// carved is this module's and is never named here.

/** @brief protocore_ip, as the caller already knows it. */
struct protocore_ip;

/** @brief What add takes. */
typedef struct
{
    const struct protocore_ip *network;
    uint8_t prefix_len;
} ForwardedTrustAddArgs;

/** @brief What add_cidr takes. */
typedef struct
{
    const char *cidr;
} ForwardedTrustAddCidrArgs;

/** @brief What contains takes. */
typedef struct
{
    const struct protocore_ip *peer;
} ForwardedTrustContainsArgs;

/** @brief What protocore_forwarded_effective_ip takes. */
typedef struct
{
    const struct protocore_ip *peer;
    const char *fwd_ip_str;
    struct protocore_ip *out;
} ForwardedTrustProtocoreForwardedEffectiveIpArgs;
typedef struct
{
    ForwardedTrustAddArgs add_args;
    ForwardedTrustAddCidrArgs add_cidr_args;
    ForwardedTrustContainsArgs contains_args;
    ForwardedTrustProtocoreForwardedEffectiveIpArgs protocore_forwarded_effective_ip_args;

    proto_bool ok;

    void (*const reset)(uint8_t *restrict work);
    void (*const add)(uint8_t *restrict work);
    void (*const add_cidr)(uint8_t *restrict work);
    void (*const contains)(uint8_t *restrict work);
    void (*const protocore_forwarded_effective_ip)(uint8_t *restrict work);
} ForwardedTrustNs;

/** @brief The one symbol this module exports. */
extern ForwardedTrustNs ForwardedTrust;

/**
 * @brief The PROTOCORE_FORWARDED_TRUST_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_forwarded_trust_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_FORWARDED_TRUST

#endif // PROTOCORE_FORWARDED_TRUST_H
