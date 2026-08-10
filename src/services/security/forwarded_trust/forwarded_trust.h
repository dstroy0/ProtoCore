// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file forwarded_trust.h
 * @brief Trusted-reverse-proxy resolution of a forwarded client address (PC_ENABLE_FORWARDED_TRUST).
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
 * and the auth-lockout table). Register upstreams with pc_forwarded_trust_add_cidr("10.0.0.0/8").
 */

#ifndef PROTOCORE_FORWARDED_TRUST_H
#define PROTOCORE_FORWARDED_TRUST_H

#include "protocore_config.h"

#if PC_ENABLE_FORWARDED_TRUST

#include "shared_primitives/ip.h"

/** @brief Empty the trusted-upstream table (trust no forwarded header). */
void pc_forwarded_trust_reset(void);

/**
 * @brief Add a trusted-upstream network.
 * @return true if added; false on a null / malformed-family @p network, an out-of-range @p prefix_len,
 *         or a full table (PC_TRUSTED_PROXY_MAX).
 */
proto_bool pc_forwarded_trust_add(const pc_ip *network, uint8_t prefix_len);

/**
 * @brief Add a trusted-upstream network from a CIDR string ("10.0.0.0/8" / "2001:db8::/32"; a bare
 *        address is taken as a host route).
 * @return true if parsed and added; false otherwise.
 */
proto_bool pc_forwarded_trust_add_cidr(const char *cidr);

/** @brief True if @p peer falls inside any trusted-upstream CIDR (always false when the table is empty). */
proto_bool pc_forwarded_trust_contains(const pc_ip *peer);

/**
 * @brief Resolve the effective client address behind a possibly-trusted proxy.
 *
 * @p out is set to the forwarded client ONLY when @p peer is a trusted upstream AND @p fwd_ip_str is a
 * valid, specified address; otherwise @p out is set to @p peer. @p out is always written when non-null.
 *
 * @param peer        the connection's real TCP source address.
 * @param fwd_ip_str  the recovered forwarded client address text, or nullptr/"" if none was present.
 * @param out         receives the effective client address.
 * @return true if the forwarded client was honored; false if the real TCP peer was kept.
 */
proto_bool pc_forwarded_effective_ip(const pc_ip *peer, const char *fwd_ip_str, pc_ip *out);

#endif // PC_ENABLE_FORWARDED_TRUST
#endif // PROTOCORE_FORWARDED_TRUST_H
