// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ws_client.h
 * @brief The WebSocket Protocol (RFC 6455), client end: the opening handshake and the base framing
 *        protocol (PROTOCORE_ENABLE_WS_CLIENT).
 *
 * RFC 6455 sec 3 gives the connection its four terms: /host/, /port/, /secure/ and the
 * /resource name/ the request-line targets. The opening handshake rides on HTTP/1.1 - a GET
 * request-line (RFC 9112 sec 3) carrying Host (RFC 9110 sec 7.2) and Upgrade (RFC 9110 sec 7.8),
 * answered by 101 Switching Protocols (RFC 9110 sec 15.2.2). After that the connection carries
 * frames, not messages, and every frame this end sends is masked (RFC 6455 sec 5.3).
 *
 * Two halves behind one handle:
 *
 *  - The codec (RFC 6455 sec 4 and sec 5) reads and writes octets in the caller's buffer and holds
 *    nothing, so it is unit-tested on the host (env:native_ws_client).
 *  - The transport drives one connection over the outbound TCP client, and wss:// over the shared
 *    persistent client TLS session. No heap; one connection at a time.
 *
 * Only a message that fits PROTOCORE_WS_CLIENT_BUF_SIZE is delivered.
 *
 * The module exports one symbol, @ref WsClient. Everything in ws_client.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_WS_CLIENT_H
#define PROTOCORE_WS_CLIENT_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_WS_CLIENT

PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

/** @brief |Sec-WebSocket-Key| room: base64 of 16 octets is 24 characters plus NUL (RFC 6455 sec 4.1). */
#define PROTOCORE_WS_KEY_CAP 25

/** @brief |Sec-WebSocket-Accept| room: base64 of the 20-octet SHA-1 is 28 characters plus NUL (RFC 6455 sec 4.2.2). */
#define PROTOCORE_WS_ACCEPT_CAP 29

// ---------------------------------------------------------------------------
// Typedefs
// ---------------------------------------------------------------------------

/** @brief The 4-bit opcode a frame carries (RFC 6455 sec 5.2); sec 5.6 data, sec 5.5 control. */
typedef enum PROTO_ENUM_PACKED
{
    WSC_OP_CONT = 0x0,   ///< continuation frame (RFC 6455 sec 5.4)
    WSC_OP_TEXT = 0x1,   ///< text frame, UTF-8 Application data (RFC 6455 sec 5.6)
    WSC_OP_BINARY = 0x2, ///< binary frame (RFC 6455 sec 5.6)
    WSC_OP_CLOSE = 0x8,  ///< connection close (RFC 6455 sec 5.5.1)
    WSC_OP_PING = 0x9,   ///< ping (RFC 6455 sec 5.5.2)
    WSC_OP_PONG = 0xA,   ///< pong (RFC 6455 sec 5.5.3)
} WsClientOpcode;

/** @brief Where a reassembled Text or Binary message is delivered (RFC 6455 sec 5.6). */
typedef void (*WsClientMessageCb)(uint8_t opcode, const uint8_t *payload, size_t len);

/**
 * @brief RFC 6455 sec 3 and sec 4.1: the four URI terms and the three fields the handshake names.
 *
 * @c accept is both ends of one value: the accept computation writes it, and the server-handshake
 * check compares the field that arrived against it.
 */
typedef struct
{
    const char *host;          ///< /host/, the Host field's value (RFC 6455 sec 4.1, RFC 9110 sec 7.2)
    uint16_t port;             ///< /port/, the port dialed (RFC 6455 sec 3)
    proto_bool secure;         ///< /secure/, the connection runs over TLS (RFC 6455 sec 3)
    const char *resource_name; ///< /resource name/, the request-line's target (RFC 6455 sec 3, RFC 9112 sec 3)
    const char *key;           ///< |Sec-WebSocket-Key|, base64 of 16 random octets (RFC 6455 sec 11.3.1)
    const char *subprotocol;   ///< |Sec-WebSocket-Protocol| offered; null or empty omits it (RFC 6455 sec 11.3.4)
    char *accept;              ///< |Sec-WebSocket-Accept|, written by one call and compared by another (sec 11.3.3)
    size_t accept_cap;         ///< its room, at least ::PROTOCORE_WS_ACCEPT_CAP
} WsHandshakeArgs;

/** @brief RFC 6455 sec 5.2: the base framing protocol's fields, set by a build and filled by a parse. */
typedef struct
{
    uint8_t opcode;             ///< the 4-bit opcode (RFC 6455 sec 5.2)
    proto_bool fin;             ///< FIN, the final fragment of a message (RFC 6455 sec 5.2, sec 5.4)
    const uint8_t *payload;     ///< the Payload data a build masks into the frame
    size_t payload_len;         ///< its octet count; a parse reports the Payload data length it found
    size_t payload_off;         ///< where a parse found Payload data inside the frame
    size_t consumed;            ///< the whole frame a parse read: header plus Payload data
    const uint8_t *masking_key; ///< the 4-octet Masking-key a build applies (RFC 6455 sec 5.3)
} WsFrameArgs;

/** @brief The octets a codec call moves: one buffer it writes, one it reads. */
typedef struct
{
    uint8_t *out;      ///< where a build writes
    size_t cap;        ///< how much room it has
    const uint8_t *in; ///< the octets a parse or a check reads
    size_t avail;      ///< how many are readable there
} WsBufArgs;

