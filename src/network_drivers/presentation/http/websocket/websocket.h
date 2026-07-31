// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
 * WsParseState::WS_HEADER1       -- read FIN + opcode byte
 * WsParseState::WS_HEADER2       -- read MASK + 7-bit payload length
 * WsParseState::WS_LEN16_HI      -- read extended 16-bit length high byte
 * WsParseState::WS_LEN16_LO      -- read extended 16-bit length low byte
 * WsParseState::WS_LEN64         -- consume 8-byte 64-bit length (reject; too large)
 * WsParseState::WS_MASK0..3      -- read 4-byte masking key
 * WsParseState::WS_PAYLOAD       -- accumulate payload bytes (unmasked)
 * WsParseState::WS_FRAME_READY   -- complete frame waiting for dispatch
 * WsParseState::WS_CLOSED        -- connection closed; slot may be recycled
 * WsParseState::WS_ERROR         -- protocol error; close frame sent
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

#include "network_drivers/transport/tcp.h"
#include "protocore_config.h"

// ---------------------------------------------------------------------------
// WebSocket opcodes (RFC 6455 §5.2)
// ---------------------------------------------------------------------------

/** @brief WebSocket frame opcodes. */
enum class WsOpcode : uint8_t
{
    WS_OP_CONTINUATION = 0x0, ///< Continuation frame (data-message fragment; reassembled into buf).
    WS_OP_TEXT = 0x1,         ///< UTF-8 text payload.
    WS_OP_BINARY = 0x2,       ///< Binary payload.
    WS_OP_CLOSE = 0x8,        ///< Connection close.
    WS_OP_PING = 0x9,         ///< Ping (auto-ponged by the library).
    WS_OP_PONG = 0xA          ///< Pong (echoed ping; ignored by library).
};

/** @brief WebSocket close status codes (RFC 6455 §7.4.1). */
enum class WsCloseCode : uint16_t
{
    WS_CLOSE_NORMAL = 1000,          ///< Normal closure.
    WS_CLOSE_GOING_AWAY = 1001,      ///< Endpoint going away.
    WS_CLOSE_PROTOCOL = 1002,        ///< Protocol error.
    WS_CLOSE_UNSUPPORTED = 1003,     ///< Received a data type the endpoint cannot accept (RFC 6455).
    WS_CLOSE_INVALID_PAYLOAD = 1007, ///< Text message that is not valid UTF-8 (RFC 6455 8.1).
    WS_CLOSE_TOO_BIG = 1009          ///< Payload too large for WS_FRAME_SIZE.
};

// ---------------------------------------------------------------------------
// Frame parser states
// ---------------------------------------------------------------------------

/** @brief States of the WebSocket frame parser. */
enum class WsParseState : uint8_t
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
};

// ---------------------------------------------------------------------------
// Per-connection WebSocket state
// ---------------------------------------------------------------------------

/**
 * @brief WebSocket connection state stored in ws_pool[].
 *
 * Allocated when an HTTP upgrade handshake succeeds.  slot_id ties this
 * entry back to conn_pool[] and the ring buffer.
 */
struct WsConn
{
    uint8_t ws_id;   ///< Index into ws_pool[] (set at init).
    uint8_t slot_id; ///< Owning TCP slot in conn_pool[].
    bool active;     ///< True when this entry is in use.

    WsParseState parse_state; ///< Current frame parser state.
    WsOpcode opcode;          ///< Opcode of the frame being parsed.
    bool fin;                 ///< FIN bit of the frame being parsed.
    bool masked;              ///< True if client sent a masking key.

    uint8_t mask_key[4];            ///< Client masking key.
    uint32_t payload_len;           ///< Expected payload byte count (current frame).
    uint32_t payload_idx;           ///< Bytes received so far (current frame).
    uint8_t len64_count;            ///< Bytes consumed from 64-bit length.
    uint8_t buf[WS_FRAME_SIZE + 1]; ///< Reassembled message payload, null-terminated.

    // Fragmentation state (RFC 6455 §5.4). A data message may span multiple
    // frames (first text/binary with FIN=0, then continuation frames). Control
    // frames may be interleaved and use a separate buffer so they never clobber
    // the partially-assembled data message.
    bool fragmenting;         ///< True between a non-FIN data frame and its FIN.
    WsOpcode msg_opcode;      ///< Opcode of the data message being assembled.
    uint32_t msg_len;         ///< Bytes assembled so far across all fragments.
    uint8_t ctl_buf[125 + 1]; ///< Control-frame payload (ping/pong/close), null-terminated.

#if PC_ENABLE_WS_DEFLATE
    bool pmd;            ///< permessage-deflate negotiated on this connection (RFC 7692).
    bool msg_compressed; ///< Current data message arrived compressed (RSV1 on its first frame).
#endif
};

