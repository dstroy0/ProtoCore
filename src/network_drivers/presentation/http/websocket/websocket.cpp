// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file websocket.cpp
 * @brief WebSocket frame parser and connection pool implementation.
 *
 * Handles RFC 6455 framing.  Control frames (ping/pong/close) are handled
 * automatically here; data frames (text/binary) are surfaced to the
 * application layer via WsParseState::WS_FRAME_READY.
 *
 * **Automatic control frame handling**
 * - Ping  -> sends Pong with the same payload immediately.
 * - Close -> sends echoed Close frame, marks slot WsParseState::WS_CLOSED.
 * - Pong  -> silently discarded (keepalive response, no action needed).
 */

#include "websocket.h"
#include "network_drivers/transport/tcp.h"
#include "shared_primitives/utf8.h"
#include <string.h>

#if PC_ENABLE_WS_DEFLATE
#include "network_drivers/presentation/codec/deflate/deflate.h"
#include "network_drivers/presentation/codec/inflate/inflate.h"
#include "server/mmgr/scratch.h"
#endif

WsConn ws_pool[MAX_WS_CONNS];

void ws_init()
{
    for (int i = 0; i < MAX_WS_CONNS; i++)
    {
        ws_pool[i] = {};
        ws_pool[i].ws_id = (uint8_t)i;
    }
}

bool ws_active(uint8_t ws_id)
{
    return ws_id < MAX_WS_CONNS && ws_pool[ws_id].active;
}

const char *ws_payload(uint8_t ws_id)
{
    return (ws_id < MAX_WS_CONNS && ws_pool[ws_id].active) ? (const char *)ws_pool[ws_id].buf : nullptr;
}

WsConn *ws_alloc(uint8_t slot_id)
{
    for (int i = 0; i < MAX_WS_CONNS; i++)
    {
        if (!ws_pool[i].active)
        {
            ws_pool[i] = {};
            ws_pool[i].ws_id = (uint8_t)i;
            ws_pool[i].slot_id = slot_id;
            ws_pool[i].active = true;
            ws_pool[i].parse_state = WsParseState::WS_HEADER1;
            return &ws_pool[i];
        }
    }
    return nullptr;
}

WsConn *ws_find(uint8_t slot_id)
{
    for (int i = 0; i < MAX_WS_CONNS; i++)
    {
        if (ws_pool[i].active && ws_pool[i].slot_id == slot_id)
        {
            return &ws_pool[i];
        }
    }
    return nullptr;
}

void ws_free(uint8_t slot_id)
{
    for (int i = 0; i < MAX_WS_CONNS; i++)
    {
        if (ws_pool[i].active && ws_pool[i].slot_id == slot_id)
        {
            ws_pool[i] = {};
            ws_pool[i].ws_id = (uint8_t)i;
            return;
        }
    }
}

// Reset only the per-frame parser fields, preserving any in-progress
// fragmented-message state (msg_len/msg_opcode/fragmenting/buf). Used to
// resume reading the next frame after handling an interleaved control frame.
static void ws_reset_perframe(WsConn *ws)
{
    ws->parse_state = WsParseState::WS_HEADER1;
    ws->opcode = WsOpcode::WS_OP_TEXT;
    ws->fin = false;
    ws->masked = false;
    ws->payload_len = 0;
    ws->payload_idx = 0;
    ws->len64_count = 0;
    ws->mask_key[0] = ws->mask_key[1] = ws->mask_key[2] = ws->mask_key[3] = 0;
}

void ws_reset_frame(WsConn *ws)
{
    ws_reset_perframe(ws);
    // Also clear reassembly state - a full reset between messages.
    ws->fragmenting = false;
    ws->msg_opcode = WsOpcode::WS_OP_TEXT;
    ws->msg_len = 0;
    ws->buf[0] = '\0';
    ws->ctl_buf[0] = '\0';
}

// ---------------------------------------------------------------------------
// Frame send helpers
// ---------------------------------------------------------------------------

