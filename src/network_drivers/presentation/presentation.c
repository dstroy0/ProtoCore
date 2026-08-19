// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
#include "mmgr/plaintext/plaintext.h"                        // the persistent end this module's state is taken from
#include "mmgr/protostr/protostr.h"                          // str: the bounded-run walks
#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool: the slot a handler is dispatched on
#include "server/core/proto_handler.h"                       // ProtoHandler (the L5 dispatch seam this registers into)
#if PROTOCORE_ENABLE_WEBSOCKET
#include "network_drivers/presentation/http/websocket/websocket.h" // Ws.find/Ws.free: a WS-upgraded slot must never be HTTP-parsed
#include "network_drivers/session/ws/ws.h" // SessionWs.close: the channel teardown that informs the application
#endif
#if PROTOCORE_ENABLE_SSE
#include "network_drivers/presentation/http/sse/sse.h" // Sse: the stream table
#include "network_drivers/session/sse/sse.h"           // SessionSse.close: the stream teardown
#endif
#if PROTOCORE_ENABLE_TLS
#include "network_drivers/tls/tls.h"
#if PROTOCORE_ENABLE_HTTP2
#include "network_drivers/presentation/http/http2/h2_server/h2_server.h"
#endif
// strcmp (ALPN check)
#endif

#if PROTOCORE_ENABLE_KEEPALIVE
uint16_t http_req_count[MAX_CONNS];
#endif

// HTTP's own per-slot state. All BSS, sized on the whole pool so the HTTP/3 dispatch slot fits.

/**
 * @brief The glue's compile-time storage: the read scratch each slot's bytes land in.
 *
 * The worker fills this once per data event and the parser walks it; nothing here reaches the
 * transport's ring a second time. All BSS.
 */
