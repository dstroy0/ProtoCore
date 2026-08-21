// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_DTLS_HANDSHAKE_H
#define PROTOCORE_DTLS_HANDSHAKE_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file dtls_handshake.h
 * @brief DTLS 1.3 handshake framing and reliability (RFC 9147 §5, §7).
 *
 * The datagram-reliability layer that sits between the DTLS record layer (protocore_dtls_record) and the
 * reused TLS 1.3 message builders (protocore_tls13_msg). TLS 1.3 assumes an in-order reliable byte stream;
 * DTLS carries the same handshake messages over lossy, reorderable datagrams, so each message gains
 * a 12-byte DTLS handshake header (RFC 9147 §5.2) that lets a fragment be placed independently of
 * the record that carried it, and lost flights are recovered with acknowledgements (§7) rather than
 * TCP retransmission.
 *
 * This file is pure framing - no crypto state, no sockets. It provides:
 * - the 12-byte handshake header (@ref protocore_dtls_hs_header_parse / @ref protocore_dtls_hs_frag_build);
 * - overlap-tolerant message reassembly (@ref DtlsHsReasm), modelled on the QUIC CRYPTO-stream
 * reassembler - a fragment may arrive split, duplicated, or overlapping (§5.4);
 * - the ACK message (@ref protocore_dtls_ack_build / @ref protocore_dtls_ack_parse, content type 26, §7);
 * - the stateless HelloRetryRequest cookie (@ref protocore_dtls_cookie_make / @ref protocore_dtls_cookie_verify,
 * the §5.1 return-routability / anti-amplification defense).
 *
 * The handshake state machine that drives these (flights, epochs, PTO) is protocore_dtls_conn; the TLS 1.3
 * message bodies and key schedule are reused verbatim from the HTTP/3 stack (protocore_tls13_msg, protocore_tls13_kdf).
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_DTLS_HANDSHAKE_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/** @brief DTLS handshake header length: msg_type(1) + length(3) + message_seq(2) + fragment_offset(3)
 *         + fragment_length(3) = 12 bytes (RFC 9147 §5.2). */
#define PROTOCORE_DTLS_HS_HDR_LEN 12

/** @brief message_hash synthetic-message type used when wrapping ClientHello1 for a HelloRetryRequest
 *         transcript (RFC 8446 §4.4.1). Framing constant; the transcript itself lives in protocore_dtls_conn. */
#define PROTOCORE_DTLS_HS_TYPE_MESSAGE_HASH 254

/** @brief Max distinct byte ranges tracked while reassembling one message (bounds the work an
 *         adversary can force by sending maximally fragmented flights). */
#define PROTOCORE_DTLS_HS_REASM_MAX_RANGES 8

/** @brief Maximum cookie length this implementation emits / accepts. Overhead is 43 bytes
 *         (version + timestamp + payload_len + HMAC); the rest is available for the payload. */
#define PROTOCORE_DTLS_COOKIE_MAX 128

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
 * @brief Reassembles the fragments of a single handshake message into a contiguous body.
 *
 * Fragments may arrive out of order, duplicated, or with overlapping ranges (RFC 9147 §5.4 requires
 * an implementation to handle overlap). Received byte ranges are merged into a bounded interval list;
 * the message is complete when a single interval covers [0, length).
 */
typedef struct
{
    proto_bool active;   ///< false until the first fragment of the target message is seen
    proto_bool have_len; ///< true once a fragment established the full message length
    uint8_t msg_type;    ///< HandshakeType of the message being reassembled
    uint16_t msg_seq;    ///< the message sequence number this reassembler accepts
    uint32_t length;     ///< full body length (from the first non-empty fragment)
    uint8_t *buf;        ///< caller-provided body buffer (>= length bytes)
    size_t buf_cap;      ///< capacity of @ref buf
    uint32_t range_lo[PROTOCORE_DTLS_HS_REASM_MAX_RANGES]; ///< received interval starts
    uint32_t range_hi[PROTOCORE_DTLS_HS_REASM_MAX_RANGES]; ///< received interval ends (exclusive)
    uint8_t range_count;                                   ///< number of active intervals
} DtlsHsReasm;

