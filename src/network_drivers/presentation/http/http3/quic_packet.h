// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_quic_packet.h
 * @brief QUIC packet headers and packet-number coding (RFC 9000 sec 17).
 *
 * The structural, version-independent layer of a QUIC packet: the long-header form (Initial /
 * 0-RTT / Handshake / Retry, plus the Version Negotiation packet whose Version is 0) and the
 * short-header 1-RTT form, and the packet-number truncation coding (sec 17.1, Appendix A.2/A.3).
 *
 * This is the unprotected structure only - it parses and builds the header fields that are not
 * covered by header protection (header form, version, connection IDs) and codes packet numbers.
 * Packet protection (AEAD) and header protection are layered on top by the QUIC crypto module.
 * Pure, zero heap, host-tested against the RFC worked examples.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_QUIC_PACKET_H
#define PROTOCORE_QUIC_PACKET_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_HTTP3

#define QUIC_VERSION_1 0x00000001u ///< RFC 9000
#define QUIC_MAX_CID_LEN 20        ///< maximum connection-ID length in QUIC version 1

/** @brief Long-header packet types (RFC 9000 sec 17.2, Table 5). */
#define QUIC_LP_INITIAL 0x00
#define QUIC_LP_0RTT 0x01
#define QUIC_LP_HANDSHAKE 0x02
#define QUIC_LP_RETRY 0x03

/** @brief A parsed long header (invariant fields). A Version of 0 marks a Version Negotiation. */
typedef struct
{
    uint8_t first;                  ///< raw first byte
    uint8_t type;                   ///< long packet type (first & 0x30) >> 4; meaningful when version != 0
    uint32_t version;               ///< QUIC version; 0 = Version Negotiation
    uint8_t dcid_len;               ///< Destination Connection ID length
    uint8_t dcid[QUIC_MAX_CID_LEN]; ///< Destination Connection ID
    uint8_t scid_len;               ///< Source Connection ID length
    uint8_t scid[QUIC_MAX_CID_LEN]; ///< Source Connection ID
    size_t hdr_len;                 ///< bytes consumed up to the start of the type-specific payload
} QuicLongHeader;

/** @brief A parsed short header (1-RTT). The DCID length is known locally, not on the wire. */
typedef struct
{
    uint8_t first;                  ///< raw first byte
    uint8_t spin;                   ///< latency spin bit (0x20)
    uint8_t key_phase;              ///< key-phase bit (0x04)
    uint8_t pn_len;                 ///< packet-number length in bytes (1..4)
    uint8_t dcid_len;               ///< Destination Connection ID length (caller-supplied)
    uint8_t dcid[QUIC_MAX_CID_LEN]; ///< Destination Connection ID
    size_t hdr_len;                 ///< bytes up to the (protected) Packet Number field
} QuicShortHeader;

/** @brief True if byte 0 selects the long header form (0x80 set). */
proto_bool pc_quic_is_long_header(uint8_t first);

/** @brief Parse a long header. @return false if truncated or a connection ID exceeds 20 bytes. */
proto_bool pc_quic_parse_long_header(const uint8_t *buf, size_t len, QuicLongHeader *out);

/**
 * @brief Build a long header's invariant fields (first byte .. Source Connection ID). The caller
 * appends the type-specific fields (Token / Length / Packet Number / payload). @p pn_len is the
 * packet-number length in bytes (1..4); the reserved bits are written as 0 (pre-protection).
 * @return bytes written, or 0 on overflow / bad length.
 */
size_t pc_quic_build_long_header(uint8_t *out, size_t cap, uint8_t type, uint32_t version, const uint8_t *dcid,
                                 uint8_t dcid_len, const uint8_t *scid, uint8_t scid_len, uint8_t pn_len);

/** @brief Parse a short (1-RTT) header given the locally chosen @p dcid_len. @return false if truncated. */
proto_bool pc_quic_parse_short_header(const uint8_t *buf, size_t len, uint8_t dcid_len, QuicShortHeader *out);

/**
 * @brief Build a Version Negotiation packet (RFC 9000 sec 17.2.1): Version 0 and the list of
 * @p versions the server supports. The caller passes the connection IDs already echoed (its DCID =
 * the received SCID, its SCID = the received DCID). @return bytes written, or 0 on overflow.
 */
size_t pc_quic_build_version_negotiation(uint8_t *out, size_t cap, const uint8_t *dcid, uint8_t dcid_len,
                                         const uint8_t *scid, uint8_t scid_len, const uint32_t *versions,
                                         size_t nversions);

// --- Packet-number coding (RFC 9000 sec 17.1, Appendix A.2 / A.3) -----------------------------

/** @brief Packet-number length in bytes (1..4) for @p full_pn; @p largest_acked < 0 means none. */
uint8_t pc_quic_pn_length(uint64_t full_pn, int64_t largest_acked);

/** @brief Encode @p full_pn truncated to pc_quic_pn_length() bytes, big-endian. @return bytes written or 0. */
size_t pc_quic_pn_encode(uint8_t *out, size_t cap, uint64_t full_pn, int64_t largest_acked);

/** @brief Recover the full packet number from a @p truncated_pn of @p pn_nbits bits (Appendix A.3). */
uint64_t pc_quic_pn_decode(uint64_t largest_pn, uint64_t truncated_pn, uint8_t pn_nbits);

#endif // PC_ENABLE_HTTP3

PROTO_END_DECLS

#endif // PROTOCORE_QUIC_PACKET_H