// WebSocket presentation config, owned by one instance (internal linkage): the outbound
// fragmentation size (RFC 6455 sec 5.4), payload bytes; 0 = one frame per message (default).
// One named owner, unreachable cross-TU. (The ws_pool[] table is the shared substrate.)
struct WsCtx
{
    uint16_t frag_size = PC_WS_FRAG_SIZE;
};
static WsCtx s_ws;
void ws_set_frag_size(uint16_t bytes)
{
    s_ws.frag_size = bytes;
}

// Emit one WebSocket frame. b0 is the finished first header byte (FIN | RSV1 | opcode). Server frames
// are never masked (RFC 6455 sec 5.1). Returns false if a transport send fails.
static bool ws_emit_one(TcpConn *conn, uint8_t b0, const uint8_t *payload, uint16_t len)
{
    uint8_t header[4];
    uint8_t hlen;
    header[0] = b0;
    if (len <= 125)
    {
        header[1] = (uint8_t)len;
        hlen = 2;
    }
    else
    {
        header[1] = 126;
        header[2] = (uint8_t)(len >> 8);
        header[3] = (uint8_t)len;
        hlen = 4;
    }
    if (!pc_conn_send(conn->id, header, hlen))
    {
        return false;
    }
    if (len > 0 && payload && !pc_conn_send(conn->id, payload, len))
    {
        return false;
    }
    return true;
}

bool ws_send_frame(WsConn *ws, WsOpcode opcode, const uint8_t *payload, uint16_t len)
{
    TcpConn *conn = &conn_pool[ws->slot_id];
    if (!pc_conn_active(ws->slot_id))
    {
        return false;
    }

    uint8_t rsv1 = 0; // permessage-deflate per-message "compressed" flag (RFC 7692)

#if PC_ENABLE_WS_DEFLATE
    // Compress data frames when permessage-deflate is negotiated. Control frames
    // (close/ping/pong) are never compressed (RFC 7692 sec 5.1). Scratch + output
    // are borrowed from the per-dispatch arena and released when this scope exits;
    // pc_conn_send copies (TCP_WRITE_FLAG_COPY) so the buffer can go immediately.
    ScratchScope scope;
    if (ws->pmd && len > 0 && (opcode == WsOpcode::WS_OP_TEXT || opcode == WsOpcode::WS_OP_BINARY))
    {
        size_t cap = (size_t)len + len / 8 + 16; // static-Huffman worst-case headroom
        void *scr = scratch_alloc(DEFLATE_SCRATCH_SIZE, 16);
        uint8_t *cbuf = (uint8_t *)scratch_alloc(cap, 1);
        if (scr && cbuf)
        {
            size_t clen = 0;
            DeflateResult rc = deflate_raw(payload, len, cbuf, cap, &clen, scr, DEFLATE_SCRATCH_SIZE);
            // Only adopt it if it actually shrank the message; otherwise send it
            // uncompressed (the per-message RSV1 flag makes that legal).
            // rc != DEFLATE_OK is unreachable here: deflate_raw returns non-OK only on
            // ERR_SCRATCH (we always pass the full DEFLATE_SCRATCH_SIZE) or ERR_OVERFLOW,
            // and cap = len + len/8 + 16 exactly bounds the fixed-Huffman worst case
            // (all-9-bit literals = 1.125*len, matches only shrink, +16 covers the fixed
            // header/EOB/stored-trailer/4-byte-marker overhead). The clen < len leg is
            // exercised both ways in test; only the rc-error leg is the dead branch below.
            if (rc == DeflateResult::DEFLATE_OK && clen < len) // GCOVR_EXCL_BR_LINE  dead rc-error leg (see above)
            {
                payload = cbuf;
                len = (uint16_t)clen;
                rsv1 = 0x40;
            }
        }
    }
#endif

    // Fragment only data frames (RFC 6455 §5.4: control frames MUST NOT be fragmented, and are small
    // anyway). frag == 0, a non-data frame, or a message that already fits -> a single FIN frame (the
    // default, unchanged). Server-to-client frames are never masked (§5.1).
    bool data = (opcode == WsOpcode::WS_OP_TEXT || opcode == WsOpcode::WS_OP_BINARY);
    uint16_t frag = s_ws.frag_size;
    if (!data || frag == 0 || len <= frag)
    {
        return ws_emit_one(conn, (uint8_t)(0x80 | rsv1 | (uint8_t)opcode), payload, len);
    }

    // Split into <= frag-byte frames: the opcode (+ RSV1) rides the first frame, the rest are
    // CONTINUATION, and FIN marks the last. The compressed bytes (RFC 7692) are split as-is - the peer
    // concatenates the fragment payloads back into one stream before inflating.
    uint16_t off = 0;
    bool first = true;
    while (off < len)
    {
        uint16_t chunk = (uint16_t)(len - off) < frag ? (uint16_t)(len - off) : frag;
        bool last = (uint16_t)(off + chunk) >= len;
        uint8_t b0 = (uint8_t)((last ? 0x80 : 0x00) |
                               (first ? (rsv1 | (uint8_t)opcode) : (uint8_t)WsOpcode::WS_OP_CONTINUATION));
        if (!ws_emit_one(conn, b0, payload + off, chunk))
        {
            return false;
        }
        off = (uint16_t)(off + chunk);
        first = false;
    }
    return true;
}

