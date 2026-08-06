// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file presentation.c
 * @brief Layer 6 (Presentation) - wires the transport ring buffer to the HTTP parser.
 *
 * This file is now a thin adapter.  The HTTP parsing logic lives in
 * http_parser.c.  This layer only knows about:
 *   - The transport RX read API (pc_conn_available / pc_conn_read_byte) - it
 *     never indexes the ring itself; transport owns rx_buffer / rx_head / rx_tail
 *   - Slot-indexed helpers that the session and application layers expect
 *
 * The slot-indexed `http_reset()` pre-stamps slot_id then delegates to
 * `http_parser_reset()`.  The slot-indexed `http_parse()` drains all
 * available bytes from the ring buffer through `http_parser_feed()` and
 * stops as soon as the parser reaches any terminal state.
 */

#include "presentation.h"
#include "shared_primitives/runops.h" // the bounded scan and case-insensitive compare the Connection header needs
#include "network_drivers/session/proto_handler.h" // ProtoHandler (the L5 dispatch seam this registers into)
#include "network_drivers/transport/tcp.h"         // conn_pool: the slot a handler is dispatched on
#if PC_ENABLE_WEBSOCKET
#include "network_drivers/presentation/http/websocket/websocket.h" // ws_find()/ws_free(): a WS-upgraded slot must never be HTTP-parsed
#endif
#if PC_ENABLE_SSE
#include "network_drivers/presentation/http/sse/sse.h" // pc_sse_free(): release an SSE binding when its HTTP slot closes/reuses
#endif
#if PC_ENABLE_TLS
#include "network_drivers/tls/tls.h"
#if PC_ENABLE_HTTP2
#include "network_drivers/presentation/http/http2/h2_server.h"
#endif
// strcmp (ALPN check)
#endif

#if PC_ENABLE_KEEPALIVE
uint16_t http_req_count[MAX_CONNS];
#endif

void http_reset(uint8_t slot_id)
{
    if (slot_id >= MAX_CONNS)
    {
        return;
    }
    http_pool[slot_id].slot_id = slot_id; // ensure slot_id is correct before reset reads it
    http_parser_reset(&http_pool[slot_id]);
}

// Release any WebSocket / SSE binding still attached to a slot. WS and SSE upgrades leave the slot
// as PROTO_HTTP (SSE is just a long-lived HTTP response; WS is pumped separately), so this
// HTTP proto handler owns their teardown. Both frees are no-ops when the slot has no such binding.
// Called on close AND on a fresh accept, because a slot can be reaped by the idle sweep or aborted
// (SSE pool full) without a close event ever firing - so a reused slot must not inherit a stale
// binding. A stale sse binding is the DoS: http_poll_slot() sees pc_sse_find(slot) and skips HTTP
// dispatch, wedging every later connection that reuses the slot.
static inline void http_release_upgrade_bindings(uint8_t slot_id)
{
#if PC_ENABLE_WEBSOCKET
    ws_free(slot_id);
#endif
#if PC_ENABLE_SSE
    pc_sse_free(slot_id);
#endif
}

void http_conn_open(uint8_t slot_id)
{
    if (slot_id >= MAX_CONNS)
    {
        return;
    }
    http_release_upgrade_bindings(slot_id); // a reused slot must not inherit a prior WS/SSE binding
#if PC_ENABLE_KEEPALIVE
    http_req_count[slot_id] = 0; // fresh connection: clear the keep-alive request tally
#endif
    http_reset(slot_id);
}

void http_parse(uint8_t slot_id)
{
    if (slot_id >= MAX_CONNS)
    {
        return;
    }

#if PC_ENABLE_WEBSOCKET
    // Once a slot upgrades to WebSocket its rx ring carries WS frames, not HTTP.
    // The WS frame parser is pumped separately (handle()/the worker loop); feeding
    // those bytes to the HTTP parser here would consume - and corrupt - the first
    // WS frame. This guard makes "never HTTP-parse a WS slot" hold for every caller
    // (the event-queue dispatch raced the WS pump and ate the first frame's header
    // byte, dropping the first connection after a reboot).
    if (ws_find(slot_id))
    {
        return;
    }
#endif

    HttpReq *req = &http_pool[slot_id];

    // Drain via the transport read API - the parser never touches the ring itself.
    // Check the terminal state BEFORE consuming so a pipelined next request is left
    // in the ring; the window is reopened by the worker's Tcp.conn->ack_consumed().
    while (pc_conn_available(slot_id) > 0)
    {
        switch (req->parse_state)
        {
        case PARSE_COMPLETE:
        case PARSE_ERROR:
        case PARSE_ENTITY_TOO_LARGE:
        case PARSE_URI_TOO_LONG:
            return; // terminal state - drain nothing further
        default:
            break;
        }

        uint8_t byte = 0;
        // The false arm cannot fire on any thread count, not merely on this single-
        // threaded host harness: pc_conn_available() and pc_conn_read_byte() both
        // compare the same slot's rx_head vs rx_tail, and nothing between the two
        // calls here mutates either (the switch above only reads req->parse_state).
        // rx_tail is single-consumer-owned - this loop is the only writer for this
        // slot - and the producer (tcp.c's recv callback) always checks
        // pc_ring_free() before writing, so rx_head can never be advanced to equal
        // the rx_tail snapshot available() just found nonzero ("the free-space check
        // ... guarantees it fits, so head can never overrun tail" - tcp.c). A
        // "drain" of the ring between the two calls would require a second consumer,
        // which the single-consumer-per-slot design does not have.
        if (!pc_conn_read_byte(slot_id, &byte))
        {
            break;
        }
        http_parser_feed(req, byte);
    }
}

