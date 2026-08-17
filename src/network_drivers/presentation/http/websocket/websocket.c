// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file websocket.c
 * @brief WebSocket frame parser and connection pool implementation.
 *
 * Handles RFC 6455 framing.  Control frames (ping/pong/close) are handled
 * automatically here; data frames (text/binary) are surfaced to the
 * application layer via WS_FRAME_READY.
 *
 * **Automatic control frame handling**
 * - Ping  -> sends Pong with the same payload immediately.
 * - Close -> sends echoed Close frame, marks slot WS_CLOSED.
 * - Pong  -> silently discarded (keepalive response, no action needed).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_WEBSOCKET

#include "mmgr/protomem.h"
#include "mmgr/secure.h"                                     // the persistent end this module's state is taken from
#include "mmgr/span.h"                                       // span.ok: whether the pool had the bytes
#include "network_drivers/presentation/http/websocket/websocket.h"
#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool: the slot a frame goes out on
#include "shared/utf8/utf8.h"

#if PROTOCORE_ENABLE_WS_DEFLATE
#include "mmgr/plaintext.h"
#include "network_drivers/presentation/codec/deflate/deflate.h"
#include "network_drivers/presentation/codec/inflate/inflate.h"
#endif

WsConn ws_pool[MAX_WS_CONNS];

// One route's handlers. They belong here rather than in the route table: a route decides where a
// request goes, and what runs once a socket is open is this module's business. A route carries the
// id that names the set, so nothing above has to hold a pointer into this module.
typedef struct
{
    WsConnectHandler on_connect;
    WsMessageHandler on_message;
    WsCloseHandler on_close;
} WsRoute;

// The per-route handler sets, the read scratch, and the outbound fragmentation size (RFC 6455
// sec 5.4) in payload bytes, 0 = one frame per message. Only what is not derivable: the regions
// live at fixed offsets in the caller's borrow, so the macro below computes them from the pointer
// rather than the context storing them.
typedef struct
{
    WsRoute route[MAX_ROUTES]; ///< one handler set per route
    uint8_t route_count;       ///< how many routes recorded one
    uint8_t rx[RX_BUF_SIZE];   ///< where a slot's bytes are staged for the frame walk
    uint16_t frag_size;        ///< outbound fragmentation size; 0 = one frame per message
} WsCtx;

// The caller's borrow, split: the context at its offset. One pointer arrives and every region is
// that pointer plus a compile-time offset, so the assert below proves the span covers them before
// anything runs.
#define WS_OFF_CTX 0u
static_assert(WS_OFF_CTX + sizeof(WsCtx) <= PROTOCORE_WS_BORROW,
              "PROTOCORE_WS_BORROW is short of the module context - raise it in protocore_config.h,"
              " which sums it into its arena");

// The region, at its offset in the caller's borrow.
#define WS_CTX(w) ((WsCtx *)(void *)((w) + WS_OFF_CTX))

static void init(uint8_t *restrict work)
{
    (void)work;
    for (int i = 0; i < MAX_WS_CONNS; i++)
    {
        ws_pool[i] = (WsConn){0};
        ws_pool[i].ws_id = (uint8_t)i;
    }
}

static void active(uint8_t *restrict work)
{
    (void)work;
    Ws.ok = Ws.ws_id < MAX_WS_CONNS && ws_pool[Ws.ws_id].active;
}

static void payload_of(uint8_t *restrict work)
{
    (void)work;
    Ws.text = (Ws.ws_id < MAX_WS_CONNS && ws_pool[Ws.ws_id].active) ? (const char *)ws_pool[Ws.ws_id].buf : NULL;
}

static void alloc(uint8_t *restrict work)
{
    (void)work;
    Ws.found = NULL;
    for (int i = 0; i < MAX_WS_CONNS; i++)
    {
        if (!ws_pool[i].active)
        {
            ws_pool[i] = (WsConn){0};
            ws_pool[i].ws_id = (uint8_t)i;
            ws_pool[i].slot_id = Ws.slot;
            ws_pool[i].route_id = Ws.id;
            ws_pool[i].active = PROTO_TRUE;
            ws_pool[i].parse_state = WS_HEADER1;
            Ws.found = &ws_pool[i];
            return;
        }
    }
}

