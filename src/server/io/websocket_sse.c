// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
#include "mmgr/protomem.h"
#include "mmgr/protostr.h" // str.has: the permessage-deflate token in Sec-WebSocket-Extensions
#include "network_drivers/presentation/http/http_parser/http_parser.h" // HttpReq, http_pool: the request being upgraded
#include "network_drivers/transport/tcp/protocol/protocol.h"           // ConnPool: the slot a refusal is written on
#include "network_drivers/transport/tcp/tcp.h"
#if PROTOCORE_ENABLE_WEBSOCKET
#include "crypto/hash/sha1.h"
#include "mmgr/secure.h" // the pool the digest borrow comes from
#include "mmgr/span.h"   // protocore_span, span.ok
#include "network_drivers/presentation/codec/base64/base64.h"
#include "network_drivers/presentation/http/websocket/websocket.h"
#include "network_drivers/session/ws/ws.h" // SessionWs: the channel this handshake opens
#endif
#if PROTOCORE_ENABLE_SSE
#include "network_drivers/presentation/http/sse/sse.h"
#include "network_drivers/session/sse/sse.h" // SessionSse: the stream this handshake opens
#endif

#if PROTOCORE_ENABLE_WEBSOCKET
// Magic GUID concatenated to the client key for the WS accept hash (RFC 6455 4.2.2).
static const char WS_MAGIC[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
#endif

// ---------------------------------------------------------------------------
// WebSocket handshake helpers
// ---------------------------------------------------------------------------

#if PROTOCORE_ENABLE_WEBSOCKET
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
    size_t key_len = str.len(client_key, WS_MAX_KEY_LEN + 1);
    if (key_len > WS_MAX_KEY_LEN)
    {
        out[0] = '\0';
        return PROTO_FALSE;
    }
    // RFC 6455 4.2.1: the Sec-WebSocket-Key must base64-decode to exactly 16 bytes.
    uint8_t raw[24];
    if (Base64.decode(client_key, raw, sizeof(raw)) != 16)
    {
        out[0] = '\0';
        return PROTO_FALSE;
    }
    size_t magic_len = sizeof(WS_MAGIC) - 1;
    char concat[WS_MAX_KEY_LEN + sizeof(WS_MAGIC)];
    mem.cpy(concat, client_key, key_len);
    mem.cpy(concat + key_len, WS_MAGIC, magic_len);

    uint8_t digest[PROTOCORE_SHA1_DIGEST_LEN];
    const size_t mark = protocore_secure_mark();
    protocore_span w = protocore_secure_span(PROTOCORE_SHA1_BORROW, 8);
    if (!span.ok(w))
    {
        protocore_secure_release(mark);
        out[0] = '\0';
        return PROTO_FALSE;
    }
    Sha1.hash_args.data = (const uint8_t *)concat;
    Sha1.hash_args.len = key_len + magic_len;
    Sha1.hash_args.out = digest;
    Sha1.hash(w.buf);
    protocore_secure_release(mark);
    Base64.encode(digest, PROTOCORE_SHA1_DIGEST_LEN, out);
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
    ConnPool.slot = slot_id;
    ConnPool.active(protocore_conn_pool_span());
    if (!ConnPool.ok)
    {
        http_parser_reset(&http_pool[slot_id]);
        return;
    }

    static const char resp[] = "HTTP/1.1 426 Upgrade Required\r\n"
                               "Sec-WebSocket-Version: 13\r\n"
                               "Content-Length: 0\r\n"
                               "Connection: close\r\n\r\n";

    ConnPool.slot = slot_id;
    ConnPool.io.data = resp;
    ConnPool.io.len = (proto_u16)(sizeof(resp) - 1);
    ConnPool.send(protocore_conn_pool_span());
    ConnPool.flush(protocore_conn_pool_span());
    ConnPool.begin_close(protocore_conn_pool_span()); // dwell in CONN_CLOSING until the response drains

    http_parser_reset(&http_pool[slot_id]);
}

/**
 * @brief Send the HTTP 101 Switching Protocols handshake and upgrade the slot.
 *
 * Does NOT close the TCP connection: the slot moves from HTTP parse ownership to WS frame parse
 * ownership.
 */
proto_bool ws_do_upgrade(uint8_t slot_id, HttpReq *req, uint8_t route_id)
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

    ConnPool.slot = slot_id;
    ConnPool.active(protocore_conn_pool_span());
    if (!ConnPool.ok)
    {
        return PROTO_FALSE;
    }

    char hdr[WS_HDR_BUF_SIZE];
    int hlen;
