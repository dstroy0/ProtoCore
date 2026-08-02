// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file websocket_sse.c
 * @brief WebSocket (RFC 6455) and Server-Sent Events upgrade, and the send/broadcast API for both.
 *
 * The WS handshake (Sec-WebSocket-Accept over SHA-1+base64, optional permessage-deflate) and the
 * SSE 200 upgrade. The frame codecs live in the presentation layer (websocket/, sse/); this file is
 * the half that owns the slot handoff. The upgrade entry points are declared in protocore.h and
 * called by the route dispatcher.
 */

#include "mmgr/membuild.h"
#include "network_drivers/transport/tcp.h"
#include "protocore.h"
#if PC_ENABLE_WEBSOCKET
#include "crypto/hash/sha1.h"
#include "network_drivers/presentation/codec/base64/base64.h"
#include "network_drivers/presentation/http/websocket/websocket.h"
#endif
#if PC_ENABLE_SSE
#include "network_drivers/presentation/http/sse/sse.h"
#endif
#include <stdio.h>
#include <string.h>

#if PC_ENABLE_WEBSOCKET
// Magic GUID concatenated to the client key for the WS accept hash (RFC 6455 4.2.2).
static const char WS_MAGIC[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
#endif

// ---------------------------------------------------------------------------
// WebSocket handshake helpers
// ---------------------------------------------------------------------------

#if PC_ENABLE_WEBSOCKET
// Longest Sec-WebSocket-Key accepted, and the bound that lets the concat buffer below have a
// compile-time size.
#define WS_MAX_KEY_LEN 64u

/**
 * @brief Compute the Sec-WebSocket-Accept value for the HTTP 101 response.
 *
 * Concatenates the client key with the RFC 6455 magic GUID, SHA-1 hashes
 * the result, and base64-encodes the 20-byte digest into @p out.
 * @p out must be at least 29 bytes (28 base64 chars + null terminator).
 */
static proto_bool ws_accept_key(const char *client_key, char *out)
{
    size_t key_len = strnlen(client_key, WS_MAX_KEY_LEN + 1);
    // GCOVR_EXCL_START  unreachable at the default header sizing: client_key is a Header::val, which
    // is MAX_VAL_LEN (48) bytes, so key_len cannot exceed WS_MAX_KEY_LEN (64). A build that raises
    // MAX_VAL_LEN past 64 does reach it, and the bound is what lets the concat buffer below be sized
    // WS_MAX_KEY_LEN + sizeof(WS_MAGIC) - so the guard stays either way.
    if (key_len > WS_MAX_KEY_LEN)
    {
        out[0] = '\0';
        return PROTO_FALSE;
    }
    // GCOVR_EXCL_STOP
    // RFC 6455 4.2.1: the Sec-WebSocket-Key must base64-decode to exactly 16 bytes.
    uint8_t raw[24];
    if (pc_base64_decode(client_key, raw, sizeof(raw)) != 16)
    {
        out[0] = '\0';
        return PROTO_FALSE;
    }
    size_t magic_len = sizeof(WS_MAGIC) - 1;
    char concat[WS_MAX_KEY_LEN + sizeof(WS_MAGIC)];
    memcpy(concat, client_key, key_len);
    memcpy(concat + key_len, WS_MAGIC, magic_len);

    uint8_t digest[PC_SHA1_DIGEST_LEN];
    pc_sha1((const uint8_t *)concat, key_len + magic_len, digest);
    pc_base64_encode(digest, PC_SHA1_DIGEST_LEN, out);
    return PROTO_TRUE;
}

/**
 * @brief Send a 426 Upgrade Required for an unsupported Sec-WebSocket-Version.
 *
 * RFC 6455 §4.2.1: if the version is not 13 the server MUST respond with a
 * 426 and include a Sec-WebSocket-Version header listing the versions it
 * supports.  Closes the connection afterward.
 */
void ws_send_version_required(uint8_t slot_id)
{
    if (!pc_conn_active(slot_id))
    {
        http_reset(slot_id);
        return;
    }

    static const char resp[] = "HTTP/1.1 426 Upgrade Required\r\n"
                               "Sec-WebSocket-Version: 13\r\n"
                               "Content-Length: 0\r\n"
                               "Connection: close\r\n\r\n";

    pc_conn_send(slot_id, resp, (proto_u16)(sizeof(resp) - 1));
    pc_conn_flush(slot_id);
    pc_conn_begin_close(slot_id); // dwell in CONN_CLOSING until the response drains

    http_reset(slot_id);
}

/**
 * @brief Send the HTTP 101 Switching Protocols handshake and upgrade the slot.
 *
 * Does NOT close the TCP connection: the slot moves from HTTP parse ownership to WS frame parse
 * ownership.
 */
proto_bool ws_do_upgrade(uint8_t slot_id, HttpReq *req, WsConnectHandler on_connect)
{
    const char *client_key = http_get_header(req, "Sec-WebSocket-Key");
    if (!client_key)
    {
        return PROTO_FALSE;
    }

    char accept[32];
    if (!ws_accept_key(client_key, accept))
    {
        return PROTO_FALSE;
    }

    if (!pc_conn_active(slot_id))
    {
        return PROTO_FALSE;
    }

    char hdr[WS_HDR_BUF_SIZE];
    int hlen;
#if PC_ENABLE_WS_DEFLATE
    // Negotiate permessage-deflate (RFC 7692) if the client offered it. We force
    // no_context_takeover in both directions so each message decompresses
    // independently (the INFLATE window is the message buffer, not a kept window).
    const char *ws_ext = http_get_header(req, "Sec-WebSocket-Extensions");
    proto_bool pmd = ws_ext && strstr(ws_ext, "permessage-deflate");
    pc_sb sb_hdr = {hdr, sizeof(hdr), 0, PROTO_TRUE};
    pc_sb_put(
        &sb_hdr,
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ");
    pc_sb_put(&sb_hdr, accept);
    pc_sb_put(&sb_hdr, "\r\n");
    pc_sb_put(&sb_hdr, pmd ? "Sec-WebSocket-Extensions: permessage-deflate; client_no_context_takeover; "
                             "server_no_context_takeover\r\n"
                           : "");
    pc_sb_put(&sb_hdr, "\r\n");
    hlen = (int)pc_sb_finish(&sb_hdr);
#else
    pc_sb sb_hdr2 = {hdr, sizeof(hdr), 0, PROTO_TRUE};
    pc_sb_put(
        &sb_hdr2,
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ");
    pc_sb_put(&sb_hdr2, accept);
    pc_sb_put(&sb_hdr2, "\r\n\r\n");
    hlen = (int)pc_sb_finish(&sb_hdr2);
#endif

    pc_conn_send(slot_id, hdr, (proto_u16)hlen);
    pc_conn_flush(slot_id);

    // Reset HTTP parser but keep the TCP slot -- WS owns it now
    http_reset(slot_id);

    WsConn *ws = ws_alloc(slot_id);
    if (!ws)
    {
        // No WS slot available -- abort the connection (transport owns the teardown)
        pc_conn_abort_slot(slot_id);
        return PROTO_FALSE;
    }

#if PC_ENABLE_WS_DEFLATE
    ws->pmd = pmd;
#endif
    if (on_connect)
    {
        on_connect(ws->ws_id);
    }

    return PROTO_TRUE;
}
#endif // PC_ENABLE_WEBSOCKET

// ---------------------------------------------------------------------------
// SSE upgrade helper
// ---------------------------------------------------------------------------

#if PC_ENABLE_SSE
/**
 * @brief Send the HTTP 200 + SSE headers and promote the slot to SSE mode.
 */
proto_bool pc_sse_do_upgrade(uint8_t slot_id, HttpReq *req, SseConnectHandler on_connect)
{
    if (!pc_conn_active(slot_id))
    {
        return PROTO_FALSE;
    }

    static const char SSE_HDR[] = "HTTP/1.1 200 OK\r\n"
                                  "Content-Type: text/event-stream\r\n"
                                  "Cache-Control: no-cache\r\n"
                                  "Connection: keep-alive\r\n\r\n";

    pc_conn_send(slot_id, SSE_HDR, (proto_u16)(sizeof(SSE_HDR) - 1));
    pc_conn_flush(slot_id);

    // Copy the path BEFORE resetting the parser: http_reset() zeroes the whole
    // HttpReq (including req->path), so a pointer into it would dangle. The saved
    // path is what pc_sse_broadcast() matches against.
    char path[MAX_PATH_LEN];
    strncpy(path, req->path, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    http_reset(slot_id);

    SseConn *sse = pc_sse_alloc(slot_id, path);
    if (!sse)
    {
        pc_conn_abort_slot(slot_id); // transport owns detach + reset + RST
        return PROTO_FALSE;
    }

    if (on_connect)
    {
        on_connect(sse->pc_sse_id);
    }

    return PROTO_TRUE;
}
#endif // PC_ENABLE_SSE

// ---------------------------------------------------------------------------
// WebSocket public API
// ---------------------------------------------------------------------------

#if PC_ENABLE_WEBSOCKET
void ws_send_text(uint8_t ws_id, const char *text)
{
    if (ws_id >= MAX_WS_CONNS || !ws_pool[ws_id].active)
    {
        return;
    }
    WsConn *ws = &ws_pool[ws_id];
    if (ws->parse_state == WS_CLOSED || ws->parse_state == WS_ERROR)
    {
        return;
    }
    uint16_t len = (uint16_t)strnlen(text, 0xFFFF);
    if (ws_send_frame(ws, WS_OP_TEXT, (const uint8_t *)text, len))
    {
        // GCOVR_EXCL_BR_START  the false half cannot fire: ws_send_frame returns true only after it
        // has itself checked pc_conn_active(), and nothing between the two can tear the slot down
        // on a single-threaded run. It is a re-check for the marshalled (ARDUINO) send path.
        if (pc_conn_active(ws->slot_id))
        {
            pc_conn_flush(ws->slot_id);
        }
        // GCOVR_EXCL_BR_STOP
    }
}

void ws_send_binary(uint8_t ws_id, const uint8_t *data, uint16_t len)
{
    if (ws_id >= MAX_WS_CONNS || !ws_pool[ws_id].active)
    {
        return;
    }
    WsConn *ws = &ws_pool[ws_id];
    if (ws->parse_state == WS_CLOSED || ws->parse_state == WS_ERROR)
    {
        return;
    }
    if (ws_send_frame(ws, WS_OP_BINARY, data, len))
    {
        // GCOVR_EXCL_BR_START  as in ws_send_text: ws_send_frame already required an active
        // connection, so the false half of this re-check is unreachable from a host test.
        if (pc_conn_active(ws->slot_id))
        {
            pc_conn_flush(ws->slot_id);
        }
        // GCOVR_EXCL_BR_STOP
    }
}

void ws_disconnect(uint8_t ws_id)
{
    if (ws_id >= MAX_WS_CONNS || !ws_pool[ws_id].active)
    {
        return;
    }
    WsConn *ws = &ws_pool[ws_id];
    ws_close(ws, WS_CLOSE_NORMAL);
    if (pc_conn_active(ws->slot_id))
    {
        pc_conn_flush(ws->slot_id);
    }
    // handle() detects WS_CLOSED next tick and fires ws_close callback
}
#endif // PC_ENABLE_WEBSOCKET

// ---------------------------------------------------------------------------
// Server-Sent Events public API
// ---------------------------------------------------------------------------

#if PC_ENABLE_SSE
void pc_sse_send(uint8_t pc_sse_id, const char *data, const char *event, const char *id)
{
    if (pc_sse_id >= MAX_SSE_CONNS || !pc_sse_pool[pc_sse_id].active)
    {
        return;
    }
    SseConn *sse = &pc_sse_pool[pc_sse_id];
    if (pc_sse_write(sse, data, event, id))
    {
        // GCOVR_EXCL_BR_START  the false half cannot fire: pc_sse_write returns true only after it
        // has itself checked pc_conn_active(), so the slot is still live here.
        if (pc_conn_active(sse->slot_id))
        {
            pc_conn_flush(sse->slot_id);
        }
        // GCOVR_EXCL_BR_STOP
    }
}

void pc_sse_broadcast(const char *path, const char *data, const char *event, const char *id)
{
    for (int i = 0; i < MAX_SSE_CONNS; i++)
    {
        if (!pc_sse_pool[i].active)
        {
            continue;
        }
        if (strcmp(pc_sse_pool[i].path, path) != 0)
        {
            continue;
        }
        SseConn *sse = &pc_sse_pool[i];
        if (pc_sse_write(sse, data, event, id))
        {
            // GCOVR_EXCL_BR_START  as in pc_sse_send: pc_sse_write already required an active
            // connection, so the false half of this re-check is unreachable from a host test.
            if (pc_conn_active(sse->slot_id))
            {
                pc_conn_flush(sse->slot_id);
            }
            // GCOVR_EXCL_BR_STOP
        }
    }
}
#endif // PC_ENABLE_SSE
