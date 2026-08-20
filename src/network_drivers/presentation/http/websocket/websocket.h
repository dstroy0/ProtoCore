// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file websocket.h
 * @brief Layer 6 (Presentation) -- WebSocket frame parser and connection pool.
 *
 * Implements RFC 6455 framing with a fixed-size payload buffer per slot.
 * Connections are tracked in ws_pool[MAX_WS_CONNS]; each entry maps to one
 * TCP slot in conn_pool[] via slot_id.
 *
 * **Frame format (client to server)**
 * ```
 *  0               1               2               3
 *  0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
 * +-+-+-+-+-------+-+-------------+-------------------------------+
 * |F|R|R|R| opcode|M| payload len |    extended payload length    |
 * |I|S|S|S|  (4)  |A|     (7)    |             (16/64)           |
 * |N|V|V|V|       |S|             +-------------------------------+
 * | |1|2|3|       |K|             |                               |
 * +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - -+
 * |     extended payload length continued, if payload len == 127  |
 * + - - - - - - - - - - - - - - -+-------------------------------+
 * |                               | masking key, if MASK set      |
 * +-------------------------------+-------------------------------+
 * | masking key (continued)       |          payload data         |
 * +-------------------------------- - - - - - - - - - - - - - - -+
 * :                     payload data continued                    :
 * +---------------------------------------------------------------+
 * ```
 *
 * **State machine**
 * ```
 * WS_HEADER1       -- read FIN + opcode byte
 * WS_HEADER2       -- read MASK + 7-bit payload length
 * WS_LEN16_HI      -- read extended 16-bit length high byte
 * WS_LEN16_LO      -- read extended 16-bit length low byte
 * WS_LEN64         -- consume 8-byte 64-bit length (reject; too large)
 * WS_MASK0..3      -- read 4-byte masking key
 * WS_PAYLOAD       -- accumulate payload bytes (unmasked)
 * WS_FRAME_READY   -- complete frame waiting for dispatch
 * WS_CLOSED        -- connection closed; slot may be recycled
 * WS_ERROR         -- protocol error; close frame sent
 * ```
 *
 * **Limitations**
 * - A reassembled message must fit in WS_FRAME_SIZE bytes; larger closes 1009.
 * - RSV bits must be zero (no extensions supported).
 *
 * **Fragmentation (RFC 6455 §5.4)**
 * Fragmented data messages are reassembled into `buf` across continuation
 * frames; the message is delivered only when the FIN frame arrives. Control
 * frames (ping/pong/close) may be interleaved between fragments and are
 * handled immediately without disturbing the partial message.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_WEBSOCKET_H
#define PROTOCORE_WEBSOCKET_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_WEBSOCKET

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_WS_DEFLATE
#include "network_drivers/presentation/codec/deflate/deflate/deflate.h"
#include "network_drivers/presentation/codec/inflate/inflate.h"

/**
 * @brief Scratch borrowed while compressing one outbound message.
 *
 * The deflate state and the output buffer are live together. PROTOCORE_WS_DEFLATE_MAX bounds the payload
 * the compressor will accept, which is what turns `len + len/8 + 16` into a constant.
 */
#define PROTOCORE_PLAINTEXT_WORK_WS_SEND                                                                               \
    (DEFLATE_SCRATCH_SIZE + PROTOCORE_WS_DEFLATE_MAX + (PROTOCORE_WS_DEFLATE_MAX / 8) + 16)

/**
 * @brief Scratch borrowed while decompressing one reassembled inbound message.
 *
 * Input (message + the RFC 7692 00 00 ff ff marker), output, and the inflate tables are live
 * together. The parser closes 1009 before a message exceeds WS_FRAME_SIZE, which bounds the input.
 */
#define PROTOCORE_PLAINTEXT_WORK_WS_RECV (WS_FRAME_SIZE + 4 + WS_FRAME_SIZE + INFLATE_SCRATCH_SIZE)
#else
#define PROTOCORE_PLAINTEXT_WORK_WS_SEND 0
#define PROTOCORE_PLAINTEXT_WORK_WS_RECV 0
#endif

// ---------------------------------------------------------------------------
// WebSocket opcodes (RFC 6455 §5.2)
// ---------------------------------------------------------------------------

/** @brief WebSocket frame opcodes. */
typedef enum PROTO_ENUM_PACKED
{
    WS_OP_CONTINUATION = 0x0, ///< Continuation frame (data-message fragment; reassembled into buf).
    WS_OP_TEXT = 0x1,         ///< UTF-8 text payload.
    WS_OP_BINARY = 0x2,       ///< Binary payload.
    WS_OP_CLOSE = 0x8,        ///< Connection close.
    WS_OP_PING = 0x9,         ///< Ping (auto-ponged by the library).
    WS_OP_PONG = 0xA          ///< Pong (echoed ping; ignored by library).
} WsOpcode;

