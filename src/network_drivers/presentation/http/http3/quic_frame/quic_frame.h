// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_quic_frame.h
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
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_QUIC_FRAME_H
#define PROTOCORE_QUIC_FRAME_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HTTP3

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

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

/** @brief What parse takes: buf, len, out. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    QuicFrameHeader *out;
} QuicFrameParseArgs;

/** @brief What build_padding takes: out, cap, n. */
typedef struct
{
    uint8_t *out;
    size_t cap;
    size_t n;
} QuicFrameBuildPaddingArgs;

/** @brief What build_ping takes: out, cap. */
typedef struct
{
    uint8_t *out;
    size_t cap;
} QuicFrameBuildPingArgs;

/** @brief What build_handshake_done takes: out, cap. */
typedef struct
{
    uint8_t *out;
    size_t cap;
} QuicFrameBuildHandshakeDoneArgs;

/** @brief What build_ack takes: out, cap, largest, delay, first_range. */
typedef struct
{
    uint8_t *out;
    size_t cap;
    uint64_t largest;
    uint64_t delay;
    uint64_t first_range;
} QuicFrameBuildAckArgs;

/** @brief What build_crypto takes: out, cap, offset, data, len. */
typedef struct
{
    uint8_t *out;
    size_t cap;
    uint64_t offset;
    const uint8_t *data;
    size_t len;
} QuicFrameBuildCryptoArgs;

/** @brief What build_stream takes: out, cap, id, offset, data, len, ... */
typedef struct
{
    uint8_t *out;
    size_t cap;
    uint64_t id;
    uint64_t offset;
    const uint8_t *data;
    size_t len;
    proto_bool fin;
} QuicFrameBuildStreamArgs;

/** @brief What build_max_data takes: out, cap, max. */
typedef struct
{
    uint8_t *out;
    size_t cap;
    uint64_t max;
} QuicFrameBuildMaxDataArgs;

/** @brief What build_connection_close takes: out, cap, app, ... */
typedef struct
{
    uint8_t *out;
    size_t cap;
    proto_bool app;
    uint64_t error_code;
    uint64_t frame_type;
    const char *reason;
    size_t reason_len;
} QuicFrameBuildConnectionCloseArgs;

/**
 * @brief QUIC frame parsing and building (RFC 9000 sec 19).
 *
 * A caller sets the members a call takes, invokes it through ::QuicFrame with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   QuicFrame.parse_args.buf = ...;
 *   QuicFrame.parse_args.len = ...;
 *   QuicFrame.parse_args.out = ...;
 *   QuicFrame.parse(work);
 *   // QuicFrame.n is what the call reports
 *
 * @var QuicFrameNs::parse_args  what parse takes: buf, len, out
 * @var QuicFrameNs::build_padding_args  what build_padding takes: out, cap, n
 * @var QuicFrameNs::build_ping_args  what build_ping takes: out, cap
 * @var QuicFrameNs::build_handshake_done_args  what build_handshake_done takes: out, cap
 * @var QuicFrameNs::build_ack_args  what build_ack takes: out, cap, largest, delay, first_range
 * @var QuicFrameNs::build_crypto_args  what build_crypto takes: out, cap, offset, data, len
 * @var QuicFrameNs::build_stream_args  what build_stream takes: out, cap, id, offset, data, len,
 * @var QuicFrameNs::build_max_data_args  what build_max_data takes: out, cap, max
 * @var QuicFrameNs::build_connection_close_args  what build_connection_close takes: out, cap, app,
 * @var QuicFrameNs::ok  a call's true/false outcome
 * @var QuicFrameNs::n  the count a call reports
 * @var QuicFrameNs::parse  parse one frame at buf. bytes consumed, or 0 on malformed / ...
 * @var QuicFrameNs::build_padding  n PADDING frames (n zero bytes). n, or 0 if it does not fit
 * @var QuicFrameNs::build_ping  A PING frame
 * @var QuicFrameNs::build_handshake_done  A HANDSHAKE_DONE frame
 * @var QuicFrameNs::build_ack  A single-range ACK frame (ACK Range Count 0): Largest, ACK Delay, ...
 * @var QuicFrameNs::build_crypto  A CRYPTO frame carrying len bytes at stream offset
 * @var QuicFrameNs::build_stream  A STREAM frame (LEN always set; OFF set when offset > 0; FIN per ...
 * @var QuicFrameNs::build_max_data  A MAX_DATA frame
 * @var QuicFrameNs::build_connection_close  A CONNECTION_CLOSE with a reason phrase. app selects the ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    QuicFrameParseArgs parse_args;
    QuicFrameBuildPaddingArgs build_padding_args;
    QuicFrameBuildPingArgs build_ping_args;
    QuicFrameBuildHandshakeDoneArgs build_handshake_done_args;
    QuicFrameBuildAckArgs build_ack_args;
    QuicFrameBuildCryptoArgs build_crypto_args;
    QuicFrameBuildStreamArgs build_stream_args;
    QuicFrameBuildMaxDataArgs build_max_data_args;
    QuicFrameBuildConnectionCloseArgs build_connection_close_args;
    proto_bool ok;
    size_t n;
} QuicFrameVars;

/** @brief The operands and the outcome. */
extern QuicFrameVars QuicFrameV;

/** @brief The entries. */
typedef struct
{
    void (*const parse)(uint8_t *restrict work);
    void (*const build_padding)(uint8_t *restrict work);
    void (*const build_ping)(uint8_t *restrict work);
    void (*const build_handshake_done)(uint8_t *restrict work);
    void (*const build_ack)(uint8_t *restrict work);
    void (*const build_crypto)(uint8_t *restrict work);
    void (*const build_stream)(uint8_t *restrict work);
    void (*const build_max_data)(uint8_t *restrict work);
    void (*const build_connection_close)(uint8_t *restrict work);
} QuicFrameNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in QuicFrameV or a region of the borrow at a fixed offset.
void protocore_quic_frame_parse(uint8_t *restrict work);
void protocore_quic_frame_build_padding(uint8_t *restrict work);
void protocore_quic_frame_build_ping(uint8_t *restrict work);
void protocore_quic_frame_build_handshake_done(uint8_t *restrict work);
void protocore_quic_frame_build_ack(uint8_t *restrict work);
void protocore_quic_frame_build_crypto(uint8_t *restrict work);
void protocore_quic_frame_build_stream(uint8_t *restrict work);
void protocore_quic_frame_build_max_data(uint8_t *restrict work);
void protocore_quic_frame_build_connection_close(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `QuicFrame.parse(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const QuicFrameNs QuicFrame __attribute__((unused)) = {
    .parse = protocore_quic_frame_parse,
    .build_padding = protocore_quic_frame_build_padding,
    .build_ping = protocore_quic_frame_build_ping,
    .build_handshake_done = protocore_quic_frame_build_handshake_done,
    .build_ack = protocore_quic_frame_build_ack,
    .build_crypto = protocore_quic_frame_build_crypto,
    .build_stream = protocore_quic_frame_build_stream,
    .build_max_data = protocore_quic_frame_build_max_data,
    .build_connection_close = protocore_quic_frame_build_connection_close,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3

#endif // PROTOCORE_QUIC_FRAME_H