// ---------------------------------------------------------------------------
// HTTP ProtoHandler - the L5 dispatch seam for an HTTP connection.
//
// This is where an HTTP connection is fed: the plaintext path drains the ring
// through http_parse() (above); the TLS path drives the handshake, then routes
// decrypted bytes to the HTTP/2 engine (ALPN "h2"), the WebSocket pump (an
// upgraded slot), or the HTTP/1.1 parser. Keeping it here (Layer 6, with the rest
// of the HTTP-connection glue) leaves the session layer's dispatcher free of any
// HTTP / TLS / h2 / ws specifics - it only routes events to registered handlers.
// ---------------------------------------------------------------------------

#if PC_ENABLE_TLS
// Abort a TLS connection (fatal handshake/read error). Tcp.conn->abort_slot owns
// the whole teardown: free the TLS context (abrupt), detach the pcb, reset the
// slot, then RST - so this never reaches into the raw tcp_pcb.
static void tls_abort(uint8_t slot)
{
    Tcp.conn->abort_slot(slot);
    http_reset(slot);
}

// Pump a TLS connection: drive the handshake to completion, then decrypt any
// application data straight into the HTTP parser (same byte-by-byte feed the
// plaintext path uses; the rx ring now holds ciphertext, consumed by the BIO).
static void tls_data(uint8_t slot)
{
    if (!pc_tls_established(slot))
    {
        int h = pc_tls_handshake(slot);
        if (h < 0)
        {
            tls_abort(slot);
            return;
        }
        if (h == 0)
        {
            return; // still handshaking; wait for more ciphertext
        }
    }

#if PC_ENABLE_HTTP2
    // Just past the handshake: if the client negotiated ALPN "h2", this connection speaks HTTP/2
    // for its lifetime - hand its decrypted bytes to the h2 engine, not the HTTP/1.1 parser.
    if (!conn_pool[slot].pc_h2_checked)
    {
        conn_pool[slot].pc_h2_checked = 1;
        const char *alpn = pc_tls_alpn(slot);
        if (alpn && strcmp(alpn, "h2") == 0)
        {
            conn_pool[slot].h2 = 1;
            conn_pool[slot].pc_resp_sink = pc_h2_server_respond; // route responses through the h2 framer
            pc_h2_server_open(slot);
        }
    }
    if (conn_pool[slot].h2)
    {
        pc_h2_server_data(slot);
        return;
    }
#endif

#if PC_ENABLE_WEBSOCKET
    // A TLS slot upgraded to WebSocket is pumped from handle() (it decrypts
    // records and feeds the WS frame parser, dispatching each frame); leave the
    // ciphertext in the rx ring for it rather than feeding the HTTP parser here.
    if (ws_find(slot))
    {
        return;
    }
#endif

    uint8_t buf[256];
    int n;
    while ((n = pc_tls_read(slot, buf, sizeof(buf))) > 0)
    {
        HttpReq *req = &http_pool[slot];
        for (int i = 0; i < n; i++)
        {
            if (req->parse_state == PARSE_COMPLETE || req->parse_state == PARSE_ERROR ||
                req->parse_state == PARSE_ENTITY_TOO_LARGE || req->parse_state == PARSE_URI_TOO_LONG)
            {
                break; // terminal state - let handle() dispatch before reading more
            }
            http_parser_feed(req, buf[i]);
        }
    }
    if (n < 0)
    {
        tls_abort(slot);
    }
}
#endif // PC_ENABLE_TLS

