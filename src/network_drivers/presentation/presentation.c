// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file presentation.c
 * @brief Layer 6 (Presentation) - wires the transport ring buffer to the HTTP parser.
 *
 * A thin adapter. The parsing itself lives in http_parser.c; this layer holds the slot's read
 * scratch, the per-slot HTTP state, and the ProtoHandler the session layer dispatches through.
 *
 * The worker fills the scratch once per data event through ::ConnPool and the parser walks it -
 * nothing here reaches the transport's ring, which is the transport's.
 */

#include "presentation.h"
#include "mmgr/protostr.h"                                   // str: the bounded-run walks
#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool: the slot a handler is dispatched on
#include "server/core/proto_handler.h"                       // ProtoHandler (the L5 dispatch seam this registers into)
#if PROTOCORE_ENABLE_WEBSOCKET
#include "network_drivers/presentation/http/websocket/websocket.h" // Ws.find/Ws.free: a WS-upgraded slot must never be HTTP-parsed
#endif
#if PROTOCORE_ENABLE_SSE
#include "network_drivers/presentation/http/sse/sse.h" // Sse.free: release a stream when its HTTP slot closes or is reused
#endif
#if PROTOCORE_ENABLE_TLS
#include "network_drivers/tls/tls.h"
#if PROTOCORE_ENABLE_HTTP2
#include "network_drivers/presentation/http/http2/h2_server.h"
#endif
// strcmp (ALPN check)
#endif

#if PROTOCORE_ENABLE_KEEPALIVE
uint16_t http_req_count[MAX_CONNS];
#endif

// HTTP's own per-slot state. All BSS, sized on the whole pool so the HTTP/3 dispatch slot fits.
uint32_t http_req_start_ms[CONN_POOL_SLOTS];
protocore_resp_sink_fn http_resp_sink[CONN_POOL_SLOTS];
#if PROTOCORE_ENABLE_HTTP2
uint8_t http_h2[CONN_POOL_SLOTS];
uint8_t http_h2_checked[CONN_POOL_SLOTS];
uint32_t http_h2_stream[CONN_POOL_SLOTS];
#endif
#if PROTOCORE_ENABLE_HTTP3
uint8_t http_h3[CONN_POOL_SLOTS];
uint32_t http_h3_conn_id[CONN_POOL_SLOTS];
uint64_t http_h3_stream[CONN_POOL_SLOTS];
#endif

/**
 * @brief The glue's compile-time storage: the read scratch each slot's bytes land in.
 *
 * The worker fills this once per data event and the parser walks it; nothing here reaches the
 * transport's ring a second time. All BSS.
 */
struct HttpConnStorage
{
    uint8_t rx[RX_BUF_SIZE]; ///< where a slot's available bytes are staged for the parser
};

/**
 * @brief The glue's state and the calls that reach it - what HttpConnNs points at.
 *
 * @var HttpConnInternal::store  the read scratch
 * @var HttpConnInternal::ns     the handle a caller sets a call's members on
 * @var HttpConnInternal::poll   the per-slot pump the application installed
 */
struct HttpConnInternal
{
    struct HttpConnStorage *store;
    HttpConnNs *ns;
    void (*poll)(uint8_t slot);
};

static struct HttpConnStorage s_store;

static struct HttpConnInternal s_http = {.store = &s_store, .ns = &HttpConn};

static void reset(struct HttpConnInternal *restrict ctx)
{
    if (ctx->ns->slot >= MAX_CONNS)
    {
        return;
    }
    http_pool[ctx->ns->slot].slot_id = ctx->ns->slot; // ensure slot_id is correct before reset reads it
    http_parser_reset(&http_pool[ctx->ns->slot]);
}

// Release any WebSocket / SSE binding still attached to a slot. WS and SSE upgrades leave the slot
// as PROTO_HTTP (SSE is just a long-lived HTTP response; WS is pumped separately), so this
// HTTP proto handler owns their teardown. Both frees are no-ops when the slot has no such binding.
// Called on close AND on a fresh accept, because a slot can be reaped by the idle sweep or aborted
// (SSE pool full) without a close event ever firing - so a reused slot must not inherit a stale
// binding. A stale sse binding is the DoS: http_poll_slot() finds a stream on the slot and skips HTTP
// dispatch, wedging every later connection that reuses the slot.
static inline void http_release_upgrade_bindings(uint8_t slot_id)
{
#if PROTOCORE_ENABLE_WEBSOCKET
    Ws.slot = slot_id;
    Ws.free(Ws.internal);
#endif
#if PROTOCORE_ENABLE_SSE
    Sse.slot = slot_id;
    Sse.free(Sse.internal);
#endif
}