static void find(uint8_t *restrict work)
{
    (void)work;
    Ws.found = NULL;
    for (int i = 0; i < MAX_WS_CONNS; i++)
    {
        if (ws_pool[i].active && ws_pool[i].slot_id == Ws.slot)
        {
            Ws.found = &ws_pool[i];
            return;
        }
    }
}

static void release(uint8_t *restrict work)
{
    (void)work;
    for (int i = 0; i < MAX_WS_CONNS; i++)
    {
        if (ws_pool[i].active && ws_pool[i].slot_id == Ws.slot)
        {
            ws_pool[i] = (WsConn){0};
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
    ws->parse_state = WS_HEADER1;
    ws->opcode = WS_OP_TEXT;
    ws->fin = PROTO_FALSE;
    ws->masked = PROTO_FALSE;
    ws->payload_len = 0;
    ws->payload_idx = 0;
    ws->len64_count = 0;
    ws->mask_key[0] = ws->mask_key[1] = ws->mask_key[2] = ws->mask_key[3] = 0;
}

void ws_reset_frame(WsConn *ws)
{
    ws_reset_perframe(ws);
    // Also clear reassembly state - a full reset between messages.
    ws->fragmenting = PROTO_FALSE;
    ws->msg_opcode = WS_OP_TEXT;
    ws->msg_len = 0;
    ws->buf[0] = '\0';
    ws->ctl_buf[0] = '\0';
}

// ---------------------------------------------------------------------------
// Frame send helpers
// ---------------------------------------------------------------------------

static void route_add(uint8_t *restrict work)
{
    if (WS_CTX(work)->route_count >= MAX_ROUTES)
    {
        Ws.u8 = PROTOCORE_WS_NONE;
        return;
    }
    WsRoute *w = &WS_CTX(work)->route[WS_CTX(work)->route_count];
    w->on_connect = Ws.route.on_connect;
    w->on_message = Ws.route.on_message;
    w->on_close = Ws.route.on_close;
    Ws.u8 = WS_CTX(work)->route_count++;
}

// Empty the handler table. A route holds the id an add returned, so this belongs with whatever
// empties the routes - otherwise every re-registration appends a set nothing can reach any more,
// and the table, which is bounded, fills and starts refusing.
static void route_reset(uint8_t *restrict work)
{
    WS_CTX(work)->route_count = 0;
}

static void route_connect(uint8_t *restrict work)
{
    Ws.connect_handler = (Ws.id >= WS_CTX(work)->route_count) ? NULL : WS_CTX(work)->route[Ws.id].on_connect;
}

static void route_message(uint8_t *restrict work)
{
    Ws.message_handler = (Ws.id >= WS_CTX(work)->route_count) ? NULL : WS_CTX(work)->route[Ws.id].on_message;
}

static void route_close(uint8_t *restrict work)
{
    Ws.close_handler = (Ws.id >= WS_CTX(work)->route_count) ? NULL : WS_CTX(work)->route[Ws.id].on_close;
}

static void set_frag_size(uint8_t *restrict work)
{
    WS_CTX(work)->frag_size = Ws.frag_size;
}

// Emit one WebSocket frame. b0 is the finished first header byte (FIN | RSV1 | opcode). Server frames
// are never masked (RFC 6455 sec 5.1). Returns false if a transport send fails.
static proto_bool ws_emit_one(uint8_t slot, uint8_t b0, const uint8_t *payload, uint16_t len)
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
    ConnPool.slot = slot;
    ConnPool.io.data = header;
    ConnPool.io.len = hlen;
    ConnPool.send(ConnPool.internal);
    if (!ConnPool.ok)
    {
        return PROTO_FALSE;
    }
    if (len > 0 && payload)
    {
        ConnPool.io.data = payload;
        ConnPool.io.len = len;
        ConnPool.send(ConnPool.internal);
        if (!ConnPool.ok)
        {
            return PROTO_FALSE;
        }
    }
    return PROTO_TRUE;
}

static void send_frame(uint8_t *restrict work)
{
    WsConn *ws = Ws.conn;
    WsOpcode opcode = Ws.frame.opcode;
    const uint8_t *payload = Ws.frame.payload;
    uint16_t len = Ws.frame.len;
    const uint8_t slot = ws->slot_id;

    Ws.ok = PROTO_FALSE;
    ConnPool.slot = slot;
    ConnPool.active(ConnPool.internal);
    if (!ConnPool.ok)
    {
        return;
    }

    uint8_t rsv1 = 0; // permessage-deflate per-message "compressed" flag (RFC 7692)

#if PROTOCORE_ENABLE_WS_DEFLATE
    // Compress data frames when permessage-deflate is negotiated. Control frames
    // (close/ping/pong) are never compressed (RFC 7692 sec 5.1). Scratch + output
    // are borrowed from the per-dispatch arena and released when this scope exits;
    // the send copies (TCP_WRITE_FLAG_COPY) so the buffer can go immediately.
    // PROTOCORE_WS_DEFLATE_MAX bounds what the compressor accepts, so the borrow below has a compile-time
    // worst case and cannot fail. A longer message is sent uncompressed, which the per-message RSV1
    // flag makes legal.
    static_assert(PROTOCORE_PLAINTEXT_WORK_WS_SEND <= PROTOCORE_PLAINTEXT_ARENA_SIZE,
                  "WS deflate scratch exceeds the arena");
    // The compressed buffer is handed to `payload` below and read by the emit calls at the end of
    // this function, so the borrow spans the whole function and is released at each exit.
    size_t pt_mark = protocore_plaintext_mark();
    if (ws->pmd && len > 0 && len <= PROTOCORE_WS_DEFLATE_MAX && (opcode == WS_OP_TEXT || opcode == WS_OP_BINARY))
    {
        size_t cap = (size_t)len + len / 8 + 16; // static-Huffman worst-case headroom
        void *scr = protocore_plaintext_alloc(DEFLATE_SCRATCH_SIZE, 16);
        uint8_t *cbuf = (uint8_t *)protocore_plaintext_alloc(cap, 1);
        if (scr && cbuf)
        {
            size_t clen = 0;
            DeflateResult rc = Deflate.raw(payload, len, cbuf, cap, &clen, scr, DEFLATE_SCRATCH_SIZE);
            // Only adopt it if it actually shrank the message; otherwise send it
            // uncompressed (the per-message RSV1 flag makes that legal).
            // rc != DEFLATE_OK is unreachable here: Deflate.raw returns non-OK only on
            // ERR_SCRATCH (we always pass the full DEFLATE_SCRATCH_SIZE) or ERR_OVERFLOW,
            // and cap = len + len/8 + 16 exactly bounds the fixed-Huffman worst case
            // (all-9-bit literals = 1.125*len, matches only shrink, +16 covers the fixed
            // header/EOB/stored-trailer/4-byte-marker overhead). The clen < len leg is
            // exercised both ways in test; only the rc-error leg is the dead branch below.
            if (rc == DEFLATE_OK && clen < len)
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
    proto_bool data = (opcode == WS_OP_TEXT || opcode == WS_OP_BINARY);
    uint16_t frag = WS_CTX(work)->frag_size;
    if (!data || frag == 0 || len <= frag)
    {
        proto_bool sent = ws_emit_one(slot, (uint8_t)(0x80 | rsv1 | (uint8_t)opcode), payload, len);
#if PROTOCORE_ENABLE_WS_DEFLATE
        protocore_plaintext_release(pt_mark);
#endif
        Ws.ok = sent;
        return;
    }

    // Split into <= frag-byte frames: the opcode (+ RSV1) rides the first frame, the rest are
    // CONTINUATION, and FIN marks the last. The compressed bytes (RFC 7692) are split as-is - the peer
    // concatenates the fragment payloads back into one stream before inflating.
    uint16_t off = 0;
    proto_bool first = PROTO_TRUE;
    while (off < len)
    {
        uint16_t chunk = (uint16_t)(len - off) < frag ? (uint16_t)(len - off) : frag;
        proto_bool last = (uint16_t)(off + chunk) >= len;
        uint8_t b0 = (uint8_t)((last ? 0x80 : 0x00) | (first ? (rsv1 | (uint8_t)opcode) : (uint8_t)WS_OP_CONTINUATION));
        if (!ws_emit_one(slot, b0, payload + off, chunk))
        {
#if PROTOCORE_ENABLE_WS_DEFLATE
            protocore_plaintext_release(pt_mark);
#endif
            return;
        }
        off = (uint16_t)(off + chunk);
        first = PROTO_FALSE;
    }
#if PROTOCORE_ENABLE_WS_DEFLATE
    protocore_plaintext_release(pt_mark);
#endif
    Ws.ok = PROTO_TRUE;
}

static void close_socket(uint8_t *restrict work)
{
    (void)work;
    WsConn *ws = Ws.conn;
    // Send Close frame with 2-byte status code payload
    uint8_t payload[2] = {(uint8_t)((uint16_t)Ws.frame.code >> 8), (uint8_t)Ws.frame.code};
    Ws.frame.opcode = WS_OP_CLOSE;
    Ws.frame.payload = payload;
    Ws.frame.len = 2;
    send_frame(work);

    ConnPool.slot = ws->slot_id;
    ConnPool.active(ConnPool.internal);
    if (ConnPool.ok)
    {
        ConnPool.flush(ConnPool.internal);
    }

    ws->parse_state = WS_CLOSED;
}

// ---------------------------------------------------------------------------
// Frame parser
// ---------------------------------------------------------------------------

// RFC 6455 §5.5: opcodes 0x8 (close), 0x9 (ping), 0xA (pong) are control frames.
static inline proto_bool ws_is_control(WsOpcode op)
{
    return ((uint8_t)op & 0x08) != 0;
}

// Called once a frame's full payload has been received (payload_idx ==
// payload_len, also true immediately for zero-length frames once the masking
// key is consumed).  Control frames are handled in place; data frames are
// reassembled and delivered as WS_FRAME_READY only when the FIN frame arrives.
static void ws_finish_frame(uint8_t *restrict work, WsConn *ws)
{
    // ---- Control frames (ping/pong/close): use the separate ctl_buf ----
    if (ws_is_control(ws->opcode))
    {
        size_t n = ws->payload_idx < sizeof(ws->ctl_buf) - 1 ? ws->payload_idx : sizeof(ws->ctl_buf) - 1;
        ws->ctl_buf[n] = '\0';

        if (ws->opcode == WS_OP_PING)
        {
            Ws.conn = ws;
            Ws.frame.opcode = WS_OP_PONG;
            Ws.frame.payload = ws->ctl_buf;
            Ws.frame.len = (uint16_t)ws->payload_idx;
            send_frame(work);
            ConnPool.slot = ws->slot_id;
            ConnPool.active(ConnPool.internal);
            if (ConnPool.ok)
            {
                ConnPool.flush(ConnPool.internal);
            }
        }
        else if (ws->opcode == WS_OP_CLOSE)
        {
            Ws.conn = ws;
            Ws.frame.code = WS_CLOSE_NORMAL;
            close_socket(work);
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
#if PROTOCORE_ENABLE_WS_DEFLATE
        // permessage-deflate: decompress the reassembled message before delivery.
        // The compressed bytes are in ws->buf; append the RFC 7692 00 00 ff ff
        // marker, INFLATE into an arena buffer, and copy the result back. All
        // scratch is borrowed per-dispatch and released when this scope exits.
        if (ws->msg_compressed)
        {
            // The parser closes 1009 before msg_len passes WS_FRAME_SIZE, so all three borrows are
            // bounded and cannot fail.
            static_assert(PROTOCORE_PLAINTEXT_WORK_WS_RECV <= PROTOCORE_PLAINTEXT_ARENA_SIZE,
                          "WS inflate scratch exceeds the arena");
            size_t pt_mark = protocore_plaintext_mark();
            size_t comp_len = ws->msg_len;
            uint8_t *in = (uint8_t *)protocore_plaintext_alloc(comp_len + 4, 1);
            uint8_t *out = (uint8_t *)protocore_plaintext_alloc(WS_FRAME_SIZE, 1);
            uint8_t *tbl = (uint8_t *)protocore_plaintext_alloc(INFLATE_SCRATCH_SIZE, 16);
            if (!in || !out || !tbl)
            {
                protocore_plaintext_release(pt_mark);
                Ws.conn = ws;
                Ws.frame.code = WS_CLOSE_PROTOCOL;
                close_socket(work); // arena exhausted: fail closed
                ws->parse_state = WS_ERROR;
                return;
            }
            mem.cpy(in, ws->buf, comp_len);
            in[comp_len] = 0x00;
            in[comp_len + 1] = 0x00;
            in[comp_len + 2] = 0xff;
            in[comp_len + 3] = 0xff;
            size_t dlen = 0;
            InflateResult rc = Inflate.raw(in, comp_len + 4, out, WS_FRAME_SIZE, &dlen, tbl, INFLATE_SCRATCH_SIZE);
            if (rc == INFLATE_ERR_OVERFLOW)
            {
                protocore_plaintext_release(pt_mark);
                Ws.conn = ws;
                Ws.frame.code = WS_CLOSE_TOO_BIG;
                close_socket(work);
                ws->parse_state = WS_ERROR;
                return;
            }
            if (rc != INFLATE_OK)
            {
                protocore_plaintext_release(pt_mark);
                Ws.conn = ws;
                Ws.frame.code = WS_CLOSE_PROTOCOL;
                close_socket(work);
                ws->parse_state = WS_ERROR;
                return;
            }
            mem.cpy(ws->buf, out, dlen);
            ws->msg_len = dlen;
            ws->msg_compressed = PROTO_FALSE;
            protocore_plaintext_release(pt_mark);
        }
#endif
        // Whole message received - surface it to the application.
        size_t n = ws->msg_len < WS_FRAME_SIZE ? ws->msg_len : WS_FRAME_SIZE;
        // RFC 6455 8.1: a TEXT message MUST be valid UTF-8 (checked on the fully
        // reassembled + decompressed message); otherwise fail the connection with 1007.
        Utf8.args.s = ws->buf;
        Utf8.args.n = n;
        Utf8.valid(Utf8.internal);
        if (ws->msg_opcode == WS_OP_TEXT && !Utf8.ok)
        {
            Ws.conn = ws;
            Ws.frame.code = WS_CLOSE_INVALID_PAYLOAD;
            close_socket(work);
            ws->parse_state = WS_ERROR;
            return;
        }
        ws->buf[n] = '\0';
        ws->opcode = ws->msg_opcode;   // report the original TEXT/BINARY opcode
        ws->payload_len = ws->msg_len; // app reads payload_len / payload_idx
        ws->payload_idx = ws->msg_len;
        ws->fragmenting = PROTO_FALSE;
        ws->parse_state = WS_FRAME_READY;
    }
    else
    {
        // More fragments to come; keep buf and msg_len, read the next frame.
        ws->fragmenting = PROTO_TRUE;
        ws_reset_perframe(ws);
    }
}

// Called by parse above its definition.
static void feed_byte(uint8_t *restrict work);

// The worker fills this slot's scratch once, then the frame state machine walks it. Stop on a
// terminal state and leave the rest where it is.
static void parse(uint8_t *restrict work)
{
    WsConn *ws = Ws.conn;
    ConnPool.slot = ws->slot_id;
    ConnPool.active(ConnPool.internal);
    if (!ConnPool.ok)
    {
        return;
    }

    ConnPool.io.buf = WS_CTX(work)->rx;
    ConnPool.io.cap = sizeof(WS_CTX(work)->rx);
    ConnPool.read(ConnPool.internal);

    for (size_t i = 0; i < ConnPool.n; i++)
    {
        if (ws->parse_state == WS_FRAME_READY || ws->parse_state == WS_CLOSED || ws->parse_state == WS_ERROR)
        {
            return;
        }
        Ws.byte = WS_CTX(work)->rx[i];
        feed_byte(work);
    }
}

static void feed_byte(uint8_t *restrict work)
{
    (void)work;
    WsConn *ws = Ws.conn;
    const uint8_t byte = Ws.byte;
    {
        switch (ws->parse_state)
        {
        case WS_HEADER1: {
            ws->fin = (byte & 0x80) != 0;
            // RSV bits are validated below, once the opcode / message position is
            // known (RSV1 is permessage-deflate's per-message "compressed" flag).
            uint8_t rsv = byte & 0x70;
            ws->opcode = (WsOpcode)(byte & 0x0F);
            // RFC 6455 §5.2: only opcodes 0x0/0x1/0x2 (data) and 0x8/0x9/0xA
            // (control) are defined; everything else MUST fail the connection.
            switch (ws->opcode)
            {
            case WS_OP_CONTINUATION:
            case WS_OP_TEXT:
            case WS_OP_BINARY:
            case WS_OP_CLOSE:
            case WS_OP_PING:
            case WS_OP_PONG:
                break;
            default:
                Ws.conn = ws;
                Ws.frame.code = WS_CLOSE_PROTOCOL;
                close_socket(work);
                ws->parse_state = WS_ERROR;
                return;
            }
            // RFC 6455 §5.5: control frames MUST NOT be fragmented (FIN set).
            if (ws_is_control(ws->opcode) && !ws->fin)
            {
                Ws.conn = ws;
                Ws.frame.code = WS_CLOSE_PROTOCOL;
                close_socket(work);
                ws->parse_state = WS_ERROR;
                return;
            }
            // RFC 6455 §5.4: fragmentation sequencing for data frames.
            if (!ws_is_control(ws->opcode))
            {
                if (ws->opcode == WS_OP_CONTINUATION)
                {
                    // A continuation with no message in progress is illegal.
                    if (!ws->fragmenting)
                    {
                        Ws.conn = ws;
                        Ws.frame.code = WS_CLOSE_PROTOCOL;
                        close_socket(work);
                        ws->parse_state = WS_ERROR;
                        return;
                    }
                }
                else
                {
                    // A new text/binary frame while a message is still open is
                    // illegal - the previous message must finish first.
                    if (ws->fragmenting)
                    {
                        Ws.conn = ws;
                        Ws.frame.code = WS_CLOSE_PROTOCOL;
                        close_socket(work);
                        ws->parse_state = WS_ERROR;
                        return;
                    }
                    // Start of a new data message.
                    ws->msg_opcode = ws->opcode;
                    ws->msg_len = 0;
#if PROTOCORE_ENABLE_WS_DEFLATE
                    // RSV1 on the first frame of a data message marks it compressed
                    // (RFC 7692); only honored when permessage-deflate was negotiated.
                    ws->msg_compressed = ws->pmd && (rsv & 0x40);
#endif
                }
            }
            // Validate reserved bits. RSV2/RSV3 are never legal; RSV1 is legal only
            // as the per-message compression flag set above (pmd + new data frame).
#if PROTOCORE_ENABLE_WS_DEFLATE
            {
                proto_bool new_data = !ws_is_control(ws->opcode) && ws->opcode != WS_OP_CONTINUATION;
                if ((rsv & 0x30) || ((rsv & 0x40) && !(ws->pmd && new_data)))
                {
                    Ws.conn = ws;
                    Ws.frame.code = WS_CLOSE_PROTOCOL;
                    close_socket(work);
                    ws->parse_state = WS_ERROR;
                    return;
                }
            }
#else
            if (rsv)
            {
                Ws.conn = ws;
                Ws.frame.code = WS_CLOSE_PROTOCOL;
                close_socket(work);
                ws->parse_state = WS_ERROR;
                return;
            }
#endif
            ws->parse_state = WS_HEADER2;
            break;
        }

        case WS_HEADER2:
            ws->masked = (byte & 0x80) != 0;
            // RFC 6455 §5.1: every client-to-server frame MUST be masked.
            if (!ws->masked)
            {
                Ws.conn = ws;
                Ws.frame.code = WS_CLOSE_PROTOCOL;
                close_socket(work);
                ws->parse_state = WS_ERROR;
                return;
            }
            {
                uint8_t len7 = byte & 0x7F;
                // RFC 6455 §5.5: control frames MUST have payload length <= 125.
                if (ws_is_control(ws->opcode) && len7 > 125)
                {
                    Ws.conn = ws;
                    Ws.frame.code = WS_CLOSE_PROTOCOL;
                    close_socket(work);
                    ws->parse_state = WS_ERROR;
                    return;
                }
                if (len7 <= 125)
                {
                    // Masking is mandatory, so always consume the 4 mask bytes
                    // next - even for zero-length frames (WS_MASK3 finishes them).
                    ws->payload_len = len7;
                    // Reassembled data message must fit in WS_FRAME_SIZE.
                    if (!ws_is_control(ws->opcode) && ws->msg_len + ws->payload_len > WS_FRAME_SIZE)
                    {
                        Ws.conn = ws;
                        Ws.frame.code = WS_CLOSE_TOO_BIG;
                        close_socket(work);
                        ws->parse_state = WS_ERROR;
                        return;
                    }
                    ws->parse_state = WS_MASK0;
                }
                else if (len7 == 126)
                {
                    ws->payload_len = 0;
                    ws->parse_state = WS_LEN16_HI;
                }
                else
                {
                    // 64-bit length -- always too large
                    ws->len64_count = 0;
                    ws->parse_state = WS_LEN64;
                }
            }
            break;

        case WS_LEN16_HI:
            ws->payload_len = (uint32_t)byte << 8;
            ws->parse_state = WS_LEN16_LO;
            break;

        case WS_LEN16_LO:
            ws->payload_len |= byte;
            // 16-bit length only occurs on data frames (control frames are
            // capped at 125); the reassembled message must fit WS_FRAME_SIZE.
            if (ws->msg_len + ws->payload_len > WS_FRAME_SIZE)
            {
                Ws.conn = ws;
                Ws.frame.code = WS_CLOSE_TOO_BIG;
                close_socket(work);
                ws->parse_state = WS_ERROR;
                return;
            }
            // Masking is mandatory; consume the 4 mask bytes next.
            ws->parse_state = WS_MASK0;
            break;

        case WS_LEN64:
            // Consume all 8 bytes then reject
            if (++ws->len64_count == 8)
            {
                Ws.conn = ws;
                Ws.frame.code = WS_CLOSE_TOO_BIG;
                close_socket(work);
                ws->parse_state = WS_ERROR;
                return;
            }
            break;

        case WS_MASK0:
            ws->mask_key[0] = byte;
            ws->parse_state = WS_MASK1;
            break;
        case WS_MASK1:
            ws->mask_key[1] = byte;
            ws->parse_state = WS_MASK2;
            break;
        case WS_MASK2:
            ws->mask_key[2] = byte;
            ws->parse_state = WS_MASK3;
            break;
        case WS_MASK3:
            ws->mask_key[3] = byte;
            if (ws->payload_len > 0)
            {
                ws->parse_state = WS_PAYLOAD;
            }
            else
            {
                ws_finish_frame(work, ws); // zero-length frame is complete now
            }
            break;

        case WS_PAYLOAD: {
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
                ws_finish_frame(work, ws);
            }
            break;
        }

        default:
            break;
        }
    }
}

static void reset_frame_call(uint8_t *restrict work)
{
    (void)work;
    ws_reset_frame(Ws.conn);
}

// Designated, so a member's position in the struct does not decide what it binds to.
// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_ws_span(void)
{
    static uint8_t *s_span;
    if (s_span == NULL)
    {
        protocore_span sp = protocore_secure_persist_span(PROTOCORE_WS_BORROW);
        if (span.ok(sp))
        {
            s_span = sp.buf;
        }
    }
    return s_span; // null while the pool was short, which every entry refuses
}

WsNs Ws = {.route_add = route_add,
           .route_reset = route_reset,
           .route_connect = route_connect,
           .route_message = route_message,
           .route_close = route_close,
           .init = init,
           .active = active,
           .payload_of = payload_of,
           .alloc = alloc,
           .find = find,
           .free = release,
           .parse = parse,
           .feed_byte = feed_byte,
           .reset_frame = reset_frame_call,
           .send_frame = send_frame,
           .set_frag_size = set_frag_size,
           .close = close_socket};

#endif // PROTOCORE_ENABLE_WEBSOCKET