/** @brief WebSocket close status codes (RFC 6455 §7.4.1). */
typedef enum PROTO_ENUM_PACKED
{
    WS_CLOSE_NORMAL = 1000,          ///< Normal closure.
    WS_CLOSE_GOING_AWAY = 1001,      ///< Endpoint going away.
    WS_CLOSE_PROTOCOL = 1002,        ///< Protocol error.
    WS_CLOSE_UNSUPPORTED = 1003,     ///< Received a data type the endpoint cannot accept (RFC 6455).
    WS_CLOSE_INVALID_PAYLOAD = 1007, ///< Text message that is not valid UTF-8 (RFC 6455 8.1).
    WS_CLOSE_TOO_BIG = 1009          ///< Payload too large for WS_FRAME_SIZE.
} WsCloseCode;

// ---------------------------------------------------------------------------
// Frame parser states
// ---------------------------------------------------------------------------

/** @brief States of the WebSocket frame parser. */
typedef enum PROTO_ENUM_PACKED
{
    WS_HEADER1,     ///< Awaiting first header byte (FIN, RSV, opcode).
    WS_HEADER2,     ///< Awaiting second header byte (MASK, 7-bit length).
    WS_LEN16_HI,    ///< Reading extended 16-bit length, high byte.
    WS_LEN16_LO,    ///< Reading extended 16-bit length, low byte.
    WS_LEN64,       ///< Consuming 8-byte 64-bit length (always rejected).
    WS_MASK0,       ///< Reading masking key byte 0.
    WS_MASK1,       ///< Reading masking key byte 1.
    WS_MASK2,       ///< Reading masking key byte 2.
    WS_MASK3,       ///< Reading masking key byte 3.
    WS_PAYLOAD,     ///< Accumulating payload bytes.
    WS_FRAME_READY, ///< Complete frame ready for dispatch.
    WS_CLOSED,      ///< Connection closed; slot may be recycled.
    WS_ERROR        ///< Protocol error; close frame has been queued.
} WsParseState;

// ---------------------------------------------------------------------------
// Per-connection WebSocket state
// ---------------------------------------------------------------------------

/**
 * @brief WebSocket connection state stored in ws_pool[].
 *
 * Allocated when an HTTP upgrade handshake succeeds.  slot_id ties this
 * entry back to conn_pool[] and the ring buffer.
 */
typedef struct
{
    uint8_t ws_id;                  ///< Index into ws_pool[] (set at init).
    uint8_t slot_id;                ///< Owning TCP slot in conn_pool[].
    uint8_t route_id;               ///< The handler set this channel was opened for.
    proto_bool active;              ///< True when this entry is in use.
    WsParseState parse_state;       ///< Current frame parser state.
    WsOpcode opcode;                ///< Opcode of the frame being parsed.
    proto_bool fin;                 ///< FIN bit of the frame being parsed.
    proto_bool masked;              ///< True if client sent a masking key.
    uint8_t mask_key[4];            ///< Client masking key.
    uint32_t payload_len;           ///< Expected payload byte count (current frame).
    uint32_t payload_idx;           ///< Bytes received so far (current frame).
    uint8_t len64_count;            ///< Bytes consumed from 64-bit length.
    uint8_t buf[WS_FRAME_SIZE + 1]; ///< Reassembled message payload, null-terminated.
    // Fragmentation state (RFC 6455 §5.4). A data message may span multiple
    // frames (first text/binary with FIN=0, then continuation frames). Control
    // frames may be interleaved and use a separate buffer so they never clobber
    // the partially-assembled data message.
    proto_bool fragmenting;   ///< True between a non-FIN data frame and its FIN.
    WsOpcode msg_opcode;      ///< Opcode of the data message being assembled.
    uint32_t msg_len;         ///< Bytes assembled so far across all fragments.
    uint8_t ctl_buf[125 + 1]; ///< Control-frame payload (ping/pong/close), null-terminated.
#if PROTOCORE_ENABLE_WS_DEFLATE
    proto_bool pmd;            ///< permessage-deflate negotiated on this connection (RFC 7692).
    proto_bool msg_compressed; ///< Current data message arrived compressed (RSV1 on its first frame).
#endif
} WsConn;
/** @brief Pool of WebSocket connection state, one per MAX_WS_CONNS. */
extern WsConn ws_pool[MAX_WS_CONNS];
// ---------------------------------------------------------------------------
// WebSocket API
// ---------------------------------------------------------------------------
/**
 * @brief Callback fired when a WebSocket connection is established.
 *
 * @param ws_id  Index into ws_pool[] for this connection.
 */
