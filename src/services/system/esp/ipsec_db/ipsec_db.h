// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
 * of any wire buffer. @ref protocore_ipsec_selector_from_ts bridges an IKEv2-negotiated TSi/TSr pair (the
 * traffic selectors carried in ikev2.h) into an SPD selector, per RFC 4301 §4.4.1.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_IPSEC_DB_H
#define PROTOCORE_IPSEC_DB_H

#include "services/security/ikev2/ikev2/ikev2.h" // the complete type a public struct below holds by value
#include "services/system/esp/esp/esp.h"         // the complete type a public struct below holds by value

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_IKEV2

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief Longest selector address (IPv6). IPv4 uses the low 4 bytes. */
#define PROTOCORE_IPSEC_ADDR_MAX 16

/** @brief Maximum policies in one SPD. */
#define PROTOCORE_IPSEC_SPD_MAX 8

/** @brief Maximum Security Associations in one SAD. */
#define PROTOCORE_IPSEC_SAD_MAX 8

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
    uint8_t src_lo[PROTOCORE_IPSEC_ADDR_MAX];
    uint8_t src_hi[PROTOCORE_IPSEC_ADDR_MAX];
    uint8_t dst_lo[PROTOCORE_IPSEC_ADDR_MAX];
    uint8_t dst_hi[PROTOCORE_IPSEC_ADDR_MAX];
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
    IpsecPolicy entries[PROTOCORE_IPSEC_SPD_MAX];
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
    uint32_t spi;                          ///< the SA's SPI (its SAD key)
    uint8_t dst[PROTOCORE_IPSEC_ADDR_MAX]; ///< SA destination address
    uint8_t addr_len;                      ///< 4 or 16
    uint8_t key[PROTOCORE_ESP_KEY_LEN];    ///< AES-256 key (SK_ei / SK_er without salt)
    uint8_t salt[PROTOCORE_ESP_SALT_LEN];  ///< AES-GCM salt (the key's tail)
    uint32_t seq;                          ///< outbound: last sequence number issued (0 = none yet)
    EspReplay replay;                      ///< inbound: anti-replay window
    proto_bool inbound;                    ///< true = receive SA, false = send SA
    proto_bool valid;                      ///< false = free slot
} IpsecSaEntry;
/** @brief The active Security Association Database, keyed by SPI. */
typedef struct
{
    IpsecSaEntry entries[PROTOCORE_IPSEC_SAD_MAX];
    size_t count;
} IpsecSad;
/** @brief What protocore_ipsec_spd_init takes: spd. */
typedef struct
{
    IpsecSpd *spd;
} IpsecDbProtocoreIpsecSpdInitArgs;
/** @brief What protocore_ipsec_spd_add takes: spd, sel, action, sa_spi. */
typedef struct
{
    IpsecSpd *spd;
    const IpsecSelector *sel;
    IpsecAction action;
    uint32_t sa_spi; ///< for a PROTECT action, the SAD SPI to bind (ignored otherwise)
} IpsecDbProtocoreIpsecSpdAddArgs;
/** @brief What protocore_ipsec_spd_lookup takes: spd, flow. */
typedef struct
{
    const IpsecSpd *spd;
    const IpsecFlow *flow;
} IpsecDbProtocoreIpsecSpdLookupArgs;
/** @brief What protocore_ipsec_selector_match takes: sel, flow. */
typedef struct
{
    const IpsecSelector *sel;
    const IpsecFlow *flow;
} IpsecDbProtocoreIpsecSelectorMatchArgs;
/** @brief What protocore_ipsec_selector_from_ts takes: out, ts_src, ... */
typedef struct
{
    IpsecSelector *out;
    const IkeTrafficSelector *ts_src;
    const IkeTrafficSelector *ts_dst;
} IpsecDbProtocoreIpsecSelectorFromTsArgs;
/** @brief What protocore_ipsec_sad_init takes: sad. */
typedef struct
{
    IpsecSad *sad;
} IpsecDbProtocoreIpsecSadInitArgs;
/** @brief What protocore_ipsec_sad_add takes: sad, spi, dst, ... */
typedef struct
{
    IpsecSad *sad;
    uint32_t spi;
    const uint8_t *dst;
    uint8_t addr_len;
    const uint8_t *key;  ///< PROTOCORE_ESP_KEY_LEN bytes.
    const uint8_t *salt; ///< PROTOCORE_ESP_SALT_LEN bytes.
    proto_bool inbound;
} IpsecDbProtocoreIpsecSadAddArgs;
/** @brief What protocore_ipsec_sad_find takes: sad, spi. */
typedef struct
{
    IpsecSad *sad;
    uint32_t spi;
} IpsecDbProtocoreIpsecSadFindArgs;
/** @brief What protocore_ipsec_sad_remove takes: sad, spi. */
typedef struct
{
    IpsecSad *sad;
    uint32_t spi;
} IpsecDbProtocoreIpsecSadRemoveArgs;
/** @brief What protocore_ipsec_sad_next_seq takes: sa, seq_out. */
typedef struct
{
    IpsecSaEntry *sa;
    uint32_t *seq_out; ///< receives the sequence number to place in the packet
} IpsecDbProtocoreIpsecSadNextSeqArgs;
/**
 * @brief IPsec Security Policy Database (SPD) + Security Association Database (SAD) - RFC 4301.
 *
 * A caller sets the members a call takes, invokes it through ::IpsecDb with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   IpsecDb.protocore_ipsec_spd_init_args.spd = ...;
 *   IpsecDb.protocore_ipsec_spd_init(work);
 *
 * @var IpsecDbNs::protocore_ipsec_spd_init_args  what protocore_ipsec_spd_init takes: spd
 * @var IpsecDbNs::protocore_ipsec_spd_add_args  what protocore_ipsec_spd_add takes: spd, sel, action, sa_spi
 * @var IpsecDbNs::protocore_ipsec_spd_lookup_args  what protocore_ipsec_spd_lookup takes: spd, flow
 * @var IpsecDbNs::protocore_ipsec_selector_match_args  what protocore_ipsec_selector_match takes: sel, flow
 * @var IpsecDbNs::protocore_ipsec_selector_from_ts_args  what protocore_ipsec_selector_from_ts takes: out, ts_src,
 * @var IpsecDbNs::protocore_ipsec_sad_init_args  what protocore_ipsec_sad_init takes: sad
 * @var IpsecDbNs::protocore_ipsec_sad_add_args  what protocore_ipsec_sad_add takes: sad, spi, dst,
 * @var IpsecDbNs::protocore_ipsec_sad_find_args  what protocore_ipsec_sad_find takes: sad, spi
 * @var IpsecDbNs::protocore_ipsec_sad_remove_args  what protocore_ipsec_sad_remove takes: sad, spi
 * @var IpsecDbNs::protocore_ipsec_sad_next_seq_args  what protocore_ipsec_sad_next_seq takes: sa, seq_out
 * @var IpsecDbNs::ok  true on success, false if spd is full or an argument is null
 * @var IpsecDbNs::ptr  the matching policy, or nullptr if none matches (the caller drops, ...
 * @var IpsecDbNs::sa  the SAD slot a find matched or an add filled, or nullptr
 * @var IpsecDbNs::protocore_ipsec_spd_init  empty an SPD (no policies)
 * @var IpsecDbNs::protocore_ipsec_spd_add  append a policy to the SPD (order is significant - first match wins ...
 * @var IpsecDbNs::protocore_ipsec_spd_lookup  find the first SPD policy whose selector matches flow (RFC 4301 ...
 * @var IpsecDbNs::protocore_ipsec_selector_match  true iff flow falls inside sel (family, protocol, address ranges, ...
 * @var IpsecDbNs::protocore_ipsec_selector_from_ts  fill out from an IKEv2-negotiated TSi / TSr pair (RFC 4301 §4.4.1
 * ...
 * @var IpsecDbNs::protocore_ipsec_sad_init  empty a SAD (no SAs)
 * @var IpsecDbNs::protocore_ipsec_sad_add  install a Security Association keyed by spi. An inbound SA's ...
 * @var IpsecDbNs::protocore_ipsec_sad_find  look up a valid SA by SPI (inbound ESP demux, RFC 4301 §4.1). ...
 * @var IpsecDbNs::protocore_ipsec_sad_remove  remove the SA with spi (e.g. on an IKE DELETE). true if one was ...
 * @var IpsecDbNs::protocore_ipsec_sad_next_seq  allocate the next outbound sequence number for sa (RFC 4303 §3.3.3, ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    IpsecDbProtocoreIpsecSpdInitArgs protocore_ipsec_spd_init_args;
    IpsecDbProtocoreIpsecSpdAddArgs protocore_ipsec_spd_add_args;
    IpsecDbProtocoreIpsecSpdLookupArgs protocore_ipsec_spd_lookup_args;
    IpsecDbProtocoreIpsecSelectorMatchArgs protocore_ipsec_selector_match_args;
    IpsecDbProtocoreIpsecSelectorFromTsArgs protocore_ipsec_selector_from_ts_args;
    IpsecDbProtocoreIpsecSadInitArgs protocore_ipsec_sad_init_args;
    IpsecDbProtocoreIpsecSadAddArgs protocore_ipsec_sad_add_args;
    IpsecDbProtocoreIpsecSadFindArgs protocore_ipsec_sad_find_args;
    IpsecDbProtocoreIpsecSadRemoveArgs protocore_ipsec_sad_remove_args;
    IpsecDbProtocoreIpsecSadNextSeqArgs protocore_ipsec_sad_next_seq_args;
    proto_bool ok;
    const IpsecPolicy *ptr;
    IpsecSaEntry *sa;
} IpsecDbVars;

/** @brief The operands and the outcome. */
extern IpsecDbVars IpsecDbV;

