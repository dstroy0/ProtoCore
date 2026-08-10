// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ipsec_db.h
 * @brief IPsec Security Policy Database (SPD) + Security Association Database (SAD) - RFC 4301.
 *
 * The ESP datapath (esp.h) is the crypto transform; this file is the two databases that decide, for a
 * given packet, WHETHER and WITH WHICH SA to apply it. Both are pure, host-testable data structures with
 * no heap and no lwIP dependency - the remaining device-side piece is only the IP input/output hook that
 * feeds packets through these lookups.
 *
 *   - SPD (RFC 4301 §4.4.1): an ordered list of policies matched against a packet's selectors (source /
 *     destination address ranges, protocol, port ranges); the FIRST matching policy wins and names an
 *     action - PROTECT (apply ESP with a bound SA), BYPASS (send in the clear), or DISCARD (drop).
 *   - SAD (RFC 4301 §4.4.2): the active Security Associations keyed by SPI. An inbound ESP packet is
 *     demuxed to its SA by SPI; an outbound PROTECT policy names the SA to encapsulate with. Each SA
 *     carries its key / salt, its outbound sequence counter, and its inbound anti-replay window.
 *
 * Selectors are value types (addresses stored inline, big-endian) so the databases persist independently
 * of any wire buffer. @ref pc_ipsec_selector_from_ts bridges an IKEv2-negotiated TSi/TSr pair (the
 * traffic selectors carried in ikev2.h) into an SPD selector, per RFC 4301 §4.4.1.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_IPSEC_DB_H
#define PROTOCORE_IPSEC_DB_H

#include "protocore_config.h"

#if PC_ENABLE_IKEV2

#include "services/security/ikev2/ikev2.h"
#include "services/system/esp/esp.h"

/** @brief Longest selector address (IPv6). IPv4 uses the low 4 bytes. */
#define PC_IPSEC_ADDR_MAX 16
/** @brief Maximum policies in one SPD. */
#define PC_IPSEC_SPD_MAX 8
/** @brief Maximum Security Associations in one SAD. */
#define PC_IPSEC_SAD_MAX 8

/** @brief SPD policy action (RFC 4301 §4.4.1). */
typedef enum PROTO_ENUM_PACKED
{
    IPSEC_ACTION_DISCARD = 0, ///< drop the packet
    IPSEC_ACTION_BYPASS = 1,  ///< forward without IPsec
    IPSEC_ACTION_PROTECT = 2, ///< apply ESP with the bound SA
} IpsecAction;

/**
 * @brief A traffic selector as an SPD range (value type, addresses inline big-endian).
 *
 * A packet matches when its family and protocol agree and its source / destination addresses and ports
 * each fall within the inclusive [lo, hi] ranges. A protocol of 0 or a port range of [0, 65535] is "any".
 */
typedef struct
{
    uint8_t addr_len;    ///< 4 (IPv4) or 16 (IPv6); also selects the family
    uint8_t ip_protocol; ///< 0 = any
    uint8_t src_lo[PC_IPSEC_ADDR_MAX];
    uint8_t src_hi[PC_IPSEC_ADDR_MAX];
    uint8_t dst_lo[PC_IPSEC_ADDR_MAX];
    uint8_t dst_hi[PC_IPSEC_ADDR_MAX];
    uint16_t src_port_lo;
    uint16_t src_port_hi;
    uint16_t dst_port_lo;
    uint16_t dst_port_hi;
} IpsecSelector;

/** @brief One SPD policy: a selector, its action, and (for PROTECT) the outbound SA's SPI. */
typedef struct
{
    IpsecSelector sel;
    IpsecAction action;
    uint32_t sa_spi; ///< PROTECT: the SAD entry to encapsulate with (0 = not yet bound)
} IpsecPolicy;

/** @brief An ordered Security Policy Database (first match wins). */
typedef struct
{
    IpsecPolicy entries[PC_IPSEC_SPD_MAX];
    size_t count;
} IpsecSpd;

/** @brief A concrete packet's 5-tuple, looked up against the SPD. Addresses point at big-endian octets. */
typedef struct
{
    uint8_t addr_len; ///< 4 or 16 (must match the selector family)
    uint8_t ip_protocol;
    const uint8_t *src; ///< @ref addr_len octets
    const uint8_t *dst; ///< @ref addr_len octets
    uint16_t src_port;
    uint16_t dst_port;
} IpsecFlow;