typedef void (*WsConnectHandler)(uint8_t ws_id);
/**
 * @brief Callback fired when a WebSocket text or binary frame arrives.
 *
 * The payload is in ws_pool[ws_id].buf, null-terminated.  Length is in
 * ws_pool[ws_id].payload_len.  Opcode is in ws_pool[ws_id].opcode.
 *
 * @param ws_id  Index into ws_pool[].
 */
typedef void (*WsMessageHandler)(uint8_t ws_id);
/**
 * @brief Callback fired when a WebSocket connection closes.
 *
 * @param ws_id  Index into ws_pool[] (slot is still valid during callback).
 */
typedef void (*WsCloseHandler)(uint8_t ws_id);
/** @brief The id a route carries when it serves no WebSocket. */
#define PROTOCORE_WS_NONE 0xFFu
/** @brief The three handlers one route records. */
typedef struct
{
    WsConnectHandler on_connect; ///< the handler recorded for a route's open
    WsMessageHandler on_message; ///< the handler recorded for a message
    WsCloseHandler on_close;     ///< the handler recorded for a close
} WsRouteArgs;
/** @brief RFC 6455 sec 5.2 base framing, and the sec 7.4 status a Close carries. */
typedef struct
{
    WsOpcode opcode;        ///< the frame type a send emits
    const uint8_t *payload; ///< its payload bytes; may be NULL for a zero-length frame
    uint16_t len;           ///< how many
    WsCloseCode code;       ///< the status code a close carries
} WsFrameArgs;
/**
 * @brief The WebSocket connections this server holds open (RFC 6455).
 *
 * A caller sets the members a call takes, invokes it through ::Ws, and reads the outcome off the
 * same handle.
 *
 * The handlers live here, not in the route table: a route decides where a request goes, and what
 * runs once the socket is open belongs to this module. A route stores the id, so nothing above
 * holds a pointer into here and the same handler set can serve more than one route.
 *
 * @var WsNs::slot        the TCP slot a call acts on
 * @var WsNs::ws_id       the socket a call names
 * @var WsNs::id          the route id a lookup names
 * @var WsNs::route      the handlers one route records
 * @var WsNs::conn        the socket a call acts on, when it takes one by pointer
 * @var WsNs::frame      one frame's type, payload and close status (RFC 6455 sec 5.2, 7.4)
 * @var WsNs::byte        one already-plaintext byte for the frame state machine
 * @var WsNs::frag_size   the outbound fragmentation size in payload bytes; 0 = off
 * @var WsNs::ok          a call's true/false outcome
 * @var WsNs::u8          the route id an add reports, or ::PROTOCORE_WS_NONE when full
 * @var WsNs::text        the reassembled message payload a lookup reports, or NULL
 * @var WsNs::found       the socket an alloc or a find reports, or NULL
 * @var WsNs::connect_handler  the connect handler an id names, or NULL
 * @var WsNs::message_handler  the message handler an id names, or NULL
 * @var WsNs::close_handler    the close handler an id names, or NULL
 * @var WsNs::route_add        record one route's handlers
 * @var WsNs::route_reset      empty the handler table; a route holds the id an add returned, so
 *                             this empties with the routes
 * @var WsNs::route_connect    the connect handler an id names
 * @var WsNs::route_message    the message handler an id names
 * @var WsNs::route_close      the close handler an id names
 * @var WsNs::init             set every pool slot inactive; called once from begin()
 * @var WsNs::active           whether ws_id is a valid, in-use socket
 * @var WsNs::payload_of       the reassembled message payload for ws_id
 * @var WsNs::alloc            take a socket and bind it to a TCP slot
 * @var WsNs::find             the socket bound to a TCP slot
 * @var WsNs::free             release the socket bound to a TCP slot
 * @var WsNs::parse            feed the slot's bytes through the frame state machine
 * @var WsNs::feed_byte        feed one already-plaintext byte through it
 * @var WsNs::reset_frame      back to WS_HEADER1, ready for the next frame
 * @var WsNs::send_frame       build and send one frame; server-to-client frames are never masked
 * @var WsNs::set_frag_size    the outbound fragmentation size (RFC 6455 sec 5.4)
 * @var WsNs::close            send a Close frame and mark the socket WS_CLOSED
 *
 * Every entry takes the module's borrow. How those bytes are carved is websocket.c's and is never
 * named here. ::protocore_ws_span is where a caller gets one.
 *
 * A caller that needs immediate delivery flushes the connection itself after a send.
 */