struct HttpConnStorage
{
    uint8_t rx[RX_BUF_SIZE];    ///< where a slot's available bytes are staged for the parser
    void (*poll)(uint8_t slot); ///< the per-slot pump the application installed
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define HTTP_CONN_OFF_CTX 0u
static_assert(HTTP_CONN_OFF_CTX + sizeof(struct HttpConnStorage) <= PROTOCORE_HTTP_CONN_BORROW,
              "PROTOCORE_HTTP_CONN_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define HTTP_CONN_CTX(w) ((struct HttpConnStorage *)(void *)((w) + HTTP_CONN_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_HTTP_CONN_BORROW persistent bytes, or null while the pool was short
} HttpConnOwnCtx;
static HttpConnOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_http_conn_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_plaintext_persist_span(PROTOCORE_HTTP_CONN_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

static void reset(uint8_t *restrict work)
{
    (void)work;
    if (HttpConn.slot >= MAX_CONNS)
    {
        return;
    }
    http_pool[HttpConn.slot].slot_id = HttpConn.slot; // ensure slot_id is correct before reset reads it
    HttpParser.reset_args.req = &http_pool[HttpConn.slot];
    HttpParser.reset(protocore_http_parser_span());
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
    // The channel's close is the session layer's: it informs the application before the number is
    // released (RFC 9293 sec 3.6 MUST-12), which a bare release does not.
    Ws.slot = slot_id;
    SessionWs.close(NULL);
#endif
#if PROTOCORE_ENABLE_SSE
    Sse.slot = slot_id;
    SessionSse.close(NULL);
#endif
}

static void conn_open(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    if (HttpConn.slot >= MAX_CONNS)
    {
        return;
    }
    http_release_upgrade_bindings(HttpConn.slot); // a reused slot must not inherit a prior WS/SSE binding
#if PROTOCORE_ENABLE_KEEPALIVE
    http_req_count[HttpConn.slot] = 0; // fresh connection: clear the keep-alive request tally
#endif
    reset(work);
}

// The worker fills this slot's scratch once, then the parser walks it. Check the terminal state
// before taking anything so a pipelined next request is left where it is; the window is reopened by
// the worker's ack_consumed.
static void parse(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    if (HttpConn.slot >= MAX_CONNS)
    {
        return;
    }

#if PROTOCORE_ENABLE_WEBSOCKET
    // Once a slot upgrades to WebSocket its bytes are WS frames, not HTTP. The WS frame parser is
    // pumped separately (handle()/the worker loop); feeding those bytes to the HTTP parser here
    // would consume - and corrupt - the first WS frame. This guard makes "never HTTP-parse a WS
    // slot" hold for every caller (the event-queue dispatch raced the WS pump and ate the first
    // frame's header byte, dropping the first connection after a reboot).
    Ws.slot = HttpConn.slot;
    Ws.find(protocore_ws_span());
    if (Ws.found)
    {
        return;
    }
#endif

    HttpReq *req = &http_pool[HttpConn.slot];

    ConnPool.slot = HttpConn.slot;
    ConnPool.io.buf = HTTP_CONN_CTX(work)->rx;
    ConnPool.io.cap = sizeof(HTTP_CONN_CTX(work)->rx);
    ConnPool.read(protocore_conn_pool_span());

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
        HttpParser.feed_args.req = req;
        HttpParser.feed_args.byte = HTTP_CONN_CTX(work)->rx[i];
        HttpParser.feed(protocore_http_parser_span());
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
    ConnPool.abort_slot(protocore_conn_pool_span());
    HttpConn.slot = slot;
    reset(protocore_http_conn_span());
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
        if (alpn && str.eq(alpn, "h2", sizeof("h2"), PROTO_FALSE))
        {
            http_h2[slot] = 1;
            http_resp_sink[slot] = protocore_h2_server_respond; // route responses through the h2 framer
            H2Server.slot = slot;
            H2Server.open(protocore_h2_server_span());
        }
    }
    if (http_h2[slot])
    {
        H2Server.slot = slot;
        H2Server.data(protocore_h2_server_span());
        return;
    }
#endif

#if PROTOCORE_ENABLE_WEBSOCKET
    // A TLS slot upgraded to WebSocket is pumped from handle() (it decrypts
    // records and feeds the WS frame parser, dispatching each frame); leave the
    // ciphertext in the rx ring for it rather than feeding the HTTP parser here.
    Ws.slot = slot;
    Ws.find(protocore_ws_span());
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
            HttpParser.feed_args.req = req;
            HttpParser.feed_args.byte = buf[i];
            HttpParser.feed(protocore_http_parser_span());
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
    conn_open(protocore_http_conn_span()); // resets the parser + (keep-alive) the per-conn request tally
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
    ConnPool.tls(protocore_conn_pool_span());
    if (ConnPool.ok)
    {
        tls_data(slot);
        return;
    }
#endif
    HttpConn.slot = slot;
    parse(protocore_http_conn_span()); // a no-op once the slot has upgraded to WebSocket (see parse)
}
static void http_evt_close(uint8_t slot)
{
#if PROTOCORE_ENABLE_TLS
    ConnPool.slot = slot;
    ConnPool.tls(protocore_conn_pool_span());
    if (ConnPool.ok)
    {
        protocore_tls_conn_free(slot); // also covers timeouts (EVT_ERROR)
    }
#endif
    http_release_upgrade_bindings(slot); // FIN/RST/error on an SSE or WS slot must free its binding
    HttpConn.slot = slot;
    reset(protocore_http_conn_span());
}
// HTTP's poll pump is instance-bound (it dispatches into a PC's routes), so the routing core
// installs it through set_poll at begin(). The trampoline lets the ProtoHandler stay a plain static
// const while the actual pump lives in the application TU - the on_poll analogue of the
// http_resp_sink TX seam. Until installed (e.g. the native harness before begin()) it is a no-op.
static void http_evt_poll(uint8_t slot)
{
    if (HTTP_CONN_CTX(protocore_http_conn_span())->poll)
    {
        HTTP_CONN_CTX(protocore_http_conn_span())->poll(slot);
    }
}

static void set_poll(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    HTTP_CONN_CTX(work)->poll = HttpConn.poll;
}

// Designated, so a member's position in the struct does not decide what it binds to. on_abort is
// unset: a null one falls back to on_close.
static const ProtoHandler s_http_handler = {
    .on_accept = http_evt_accept, .on_data = http_evt_data, .on_close = http_evt_close, .on_poll = http_evt_poll};

static void proto_handler(uint8_t *restrict work)
{
    (void)work;
    HttpConn.handler = &s_http_handler;
}

#if PROTOCORE_ENABLE_KEEPALIVE || PROTOCORE_ENABLE_WEBSOCKET
// Case-insensitive search for @p token as a comma/space-delimited element of a
// Connection header value (e.g. "keep-alive" in "Keep-Alive, Upgrade"). Shared by
// keep-alive evaluation and the WebSocket Upgrade-token check.
static void has_token(uint8_t *restrict work)
{
    (void)work;
    HttpConn.ok = PROTO_FALSE;
    if (HttpConn.hdr_args.hdr == NULL)
    {
        return;
    }
    const char *token = HttpConn.hdr_args.token;
    size_t tlen = str.len(token, 32);
    const char *p = HttpConn.hdr_args.hdr;
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
            HttpConn.ok = PROTO_TRUE;
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
static void keepalive_eval(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    HttpConn.ok = PROTO_FALSE;
    HttpReq *req = &http_pool[HttpConn.slot];
    // Only a cleanly-parsed request has a known message boundary; errors close.
    if (req->parse_state != PARSE_COMPLETE)
    {
        return;
    }

    HttpParser.get_header_args.req = req;
    HttpParser.get_header_args.key = "Connection";
    HttpParser.get_header(protocore_http_parser_span());
    HttpConn.hdr_args.hdr = HttpParser.text;
    proto_bool keep;
    if (req->version == HTTP_11)
    {
        HttpConn.hdr_args.token = "close";
        has_token(work);
        keep = !HttpConn.ok; // 1.1 default: persistent
    }
    else
    {
        HttpConn.hdr_args.token = "keep-alive";
        has_token(work);
        keep = HttpConn.ok; // 1.0/unknown default: close
    }
    HttpConn.ok = PROTO_FALSE;
    if (!keep)
    {
        return;
    }

    // Fairness bound: serve at most PROTOCORE_KEEPALIVE_MAX_REQUESTS, then close.
    http_req_count[HttpConn.slot]++;
    if (http_req_count[HttpConn.slot] >= PROTOCORE_KEEPALIVE_MAX_REQUESTS)
    {
        return;
    }
    HttpConn.ok = PROTO_TRUE;
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
                       .set_poll = set_poll};