void ws_close(WsConn *ws, WsCloseCode code)
{
    // Send Close frame with 2-byte status code payload
    uint8_t payload[2] = {(uint8_t)((uint16_t)code >> 8), (uint8_t)code};
    ws_send_frame(ws, WsOpcode::WS_OP_CLOSE, payload, 2);

    if (pc_conn_active(ws->slot_id))
    {
        pc_conn_flush(ws->slot_id);
    }

    ws->parse_state = WsParseState::WS_CLOSED;
}

// ---------------------------------------------------------------------------
// Frame parser
// ---------------------------------------------------------------------------

// RFC 6455 §5.5: opcodes 0x8 (close), 0x9 (ping), 0xA (pong) are control frames.
static inline bool ws_is_control(WsOpcode op)
{
    return ((uint8_t)op & 0x08) != 0;
}

// Called once a frame's full payload has been received (payload_idx ==
// payload_len, also true immediately for zero-length frames once the masking
// key is consumed).  Control frames are handled in place; data frames are
// reassembled and delivered as WsParseState::WS_FRAME_READY only when the FIN frame arrives.
static void ws_finish_frame(WsConn *ws, TcpConn *conn)
{
    // ---- Control frames (ping/pong/close): use the separate ctl_buf ----
    if (ws_is_control(ws->opcode))
    {
        size_t n = ws->payload_idx < sizeof(ws->ctl_buf) - 1 ? ws->payload_idx : sizeof(ws->ctl_buf) - 1;
        ws->ctl_buf[n] = '\0';

        if (ws->opcode == WsOpcode::WS_OP_PING)
        {
            ws_send_frame(ws, WsOpcode::WS_OP_PONG, ws->ctl_buf, (uint16_t)ws->payload_idx);
            if (pc_conn_active(conn->id))
            {
                pc_conn_flush(conn->id);
            }
        }
        else if (ws->opcode == WsOpcode::WS_OP_CLOSE)
        {
            ws_close(ws, WsCloseCode::WS_CLOSE_NORMAL);
            return;
        }
        // PONG: silently ignored.

        // Resume reading the next frame, keeping any partial data message.
        ws_reset_perframe(ws);
        return;
    }

    // ---- Data frames (text/binary/continuation): reassemble into buf ----
    ws->msg_len += ws->payload_idx;

    if (ws->fin)
    {
#if PC_ENABLE_WS_DEFLATE
        // permessage-deflate: decompress the reassembled message before delivery.
        // The compressed bytes are in ws->buf; append the RFC 7692 00 00 ff ff
        // marker, INFLATE into an arena buffer, and copy the result back. All
        // scratch is borrowed per-dispatch and released when this scope exits.
        if (ws->msg_compressed)
        {
            ScratchScope scope;
            size_t comp_len = ws->msg_len;
            uint8_t *in = (uint8_t *)scratch_alloc(comp_len + 4, 1);
            uint8_t *out = (uint8_t *)scratch_alloc(WS_FRAME_SIZE, 1);
            uint8_t *tbl = (uint8_t *)scratch_alloc(INFLATE_SCRATCH_SIZE, 16);
            if (!in || !out || !tbl)
            {
                ws_close(ws, WsCloseCode::WS_CLOSE_PROTOCOL); // arena exhausted: fail closed
                ws->parse_state = WsParseState::WS_ERROR;
                return;
            }
            memcpy(in, ws->buf, comp_len);
            in[comp_len] = 0x00;
            in[comp_len + 1] = 0x00;
            in[comp_len + 2] = 0xff;
            in[comp_len + 3] = 0xff;
            size_t dlen = 0;
            InflateResult rc = inflate_raw(in, comp_len + 4, out, WS_FRAME_SIZE, &dlen, tbl, INFLATE_SCRATCH_SIZE);
            if (rc == InflateResult::INFLATE_ERR_OVERFLOW)
            {
                ws_close(ws, WsCloseCode::WS_CLOSE_TOO_BIG);
                ws->parse_state = WsParseState::WS_ERROR;
                return;
            }
            if (rc != InflateResult::INFLATE_OK)
            {
                ws_close(ws, WsCloseCode::WS_CLOSE_PROTOCOL);
                ws->parse_state = WsParseState::WS_ERROR;
                return;
            }
            memcpy(ws->buf, out, dlen);
            ws->msg_len = dlen;
            ws->msg_compressed = false;
        }
#endif
        // Whole message received - surface it to the application.
        size_t n = ws->msg_len < WS_FRAME_SIZE ? ws->msg_len : WS_FRAME_SIZE;
        // RFC 6455 8.1: a TEXT message MUST be valid UTF-8 (checked on the fully
        // reassembled + decompressed message); otherwise fail the connection with 1007.
        if (ws->msg_opcode == WsOpcode::WS_OP_TEXT && !pc_utf8_valid(ws->buf, n))
        {
            ws_close(ws, WsCloseCode::WS_CLOSE_INVALID_PAYLOAD);
            ws->parse_state = WsParseState::WS_ERROR;
            return;
        }
        ws->buf[n] = '\0';
        ws->opcode = ws->msg_opcode;   // report the original TEXT/BINARY opcode
        ws->payload_len = ws->msg_len; // app reads payload_len / payload_idx
        ws->payload_idx = ws->msg_len;
        ws->fragmenting = false;
        ws->parse_state = WsParseState::WS_FRAME_READY;
    }
    else
    {
        // More fragments to come; keep buf and msg_len, read the next frame.
        ws->fragmenting = true;
        ws_reset_perframe(ws);
    }
}

