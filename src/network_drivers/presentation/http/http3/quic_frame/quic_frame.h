// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_QUIC_FRAME_H
#define PROTOCORE_QUIC_FRAME_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file quic_frame.h
 * @brief QUIC frame parsing and building (RFC 9000 sec 19).
 *
 * The payload of a QUIC packet is a sequence of frames, each `Frame Type (i)` followed by
 * type-specific fields coded with QUIC varints. This module reads one frame at a time into a
 * tagged QuicFrameHeader and builds the frames a server sends. It covers the frames a minimal HTTP/3
 * server needs - PADDING, PING, ACK, CRYPTO, STREAM, MAX_DATA, CONNECTION_CLOSE, HANDSHAKE_DONE -
 * and reports the frame type for anything else so the caller can decide.
 *
 * Data-bearing frames (CRYPTO / STREAM / CONNECTION_CLOSE reason) point into the caller's packet
 * buffer; nothing is copied. Pure, zero heap, host-tested.
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_QUIC_FRAME_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

#define QUIC_FT_PADDING 0x00
#define QUIC_FT_PING 0x01
#define QUIC_FT_ACK 0x02 ///< 0x02 (no ECN) .. 0x03 (with ECN counts)
#define QUIC_FT_ACK_ECN 0x03
#define QUIC_FT_CRYPTO 0x06
#define QUIC_FT_STREAM 0x08 ///< 0x08..0x0f; low 3 bits are OFF (0x04) / LEN (0x02) / FIN (0x01)
#define QUIC_FT_MAX_DATA 0x10
#define QUIC_FT_CONNECTION_CLOSE 0x1c     ///< transport-level close (carries the triggering frame type)
#define QUIC_FT_CONNECTION_CLOSE_APP 0x1d ///< application-level close
#define QUIC_FT_HANDSHAKE_DONE 0x1e

// Frames the minimal server does not act on but MUST still parse (skip) so a well-formed frame from
// a real client is not rejected as a FRAME_ENCODING_ERROR (RFC 9000 sec 12.4). Grouped by wire shape
// in ::QuicFrameNs::parse: 3 varints (RESET_STREAM), 2 varints (STOP_SENDING / MAX_STREAM_DATA /
// STREAM_DATA_BLOCKED), 1 varint (MAX_STREAMS / DATA_BLOCKED / STREAMS_BLOCKED / RETIRE_CONNECTION_ID),
// and the length-prefixed / fixed-width shapes (NEW_TOKEN, NEW_CONNECTION_ID, PATH_CHALLENGE/RESPONSE).
#define QUIC_FT_RESET_STREAM 0x04
#define QUIC_FT_STOP_SENDING 0x05
#define QUIC_FT_NEW_TOKEN 0x07
#define QUIC_FT_MAX_STREAM_DATA 0x11
#define QUIC_FT_MAX_STREAMS_BIDI 0x12
#define QUIC_FT_MAX_STREAMS_UNI 0x13
#define QUIC_FT_DATA_BLOCKED 0x14
#define QUIC_FT_STREAM_DATA_BLOCKED 0x15
#define QUIC_FT_STREAMS_BLOCKED_BIDI 0x16
#define QUIC_FT_STREAMS_BLOCKED_UNI 0x17
#define QUIC_FT_NEW_CONNECTION_ID 0x18
#define QUIC_FT_RETIRE_CONNECTION_ID 0x19
#define QUIC_FT_PATH_CHALLENGE 0x1a
#define QUIC_FT_PATH_RESPONSE 0x1b

/** @brief STREAM frame type bits. */
#define QUIC_STREAM_FIN 0x01
#define QUIC_STREAM_LEN 0x02
#define QUIC_STREAM_OFF 0x04