#if PROTOCORE_ENABLE_WS_DEFLATE
    // Negotiate permessage-deflate (RFC 7692) if the client offered it. We force
    // no_context_takeover in both directions so each message decompresses
    // independently (the INFLATE window is the message buffer, not a kept window).
    const char *ws_ext = http_get_header(req, "Sec-WebSocket-Extensions");
    proto_bool pmd =
        ws_ext && str.has(ws_ext, MAX_VAL_LEN, "permessage-deflate", sizeof("permessage-deflate"), PROTO_FALSE);
    protocore_sb sb_hdr = {hdr, sizeof(hdr), 0, PROTO_TRUE};
    Sb.put(&sb_hdr,
           "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ");
    Sb.put(&sb_hdr, accept);
    Sb.put(&sb_hdr, "\r\n");
    Sb.put(&sb_hdr, pmd ? "Sec-WebSocket-Extensions: permessage-deflate; client_no_context_takeover; "
                          "server_no_context_takeover\r\n"
                        : "");
    Sb.put(&sb_hdr, "\r\n");
    hlen = (int)Sb.finish(&sb_hdr);
#else
    protocore_sb sb_hdr2 = {hdr, sizeof(hdr), 0, PROTO_TRUE};
    Sb.put(&sb_hdr2,
           "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ");
    Sb.put(&sb_hdr2, accept);
    Sb.put(&sb_hdr2, "\r\n\r\n");
    hlen = (int)Sb.finish(&sb_hdr2);
#endif

    ConnPool.slot = slot_id;
    ConnPool.io.data = hdr;
    ConnPool.io.len = (proto_u16)hlen;
    ConnPool.send(protocore_conn_pool_span());
    ConnPool.slot = slot_id;
    ConnPool.flush(protocore_conn_pool_span());

    // Reset HTTP parser but keep the TCP slot -- WS owns it now
    http_parser_reset(&http_pool[slot_id]);

    // The channel is the session layer's: it takes the number, binds it to this slot and runs the
    // route's connect. This layer sent the handshake bytes.
    Ws.slot = slot_id;
    Ws.id = route_id;
#if PROTOCORE_ENABLE_WS_DEFLATE
    Ws.pmd = pmd;
#endif
    SessionWs.open(NULL);
    if (!SessionWs.ok)
    {
        // No channel available -- abort the connection (transport owns the teardown)
        ConnPool.slot = slot_id;
        ConnPool.abort_slot(protocore_conn_pool_span());
        return PROTO_FALSE;
    }

    return PROTO_TRUE;
}
#endif // PROTOCORE_ENABLE_WEBSOCKET

// ---------------------------------------------------------------------------
// SSE upgrade helper
// ---------------------------------------------------------------------------

#if PROTOCORE_ENABLE_SSE
/**
 * @brief Send the HTTP 200 + SSE headers and promote the slot to SSE mode.
 */
proto_bool protocore_sse_do_upgrade(uint8_t slot_id, HttpReq *req, uint8_t route_id)
{
    ConnPool.slot = slot_id;
    ConnPool.active(protocore_conn_pool_span());
    if (!ConnPool.ok)
    {
        return PROTO_FALSE;
    }

    static const char SSE_HDR[] = "HTTP/1.1 200 OK\r\n"
                                  "Content-Type: text/event-stream\r\n"
                                  "Cache-Control: no-cache\r\n"
                                  "Connection: keep-alive\r\n\r\n";

    ConnPool.slot = slot_id;
    ConnPool.io.data = SSE_HDR;
    ConnPool.io.len = (proto_u16)(sizeof(SSE_HDR) - 1);
    ConnPool.send(protocore_conn_pool_span());
    ConnPool.slot = slot_id;
    ConnPool.flush(protocore_conn_pool_span());

    // Copy the path BEFORE resetting the parser: http_reset() zeroes the whole
    // HttpReq (including req->path), so a pointer into it would dangle. The saved
    // path is what protocore_sse_broadcast() matches against.
    char path[MAX_PATH_LEN];
    str.copy(path, req->path, sizeof(path));
    http_parser_reset(&http_pool[slot_id]);

    // The stream is the session layer's: it takes the number, binds it to this connection and runs
    // the route's connect. This layer sent the handshake bytes.
    Sse.slot = slot_id;
    Sse.route.path = path;
    Sse.id = route_id;
    SessionSse.open(NULL);
    if (!SessionSse.ok)
    {
        ConnPool.slot = slot_id;
        ConnPool.abort_slot(protocore_conn_pool_span()); // transport owns detach + reset + RST
        return PROTO_FALSE;
    }

    return PROTO_TRUE;
}
#endif // PROTOCORE_ENABLE_SSE

