// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_h2_server.h
 * @brief Bridge between the HTTP/2 engine (protocore_h2_conn) and the server's request pipeline.
 *
 * When a TLS connection negotiates ALPN "h2", the session layer hands its decrypted bytes to
 * this module instead of the HTTP/1.1 parser. It runs one protocore_h2_conn per connection slot, maps each
 * decoded request's pseudo-headers (:method / :path / :authority) and headers into the slot's
 * HttpReq, and marks it PARSE_COMPLETE so the existing route dispatcher serves it. Responses from
 * the handlers route back here (send branches on the slot's h2 flag) and are
 * serialized as HEADERS + DATA frames, leaving the connection open for the next stream.
 *
 * The per-slot engines are large (a whole frame is buffered), so their pool lives in PSRAM where
 * available; HTTP/2 is therefore practical on PSRAM boards (ESP32-S3/-P4, WROVER).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_H2_SERVER_H
#define PROTOCORE_H2_SERVER_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_HTTP2 && PROTOCORE_ENABLE_TLS

/** @brief RFC 9113 sec 8.3: what one HEADERS + DATA response carries. */
typedef struct
{
    int code;                 ///< the status a response carries
    const char *content_type; ///< its media type
    const char *body;         ///< its body bytes
    size_t len;               ///< how many
} H2RespArgs;

/** @brief The engine's own state and the calls that reach it, described only in h2_server.c. */
struct H2ServerInternal;

/**
 * @brief The HTTP/2 engine (RFC 9113): one connection per transport slot.
 *
 * A caller sets the members a call takes, invokes it through ::H2Server, and reads the outcome off
 * the same handle.
 *
 * @var H2ServerNs::slot          the connection a call acts on
 * @var H2ServerNs::resp          what a serialized response carries
 * @var H2ServerNs::ok            a call's true/false outcome
 * @var H2ServerNs::open     start the engine after ALPN "h2"; sends our initial SETTINGS
 * @var H2ServerNs::data     feed the slot's decrypted inbound bytes in; drives requests via HttpReq
 * @var H2ServerNs::respond  serialize a response for the slot's current stream (HEADERS + DATA),
 *                           then ready the slot's HttpReq for the next stream; the connection stays
 *                           open
 * @var H2ServerNs::close    release per-slot state on connection close
 * @var H2ServerNs::internal the engine's state and the calls that reach it
 */
typedef struct
{
    uint8_t slot; ///< the connection a call acts on

    H2RespArgs resp; ///< what a serialized response carries

    proto_bool ok;

    void (*open)(struct H2ServerInternal *ctx);
    void (*data)(struct H2ServerInternal *ctx);
    void (*respond)(struct H2ServerInternal *ctx);
    void (*close)(struct H2ServerInternal *ctx);

    struct H2ServerInternal *internal;
} H2ServerNs;

/** @brief The one symbol this module exports. */
extern H2ServerNs H2Server;

/**
 * @brief The response sink the presentation layer installs at ALPN.
 *
 * Its shape is the seam's (::protocore_resp_sink_fn), not this module's, so it stays a plain
 * function and carries its arguments onto the handle before the call that does the work.
 */
proto_bool protocore_h2_server_respond(uint8_t slot, int code, const char *content_type, const char *body, size_t len);

#endif // PROTOCORE_ENABLE_HTTP2 && PROTOCORE_ENABLE_TLS

PROTOCORE_END_DECLS

#endif // PROTOCORE_H2_SERVER_H