/** @brief The entries. */
typedef struct
{
    void (*const protocore_ipsec_spd_init)(uint8_t *restrict work);
    void (*const protocore_ipsec_spd_add)(uint8_t *restrict work);
    void (*const protocore_ipsec_spd_lookup)(uint8_t *restrict work);
    void (*const protocore_ipsec_selector_match)(uint8_t *restrict work);
    void (*const protocore_ipsec_selector_from_ts)(uint8_t *restrict work);
    void (*const protocore_ipsec_sad_init)(uint8_t *restrict work);
    void (*const protocore_ipsec_sad_add)(uint8_t *restrict work);
    void (*const protocore_ipsec_sad_find)(uint8_t *restrict work);
    void (*const protocore_ipsec_sad_remove)(uint8_t *restrict work);
    void (*const protocore_ipsec_sad_next_seq)(uint8_t *restrict work);
} IpsecDbNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in IpsecDbV or a region of the borrow at a fixed offset.
void protocore_ipsec_db_protocore_ipsec_spd_init(uint8_t *restrict work);
void protocore_ipsec_db_protocore_ipsec_spd_add(uint8_t *restrict work);
void protocore_ipsec_db_protocore_ipsec_spd_lookup(uint8_t *restrict work);
void protocore_ipsec_db_protocore_ipsec_selector_match(uint8_t *restrict work);
void protocore_ipsec_db_protocore_ipsec_selector_from_ts(uint8_t *restrict work);
void protocore_ipsec_db_protocore_ipsec_sad_init(uint8_t *restrict work);
void protocore_ipsec_db_protocore_ipsec_sad_add(uint8_t *restrict work);
void protocore_ipsec_db_protocore_ipsec_sad_find(uint8_t *restrict work);
void protocore_ipsec_db_protocore_ipsec_sad_remove(uint8_t *restrict work);
void protocore_ipsec_db_protocore_ipsec_sad_next_seq(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `IpsecDb.protocore_ipsec_spd_init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const IpsecDbNs IpsecDb __attribute__((unused)) = {
    .protocore_ipsec_spd_init = protocore_ipsec_db_protocore_ipsec_spd_init,
    .protocore_ipsec_spd_add = protocore_ipsec_db_protocore_ipsec_spd_add,
    .protocore_ipsec_spd_lookup = protocore_ipsec_db_protocore_ipsec_spd_lookup,
    .protocore_ipsec_selector_match = protocore_ipsec_db_protocore_ipsec_selector_match,
    .protocore_ipsec_selector_from_ts = protocore_ipsec_db_protocore_ipsec_selector_from_ts,
    .protocore_ipsec_sad_init = protocore_ipsec_db_protocore_ipsec_sad_init,
    .protocore_ipsec_sad_add = protocore_ipsec_db_protocore_ipsec_sad_add,
    .protocore_ipsec_sad_find = protocore_ipsec_db_protocore_ipsec_sad_find,
    .protocore_ipsec_sad_remove = protocore_ipsec_db_protocore_ipsec_sad_remove,
    .protocore_ipsec_sad_next_seq = protocore_ipsec_db_protocore_ipsec_sad_next_seq,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_IKEV2

#endif // PROTOCORE_IPSEC_DB_H