static void conn_open(struct HttpConnInternal *restrict ctx)
{
    if (ctx->ns->slot >= MAX_CONNS)
    {
        return;
    }
    http_release_upgrade_bindings(ctx->ns->slot); // a reused slot must not inherit a prior WS/SSE binding
#if PROTOCORE_ENABLE_KEEPALIVE
    http_req_count[ctx->ns->slot] = 0; // fresh connection: clear the keep-alive request tally
#endif
    reset(ctx);
}

// The worker fills this slot's scratch once, then the parser walks it. Check the terminal state
// before taking anything so a pipelined next request is left where it is; the window is reopened by
// the worker's ack_consumed.
static void parse(struct HttpConnInternal *restrict ctx)
{
    if (ctx->ns->slot >= MAX_CONNS)
    {
        return;
    }

#if PROTOCORE_ENABLE_WEBSOCKET
    // Once a slot upgrades to WebSocket its bytes are WS frames, not HTTP. The WS frame parser is
    // pumped separately (handle()/the worker loop); feeding those bytes to the HTTP parser here
    // would consume - and corrupt - the first WS frame. This guard makes "never HTTP-parse a WS
    // slot" hold for every caller (the event-queue dispatch raced the WS pump and ate the first
    // frame's header byte, dropping the first connection after a reboot).
    Ws.slot = ctx->ns->slot;
    Ws.find(Ws.internal);
    if (Ws.found)
    {
        return;
    }
#endif

    HttpReq *req = &http_pool[ctx->ns->slot];

    ConnPool.slot = ctx->ns->slot;
    ConnPool.io.buf = ctx->store->rx;
    ConnPool.io.cap = sizeof(ctx->store->rx);
    ConnPool.read(ConnPool.internal);

    for (size_t i = 0; i < ConnPool.n; i++)
    {
        switch (req->parse_state)
        {
        case PARSE_COMPLETE:
        case PARSE_ERROR:
        case PARSE_ENTITY_TOO_LARGE:
        case PARSE_URI_TOO_LONG:
            return; // terminal state - feed nothing further
        default:
            break;
        }
        http_parser_feed(req, ctx->store->rx[i]);
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

#if PROTOCORE_ENABLE_TLS
// Abort a TLS connection (fatal handshake/read error). ConnPool.abort_slot owns
// the whole teardown: free the TLS context (abrupt), detach the pcb, reset the
// slot, then RST - so this never reaches into the raw control block.
static void tls_abort(uint8_t slot)
{
    ConnPool.slot = slot;
    ConnPool.abort_slot(ConnPool.internal);
    HttpConn.slot = slot;
    reset(&s_http);
}

// Pump a TLS connection: drive the handshake to completion, then decrypt any
// application data straight into the HTTP parser (same byte-by-byte feed the
// plaintext path uses; the rx ring now holds ciphertext, consumed by the BIO).
static void tls_data(uint8_t slot)
{
    if (!protocore_tls_established(slot))
    {
        int h = protocore_tls_handshake(slot);
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

#if PROTOCORE_ENABLE_HTTP2
    // Just past the handshake: if the client negotiated ALPN "h2", this connection speaks HTTP/2
    // for its lifetime - hand its decrypted bytes to the h2 engine, not the HTTP/1.1 parser.
    if (!http_h2_checked[slot])
    {
        http_h2_checked[slot] = 1;
        const char *alpn = protocore_tls_alpn(slot);
        if (alpn && strcmp(alpn, "h2") == 0)
        {
            http_h2[slot] = 1;
            http_resp_sink[slot] = protocore_h2_server_respond; // route responses through the h2 framer
            H2Server.slot = slot;
            H2Server.open(H2Server.internal);
        }
    }
    if (http_h2[slot])
    {
        H2Server.slot = slot;
        H2Server.data(H2Server.internal);
        return;
    }
#endif

#if PROTOCORE_ENABLE_WEBSOCKET
    // A TLS slot upgraded to WebSocket is pumped from handle() (it decrypts
    // records and feeds the WS frame parser, dispatching each frame); leave the
    // ciphertext in the rx ring for it rather than feeding the HTTP parser here.
    Ws.slot = slot;
    Ws.find(Ws.internal);
    if (Ws.found)
    {
        return;
    }
#endif

    uint8_t buf[256];
    int n;
    while ((n = protocore_tls_read(slot, buf, sizeof(buf))) > 0)
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
#endif // PROTOCORE_ENABLE_TLS

// The data/close paths branch on TLS (a TLS slot's rx ring holds ciphertext,
// decrypted into the parser); accept maps directly.
static void http_evt_accept(uint8_t slot)
{
    HttpConn.slot = slot;
    conn_open(&s_http); // resets the parser + (keep-alive) the per-conn request tally
#if PROTOCORE_ENABLE_HTTP2
    http_h2[slot] = 0; // a reused slot must re-run the post-handshake ALPN check
    http_h2_checked[slot] = 0;
    http_resp_sink[slot] = NULL; // back to the HTTP/1.1 builder until ALPN says otherwise
#endif
}
static void http_evt_data(uint8_t slot)
{
#if PROTOCORE_ENABLE_TLS
    ConnPool.slot = slot;
    ConnPool.tls(ConnPool.internal);
    if (ConnPool.ok)
    {
        tls_data(slot);
        return;
    }
#endif
    HttpConn.slot = slot;
    parse(&s_http); // a no-op once the slot has upgraded to WebSocket (see parse)
}
static void http_evt_close(uint8_t slot)
{
#if PROTOCORE_ENABLE_TLS
    ConnPool.slot = slot;
    ConnPool.tls(ConnPool.internal);
    if (ConnPool.ok)
    {
        protocore_tls_conn_free(slot); // also covers timeouts (EVT_ERROR)
    }
#endif
    http_release_upgrade_bindings(slot); // FIN/RST/error on an SSE or WS slot must free its binding
    HttpConn.slot = slot;
    reset(&s_http);
}
// HTTP's poll pump is instance-bound (it dispatches into a PC's routes), so the routing core
// installs it through set_poll at begin(). The trampoline lets the ProtoHandler stay a plain static
// const while the actual pump lives in the application TU - the on_poll analogue of the
// http_resp_sink TX seam. Until installed (e.g. the native harness before begin()) it is a no-op.
static void http_evt_poll(uint8_t slot)
{
    if (s_http.poll)
    {
        s_http.poll(slot);
    }
}

static void set_poll(struct HttpConnInternal *restrict ctx)
{
    ctx->poll = ctx->ns->poll;
}

// Designated, so a member's position in the struct does not decide what it binds to. on_abort is
// unset: a null one falls back to on_close.
static const ProtoHandler s_http_handler = {
    .on_accept = http_evt_accept, .on_data = http_evt_data, .on_close = http_evt_close, .on_poll = http_evt_poll};

static void proto_handler(struct HttpConnInternal *restrict ctx)
{
    ctx->ns->handler = &s_http_handler;
}

#if PROTOCORE_ENABLE_KEEPALIVE || PROTOCORE_ENABLE_WEBSOCKET
// Case-insensitive search for @p token as a comma/space-delimited element of a
// Connection header value (e.g. "keep-alive" in "Keep-Alive, Upgrade"). Shared by
// keep-alive evaluation and the WebSocket Upgrade-token check.
static void has_token(struct HttpConnInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    if (ctx->ns->hdr_args.hdr == NULL)
    {
        return;
    }
    const char *token = ctx->ns->hdr_args.token;
    size_t tlen = str.len(token, 32);
    const char *p = ctx->ns->hdr_args.hdr;
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
        if (len == tlen && str.diff(start, token, tlen, PROTO_TRUE) == tlen)
        {
            ctx->ns->ok = PROTO_TRUE;
            return;
        }
        if (*p == ',')
        {
            p++;
        }
    }
}
#endif // PROTOCORE_ENABLE_KEEPALIVE || PROTOCORE_ENABLE_WEBSOCKET

#if PROTOCORE_ENABLE_KEEPALIVE
static void keepalive_eval(struct HttpConnInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    HttpReq *req = &http_pool[ctx->ns->slot];
    // Only a cleanly-parsed request has a known message boundary; errors close.
    if (req->parse_state != PARSE_COMPLETE)
    {
        return;
    }

    ctx->ns->hdr_args.hdr = http_get_header(req, "Connection");
    proto_bool keep;
    if (req->version == HTTP_11)
    {
        ctx->ns->hdr_args.token = "close";
        has_token(ctx);
        keep = !ctx->ns->ok; // 1.1 default: persistent
    }
    else
    {
        ctx->ns->hdr_args.token = "keep-alive";
        has_token(ctx);
        keep = ctx->ns->ok; // 1.0/unknown default: close
    }
    ctx->ns->ok = PROTO_FALSE;
    if (!keep)
    {
        return;
    }

    // Fairness bound: serve at most PROTOCORE_KEEPALIVE_MAX_REQUESTS, then close.
    http_req_count[ctx->ns->slot]++;
    if (http_req_count[ctx->ns->slot] >= PROTOCORE_KEEPALIVE_MAX_REQUESTS)
    {
        return;
    }
    ctx->ns->ok = PROTO_TRUE;
}
#endif // PROTOCORE_ENABLE_KEEPALIVE

// Designated, so a member's position in the struct does not decide what it binds to.
HttpConnNs HttpConn = {.reset = reset,
                       .conn_open = conn_open,
                       .parse = parse,
#if PROTOCORE_ENABLE_KEEPALIVE
                       .keepalive_eval = keepalive_eval,
#endif
#if PROTOCORE_ENABLE_KEEPALIVE || PROTOCORE_ENABLE_WEBSOCKET
                       .has_token = has_token,
#endif
                       .proto_handler = proto_handler,
                       .set_poll = set_poll,
                       .internal = &s_http};
