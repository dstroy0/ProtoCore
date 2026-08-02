// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ws_client.h
 * @brief Zero-heap outbound WebSocket client, RFC 6455 (PC_ENABLE_WS_CLIENT).
 *
 * Connects to a remote WebSocket endpoint (ws://, or wss:// over client-side
 * mbedTLS) and exchanges text/binary messages - the device as a WebSocket client
 * to a cloud dashboard or control plane. Split, like the other services, into a
 * pure host-testable codec and an ESP32-only transport:
 *
 *  - ws_client_build_handshake / ws_client_accept_for_key /
 *    ws_client_check_response / ws_client_build_frame / ws_client_parse_frame are
 *    pure functions, unit-tested on the host (env:native_ws_client).
 *  - ws_client_connect / ws_client_send_text / ws_client_loop drive the
 *    connection over raw lwIP (wss:// via the shared persistent client TLS
 *    session). No heap; one connection at a time.
 *
 * Client frames are always masked (RFC 6455 §5.3); server frames are not. Only
 * unfragmented messages that fit PC_WS_CLIENT_BUF_SIZE are delivered.
 */

#ifndef PROTOCORE_WS_CLIENT_H
#define PROTOCORE_WS_CLIENT_H

#include "protocore_config.h"

#if PC_ENABLE_WS_CLIENT

/** @brief WebSocket opcodes (RFC 6455 §5.2). */
typedef enum PROTO_ENUM_PACKED
{
    WSC_OP_CONT = 0x0,
    WSC_OP_TEXT = 0x1,
    WSC_OP_BINARY = 0x2,
    WSC_OP_CLOSE = 0x8,
    WSC_OP_PING = 0x9,
    WSC_OP_PONG = 0xA,
} WsClientOpcode;

// ---------------------------------------------------------------------------
// Pure codec (host-testable; no sockets, no heap)
// ---------------------------------------------------------------------------

/**
 * @brief Compute the expected Sec-WebSocket-Accept for a client key.
 *
 * accept = base64(SHA1(key_b64 + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")), per
 * RFC 6455 §4.2.2. @p out must hold at least 29 bytes (28 + NUL).
 */
void ws_client_accept_for_key(const char *key_b64, char *out, size_t out_cap);

/**
 * @brief Build the client opening handshake (an HTTP/1.1 Upgrade GET).
 * @param subprotocol  requested Sec-WebSocket-Protocol (e.g. "wamp.2.json"), or null/empty to omit the header.
 * @return bytes written to @p out, or 0 if it would not fit @p cap.
 */
size_t ws_client_build_handshake(uint8_t *out, size_t cap, const char *host, const char *path, const char *key_b64,
                                 const char *subprotocol);

/**
 * @brief Validate a server handshake response.
 * @return true if it is "101 Switching Protocols" and carries
 *         Sec-WebSocket-Accept == @p expected_accept.
 */
proto_bool ws_client_check_response(const uint8_t *buf, size_t len, const char *expected_accept);

/**
 * @brief Build a masked client frame (FIN set) for @p opcode.
 * @param mask  4-byte masking key (random per frame on the wire).
 * @return total frame length, or 0 if it would not fit @p cap.
 */
size_t ws_client_build_frame(uint8_t *out, size_t cap, WsClientOpcode opcode, const uint8_t *payload, size_t len,
                             const uint8_t mask[4]);

/**
 * @brief Parse one inbound (server, unmasked) frame at @p buf.
 * @param payload_off  receives the payload offset within @p buf.
 * @param consumed     receives the total frame size (header + payload).
 * @return true if a complete frame is present in @p avail bytes; false if more
 *         bytes are needed.
 */
proto_bool ws_client_parse_frame(const uint8_t *buf, size_t avail, uint8_t *opcode, proto_bool *fin,
                                 size_t *payload_off, size_t *payload_len, size_t *consumed);

// ---------------------------------------------------------------------------
// Transport (ESP32 only; no-ops / false on a host build)
// ---------------------------------------------------------------------------

/** @brief Callback for an inbound text/binary message (opcode is WSC_OP_TEXT/BINARY). */
typedef void (*WsClientMessageCb)(uint8_t opcode, const uint8_t *payload, size_t len);

/** @brief Register the inbound-message callback (call before ws_client_connect). */
void ws_client_on_message(WsClientMessageCb cb);

/**
 * @brief Connect and complete the WebSocket handshake (blocking).
 * @return true on a verified 101 upgrade.
 */
proto_bool ws_client_connect(const char *host, uint16_t port, proto_bool use_tls, const char *path);

/** @brief Send a UTF-8 text message (masked). @return true if sent. */
proto_bool ws_client_send_text(const char *text);

/** @brief Send a binary message (masked). @return true if sent. */
proto_bool ws_client_send_binary(const uint8_t *data, size_t len);

/**
 * @brief Pump the connection: read inbound frames (dispatching text/binary to the
 *        callback), answer ping with pong, and handle close. Call once per loop().
 * @return false if the connection has dropped.
 */
proto_bool ws_client_loop();

/** @brief True while the WebSocket connection is open. */
proto_bool ws_client_connected();

/** @brief Send a Close frame and drop the connection. */
void ws_client_close();

#endif // PC_ENABLE_WS_CLIENT

#endif // PROTOCORE_WS_CLIENT_H