// The data/close paths branch on TLS (a TLS slot's rx ring holds ciphertext,
// decrypted into the parser); accept maps directly.
static void http_evt_accept(uint8_t slot)
{
    http_conn_open(slot); // resets the parser + (keep-alive) the per-conn request tally
#if PC_ENABLE_HTTP2
    conn_pool[slot].h2 = 0; // a reused slot must re-run the post-handshake ALPN check
    conn_pool[slot].pc_h2_checked = 0;
    conn_pool[slot].pc_resp_sink = NULL; // back to the HTTP/1.1 builder until ALPN says otherwise
#endif
}
static void http_evt_data(uint8_t slot)
{
#if PC_ENABLE_TLS
    if (conn_pool[slot].tls)
    {
        tls_data(slot);
        return;
    }
#endif
    http_parse(slot); // a no-op once the slot has upgraded to WebSocket (see http_parse)
}
static void http_evt_close(uint8_t slot)
{
#if PC_ENABLE_TLS
    if (conn_pool[slot].tls)
    {
        pc_tls_conn_free(slot); // also covers timeouts (EVT_ERROR)
    }
#endif
    http_release_upgrade_bindings(slot); // FIN/RST/error on an SSE or WS slot must free its binding
    http_reset(slot);
}
// HTTP's poll pump is instance-bound (it dispatches into a PC's routes), so the routing
// core installs it here at begin() via http_proto_set_poll(). The trampoline lets the ProtoHandler
// stay a plain static const while the actual pump lives in the application TU - the on_poll analogue
// of the pc_resp_sink TX seam. Until installed (e.g. the native harness before begin()) it is a no-op.
static void (*s_http_poll)(uint8_t slot) = NULL;
static void http_evt_poll(uint8_t slot)
{
    if (s_http_poll)
    {
        s_http_poll(slot);
    }
}
void http_proto_set_poll(void (*fn)(uint8_t slot))
{
    s_http_poll = fn;
}

static const ProtoHandler s_http_handler = {http_evt_accept, http_evt_data, http_evt_close, http_evt_poll};

const ProtoHandler *http_proto_handler(void)
{
    return &s_http_handler;
}

#if PC_ENABLE_KEEPALIVE || PC_ENABLE_WEBSOCKET
// Case-insensitive search for @p token as a comma/space-delimited element of a
// Connection header value (e.g. "keep-alive" in "Keep-Alive, Upgrade"). Shared by
// keep-alive evaluation and the WebSocket Upgrade-token check.
proto_bool pc_http_conn_has_token(const char *hdr, const char *token)
{
    if (hdr == NULL)
    {
        return PROTO_FALSE;
    }
    size_t tlen = proto_scan_nul(token, 32);
    const char *p = hdr;
    while (*p)
    {
        while (*p == ' ' || *p == ',' || *p == '\t')
        {
            p++;
        }
        const char *start = p;
        while (*p && *p != ',')
        {
            p++;
        }
        size_t len = (size_t)(p - start);
        while (len && (start[len - 1] == ' ' || start[len - 1] == '\t'))
        {
            len--;
        }
        // The element is a slice of the header value, not its own string, so it has no terminator to
        // measure against: the trimmed length is the bound, and the length test above it is what
        // stops a longer token matching on its prefix.
        if (len == tlen && proto_diff_ci(start, token, tlen) == tlen)
        {
            return PROTO_TRUE;
        }
        if (*p == ',')
        {
            p++;
        }
    }
    return PROTO_FALSE;
}
#endif // PC_ENABLE_KEEPALIVE || PC_ENABLE_WEBSOCKET

#if PC_ENABLE_KEEPALIVE
proto_bool keepalive_eval(uint8_t slot_id)
{
    HttpReq *req = &http_pool[slot_id];
    // Only a cleanly-parsed request has a known message boundary; errors close.
    if (req->parse_state != PARSE_COMPLETE)
    {
        return PROTO_FALSE;
    }

    const char *c = http_get_header(req, "Connection");
    proto_bool keep;
    if (req->version == HTTP_11)
    {
        keep = !pc_http_conn_has_token(c, "close"); // 1.1 default: persistent
    }
    else
    {
        keep = pc_http_conn_has_token(c, "keep-alive"); // 1.0/unknown default: close
    }
    if (!keep)
    {
        return PROTO_FALSE;
    }

    // Fairness bound: serve at most PC_KEEPALIVE_MAX_REQUESTS, then close.
    http_req_count[slot_id]++;
    if (http_req_count[slot_id] >= PC_KEEPALIVE_MAX_REQUESTS)
    {
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}
#endif // PC_ENABLE_KEEPALIVE