/** @brief Transport error codes for CONNECTION_CLOSE (RFC 9000 sec 20.1). */
#define QUIC_ERR_NO_ERROR 0x00
#define QUIC_ERR_INTERNAL 0x01
#define QUIC_ERR_FLOW_CONTROL 0x03
#define QUIC_ERR_STREAM_LIMIT 0x04
#define QUIC_ERR_FRAME_ENCODING 0x07     ///< a frame could not be decoded
#define QUIC_ERR_PROTOCOL_VIOLATION 0x0a ///< a frame/packet violated the protocol
#define QUIC_ERR_APPLICATION 0x0c        ///< the application abandoned the connection (sec 20.1)
#define QUIC_ERR_CRYPTO_BASE 0x0100      ///< 0x0100 + the TLS alert code (RFC 9001 sec 4.8)

/** @brief ACK payload (RFC 9000 sec 19.3). */
typedef struct
{
    uint64_t largest;     ///< Largest Acknowledged
    uint64_t delay;       ///< ACK Delay (encoded units)
    uint64_t range_count; ///< number of additional ACK Ranges (skipped, but counted)
    uint64_t first_range; ///< First ACK Range
} QuicAckFrame;

/** @brief CRYPTO payload (RFC 9000 sec 19.6). @c data aliases the input buffer. */
typedef struct
{
    uint64_t offset;
    uint64_t length;
    const uint8_t *data;
} QuicCryptoFrame;

/** @brief STREAM payload (RFC 9000 sec 19.8). @c data aliases the input buffer. */
typedef struct
{
    uint64_t id;
    uint64_t offset; ///< 0 when the OFF bit is clear
    uint64_t length;
    const uint8_t *data;
    uint8_t fin;
} QuicStreamFrame;

/** @brief MAX_DATA payload (RFC 9000 sec 19.9). */
typedef struct
{
    uint64_t max;
} QuicMaxDataFrame;

/** @brief CONNECTION_CLOSE payload (RFC 9000 sec 19.19). @c reason aliases the input buffer. */
typedef struct
{
    uint64_t error_code;
    uint64_t frame_type; ///< 0 for the application-level variant (0x1d)
    uint64_t reason_len;
    const uint8_t *reason;
    uint8_t app; ///< 1 if this was the application-level close (0x1d)
} QuicCloseFrame;

/** @brief One parsed frame. Pointer fields alias the input buffer (not copied). */
typedef struct
{
    uint64_t type; ///< the frame type (STREAM reported as its exact 0x08..0x0f value)
    union {
        QuicAckFrame ack;
        QuicCryptoFrame crypto;
        QuicStreamFrame stream;
        QuicMaxDataFrame max_data;
        QuicCloseFrame close;
    };
} QuicFrameHeader;

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    size_t (*parse)(uint8_t *restrict, const uint8_t *, size_t, QuicFrameHeader *);
    size_t (*build_padding)(uint8_t *restrict, uint8_t *, size_t, size_t);
    size_t (*build_ping)(uint8_t *restrict, uint8_t *, size_t);
    size_t (*build_handshake_done)(uint8_t *restrict, uint8_t *, size_t);
    size_t (*build_ack)(uint8_t *restrict, uint8_t *, size_t, uint64_t, uint64_t, uint64_t);
    size_t (*build_crypto)(uint8_t *restrict, uint8_t *, size_t, uint64_t, const uint8_t *, size_t);
    size_t (*build_stream)(uint8_t *restrict, uint8_t *, size_t, uint64_t, uint64_t, const uint8_t *, size_t,
                           proto_bool);
    size_t (*build_max_data)(uint8_t *restrict, uint8_t *, size_t, uint64_t);
    size_t (*build_connection_close)(uint8_t *restrict, uint8_t *, size_t, proto_bool, uint64_t, uint64_t, const char *,
                                     size_t);
} QuicFrameNs;
PROTOCORE_NS_LAYOUT(QuicFrameNs, parse, build_padding, build_ping, build_handshake_done, build_ack, build_crypto,
                    build_stream, build_max_data, build_connection_close);

/**
 * @brief Parse one frame at buf. bytes consumed, or 0 on malformed / .
 * @param work PROTOCORE_QUIC_FRAME_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param len Len
 * @param out Out
 * @return The size_t.
 */