// ---------------------------------------------------------------------------
// WebSocket public API
// ---------------------------------------------------------------------------

#if PROTOCORE_ENABLE_WEBSOCKET
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
    uint16_t len = (uint16_t)str.len(text, 0xFFFF);
    Ws.conn = ws;
    Ws.frame.opcode = WS_OP_TEXT;
    Ws.frame.payload = (const uint8_t *)text;
    Ws.frame.len = len;
    Ws.send_frame(protocore_ws_span());
    if (Ws.ok)
    {
        // has itself checked protocore_conn_active(), and nothing between the two can tear the slot down
        // on a single-threaded run. It is a re-check for the marshalled send path.
        ConnPool.slot = ws->slot_id;
        ConnPool.active(protocore_conn_pool_span());
        if (ConnPool.ok)
        {
            ConnPool.slot = ws->slot_id;
            ConnPool.flush(protocore_conn_pool_span());
        }
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
    Ws.conn = ws;
    Ws.frame.opcode = WS_OP_BINARY;
    Ws.frame.payload = data;
    Ws.frame.len = len;
    Ws.send_frame(protocore_ws_span());
    if (Ws.ok)
    {
        // connection, so the false half of this re-check is unreachable from a host test.
        ConnPool.slot = ws->slot_id;
        ConnPool.active(protocore_conn_pool_span());
        if (ConnPool.ok)
        {
            ConnPool.slot = ws->slot_id;
            ConnPool.flush(protocore_conn_pool_span());
        }
    }
}

void ws_disconnect(uint8_t ws_id)
{
    if (ws_id >= MAX_WS_CONNS || !ws_pool[ws_id].active)
    {
        return;
    }
    WsConn *ws = &ws_pool[ws_id];
    Ws.conn = ws;
    Ws.frame.code = WS_CLOSE_NORMAL;
    Ws.close(protocore_ws_span());
    ConnPool.slot = ws->slot_id;
    ConnPool.active(protocore_conn_pool_span());
    if (ConnPool.ok)
    {
        ConnPool.slot = ws->slot_id;
        ConnPool.flush(protocore_conn_pool_span());
    }
    // handle() detects WS_CLOSED next tick and fires ws_close callback
}
#endif // PROTOCORE_ENABLE_WEBSOCKET

// ---------------------------------------------------------------------------
// Server-Sent Events public API
// ---------------------------------------------------------------------------

#if PROTOCORE_ENABLE_SSE
void protocore_sse_send(uint8_t protocore_sse_id, const char *data, const char *event, const char *id)
{
    if (protocore_sse_id >= MAX_SSE_CONNS || !protocore_sse_pool[protocore_sse_id].active)
    {
        return;
    }
    SseConn *sse = &protocore_sse_pool[protocore_sse_id];
    Sse.stream = sse;
    Sse.event_args.data = data;
    Sse.event_args.event = event;
    Sse.event_args.event_id = id;
    Sse.write(protocore_sse_span());
    if (Sse.ok)
    {
        // has itself checked protocore_conn_active(), so the slot is still live here.
        ConnPool.slot = sse->slot_id;
        ConnPool.active(protocore_conn_pool_span());
        if (ConnPool.ok)
        {
            ConnPool.slot = sse->slot_id;
            ConnPool.flush(protocore_conn_pool_span());
        }
    }
}

void protocore_sse_broadcast(const char *path, const char *data, const char *event, const char *id)
{
    for (int i = 0; i < MAX_SSE_CONNS; i++)
    {
        if (!protocore_sse_pool[i].active)
        {
            continue;
        }
        if (!str.eq(protocore_sse_pool[i].path, path, MAX_PATH_LEN, PROTO_FALSE))
        {
            continue;
        }
        SseConn *sse = &protocore_sse_pool[i];
        Sse.stream = sse;
        Sse.event_args.data = data;
        Sse.event_args.event = event;
        Sse.event_args.event_id = id;
        Sse.write(protocore_sse_span());
        if (Sse.ok)
        {
            // connection, so the false half of this re-check is unreachable from a host test.
            ConnPool.slot = sse->slot_id;
            ConnPool.active(protocore_conn_pool_span());
            if (ConnPool.ok)
            {
                ConnPool.slot = sse->slot_id;
                ConnPool.flush(protocore_conn_pool_span());
            }
        }
    }
}
#endif // PROTOCORE_ENABLE_SSE
