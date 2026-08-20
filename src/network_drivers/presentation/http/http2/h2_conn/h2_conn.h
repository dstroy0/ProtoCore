// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_h2_conn.h
 * @brief HTTP/2 connection + stream engine (RFC 9113) over the HPACK + frame layers.
 *
 * One H2Conn drives a single HTTP/2 connection: it consumes the client connection preface, the
 * SETTINGS exchange, and the frame stream; reassembles each request's header block (HEADERS +
 * CONTINUATION) and HPACK-decodes it; tracks per-stream state and connection / stream flow
 * control; and answers control frames (SETTINGS ACK, PING ACK, WINDOW_UPDATE). Decoded requests
 * and body data are handed to the application through callbacks, and protocore_h2_conn_respond() serializes
 * a response as HEADERS + DATA frames. Outbound bytes go through a caller-supplied writer, so the
 * engine has no transport dependency and is host-testable by feeding it a byte stream.
 *
 * Fixed storage, no heap: one frame-reassembly buffer, one header-block buffer, an HPACK decoder
 * table, and a small stream table per connection (sizes from PROTOCORE_H2_*).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_H2_CONN_H
#define PROTOCORE_H2_CONN_H

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HTTP2

PROTOCORE_BEGIN_DECLS

#define PROTOCORE_H2_FRAME_HDR_CAP 16u
/** @brief The widest frame a received frame provokes us to send: a header plus 8 opaque bytes. */
#define PROTOCORE_H2_CTL_FRAME_MAX 17u
/**
 * @brief This module's draw on the plaintext pool, declared here and asserted in h2_conn.c.
 *
 * One borrow per connection from the secure pool's persistent end, split by offset into the engine
 * context, the frame payload buffer, the header-block buffer, the HPACK emit scratch and the
 * 9-octet frame header. HTTP/2 runs over TLS, so the bytes are secure and the connection owns them.
 */
#define PROTOCORE_H2_CONN_BORROW                                                                                       \
    ((size_t)PROTOCORE_H2_CONN_RECORD + PROTOCORE_H2_MAX_FRAME + PROTOCORE_H2_HDR_BLOCK + PROTOCORE_H2_HDR_BLOCK +     \
     PROTOCORE_H2_FRAME_HDR_CAP)

/** @brief Application callbacks the engine drives (all optional except write). */
typedef struct
{
    /** @brief Send @p len bytes to the peer (through TLS/transport); must send all. */
    void (*write)(void *io, const uint8_t *data, size_t len);
    /** @brief One decoded request header on @p stream_id (pseudo-headers included). */
    void (*on_header)(void *app, uint32_t stream_id, const char *name, size_t nlen, const char *val, size_t vlen);
    /**
     * @brief The request header block for @p stream_id is complete. @p end_stream: no body.
     * @return false if the request is malformed; the engine resets the stream (RFC 9113 sec 8.1.1).
     */
    proto_bool (*on_headers_end)(void *app, uint32_t stream_id, proto_bool end_stream);
    /** @brief Request body bytes on @p stream_id (@p end_stream marks the last). */
    void (*on_data)(void *app, uint32_t stream_id, const uint8_t *data, size_t len, proto_bool end_stream);
    void *io;  ///< opaque, passed to write()
    void *app; ///< opaque, passed to the on_* callbacks
} H2Callbacks;

/** @brief What ::H2ConnNs::init installs. */
typedef struct
{
    const H2Callbacks *cb; ///< the callbacks the engine drives
} H2ConnInitArgs;

/** @brief The inbound bytes ::H2ConnNs::recv feeds through the state machine. */
typedef struct
{
    const uint8_t *data; ///< the bytes that arrived
    size_t len;          ///< how many
} H2ConnRecvArgs;

/** @brief RFC 9113 sec 8.3: what one HEADERS + DATA response carries. */
typedef struct
{
    uint32_t stream_id;       ///< the stream it answers
    int status;               ///< the status it carries
    const char *content_type; ///< its media type, or NULL
    const char *body;         ///< its body bytes
    size_t body_len;          ///< how many
} H2ConnRespondArgs;

/** @brief RFC 9113 sec 6.8: the error a graceful shutdown reports. */
typedef struct
{
    uint32_t error; ///< the code the GOAWAY carries
} H2ConnGoawayArgs;

/**
 * @brief One HTTP/2 connection's engine (RFC 9113).
 *
 * A caller sets the members a call takes, invokes it through ::H2Conn, and reads the outcome off
 * the same handle.
 *
 * @var H2ConnNs::init_args     the callbacks an init installs
 * @var H2ConnNs::recv_args     the bytes a feed carries
 * @var H2ConnNs::respond_args  what a serialized response carries
 * @var H2ConnNs::goaway_args   the error a shutdown reports
 * @var H2ConnNs::ok            a call's true/false outcome
 * @var H2ConnNs::init     start the engine and send our initial SETTINGS through cb.write
 * @var H2ConnNs::recv     feed inbound bytes; drives the machine, invokes callbacks, writes control frames
 * @var H2ConnNs::respond  serialize HEADERS + DATA on a stream and close it
 * @var H2ConnNs::goaway   send a GOAWAY to begin a graceful shutdown
 *
 * Every entry takes one connection's borrow. How those bytes are carved is h2_conn.c's and is
 * never named here; ::PROTOCORE_H2_CONN_BORROW is how many a caller must hand over.
 */
typedef struct
{
    H2ConnInitArgs init_args;       ///< the members ::H2ConnNs::init takes
    H2ConnRecvArgs recv_args;       ///< the members ::H2ConnNs::recv takes
    H2ConnRespondArgs respond_args; ///< the members ::H2ConnNs::respond takes
    H2ConnGoawayArgs goaway_args;   ///< the members ::H2ConnNs::goaway takes
    proto_bool ok;                  ///< a call's true/false outcome
} H2ConnVars;

/** @brief The operands and the outcome. */
extern H2ConnVars H2ConnV;

/** @brief The entries. */
typedef struct
{
    void (*const init)(uint8_t *restrict work);
    void (*const recv)(uint8_t *restrict work);
    void (*const respond)(uint8_t *restrict work);
    void (*const goaway)(uint8_t *restrict work);
} H2ConnNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in H2ConnV or a region of the borrow at a fixed offset.
void protocore_h2_conn_init(uint8_t *restrict work);
void protocore_h2_conn_recv(uint8_t *restrict work);
void protocore_h2_conn_respond(uint8_t *restrict work);
void protocore_h2_conn_goaway(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `H2Conn.init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const H2ConnNs H2Conn __attribute__((unused)) = {
    .init = protocore_h2_conn_init,
    .recv = protocore_h2_conn_recv,
    .respond = protocore_h2_conn_respond,
    .goaway = protocore_h2_conn_goaway,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP2

#endif // PROTOCORE_H2_CONN_H