/** @brief A record identified for acknowledgement: (epoch, sequence_number), 8 bytes each on the
 *         wire (RFC 9147 §7). */
typedef struct
{
    uint64_t epoch;
    uint64_t seq;
} DtlsRecordNumber;

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    size_t (*header_parse)(uint8_t *restrict, const uint8_t *, size_t, DtlsHsHeader *);
    size_t (*frag_build)(uint8_t *restrict, uint8_t, uint16_t, uint32_t, uint32_t, const uint8_t *, uint32_t, uint8_t *,
                         size_t);
    void (*reasm_init)(uint8_t *restrict, DtlsHsReasm *, uint16_t, uint8_t *, size_t);
    size_t (*reasm_add)(uint8_t *restrict, DtlsHsReasm *, const DtlsHsHeader *);
    size_t (*ack_build)(uint8_t *restrict, const DtlsRecordNumber *, size_t, uint8_t *, size_t);
    proto_bool (*ack_parse)(uint8_t *restrict, const uint8_t *, size_t, DtlsRecordNumber *, size_t, size_t *);
    size_t (*cookie_make)(uint8_t *restrict, uint8_t *, const uint8_t *, uint64_t, const uint8_t *, size_t,
                          const uint8_t *, size_t, uint8_t *, size_t);
    proto_bool (*cookie_verify)(uint8_t *restrict, uint8_t *, const uint8_t *, uint64_t, uint64_t, const uint8_t *,
                                size_t, const uint8_t *, size_t, uint8_t *, size_t, size_t *);
} DtlsHandshakeNs;
PROTOCORE_NS_LAYOUT(DtlsHandshakeNs, header_parse, frag_build, reasm_init, reasm_add, ack_build, ack_parse, cookie_make,
                    cookie_verify);

/**
 * @brief The 12-byte DTLS handshake header; bytes consumed, or 0 if truncated.
 * @param work PROTOCORE_DTLS_HANDSHAKE_BORROW bytes the caller took. Not held past the call.
 * @param p P
 * @param len Len
 * @param out Out
 * @return The size_t.
 */
size_t protocore_dtls_handshake_header_parse(uint8_t *restrict work, const uint8_t *p, size_t len, DtlsHsHeader *out);
/**
 * @brief One handshake fragment, header and body; bytes written, or 0 on .
 * @param work PROTOCORE_DTLS_HANDSHAKE_BORROW bytes the caller took. Not held past the call.
 * @param msg_type Msg type
 * @param msg_seq Msg seq
 * @param full_len Full len
 * @param frag_offset Frag offset
 * @param frag Frag
 * @param frag_len Frag len
 * @param out Out
 * @param out_cap Out cap
 * @return The size_t.
 */
size_t protocore_dtls_handshake_frag_build(uint8_t *restrict work, uint8_t msg_type, uint16_t msg_seq,
                                           uint32_t full_len, uint32_t frag_offset, const uint8_t *frag,
                                           uint32_t frag_len, uint8_t *out, size_t out_cap);
/**
 * @brief Bind a reassembler to a caller buffer for one message sequence .
 * @param work PROTOCORE_DTLS_HANDSHAKE_BORROW bytes the caller took. Not held past the call.
 * @param r R
 * @param msg_seq Msg seq
 * @param buf Buf
 * @param buf_cap Buf cap
 */
void protocore_dtls_handshake_reasm_init(uint8_t *restrict work, DtlsHsReasm *r, uint16_t msg_seq, uint8_t *buf,
                                         size_t buf_cap);
/**
 * @brief Add one fragment to the reassembly in progress.
 * @param work PROTOCORE_DTLS_HANDSHAKE_BORROW bytes the caller took. Not held past the call.
 * @param r R
 * @param frag Frag
 * @return The size_t.
 */