void ws_parse(WsConn *ws)
{
    if (!pc_conn_active(ws->slot_id))
    {
        return;
    }

    while (pc_conn_available(ws->slot_id) > 0)
    {
        // Stop if we hit a terminal state (leave the rest in the ring)
        if (ws->parse_state == WsParseState::WS_FRAME_READY || ws->parse_state == WsParseState::WS_CLOSED ||
            ws->parse_state == WsParseState::WS_ERROR)
        {
            return;
        }

        uint8_t byte = 0;
        // Unreachable by construction (not merely untested): this while-condition's
        // pc_conn_available() and this call's pc_conn_read_byte() read the identical
        // rx_head/rx_tail pair for this slot with no call in between that could touch either
        // index. rx_head is advanced only by lowlevel_recv_cb() (tcp.cpp), which refuses
        // (ERR_MEM, backpressure) any segment that would not fit in the free space already
        // computed against rx_tail - so head can never overrun tail, meaning available() can
        // only grow, never shrink, between the two reads. rx_tail is advanced only by this
        // same synchronous consumer call. So available() > 0 here structurally guarantees
        // read_byte() succeeds - true on the host AND on-device (SPSC ring, single producer /
        // single consumer per slot); there is no interrupt-race window at this specific call
        // site to reproduce.
        if (!pc_conn_read_byte(ws->slot_id, &byte)) // GCOVR_EXCL_BR_LINE  see above: cannot fail here by construction
        {
            break; // GCOVR_EXCL_LINE
        }
        ws_feed_byte(ws, byte);
    }
}

