// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_h2_server.h
 * @brief Bridge between the HTTP/2 engine (pc_h2_conn) and the server's request pipeline.
 *
 * When a TLS connection negotiates ALPN "h2", the session layer hands its decrypted bytes to
 * this module instead of the HTTP/1.1 parser. It runs one pc_h2_conn per connection slot, maps each
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

PROTO_BEGIN_DECLS

#if PC_ENABLE_HTTP2 && PC_ENABLE_TLS

/** @brief Start the HTTP/2 engine for @p slot after ALPN "h2" (sends our initial SETTINGS). */
void pc_h2_server_open(uint8_t slot);

/** @brief Feed the slot's decrypted inbound bytes into the engine; drives requests via HttpReq. */
void pc_h2_server_data(uint8_t slot);

/**
 * @brief Serialize a handler's response for the slot's current stream (HEADERS + DATA), then ready
 * the slot's HttpReq for the next stream (the connection stays open). @return false on error.
 */
proto_bool pc_h2_server_respond(uint8_t slot, int code, const char *content_type, const char *body, size_t len);

/** @brief Release per-slot HTTP/2 state on connection close. */
void pc_h2_server_close(uint8_t slot);

#endif // PC_ENABLE_HTTP2 && PC_ENABLE_TLS

PROTO_END_DECLS

#endif // PROTOCORE_H2_SERVER_H