size_t protocore_dtls_handshake_reasm_add(uint8_t *restrict work, DtlsHsReasm *r, const DtlsHsHeader *frag);
/**
 * @brief An ACK body (RFC 9147 sec 7) over count record numbers.
 * @param work PROTOCORE_DTLS_HANDSHAKE_BORROW bytes the caller took. Not held past the call.
 * @param nums Nums
 * @param count Count
 * @param out Out
 * @param out_cap Out cap
 * @return The size_t.
 */
size_t protocore_dtls_handshake_ack_build(uint8_t *restrict work, const DtlsRecordNumber *nums, size_t count,
                                          uint8_t *out, size_t out_cap);
/**
 * @brief The same body back into at most out_cap record numbers.
 * @param work PROTOCORE_DTLS_HANDSHAKE_BORROW bytes the caller took. Not held past the call.
 * @param body Body
 * @param len Len
 * @param out Out
 * @param out_cap Out cap
 * @param out_count Out count
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_dtls_handshake_ack_parse(uint8_t *restrict work, const uint8_t *body, size_t len,
                                              DtlsRecordNumber *out, size_t out_cap, size_t *out_count);
/**
 * @brief A stateless HelloRetryRequest cookie bound to the client address.
 * @param work PROTOCORE_DTLS_HANDSHAKE_BORROW bytes the caller took. Not held past the call.
 * @param mac_work Mac work
 * @param protocore_hmac_key 32 bytes
 * @param timestamp Timestamp
 * @param payload Payload
 * @param payload_len Payload len
 * @param client_addr Client addr
 * @param addr_len Addr len
 * @param out Out
 * @param out_cap Out cap
 * @return The size_t.
 */
size_t protocore_dtls_handshake_cookie_make(uint8_t *restrict work, uint8_t *mac_work,
                                            const uint8_t *protocore_hmac_key, uint64_t timestamp,
                                            const uint8_t *payload, size_t payload_len, const uint8_t *client_addr,
                                            size_t addr_len, uint8_t *out, size_t out_cap);
/**
 * @brief The same cookie back, checking the address binding and the age .
 * @param work PROTOCORE_DTLS_HANDSHAKE_BORROW bytes the caller took. Not held past the call.
 * @param mac_work Mac work
 * @param protocore_hmac_key 32 bytes
 * @param now Now
 * @param max_age Max age
 * @param client_addr Client addr
 * @param addr_len Addr len
 * @param cookie Cookie
 * @param cookie_len Cookie len
 * @param payload_out Payload out
 * @param payload_cap Payload cap
 * @param payload_len_out Payload len out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_dtls_handshake_cookie_verify(uint8_t *restrict work, uint8_t *mac_work,
                                                  const uint8_t *protocore_hmac_key, uint64_t now, uint64_t max_age,
                                                  const uint8_t *client_addr, size_t addr_len, const uint8_t *cookie,
                                                  size_t cookie_len, uint8_t *payload_out, size_t payload_cap,
                                                  size_t *payload_len_out);

/** @brief Module namespace. */
PROTOCORE_NS DtlsHandshakeNs DtlsHandshake PROTOCORE_UNUSED = {.header_parse = protocore_dtls_handshake_header_parse,
                                                               .frag_build = protocore_dtls_handshake_frag_build,
                                                               .reasm_init = protocore_dtls_handshake_reasm_init,
                                                               .reasm_add = protocore_dtls_handshake_reasm_add,
                                                               .ack_build = protocore_dtls_handshake_ack_build,
                                                               .ack_parse = protocore_dtls_handshake_ack_parse,
                                                               .cookie_make = protocore_dtls_handshake_cookie_make,
                                                               .cookie_verify = protocore_dtls_handshake_cookie_verify};

PROTOCORE_END_DECLS

#endif // PROTOCORE_DTLS_HANDSHAKE_H