void ws_feed_byte(WsConn *ws, uint8_t byte)
{
    TcpConn *conn = &conn_pool[ws->slot_id];
    {
        switch (ws->parse_state)
        {
        case WsParseState::WS_HEADER1: {
            ws->fin = (byte & 0x80) != 0;
            // RSV bits are validated below, once the opcode / message position is
            // known (RSV1 is permessage-deflate's per-message "compressed" flag).
            uint8_t rsv = byte & 0x70;
            ws->opcode = static_cast<WsOpcode>(byte & 0x0F);
            // RFC 6455 §5.2: only opcodes 0x0/0x1/0x2 (data) and 0x8/0x9/0xA
            // (control) are defined; everything else MUST fail the connection.
            switch (ws->opcode)
            {
            case WsOpcode::WS_OP_CONTINUATION:
            case WsOpcode::WS_OP_TEXT:
            case WsOpcode::WS_OP_BINARY:
            case WsOpcode::WS_OP_CLOSE:
            case WsOpcode::WS_OP_PING:
            case WsOpcode::WS_OP_PONG:
                break;
            default:
                ws_close(ws, WsCloseCode::WS_CLOSE_PROTOCOL);
                ws->parse_state = WsParseState::WS_ERROR;
                return;
            }
            // RFC 6455 §5.5: control frames MUST NOT be fragmented (FIN set).
            if (ws_is_control(ws->opcode) && !ws->fin)
            {
                ws_close(ws, WsCloseCode::WS_CLOSE_PROTOCOL);
                ws->parse_state = WsParseState::WS_ERROR;
                return;
            }
            // RFC 6455 §5.4: fragmentation sequencing for data frames.
            if (!ws_is_control(ws->opcode))
            {
                if (ws->opcode == WsOpcode::WS_OP_CONTINUATION)
                {
                    // A continuation with no message in progress is illegal.
                    if (!ws->fragmenting)
                    {
                        ws_close(ws, WsCloseCode::WS_CLOSE_PROTOCOL);
                        ws->parse_state = WsParseState::WS_ERROR;
                        return;
                    }
                }
                else
                {
                    // A new text/binary frame while a message is still open is
                    // illegal - the previous message must finish first.
                    if (ws->fragmenting)
                    {
                        ws_close(ws, WsCloseCode::WS_CLOSE_PROTOCOL);
                        ws->parse_state = WsParseState::WS_ERROR;
                        return;
                    }
                    // Start of a new data message.
                    ws->msg_opcode = ws->opcode;
                    ws->msg_len = 0;
#if PC_ENABLE_WS_DEFLATE
                    // RSV1 on the first frame of a data message marks it compressed
                    // (RFC 7692); only honored when permessage-deflate was negotiated.
                    ws->msg_compressed = ws->pmd && (rsv & 0x40);
#endif
                }
            }
            // Validate reserved bits. RSV2/RSV3 are never legal; RSV1 is legal only
            // as the per-message compression flag set above (pmd + new data frame).
#if PC_ENABLE_WS_DEFLATE
            {
                bool new_data = !ws_is_control(ws->opcode) && ws->opcode != WsOpcode::WS_OP_CONTINUATION;
                if ((rsv & 0x30) || ((rsv & 0x40) && !(ws->pmd && new_data)))
                {
                    ws_close(ws, WsCloseCode::WS_CLOSE_PROTOCOL);
                    ws->parse_state = WsParseState::WS_ERROR;
                    return;
                }
            }
#else
            if (rsv)
            {
                ws_close(ws, WsCloseCode::WS_CLOSE_PROTOCOL);
                ws->parse_state = WsParseState::WS_ERROR;
                return;
            }
#endif
            ws->parse_state = WsParseState::WS_HEADER2;
            break;
        }

        case WsParseState::WS_HEADER2:
            ws->masked = (byte & 0x80) != 0;
            // RFC 6455 §5.1: every client-to-server frame MUST be masked.
            if (!ws->masked)
            {
                ws_close(ws, WsCloseCode::WS_CLOSE_PROTOCOL);
                ws->parse_state = WsParseState::WS_ERROR;
                return;
            }
            {
                uint8_t len7 = byte & 0x7F;
                // RFC 6455 §5.5: control frames MUST have payload length <= 125.
                if (ws_is_control(ws->opcode) && len7 > 125)
                {
                    ws_close(ws, WsCloseCode::WS_CLOSE_PROTOCOL);
                    ws->parse_state = WsParseState::WS_ERROR;
                    return;
                }
                if (len7 <= 125)
                {
                    // Masking is mandatory, so always consume the 4 mask bytes
                    // next - even for zero-length frames (WsParseState::WS_MASK3 finishes them).
                    ws->payload_len = len7;
                    // Reassembled data message must fit in WS_FRAME_SIZE.
                    if (!ws_is_control(ws->opcode) && ws->msg_len + ws->payload_len > WS_FRAME_SIZE)
                    {
                        ws_close(ws, WsCloseCode::WS_CLOSE_TOO_BIG);
                        ws->parse_state = WsParseState::WS_ERROR;
                        return;
                    }
                    ws->parse_state = WsParseState::WS_MASK0;
                }
                else if (len7 == 126)
                {
                    ws->payload_len = 0;
                    ws->parse_state = WsParseState::WS_LEN16_HI;
                }
                else
                {
                    // 64-bit length -- always too large
                    ws->len64_count = 0;
                    ws->parse_state = WsParseState::WS_LEN64;
                }
            }
            break;

        case WsParseState::WS_LEN16_HI:
            ws->payload_len = (uint32_t)byte << 8;
            ws->parse_state = WsParseState::WS_LEN16_LO;
            break;

        case WsParseState::WS_LEN16_LO:
            ws->payload_len |= byte;
            // 16-bit length only occurs on data frames (control frames are
            // capped at 125); the reassembled message must fit WS_FRAME_SIZE.
            if (ws->msg_len + ws->payload_len > WS_FRAME_SIZE)
            {
                ws_close(ws, WsCloseCode::WS_CLOSE_TOO_BIG);
                ws->parse_state = WsParseState::WS_ERROR;
                return;
            }
            // Masking is mandatory; consume the 4 mask bytes next.
            ws->parse_state = WsParseState::WS_MASK0;
            break;

        case WsParseState::WS_LEN64:
            // Consume all 8 bytes then reject
            if (++ws->len64_count == 8)
            {
                ws_close(ws, WsCloseCode::WS_CLOSE_TOO_BIG);
                ws->parse_state = WsParseState::WS_ERROR;
                return;
            }
            break;

        case WsParseState::WS_MASK0:
            ws->mask_key[0] = byte;
            ws->parse_state = WsParseState::WS_MASK1;
            break;
        case WsParseState::WS_MASK1:
            ws->mask_key[1] = byte;
            ws->parse_state = WsParseState::WS_MASK2;
            break;
        case WsParseState::WS_MASK2:
            ws->mask_key[2] = byte;
            ws->parse_state = WsParseState::WS_MASK3;
            break;
        case WsParseState::WS_MASK3:
            ws->mask_key[3] = byte;
            if (ws->payload_len > 0)
            {
                ws->parse_state = WsParseState::WS_PAYLOAD;
            }
            else
            {
                ws_finish_frame(ws, conn); // zero-length frame is complete now
            }
            break;

        case WsParseState::WS_PAYLOAD: {
            // Mask is applied per frame, so the keystream index is the
            // within-frame position.
            uint8_t unmasked = byte ^ ws->mask_key[ws->payload_idx % 4];
            if (ws_is_control(ws->opcode))
            {
                // Control payload goes to its own buffer so it never disturbs
                // a partially-assembled data message.
                if (ws->payload_idx < sizeof(ws->ctl_buf) - 1)
                {
                    ws->ctl_buf[ws->payload_idx] = unmasked;
                }
            }
            else
            {
                // Data payload appends after any earlier fragments.
                uint32_t pos = ws->msg_len + ws->payload_idx;
                if (pos < WS_FRAME_SIZE)
                {
                    ws->buf[pos] = unmasked;
                }
            }
            ws->payload_idx++;

            if (ws->payload_idx >= ws->payload_len)
            {
                ws_finish_frame(ws, conn);
            }
            break;
        }

        default:
            break;
        }
    }
}