/** @brief RFC 6455 sec 5.6: the Application data a Data frame carries, and where an inbound one lands. */
typedef struct
{
    const char *text;             ///< the UTF-8 Application data a Text frame carries
    const uint8_t *data;          ///< the Application data a Binary frame carries
    size_t len;                   ///< its octet count
    WsClientMessageCb on_message; ///< where a reassembled Text or Binary message is delivered
} WsMessageArgs;

/** @brief The connection's own state and the calls that reach it, described only in ws_client.c. */
struct WsClientInternal;

/**
 * @brief The client end of the WebSocket Protocol (RFC 6455).
 *
 * A caller sets the members a call takes, invokes it through ::WsClient, and reads the outcome off
 * the same handle. The connection and its buffers are behind @ref internal.
 *
 * No slot member: this end drives one connection at a time, so no call names one.
 *
 * @var WsClientNs::handshake  the four URI terms and the three handshake fields (RFC 6455 sec 3, sec 4.1)
 * @var WsClientNs::frame      the base framing protocol's fields (RFC 6455 sec 5.2)
 * @var WsClientNs::buf        the octets a codec call writes or reads
 * @var WsClientNs::msg        the Application data a Data frame carries (RFC 6455 sec 5.6)
 * @var WsClientNs::ok         a call's true/false outcome
 * @var WsClientNs::n          the octets a build wrote, 0 when they would not fit @ref WsBufArgs::cap
 * @var WsClientNs::accept_for_key
 * Write base64(SHA-1(@c key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")) into @c accept, the value the
 * server's |Sec-WebSocket-Accept| must carry (RFC 6455 sec 1.3, sec 4.2.2 step 5).
 * @var WsClientNs::build_opening_handshake
 * Build the client's opening handshake into @c buf: a GET request-line naming @c resource_name
 * (RFC 9112 sec 3) with Host, Upgrade, Connection, |Sec-WebSocket-Key|, an optional
 * |Sec-WebSocket-Protocol|, and |Sec-WebSocket-Version| 13 (RFC 6455 sec 4.1).
 * @var WsClientNs::check_server_handshake
 * True when the octets in @c buf are a 101 Switching Protocols status-line (RFC 9110 sec 15.2.2)
 * carrying a |Sec-WebSocket-Accept| field equal to @c accept (RFC 6455 sec 4.1).
 * @var WsClientNs::build_frame
 * Build one FIN frame for @c opcode into @c buf, its Payload data masked with @c masking_key
 * (RFC 6455 sec 5.2, sec 5.3).
 * @var WsClientNs::parse_frame
 * Read one inbound frame from @c buf, filling @c opcode, @c fin, @c payload_off, @c payload_len and
 * @c consumed. False when @c avail holds less than the whole frame (RFC 6455 sec 5.2).
 * @var WsClientNs::on_message  record @c on_message; call it before @ref WsClientNs::connect
 * @var WsClientNs::connect
 * Dial /host/ and /port/, run TLS when /secure/ is set, then exchange the opening handshake and
 * verify the response. True once the WebSocket connection is established (RFC 6455 sec 4.1).
 * @var WsClientNs::send_text    send @c text as a masked Text frame (RFC 6455 sec 5.6)
 * @var WsClientNs::send_binary  send @c data as a masked Binary frame (RFC 6455 sec 5.6)
 * @var WsClientNs::loop
 * Read inbound frames, reassemble fragments, answer Ping with Pong (RFC 6455 sec 5.5.2) and echo a
 * Close (RFC 6455 sec 5.5.1). False once the connection is gone. Call once per loop().
 * @var WsClientNs::connected  true while the WebSocket connection is established
 * @var WsClientNs::close
 * Send a Close frame and then Close the WebSocket Connection (RFC 6455 sec 5.5.1, sec 7.1.1).
 * @var WsClientNs::internal   the connection's state and the calls that reach it
 */
typedef struct
{
    WsHandshakeArgs handshake; ///< the URI terms and the handshake fields
    WsFrameArgs frame;         ///< the base framing protocol's fields
    WsBufArgs buf;             ///< the octets a codec call moves
    WsMessageArgs msg;         ///< the Application data a Data frame carries

    proto_bool ok;
    size_t n;

    void (*accept_for_key)(struct WsClientInternal *ctx);
    void (*build_opening_handshake)(struct WsClientInternal *ctx);
    void (*check_server_handshake)(struct WsClientInternal *ctx);
    void (*build_frame)(struct WsClientInternal *ctx);
    void (*parse_frame)(struct WsClientInternal *ctx);
    void (*on_message)(struct WsClientInternal *ctx);
    void (*connect)(struct WsClientInternal *ctx);
    void (*send_text)(struct WsClientInternal *ctx);
    void (*send_binary)(struct WsClientInternal *ctx);
    void (*loop)(struct WsClientInternal *ctx);
    void (*connected)(struct WsClientInternal *ctx);
    void (*close)(struct WsClientInternal *ctx);

    struct WsClientInternal *internal;
} WsClientNs;

/** @brief The one symbol this module exports. */
extern WsClientNs WsClient;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_WS_CLIENT

#endif // PROTOCORE_WS_CLIENT_H