/** @brief Pool of WebSocket connection state, one per MAX_WS_CONNS. */
extern WsConn ws_pool[MAX_WS_CONNS];

// ---------------------------------------------------------------------------
// WebSocket API
// ---------------------------------------------------------------------------

/**
 * @brief Initialize all WebSocket pool slots to inactive.
 *
 * Called once from PC::begin().
 */
void ws_init();

/// @brief True if @p ws_id is a valid, in-use WebSocket slot. Use this instead of reaching into
///        ws_pool[ws_id].active from another module.
bool ws_active(uint8_t ws_id);

/// @brief The NUL-terminated reassembled message payload for @p ws_id, or nullptr if the slot is
///        out of range / inactive. Use this instead of reaching into ws_pool[ws_id].buf.
const char *ws_payload(uint8_t ws_id);

/**
 * @brief Allocate a WsConn slot and bind it to a TCP slot.
 *
 * @param slot_id  TCP connection slot that just completed an upgrade.
 * @return Pointer to the allocated WsConn, or nullptr if the pool is full.
 */
WsConn *ws_alloc(uint8_t slot_id);

/**
 * @brief Find the WsConn for a given TCP slot, or nullptr if none.
 *
 * @param slot_id  TCP connection slot index.
 */
WsConn *ws_find(uint8_t slot_id);

/**
 * @brief Free the WsConn associated with a TCP slot.
 *
 * @param slot_id  TCP connection slot index.
 */
void ws_free(uint8_t slot_id);

/**
 * @brief Drain the ring buffer for slot_id and feed bytes to the WS parser.
 *
 * Stops when the ring buffer is empty or the parser reaches a terminal state
 * (WsParseState::WS_FRAME_READY, WsParseState::WS_CLOSED, WsParseState::WS_ERROR).
 *
 * @param ws  WebSocket connection to drain into.
 */
void ws_parse(WsConn *ws);

/**
 * @brief Feed one already-plaintext byte through the WS frame state machine.
 *
 * The per-byte core shared by ws_parse() (which reads the plaintext rx ring) and
 * the TLS receive path (which decrypts ciphertext and feeds the plaintext here).
 * Callers must stop feeding once parse_state reaches a terminal state
 * (WsParseState::WS_FRAME_READY / WsParseState::WS_CLOSED / WsParseState::WS_ERROR) and dispatch/reset before
 * continuing.
 *
 * @param ws    WebSocket connection.
 * @param byte  Next plaintext byte of the client frame stream.
 */
void ws_feed_byte(WsConn *ws, uint8_t byte);

/**
 * @brief Reset the frame parser back to WsParseState::WS_HEADER1, ready for the next frame.
 *
 * Does not change ws->active or ws->slot_id.
 *
 * @param ws  WebSocket connection to reset.
 */
void ws_reset_frame(WsConn *ws);

/**
 * @brief Send a WebSocket frame to the client.
 *
 * Builds the header (no masking -- server-to-client frames are never masked)
 * and hands both to the transport layer (pc_conn_send()).  The caller is
 * responsible for flushing afterwards (pc_conn_flush()).
 *
 * @param ws       WebSocket connection.
 * @param opcode   Frame opcode (WsOpcode::WS_OP_TEXT, WsOpcode::WS_OP_BINARY, WsOpcode::WS_OP_PONG, etc.).
 * @param payload  Payload bytes (may be nullptr for zero-length frames).
 * @param len      Payload length in bytes.
 * @return true on success, false if the TCP slot is not active.
 */
bool ws_send_frame(WsConn *ws, WsOpcode opcode, const uint8_t *payload, uint16_t len);

/**
 * @brief Set the outbound fragmentation size (RFC 6455 sec 5.4), in payload bytes; 0 = off.
 *
 * A runtime override of PC_WS_FRAG_SIZE. When >0, a data message longer than @p bytes is split into
 * that-sized frames by ws_send_frame() (see PC_WS_FRAG_SIZE). Applies to all connections.
 */
void ws_set_frag_size(uint16_t bytes);

/**
 * @brief Send a Close frame and mark the slot WsParseState::WS_CLOSED.
 *
 * @param ws    WebSocket connection.
 * @param code  Close status code (e.g. WsCloseCode::WS_CLOSE_NORMAL).
 */
void ws_close(WsConn *ws, WsCloseCode code);

#endif
