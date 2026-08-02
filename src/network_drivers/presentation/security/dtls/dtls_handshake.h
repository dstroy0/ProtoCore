// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_dtls_handshake.h
 * @brief DTLS 1.3 handshake framing and reliability (RFC 9147 §5, §7).
 *
 * The datagram-reliability layer that sits between the DTLS record layer (pc_dtls_record) and the
 * reused TLS 1.3 message builders (pc_tls13_msg). TLS 1.3 assumes an in-order reliable byte stream;
 * DTLS carries the same handshake messages over lossy, reorderable datagrams, so each message gains
 * a 12-byte DTLS handshake header (RFC 9147 §5.2) that lets a fragment be placed independently of
 * the record that carried it, and lost flights are recovered with acknowledgements (§7) rather than
 * TCP retransmission.
 *
 * This file is pure framing - no crypto state, no sockets. It provides:
 *   - the 12-byte handshake header (@ref pc_dtls_hs_header_parse / @ref pc_dtls_hs_frag_build);
 *   - overlap-tolerant message reassembly (@ref DtlsHsReasm), modelled on the QUIC CRYPTO-stream
 *     reassembler - a fragment may arrive split, duplicated, or overlapping (§5.4);
 *   - the ACK message (@ref pc_dtls_ack_build / @ref pc_dtls_ack_parse, content type 26, §7);
 *   - the stateless HelloRetryRequest cookie (@ref pc_dtls_cookie_make / @ref pc_dtls_cookie_verify,
 *     the §5.1 return-routability / anti-amplification defense).
 *
 * The handshake state machine that drives these (flights, epochs, PTO) is pc_dtls_conn; the TLS 1.3
 * message bodies and key schedule are reused verbatim from the HTTP/3 stack (pc_tls13_msg, pc_tls13_kdf).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DTLS_HANDSHAKE_H
#define PROTOCORE_DTLS_HANDSHAKE_H

#include "protocore_config.h"

#if PC_ENABLE_DTLS

/** @brief DTLS handshake header length: msg_type(1) + length(3) + message_seq(2) + fragment_offset(3)
 *         + fragment_length(3) = 12 bytes (RFC 9147 §5.2). */
#define PC_DTLS_HS_HDR_LEN 12

/** @brief message_hash synthetic-message type used when wrapping ClientHello1 for a HelloRetryRequest
 *         transcript (RFC 8446 §4.4.1). Framing constant; the transcript itself lives in pc_dtls_conn. */
#define PC_DTLS_HS_TYPE_MESSAGE_HASH 254

// ---------------------------------------------------------------------------
// Handshake message header (RFC 9147 §5.2)
// ---------------------------------------------------------------------------

/** @brief Parsed view of one DTLS handshake message fragment (fields point into the caller buffer). */
typedef struct
{
    uint8_t msg_type;        ///< HandshakeType (client_hello, server_hello, finished, ...)
    uint32_t length;         ///< full reassembled body length (uint24 on the wire)
    uint16_t msg_seq;        ///< handshake message sequence number
    uint32_t frag_offset;    ///< byte offset of this fragment within the body (uint24)
    uint32_t frag_length;    ///< length of this fragment (uint24)
    const uint8_t *fragment; ///< fragment bytes, into the input buffer
} DtlsHsHeader;

/**
 * @brief Parse one DTLS handshake message fragment.
 *
 * Validates that @p len holds the 12-byte header plus @c fragment_length bytes and that the fragment
 * lies within the declared message length.
 *
 * @return bytes consumed (PC_DTLS_HS_HDR_LEN + fragment_length), or 0 if malformed / truncated.
 */
size_t pc_dtls_hs_header_parse(const uint8_t *p, size_t len, DtlsHsHeader *out);

/**
 * @brief Build one DTLS handshake message fragment: the 12-byte header followed by the fragment body.
 *
 * @param msg_type     HandshakeType.
 * @param msg_seq      handshake message sequence number.
 * @param full_len     total (reassembled) message body length.
 * @param frag_offset  byte offset of this fragment within the body.
 * @param frag         fragment bytes.
 * @param frag_len     number of fragment bytes.
 * @return total bytes written (PC_DTLS_HS_HDR_LEN + @p frag_len), or 0 on overflow / range error.
 */
size_t pc_dtls_hs_frag_build(uint8_t msg_type, uint16_t msg_seq, uint32_t full_len, uint32_t frag_offset,
                             const uint8_t *frag, uint32_t frag_len, uint8_t *out, size_t out_cap);

// ---------------------------------------------------------------------------
// Message reassembly (RFC 9147 §5.4): overlap-tolerant, no heap
// ---------------------------------------------------------------------------

/** @brief Max distinct byte ranges tracked while reassembling one message (bounds the work an
 *         adversary can force by sending maximally fragmented flights). */
#define PC_DTLS_HS_REASM_MAX_RANGES 8

/**
 * @brief Reassembles the fragments of a single handshake message into a contiguous body.
 *
 * Fragments may arrive out of order, duplicated, or with overlapping ranges (RFC 9147 §5.4 requires
 * an implementation to handle overlap). Received byte ranges are merged into a bounded interval list;
 * the message is complete when a single interval covers [0, length).
 */