size_t protocore_quic_frame_parse(uint8_t *restrict work, const uint8_t *buf, size_t len, QuicFrameHeader *out);
/**
 * @brief N PADDING frames (n zero bytes). n, or 0 if it does not fit.
 * @param work PROTOCORE_QUIC_FRAME_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param n N
 * @return The size_t.
 */
size_t protocore_quic_frame_build_padding(uint8_t *restrict work, uint8_t *out, size_t cap, size_t n);
/**
 * @brief A PING frame.
 * @param work PROTOCORE_QUIC_FRAME_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @return The size_t.
 */
size_t protocore_quic_frame_build_ping(uint8_t *restrict work, uint8_t *out, size_t cap);
/**
 * @brief A HANDSHAKE_DONE frame.
 * @param work PROTOCORE_QUIC_FRAME_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @return The size_t.
 */
size_t protocore_quic_frame_build_handshake_done(uint8_t *restrict work, uint8_t *out, size_t cap);
/**
 * @brief A single-range ACK frame (ACK Range Count 0): Largest, ACK Delay, .
 * @param work PROTOCORE_QUIC_FRAME_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param largest Largest
 * @param delay Delay
 * @param first_range First range
 * @return The size_t.
 */
size_t protocore_quic_frame_build_ack(uint8_t *restrict work, uint8_t *out, size_t cap, uint64_t largest,
                                      uint64_t delay, uint64_t first_range);
/**
 * @brief A CRYPTO frame carrying len bytes at stream offset.
 * @param work PROTOCORE_QUIC_FRAME_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param offset Offset
 * @param data Data
 * @param len Len
 * @return The size_t.
 */
size_t protocore_quic_frame_build_crypto(uint8_t *restrict work, uint8_t *out, size_t cap, uint64_t offset,
                                         const uint8_t *data, size_t len);
/**
 * @brief A STREAM frame (LEN always set; OFF set when offset > 0; FIN per .
 * @param work PROTOCORE_QUIC_FRAME_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param id Id
 * @param offset Offset
 * @param data Data
 * @param len Len
 * @param fin Fin
 * @return The size_t.
 */
size_t protocore_quic_frame_build_stream(uint8_t *restrict work, uint8_t *out, size_t cap, uint64_t id, uint64_t offset,
                                         const uint8_t *data, size_t len, proto_bool fin);
/**
 * @brief A MAX_DATA frame.
 * @param work PROTOCORE_QUIC_FRAME_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param max Max
 * @return The size_t.
 */
size_t protocore_quic_frame_build_max_data(uint8_t *restrict work, uint8_t *out, size_t cap, uint64_t max);
/**
 * @brief A CONNECTION_CLOSE with a reason phrase. app selects the .
 * @param work PROTOCORE_QUIC_FRAME_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param app App
 * @param error_code Error code
 * @param frame_type Frame type
 * @param reason Reason
 * @param reason_len Reason len
 * @return The size_t.
 */
size_t protocore_quic_frame_build_connection_close(uint8_t *restrict work, uint8_t *out, size_t cap, proto_bool app,
                                                   uint64_t error_code, uint64_t frame_type, const char *reason,
                                                   size_t reason_len);

/** @brief Module namespace. */
PROTOCORE_NS QuicFrameNs QuicFrame PROTOCORE_UNUSED = {
    .parse = protocore_quic_frame_parse,
    .build_padding = protocore_quic_frame_build_padding,
    .build_ping = protocore_quic_frame_build_ping,
    .build_handshake_done = protocore_quic_frame_build_handshake_done,
    .build_ack = protocore_quic_frame_build_ack,
    .build_crypto = protocore_quic_frame_build_crypto,
    .build_stream = protocore_quic_frame_build_stream,
    .build_max_data = protocore_quic_frame_build_max_data,
    .build_connection_close = protocore_quic_frame_build_connection_close};

PROTOCORE_END_DECLS

#endif // PROTOCORE_QUIC_FRAME_H
