// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ip.h
 * @brief Layer 3 (Network) - a family-tagged IP address (IPv4 or IPv6) with RFC-faithful
 *        text parsing, canonical formatting, and scope classification.
 *
 * One representation for both address families so the rest of the stack can carry a peer
 * address without caring whether it is v4 or v6. The address bytes are stored in network
 * (big-endian, left-to-right) order: a v4 address uses bytes[0..3], a v6 address bytes[0..15].
 *
 * Pure and host-testable - no lwIP, no Arduino, no heap, no stdlib parsing. The parser
 * implements RFC 4291 §2.2 text forms (dotted-quad v4; v6 with `::` zero-compression and the
 * embedded-v4 `::ffff:a.b.c.d` tail); the formatter emits the RFC 5952 canonical form
 * (lower-case, no leading zeros, the longest zero run compressed to `::`, v4-mapped shown as
 * dotted). ESP32 dual-stack bring-up (enabling IPv6 on the netif) lives in the physical layer
 * behind PROTOCORE_ENABLE_IPV6; the TCP/UDP listeners already bind IPADDR_TYPE_ANY, so the server
 * accepts v6 connections the moment the interface has a v6 address.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_IP_H
#define PROTOCORE_IP_H

#include "protocore_config.h" // the entry point; it sets the widths and reaches protocore_types.h for proto_bool

PROTOCORE_BEGIN_DECLS

/** @brief Address family tag. */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_IP_NONE = 0, ///< empty / unparsed
    PROTOCORE_IP_V4 = 4,   ///< IPv4 (bytes[0..3])
    PROTOCORE_IP_V6 = 6,   ///< IPv6 (bytes[0..15])
} protocore_ip_family;
static_assert(sizeof(protocore_ip_family) == 1, "protocore_ip_family must stay one byte (PROTO_ENUM_PACKED); "
                                                "protocore_ip is embedded wherever an address is stored");

/** @brief Address scope, in rough order of reachability (used for allow/deny policy + logging). */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_IP_SCOPE_UNSPECIFIED = 0, ///< 0.0.0.0 / ::
    PROTOCORE_IP_SCOPE_LOOPBACK,        ///< 127.0.0.0/8 / ::1
    PROTOCORE_IP_SCOPE_LINK_LOCAL,      ///< 169.254.0.0/16 / fe80::/10
    PROTOCORE_IP_SCOPE_PRIVATE,         ///< RFC1918 (10/8, 172.16/12, 192.168/16) / ULA fc00::/7
    PROTOCORE_IP_SCOPE_MULTICAST,       ///< 224.0.0.0/4 / ff00::/8
    PROTOCORE_IP_SCOPE_GLOBAL,          ///< globally routable unicast
} protocore_ip_scope;
static_assert(sizeof(protocore_ip_scope) == 1, "protocore_ip_scope must stay one byte (PROTO_ENUM_PACKED)");

/** @brief A v4 or v6 address in network (big-endian) byte order. */
typedef struct
{
    protocore_ip_family family; ///< address family tag
    uint8_t bytes[16];          ///< network order; v4 uses the first 4
} protocore_ip;

/** @brief Longest text an ::IpNs::format can produce, including the NUL (RFC 5952 v4-mapped). */
#define PROTOCORE_IP_STR_MAX 46

/**
 * @brief Parse an IPv4 or IPv6 textual address (RFC 4291 §2.2) into @p out.
 * @return true on success (@p out->family set to PROTOCORE_IP_V4/V6), false if @p s is malformed.
 */

/**
 * @brief Format @p ip into @p out as its RFC 5952 canonical text.
 * @return the length written (excluding the NUL), or 0 if @p ip is empty or @p cap is too small
 *         (need up to ::PROTOCORE_IP_STR_MAX).
 */

/** @brief Classify @p ip into a ::protocore_ip_scope. */

/** @brief True if @p a and @p b are the same family and address. */

/** @brief True if @p ip is an IPv4-mapped IPv6 address (::ffff:a.b.c.d, RFC 4291 §2.5.5.2). */
proto_bool protocore_ip_is_v4_mapped(const protocore_ip *ip);

/**
 * @brief Build a v4 ::protocore_ip from four octets (a.b.c.d).
 */
protocore_ip protocore_ip_from_v4_octets(uint8_t a, uint8_t b, uint8_t c, uint8_t d);

/**
 * @brief Build a v6 ::protocore_ip from 16 address bytes in network (big-endian) order.
 */
protocore_ip protocore_ip_from_v6_bytes(const uint8_t bytes[16]);

/**
 * @brief The v4 address as a big-endian (network-order) uint32 (a<<24 | b<<16 | c<<8 | d).
 * @return 0 if @p ip is not a v4 (or v4-mapped) address.
 */
uint32_t protocore_ip_to_v4_be(const protocore_ip *ip);

/** @brief True if @p ip is empty (PROTOCORE_IP_NONE) or the all-zero unspecified address (0.0.0.0 / ::). */

/**
 * @brief CIDR containment: is @p addr inside the @p net / @p prefix_len block?
 *
 * The two must be the same family. @p prefix_len is 0..32 for v4, 0..128 for v6; the top
 * @p prefix_len bits of the address bytes must match @p net (a prefix of 0 matches everything).
 * This is the standard v4/v6 allowlist match.
 * @return true if @p addr is covered; false on a family mismatch or an out-of-range prefix.
 */

/**
 * @brief An IP address, as a value.
 *
 * @var IpNs::parse           read a textual address, v4 or v6, into @c out
 * @var IpNs::format          write @c ip as text into @c out, returning the length
 * @var IpNs::classify        what scope the address names
 * @var IpNs::equal           whether two addresses are the same address
 * @var IpNs::is_unspecified  whether the address names nothing
 * @var IpNs::prefix_match    whether @c addr falls inside @c net at @c prefix_len bits
 *
 * No storage member: every operation reads its operands and holds nothing. Reached as
 * network.ip->parse(...), or directly as @ref Ip from inside the network layer.
 * , or directly as @ref Ip from inside the network layer.
 */
typedef struct
{
    proto_bool (*parse)(const char *s, protocore_ip *out);
    size_t (*format)(const protocore_ip *ip, char *out, size_t cap);
    protocore_ip_scope (*classify)(const protocore_ip *ip);
    proto_bool (*equal)(const protocore_ip *a, const protocore_ip *b);
    proto_bool (*is_unspecified)(const protocore_ip *ip);
    proto_bool (*prefix_match)(const protocore_ip *addr, const protocore_ip *net, uint8_t prefix_len);
} IpNs;

/** @brief The one symbol this module exports. */
extern const IpNs Ip;

PROTOCORE_END_DECLS

#endif // PROTOCORE_IP_H