/** @brief One Security Association (RFC 4301 §4.4.2). */
typedef struct
{
    uint32_t spi;                   ///< the SA's SPI (its SAD key)
    uint8_t dst[PC_IPSEC_ADDR_MAX]; ///< SA destination address
    uint8_t addr_len;               ///< 4 or 16
    uint8_t key[PC_ESP_KEY_LEN];    ///< AES-256 key (SK_ei / SK_er without salt)
    uint8_t salt[PC_ESP_SALT_LEN];  ///< AES-GCM salt (the key's tail)
    uint32_t seq;                   ///< outbound: last sequence number issued (0 = none yet)
    EspReplay replay;               ///< inbound: anti-replay window
    proto_bool inbound;             ///< true = receive SA, false = send SA
    proto_bool valid;               ///< false = free slot
} IpsecSaEntry;

/** @brief The active Security Association Database, keyed by SPI. */
typedef struct
{
    IpsecSaEntry entries[PC_IPSEC_SAD_MAX];
    size_t count;
} IpsecSad;

// ── SPD ─────────────────────────────────────────────────────────────────────────────────────────

/** @brief Empty an SPD (no policies). */
void pc_ipsec_spd_init(IpsecSpd *spd);

/**
 * @brief Append a policy to the SPD (order is significant - first match wins on lookup).
 * @param sa_spi for a PROTECT action, the SAD SPI to bind (ignored otherwise).
 * @return true on success, false if @p spd is full or an argument is null.
 */
proto_bool pc_ipsec_spd_add(IpsecSpd *spd, const IpsecSelector *sel, IpsecAction action, uint32_t sa_spi);

/**
 * @brief Find the first SPD policy whose selector matches @p flow (RFC 4301 §4.4.1 ordered match).
 * @return the matching policy, or nullptr if none matches (the caller drops, per the default-deny rule).
 */
const IpsecPolicy *pc_ipsec_spd_lookup(const IpsecSpd *spd, const IpsecFlow *flow);

/** @brief True iff @p flow falls inside @p sel (family, protocol, address ranges, and port ranges). */
proto_bool pc_ipsec_selector_match(const IpsecSelector *sel, const IpsecFlow *flow);

/**
 * @brief Fill @p out from an IKEv2-negotiated TSi / TSr pair (RFC 4301 §4.4.1 SPD-from-TS).
 *
 * @p ts_src is the local (initiator) traffic selector, @p ts_dst the peer (responder) one; the protocol
 * is taken from the pair (they must agree, or 0/any is honored). Both selectors must share an address
 * family.
 * @return true on success, false on a null argument or a family / length mismatch.
 */
proto_bool pc_ipsec_selector_from_ts(IpsecSelector *out, const IkeTrafficSelector *ts_src,
                                     const IkeTrafficSelector *ts_dst);

// ── SAD ─────────────────────────────────────────────────────────────────────────────────────────

/** @brief Empty a SAD (no SAs). */
void pc_ipsec_sad_init(IpsecSad *sad);

/**
 * @brief Install a Security Association keyed by @p spi.
 *
 * An inbound SA's anti-replay window is initialized; an outbound SA's sequence counter starts at 0. A
 * duplicate SPI is rejected (SPIs must be unique within the SAD).
 * @return the installed entry, or nullptr if the SAD is full, an argument is null, or @p spi already exists.
 */
IpsecSaEntry *pc_ipsec_sad_add(IpsecSad *sad, uint32_t spi, const uint8_t *dst, uint8_t addr_len,
                               const uint8_t key[PC_ESP_KEY_LEN], const uint8_t salt[PC_ESP_SALT_LEN],
                               proto_bool inbound);

/** @brief Look up a valid SA by SPI (inbound ESP demux, RFC 4301 §4.1). nullptr if absent. */
IpsecSaEntry *pc_ipsec_sad_find(IpsecSad *sad, uint32_t spi);

/** @brief Remove the SA with @p spi (e.g. on an IKE DELETE). @return true if one was removed. */
proto_bool pc_ipsec_sad_remove(IpsecSad *sad, uint32_t spi);

/**
 * @brief Allocate the next outbound sequence number for @p sa (RFC 4303 §3.3.3, pre-increment from 1).
 * @param seq_out receives the sequence number to place in the packet.
 * @return true on success; false (and @p sa left unchanged) if the 32-bit counter would wrap - the SA
 *         must be rekeyed before any further packets, since a repeated sequence number breaks GCM.
 */
proto_bool pc_ipsec_sad_next_seq(IpsecSaEntry *sa, uint32_t *seq_out);

#endif // PC_ENABLE_IKEV2
#endif // PROTOCORE_IPSEC_DB_H
