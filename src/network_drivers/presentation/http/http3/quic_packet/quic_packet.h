// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_QUIC_PACKET_H
#define PROTOCORE_QUIC_PACKET_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file quic_packet.h
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
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_QUIC_PACKET_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

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

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*is_long_header)(uint8_t *, uint8_t);
    proto_bool (*parse_long_header)(uint8_t *, const uint8_t *, size_t, QuicLongHeader *);
    size_t (*build_long_header)(uint8_t *, uint8_t *, size_t, uint8_t, uint32_t, const uint8_t *, uint8_t,
                                const uint8_t *, uint8_t, uint8_t);
    proto_bool (*parse_short_header)(uint8_t *, const uint8_t *, size_t, uint8_t, QuicShortHeader *);
    size_t (*build_version_negotiation)(uint8_t *, uint8_t *, size_t, const uint8_t *, uint8_t, const uint8_t *,
                                        uint8_t, const uint32_t *, size_t);
    uint8_t (*pn_length)(uint8_t *, uint64_t, int64_t);
    size_t (*pn_encode)(uint8_t *, uint8_t *, size_t, uint64_t, int64_t);
    uint64_t (*pn_decode)(uint8_t *, uint64_t, uint64_t, uint8_t);
} QuicPacketNs;
PROTOCORE_NS_LAYOUT(QuicPacketNs, is_long_header, parse_long_header, build_long_header, parse_short_header,
                    build_version_negotiation, pn_length, pn_encode, pn_decode);

/**
 * @brief True if byte 0 selects the long header form (0x80 set).
 * @param work PROTOCORE_QUIC_PACKET_BORROW bytes the caller took. Not held past the call.
 * @param first First
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_quic_packet_is_long_header(uint8_t *work, uint8_t first);
/**
 * @brief Parse a long header. false if truncated or a connection ID exceeds .
 * @param work PROTOCORE_QUIC_PACKET_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param len Len
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_quic_packet_parse_long_header(uint8_t *work, const uint8_t *buf, size_t len, QuicLongHeader *out);
/**
 * @brief Build a long header's invariant fields (first byte .. Source .
 * @param work PROTOCORE_QUIC_PACKET_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param type Type
 * @param version Version
 * @param dcid Dcid
 * @param dcid_len Dcid len
 * @param scid Scid
 * @param scid_len Scid len
 * @param pn_len Pn len
 * @return The size_t.
 */
size_t protocore_quic_packet_build_long_header(uint8_t *work, uint8_t *out, size_t cap, uint8_t type, uint32_t version,
                                               const uint8_t *dcid, uint8_t dcid_len, const uint8_t *scid,
                                               uint8_t scid_len, uint8_t pn_len);
/**
 * @brief Parse a short (1-RTT) header given the locally chosen dcid_len. .
 * @param work PROTOCORE_QUIC_PACKET_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param len Len
 * @param dcid_len Dcid len
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_quic_packet_parse_short_header(uint8_t *work, const uint8_t *buf, size_t len, uint8_t dcid_len,
                                                    QuicShortHeader *out);
/**
 * @brief Build a Version Negotiation packet (RFC 9000 sec 17.2.1): Version 0 .
 * @param work PROTOCORE_QUIC_PACKET_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param dcid Dcid
 * @param dcid_len Dcid len
 * @param scid Scid
 * @param scid_len Scid len
 * @param versions Versions
 * @param nversions Nversions
 * @return The size_t.
 */
size_t protocore_quic_packet_build_version_negotiation(uint8_t *work, uint8_t *out, size_t cap, const uint8_t *dcid,
                                                       uint8_t dcid_len, const uint8_t *scid, uint8_t scid_len,
                                                       const uint32_t *versions, size_t nversions);
/**
 * @brief Packet-number length in bytes (1..4) for full_pn; largest_acked < 0 .
 * @param work PROTOCORE_QUIC_PACKET_BORROW bytes the caller took. Not held past the call.
 * @param full_pn Full pn
 * @param largest_acked Largest acked
 * @return The uint8_t.
 */
uint8_t protocore_quic_packet_pn_length(uint8_t *work, uint64_t full_pn, int64_t largest_acked);
/**
 * @brief Encode full_pn truncated to ::QuicPacketNs::pn_length bytes, .
 * @param work PROTOCORE_QUIC_PACKET_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param full_pn Full pn
 * @param largest_acked Largest acked
 * @return The size_t.
 */
size_t protocore_quic_packet_pn_encode(uint8_t *work, uint8_t *out, size_t cap, uint64_t full_pn,
                                       int64_t largest_acked);
/**
 * @brief Recover the full packet number from a truncated_pn of pn_nbits bits .
 * @param work PROTOCORE_QUIC_PACKET_BORROW bytes the caller took. Not held past the call.
 * @param largest_pn Largest pn
 * @param truncated_pn Truncated pn
 * @param pn_nbits Pn nbits
 * @return The uint64_t.
 */
uint64_t protocore_quic_packet_pn_decode(uint8_t *work, uint64_t largest_pn, uint64_t truncated_pn, uint8_t pn_nbits);

/** @brief Module namespace. */
PROTOCORE_NS QuicPacketNs QuicPacket PROTOCORE_UNUSED = {.is_long_header = protocore_quic_packet_is_long_header,
                                                         .parse_long_header = protocore_quic_packet_parse_long_header,
                                                         .build_long_header = protocore_quic_packet_build_long_header,
                                                         .parse_short_header = protocore_quic_packet_parse_short_header,
                                                         .build_version_negotiation =
                                                             protocore_quic_packet_build_version_negotiation,
                                                         .pn_length = protocore_quic_packet_pn_length,
                                                         .pn_encode = protocore_quic_packet_pn_encode,
                                                         .pn_decode = protocore_quic_packet_pn_decode};

PROTOCORE_END_DECLS

#endif // PROTOCORE_QUIC_PACKET_H
