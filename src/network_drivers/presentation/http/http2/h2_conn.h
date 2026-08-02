// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_h2_conn.h
 * @brief HTTP/2 connection + stream engine (RFC 9113) over the HPACK + frame layers.
 *
 * One H2Conn drives a single HTTP/2 connection: it consumes the client connection preface, the
 * SETTINGS exchange, and the frame stream; reassembles each request's header block (HEADERS +
 * CONTINUATION) and HPACK-decodes it; tracks per-stream state and connection / stream flow
 * control; and answers control frames (SETTINGS ACK, PING ACK, WINDOW_UPDATE). Decoded requests
 * and body data are handed to the application through callbacks, and pc_h2_conn_respond() serializes
 * a response as HEADERS + DATA frames. Outbound bytes go through a caller-supplied writer, so the
 * engine has no transport dependency and is host-testable by feeding it a byte stream.
 *
 * Fixed storage, no heap: one frame-reassembly buffer, one header-block buffer, an HPACK decoder
 * table, and a small stream table per connection (sizes from PC_H2_*).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_H2_CONN_H
#define PROTOCORE_H2_CONN_H

#include "protocore_config.h"

#if PC_ENABLE_HTTP2

#include "network_drivers/presentation/http/http2/h2_frame.h"
#include "network_drivers/presentation/http/http2/hpack.h"

/** @brief Per-stream state (RFC 9113 sec 5.1, server side of a client-initiated stream). A
 *  mutually-exclusive internal lifecycle state, not a wire value. */
typedef enum PROTO_ENUM_PACKED
{
    H2_ST_IDLE = 0,
    H2_ST_OPEN,        ///< receiving (headers seen, no END_STREAM yet)
    H2_ST_HALF_CLOSED, ///< client finished (END_STREAM); we may still respond
    H2_ST_CLOSED,
} H2StreamState;

typedef struct
{
    uint32_t id;         ///< stream identifier (0 = free slot)
    H2StreamState state; ///< lifecycle state
    int32_t send_window; ///< our remaining DATA flow window for this stream
} H2Stream;

/** @brief Application callbacks the engine drives (all optional except write). */
typedef struct
{
    /** @brief Send @p len bytes to the peer (through TLS/transport); must send all. */
    void (*write)(void *io, const uint8_t *data, size_t len);
    /** @brief One decoded request header on @p stream_id (pseudo-headers included). */
    void (*on_header)(void *app, uint32_t stream_id, const char *name, size_t nlen, const char *val, size_t vlen);
    /** @brief The request header block for @p stream_id is complete. @p end_stream: no body. */
    void (*on_headers_end)(void *app, uint32_t stream_id, proto_bool end_stream);
    /** @brief Request body bytes on @p stream_id (@p end_stream marks the last). */
    void (*on_data)(void *app, uint32_t stream_id, const uint8_t *data, size_t len, proto_bool end_stream);
    void *io;  ///< opaque, passed to write()
    void *app; ///< opaque, passed to the on_* callbacks
} H2Callbacks;

/** @brief One HTTP/2 connection's engine state (fixed storage, no heap). */
typedef struct
{
    uint8_t phase; ///< 0 = awaiting preface, 1 = running, 2 = closed
    H2Callbacks cb;

    // Inbound frame reassembly.
    uint8_t fbuf[H2_FRAME_HEADER_LEN + PC_H2_MAX_FRAME];
    size_t fhave; ///< bytes buffered for the current frame
    size_t pre;   ///< preface bytes matched so far

    // Header-block reassembly (HEADERS + CONTINUATION); empty when a frame carries END_HEADERS.
    uint8_t hblock[PC_H2_HDR_BLOCK];
    size_t hblock_len;
    uint32_t hblock_stream;
    proto_bool hblock_end_stream;
    proto_bool in_header_block; ///< between a non-END_HEADERS HEADERS and its END_HEADERS CONTINUATION

    HpackDynTable hdec;             ///< HPACK decoder (peer's encoder state)
    char hscratch[PC_H2_HDR_BLOCK]; ///< HPACK per-header emit scratch

    H2Settings peer;          ///< the peer's settings (affect how we send)
    int32_t conn_send_window; ///< our connection-level DATA flow window

    H2Stream streams[PC_H2_MAX_STREAMS];
    uint32_t last_peer_stream; ///< highest client (odd) stream id accepted
} H2Conn;

/** @brief Initialize a connection engine and send our initial SETTINGS via cb.write. */
void pc_h2_conn_init(H2Conn *c, const H2Callbacks *cb);

/**
 * @brief Feed inbound bytes. Drives the state machine, invokes callbacks, and writes control
 * frames. @return false on a fatal connection error (the caller sends GOAWAY and closes).
 */
proto_bool pc_h2_conn_recv(H2Conn *c, const uint8_t *data, size_t len);

/**
 * @brief Serialize a complete response (status + optional content-type + body) as a HEADERS
 * frame (HPACK) followed by a DATA frame on @p stream_id, and close the stream. @return false on
 * a bad stream / serialization overflow.
 */
proto_bool pc_h2_conn_respond(H2Conn *c, uint32_t stream_id, int status, const char *content_type, const char *body,
                              size_t body_len);

/** @brief Send a GOAWAY (last accepted stream, @p error) to begin a graceful shutdown. */
void pc_h2_conn_goaway(H2Conn *c, uint32_t error);

#endif // PC_ENABLE_HTTP2
#endif // PROTOCORE_H2_CONN_H