typedef struct
{
    proto_bool active;                              ///< false until the first fragment of the target message is seen
    proto_bool have_len;                            ///< true once a fragment established the full message length
    uint8_t msg_type;                               ///< HandshakeType of the message being reassembled
    uint16_t msg_seq;                               ///< the message sequence number this reassembler accepts
    uint32_t length;                                ///< full body length (from the first non-empty fragment)
    uint8_t *buf;                                   ///< caller-provided body buffer (>= length bytes)
    size_t buf_cap;                                 ///< capacity of @ref buf
    uint32_t range_lo[PC_DTLS_HS_REASM_MAX_RANGES]; ///< received interval starts
    uint32_t range_hi[PC_DTLS_HS_REASM_MAX_RANGES]; ///< received interval ends (exclusive)
    uint8_t range_count;                            ///< number of active intervals
} DtlsHsReasm;

/**
 * @brief Prepare a reassembler for the message numbered @p msg_seq into @p buf.
 * @param buf      body buffer the reassembled message is written into.
 * @param buf_cap  capacity of @p buf; a message longer than this is rejected.
 */
void pc_dtls_hs_reasm_init(DtlsHsReasm *r, uint16_t msg_seq, uint8_t *buf, size_t buf_cap);

/**
 * @brief Feed one parsed fragment into the reassembler.
 *
 * Fragments whose @c msg_seq differs from the target are ignored (return 0): the state machine, not
 * the reassembler, decides what to do with past/future messages.
 *
 * @return 1 if the message is now complete (@ref DtlsHsReasm::buf holds @ref DtlsHsReasm::length
 *         bytes), 0 if accepted but incomplete (or not this message), -1 on error (length overflow,
 *         inconsistent length, or too many distinct ranges).
 */
int pc_dtls_hs_reasm_add(DtlsHsReasm *r, const DtlsHsHeader *frag);

// ---------------------------------------------------------------------------
// ACK message (RFC 9147 §7): content type 26
// ---------------------------------------------------------------------------

/** @brief A record identified for acknowledgement: (epoch, sequence_number), 8 bytes each on the
 *         wire (RFC 9147 §7). */
typedef struct
{
    uint64_t epoch;
    uint64_t seq;
} DtlsRecordNumber;

/**
 * @brief Build an ACK message body (RFC 9147 §7): uint16 length prefix then @p count 16-byte record
 *        numbers, big-endian epoch then sequence_number.
 * @return body bytes written (2 + 16*count), or 0 on overflow.
 */
size_t pc_dtls_ack_build(const DtlsRecordNumber *nums, size_t count, uint8_t *out, size_t out_cap);

/**
 * @brief Parse an ACK message body into record numbers.
 * @param out        destination array.
 * @param out_cap    capacity of @p out in entries.
 * @param out_count  receives the number of record numbers parsed.
 * @return true on a well-formed ACK that fits in @p out; false if malformed or too many entries.
 */
proto_bool pc_dtls_ack_parse(const uint8_t *body, size_t len, DtlsRecordNumber *out, size_t out_cap, size_t *out_count);

// ---------------------------------------------------------------------------
// HelloRetryRequest cookie (RFC 9147 §5.1): stateless return-routability
// ---------------------------------------------------------------------------

/** @brief Maximum cookie length this implementation emits / accepts. Overhead is 43 bytes
 *         (version + timestamp + payload_len + HMAC); the rest is available for the payload. */
#define PC_DTLS_COOKIE_MAX 128

/**
 * @brief Build a stateless HRR cookie binding the client address and an opaque server payload.
 *
 * Wire layout: version(1)=1 || timestamp_be(8) || payload_len(2) || payload || HMAC-SHA256(32).
 * The MAC covers version || timestamp || @p client_addr || payload_len || payload, so the client
 * address is authenticated without being stored (RFC 9147 §5.1: embedding the apparent client
 * address stops an attacker forging a cookie for another peer). The @p payload carries whatever the
 * state machine needs to resume statelessly after the retry (e.g. the ClientHello1 transcript hash
 * plus the selected group).
 *
 * @param pc_hmac_key     32-byte server secret (rotate periodically per §5.1).
 * @param timestamp    monotonic value stamped into the cookie for freshness checks.
 * @param payload      opaque server state to carry through the retry.
 * @param client_addr  serialized client address, mixed into the MAC.
 * @return cookie bytes written, or 0 on overflow / oversized payload.
 */
size_t pc_dtls_cookie_make(const uint8_t pc_hmac_key[32], uint64_t timestamp, const uint8_t *payload,
                           size_t payload_len, const uint8_t *client_addr, size_t addr_len, uint8_t *out,
                           size_t out_cap);

/**
 * @brief Validate a cookie echoed in a second ClientHello and recover its payload.
 *
 * Recomputes the MAC over the cookie fields plus @p client_addr and compares in constant time, then
 * checks the timestamp is within (@p now - @p max_age, @p now]. A cookie minted for a different
 * client address fails the MAC check.
 *
 * @param now          current timestamp in the same units as @ref pc_dtls_cookie_make.
 * @param max_age      maximum accepted age; 0 disables the freshness check.
 * @param payload_out  receives the carried payload on success.
 * @param payload_cap  capacity of @p payload_out.
 * @param payload_len_out  receives the payload length on success.
 * @return true if the cookie is authentic, fresh, and bound to @p client_addr.
 */
proto_bool pc_dtls_cookie_verify(const uint8_t pc_hmac_key[32], uint64_t now, uint64_t max_age,
                                 const uint8_t *client_addr, size_t addr_len, const uint8_t *cookie, size_t cookie_len,
                                 uint8_t *payload_out, size_t payload_cap, size_t *payload_len_out);

#endif // PC_ENABLE_DTLS
#endif // PROTOCORE_DTLS_HANDSHAKE_H