typedef struct
{
    uint8_t slot;       ///< the TCP slot a call acts on
    uint8_t ws_id;      ///< the socket a call names
    uint8_t id;         ///< the route id a lookup names
    WsConn *conn;       ///< the socket a call acts on, when it takes one by pointer
    uint8_t byte;       ///< one already-plaintext byte for the frame state machine
    uint16_t frag_size; ///< the outbound fragmentation size in payload bytes; 0 = off
#if PROTOCORE_ENABLE_WS_DEFLATE
    proto_bool pmd; ///< what an alloc records on the new connection: RFC 7692 permessage-deflate,
                    ///< as the handshake negotiated it. The layer that read the Sec-WebSocket-
                    ///< Extensions header is the only one that knows, so it states it here.
#endif
    WsRouteArgs route; ///< the handlers one route records
    WsFrameArgs frame; ///< one frame's type, payload and close status
    proto_bool ok;
    uint8_t u8;
    const char *text;
    WsConn *found;
    WsConnectHandler connect_handler;
    WsMessageHandler message_handler;
    WsCloseHandler close_handler;
} WsVars;

/** @brief The operands and the outcome. */
extern WsVars WsV;

/** @brief The entries. */
typedef struct
{
    void (*const route_add)(uint8_t *restrict work);
    void (*const route_reset)(uint8_t *restrict work);
    void (*const route_connect)(uint8_t *restrict work);
    void (*const route_message)(uint8_t *restrict work);
    void (*const route_close)(uint8_t *restrict work);
    void (*const init)(uint8_t *restrict work);
    void (*const active)(uint8_t *restrict work);
    void (*const payload_of)(uint8_t *restrict work);
    void (*const alloc)(uint8_t *restrict work);
    void (*const find)(uint8_t *restrict work);
    void (*const free)(uint8_t *restrict work);
    void (*const parse)(uint8_t *restrict work);
    void (*const feed_byte)(uint8_t *restrict work);
    void (*const reset_frame)(uint8_t *restrict work);
    void (*const send_frame)(uint8_t *restrict work);
    void (*const set_frag_size)(uint8_t *restrict work);
    void (*const close)(uint8_t *restrict work);
} WsNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in WsV or a region of the borrow at a fixed offset.
void protocore_websocket_route_add(uint8_t *restrict work);
void protocore_websocket_route_reset(uint8_t *restrict work);
void protocore_websocket_route_connect(uint8_t *restrict work);
void protocore_websocket_route_message(uint8_t *restrict work);
void protocore_websocket_route_close(uint8_t *restrict work);
void protocore_websocket_init(uint8_t *restrict work);
void protocore_websocket_active(uint8_t *restrict work);
void protocore_websocket_payload_of(uint8_t *restrict work);
void protocore_websocket_alloc(uint8_t *restrict work);
void protocore_websocket_find(uint8_t *restrict work);
void protocore_websocket_free(uint8_t *restrict work);
void protocore_websocket_parse(uint8_t *restrict work);
void protocore_websocket_feed_byte(uint8_t *restrict work);
void protocore_websocket_reset_frame(uint8_t *restrict work);
void protocore_websocket_send_frame(uint8_t *restrict work);
void protocore_websocket_set_frag_size(uint8_t *restrict work);
void protocore_websocket_close(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Ws.route_add(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const WsNs Ws __attribute__((unused)) = {
    .route_add = protocore_websocket_route_add,
    .route_reset = protocore_websocket_route_reset,
    .route_connect = protocore_websocket_route_connect,
    .route_message = protocore_websocket_route_message,
    .route_close = protocore_websocket_route_close,
    .init = protocore_websocket_init,
    .active = protocore_websocket_active,
    .payload_of = protocore_websocket_payload_of,
    .alloc = protocore_websocket_alloc,
    .find = protocore_websocket_find,
    .free = protocore_websocket_free,
    .parse = protocore_websocket_parse,
    .feed_byte = protocore_websocket_feed_byte,
    .reset_frame = protocore_websocket_reset_frame,
    .send_frame = protocore_websocket_send_frame,
    .set_frag_size = protocore_websocket_set_frag_size,
    .close = protocore_websocket_close,
};

/** @brief Not an entry: an entry takes a borrow and this is where that borrow comes from. */
uint8_t *protocore_ws_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_WEBSOCKET

#endif
