// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore.c
 * @brief Layer 7 (Application) - HTTP routing and request handler implementation.
 *
 * **Dispatch pipeline (called from handle())**
 * ```
 * handle()
 *   └─ server_tick()                 ← drain the session event queue
 *   └─ for each slot:
 *        PARSE_COMPLETE          → Http.match_and_execute()
 *        PARSE_ERROR             → send_text(400)
 *        PARSE_ENTITY_TOO_LARGE  → send_text(413)
 *        PARSE_URI_TOO_LONG      → send_text(414)
 * ```
 *
 * **Route table**
 * Routes are stored in a fixed-size array of `Route` structs.  Both exact
 * and wildcard (suffix `*`) routes are supported; exact routes always take
 * priority because the loop checks them in insertion order and returns on
 * the first match.
 *
 * **Connection teardown ownership**
 * All TCP I/O and teardown go through the transport connection API
 * (Tcp.conn->send / Tcp.conn->flush / Tcp.conn->begin_close / Tcp.conn->close /
 * Tcp.conn->abort_slot). This layer addresses a connection by slot index and owns
 * no part of its lifecycle: transport releases the slot before it emits the
 * FIN/RST, so a stack event that fires mid-teardown finds the slot already free
 * and does nothing. L7 chooses only the kind of close: Tcp.conn->close(slot) for a
 * graceful local close, Tcp.conn->abort_slot(slot) for a hard reset, and
 * Tcp.conn->begin_close(slot) for the drain-then-close dwell.
 */

#include "protocore.h"
#include "network_drivers/presentation/http/http.h"
#include "mmgr/frame.h"     // the diag document is a frame spec, not a concatenation
#include "mmgr/membuild.h"  // pc_sb frame builder
#include "mmgr/plaintext.h" // the diag document is borrowed, not a stack array
#include "mmgr/rawmemcpy.h" // proto_raw_read: every move here is into our own buffer
#include "network_drivers/network/route.h"
#include "network_drivers/presentation/presentation.h" // http_proto_set_poll (install the instance-bound HTTP poll)
#include "network_drivers/session/proto_handler.h"
#include "network_drivers/session/worker.h"
#include "network_drivers/tls/tls.h"
#include "network_drivers/transport/tcp.h" // TcpConn, conn_pool, pc_ap_ip: the slots this drives
#include "server/clock/clock.h"            // pc_millis(): the QUIC poll stamp and the request timeout
#include "shared_primitives/hex.h"
#include "shared_primitives/mime.h"
#include "shared_primitives/runops.h" // every string scan, compare, copy and search on this layer
#if PC_ENABLE_HTTP2
#include "network_drivers/presentation/http/http2/h2_server.h"
#endif
#if PC_ENABLE_HTTP3
#include "network_drivers/presentation/http/http3/quic_server.h"
#endif
#if PC_ENABLE_HTTP_DELIVERY
#include "services/file_transfer/http_delivery/http_delivery.h" // pc_delivery_cache_control (SWR directive)
#endif
#if PC_ENABLE_CSRF
#include "services/security/csrf/csrf.h"
#endif
#if PC_ENABLE_WEBDAV
#include "network_drivers/application/webdav/webdav.h"
#include "server/webdav_handler.h" // try_serve_dav()
#include <time.h>                  // RFC 1123 Last-Modified formatting
#endif
#if PC_ENABLE_METRICS || PC_ENABLE_STATS
#include "network_drivers/application/web_assets.h" // PC_METRICS_PROM / PC_STATS_JSON (generated)
#endif
#if PC_HTTP_EMIT_DATE
#if PC_ENABLE_TIME_SOURCE
#include "services/timing_position/time_source/time_source.h" // pc_time_http_date() - any NTP/GPS/RTC/... source
#else
#include "network_drivers/application/ntp_service/ntp_service.h" // pc_ntp_http_date() - direct NTP (or the host test seam)
#endif
#endif
// No <string.h> and no <stdio.h>: every scan, compare, copy and search on this layer goes through
// shared_primitives/runops.h, and nothing here formats. strnlen and the strcasecmp pair are POSIX
// rather than ISO C, so they are absent under -std=c11 on a conforming libc.

// Outbound-transfer state is not held here. Each kind of transfer belongs to the TU that runs it:
// the chunked-send state to server/response.cpp, the file-send state to
// network_drivers/application/file_serving/file_serving.cpp. The poll below asks each owner whether it holds a slot
// instead of reading its state.




// The server's own state, owned here (internal linkage): where the access log goes, and the HTTP/3
// credentials held until begin() binds them. Nothing outside this file reads any of it.
typedef struct
{
    RequestLogCb log_cb; ///< Per-request access-log hook; may be null.

#if PC_ENABLE_HTTP3
    proto_bool pc_h3_running;
    const uint8_t *h3_cert;
    size_t h3_cert_len;
    uint8_t h3_seed[32];
    uint16_t h3_port;
    proto_bool h3_enabled;
#endif
} ServerCtx;

// Static storage duration zero-initializes every field: no handlers bound, no listeners registered.
static ServerCtx s_inst;

void pc_server_reset(void)
{
    // The server's state is spread across the files that own it, which is the point - but "start
    // over" is one concern, so it is one call rather than a checklist each caller has to keep in
    // agreement. The blank template lives in rodata, so the reset is a plain copy and never
    // materializes a sizeof(ServerCtx) temporary on the caller's stack.
    static const ServerCtx blank = {0};
    s_inst = blank;
    Tcp.listener->reserve_reset(); // the ports the app named, which begin() would bind again
    network.route->reset();
#if PC_ENABLE_AUTH
    // A credential id names a row by index and a route holds that id, so the two tables empty
    // together: routes left behind rows the table has no way to reach, and the table is bounded.
    Auth.reset();
#endif
    pc_mnt_point_reset(); // the same, for the mount id a static or DAV route holds
    pc_resp_reset();
    pc_middleware_reset();
    pc_signal_reset();
}

void on_request_log(RequestLogCb cb)
{
    s_inst.log_cb = cb;
}

// Record a completed response: bump stats counters and fire the access-log hook.
// The request's method/path are still intact in http_pool[slot_id] (http_reset
// has not run yet at the call sites).
void note_response(uint8_t slot_id, int code, int body_len)
{
    // Deposited, not tallied here. The loop knows the status at the instant it goes out, and
    // signaling is where a reader finds it; counting it here as well would be a second tally beside
    // the one every reader already consults.
    pc_signal_put_response(code);
    if (s_inst.log_cb)
    {
        const HttpReq *r = &http_pool[slot_id];
        s_inst.log_cb(r->method, r->path, code, body_len);
    }
}



// Finish a response: flush, then either begin the graceful CONN_CLOSING dwell
// (close path) or leave the slot active for reuse (keep-alive). The HTTP parser
// is reset either way, returning a kept-alive slot to PARSE_METHOD ready for the
// next request. The slot stays CONN_ACTIVE through the write on BOTH paths so its
// callbacks stay live; the close path then dwells in CONN_CLOSING from here, so the
// slot is reclaimed only once the peer ACKs the response (or the CLOSING timeout fires), not
// before it is delivered.
//
// The connection is addressed by slot alone and the transport resolves the pcb internally, the
// same way the RX read path does: no pcb is threaded through the app layer, so the send target
// cannot disagree with the slot.
void pc_resp_end(uint8_t slot_id, int code, int body_len, proto_bool keep, proto_bool pre_flushed)
{
    if (!pre_flushed)
    {
        Tcp.conn->flush(slot_id); // a pre_flushed caller already did tcp_output in its final send
    }
    if (!keep)
    {
        Tcp.conn->begin_close(slot_id); // ACTIVE -> CONN_CLOSING; finalizes on ACK
    }
    note_response(slot_id, code, body_len);
    http_reset(slot_id);
}

// Resolve the Connection response header (and report keep-alive intent) in one
// place so every response path agrees. Keep-alive compiled out always closes.
const char *pc_resp_conn_hdr(uint8_t slot_id, proto_bool *keep_out)
{
    proto_bool keep = PROTO_FALSE;
#if PC_ENABLE_KEEPALIVE
    keep = keepalive_eval(slot_id);
#else
    (void)slot_id;
#endif
    // The null half cannot fire: every call site passes the address of its own local `keep`. Kept so
    // the signature keeps saying the report-back is optional.
    if (keep_out)
    {
        *keep_out = keep;
    }
    return keep ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
}

// Append the shared response trailer (CORS block + custom headers + Connection +
// the terminating blank line) to a header buffer already holding the status line
// and per-response headers. One owner for the trailer every dynamic response ends
// with. Returns the new total length.
const char PC_RESP_HDR_OVERFLOW[] = "HTTP/1.1 500 Internal Server Error\r\n"
                                    "Content-Length: 0\r\n"
                                    "Connection: close\r\n\r\n";
// Taken with sizeof at the definition, where the array bound is still visible. The send site sees
// only `extern const char[]`, so measuring it there would mean scanning a string whose length was
// known when it was written.
const size_t PC_RESP_HDR_OVERFLOW_LEN = sizeof(PC_RESP_HDR_OVERFLOW) - 1;

int proto_append_resp_trailer(char *buf, size_t cap, int hlen, uint8_t slot_id, const char *cl)
{
    // hlen is the caller's status-line length from pc_sb_finish, which reports 0 for a status line
    // that did not fit. Appending the trailer at offset 0 in that case would emit a response with
    // no status line at all, so 0 propagates as failure and the caller sends a canned reply.
    //
    // A response either fits or is refused; it is never clamped to cap and sent. A header block cut
    // mid-field has no terminating CRLF, so the peer reads the body as continued headers and the
    // connection desynchronizes - worse than sending nothing.
    if (hlen <= 0)
    {
        return 0;
    }
    if ((size_t)hlen >= cap)
    {
        return 0;
    }
#if PC_HTTP_EMIT_DATE
    // RFC 7231 7.1.1.2: emit Date only when a real wall-clock time exists; a clock-less device (no
    // synced/valid time source yet) omits it. The time comes from the multi-source registry (any
    // enabled NTP / GPS / RTC / ... by priority) when PC_ENABLE_TIME_SOURCE is set, else straight
    // from NTP.
    char date_hdr[48] = "";
    char imf[40];
#if PC_ENABLE_TIME_SOURCE
    if (pc_time_http_date(imf, sizeof(imf)) > 0)
#else
    if (pc_ntp_http_date(imf, sizeof(imf)) > 0)
#endif
    {
        pc_sb sb_date_hdr = {date_hdr, sizeof(date_hdr), 0, PROTO_TRUE};
        pc_sb_put(&sb_date_hdr, "Date: ");
        pc_sb_put(&sb_date_hdr, imf);
        pc_sb_put(&sb_date_hdr, "\r\n");
        if (pc_sb_finish(&sb_date_hdr) == 0)
        {
            date_hdr[0] = '\0';
        }
    }
#else
    const char *date_hdr = "";
#endif
    pc_sb sb411 = {buf + hlen, cap - (size_t)hlen, 0, PROTO_TRUE};
    pc_sb_put(&sb411, date_hdr);
    pc_sb_put(&sb411, pc_resp_cors_enabled() ? pc_resp_cors_header() : "");
    pc_sb_put(&sb411, pc_resp_extra_hdr(slot_id));
    pc_sb_put(&sb411, cl);
    pc_sb_put(&sb411, "\r\n");
    int n = (int)pc_sb_finish(&sb411);
    if (!sb411.ok)
    {
        return 0; // trailer does not fit: refuse the response rather than send a headless one
    }
    return hlen + n;
}

int32_t listen(uint16_t port, ConnProto proto)
{
    // Returns the listener id (its index), not PC_OK: the accept path stamps that same index onto
    // every slot it claims, so this id is what pc_relay_publish() and pc_ssh_forward_begin() match
    // against. Errors are negative.
    int32_t id = Tcp.listener->reserve(port, proto, PROTO_FALSE);
    if (id < 0)
    {
        return (int32_t)PC_ERR_LISTENER_FULL;
    }
    return id;
}

#if PROTOCORE_HOT
// The worker task's per-tick entry (registered with pc_workers_start below); ESP32-only, so it is
// compiled only where it is used - on host the pipeline runs inline via handle().
static void pc_pump_trampoline(int worker_id)
{
    service_once(worker_id);
}
#endif

#if PC_ENABLE_HTTP3
// Adapts the pc_quic_server request seam, which carries an opaque app pointer, to the route
// dispatcher. The route table is a global owner, so nothing is carried in @p app and it is ignored.
static void pc_h3_request_trampoline(void *app, uint32_t conn_id, uint64_t stream_id, const char *method,
                                     const char *path, const char *authority, const uint8_t *body, size_t body_len)
{
    (void)app;
    dispatch_h3_request(conn_id, stream_id, method, path, authority, body, body_len);
}

// Randomness for the QUIC ephemeral X25519 key, the ServerHello random, and our connection IDs: the
// hardware TRNG on device; a deterministic PRNG on host (test builds carry no security context and
// have no esp_random).
static void pc_h3_rng(uint8_t *out, size_t len)
{
#if PROTOCORE_HOT
    size_t i = 0;
    while (i < len)
    {
        uint32_t r = pc_platform_rand_u32();
        size_t n = (len - i) < 4 ? (len - i) : 4;
        proto_raw_read(out + i, &r, n);
        i += n;
    }
#else
    static uint32_t s = 0x9e3779b9u;
    for (size_t i = 0; i < len; i++)
    {
        s = s * 1664525u + 1013904223u;
        out[i] = (uint8_t)(s >> 24);
    }
#endif
}
#endif // PC_ENABLE_HTTP3

// Installed by begin() as the HTTP ProtoHandler's on_poll, so the worker loop pumps HTTP through
// the same uniform seam as every other protocol. The ProtoHandler seam takes a plain slot, which is
// all http_poll_slot() needs: the route table and the slot pools are single global owners, so there
// is no per-server context to thread through.
static void pc_http_on_poll(uint8_t slot)
{
    http_poll_slot(slot);
}

int32_t proto_begin(const WebServerConfig *cfg)
{
    if (Tcp.listener->reserved() == 0
#if PC_ENABLE_HTTP3
        && !s_inst.h3_enabled // an HTTP/3-only server binds UDP, not a TCP listener
#endif
    )
    {
        return (int32_t)PC_ERR_NO_LISTENERS;
    }
    Tcp.conn->init(cfg);
#if PC_ENABLE_AUTH
    Auth.rekey(); // fresh server keying secret per begin()
#endif
#if PC_ENABLE_CSRF
    {
        // Seed the CSRF HMAC secret from the hardware RNG (a fixed dev secret on
        // native/test builds, which have no esp_random).
        uint8_t sec[32];
#if PROTOCORE_HOT
        for (int i = 0; i < 8; i++)
        {
            uint32_t r = pc_platform_rand_u32();
            proto_raw_read(sec + i * 4, &r, 4);
        }
#else
        for (int i = 0; i < 32; i++)
        {
            sec[i] = (uint8_t)(0xA5 ^ i);
        }
#endif
        pc_csrf_set_secret(sec, sizeof(sec));
    }
#endif
    for (uint8_t i = 0; i < MAX_CONNS; i++)
    {
        http_reset(i);
    }
#if PC_ENABLE_WEBSOCKET
    ws_init();
#endif
#if PC_ENABLE_SSE
    pc_sse_init();
#endif
    if (!Tcp.listener->bind_reserved())
    {
        return (int32_t)PC_ERR_LISTEN_FAILED;
    }
#if PC_ENABLE_HTTP3
    // Bind the HTTP/3 QUIC server (UDP on device; on host it is fed via pc_quic_server_ingest). Requests
    // dispatch through the route table via the trampoline; pc_quic_server_poll() runs in service_once.
    if (s_inst.h3_enabled)
    {
        QuicServerConfig h3cfg = {0};
        h3cfg.cert_der = s_inst.h3_cert;
        h3cfg.cert_len = s_inst.h3_cert_len;
        proto_raw_read(h3cfg.ed25519_seed, s_inst.h3_seed, sizeof(h3cfg.ed25519_seed));
        h3cfg.rng = pc_h3_rng;
        // No app pointer: the trampoline dispatches through the global route table.
        s_inst.pc_h3_running = pc_quic_server_begin(s_inst.h3_port, &h3cfg, pc_h3_request_trampoline, NULL);
    }
#endif
#if PROTOCORE_HOT
    // Routes/listeners are now fixed; start the worker task(s) that drive the
    // pipeline off the user's loop(). On host the pipeline runs inline via handle().
    pc_workers_start(pc_pump_trampoline);
#endif
    return (int32_t)PC_OK;
}

int32_t begin_http(uint16_t port, const WebServerConfig *cfg)
{
    int32_t rc = listen(port, PROTO_HTTP);
    if (rc < 0)
    {
        return rc;
    }
    return proto_begin(cfg);
}

#if PC_ENABLE_HTTP3
proto_bool pc_h3_cert(const uint8_t *cert_der, size_t cert_len, const uint8_t ed25519_seed[32], uint16_t port)
{
    if (!cert_der || cert_len == 0 || !ed25519_seed)
    {
        return PROTO_FALSE;
    }
    s_inst.h3_cert = cert_der;
    s_inst.h3_cert_len = cert_len;
    proto_raw_read(s_inst.h3_seed, ed25519_seed, sizeof(s_inst.h3_seed));
    s_inst.h3_port = port;
    s_inst.h3_enabled = PROTO_TRUE;
    return PROTO_TRUE;
}

// Response sink for the HTTP/3 dispatch slot: route (code, content_type, body) onto the QUIC stream
// the request arrived on (ids stashed on the slot by dispatch_h3_request). Installed as conn->pc_resp_sink
// so send_text()/send_empty() stay protocol-agnostic.
static proto_bool pc_h3_resp_sink(uint8_t slot, int code, const char *content_type, const char *body, size_t len)
{
    TcpConn *c = &conn_pool[slot];
    return pc_quic_server_respond(c->pc_h3_conn_id, c->pc_h3_stream, code, content_type, (const uint8_t *)body, len);
}

void dispatch_h3_request(uint32_t conn_id, uint64_t stream_id, const char *method, const char *path,
                         const char *authority, const uint8_t *body, size_t body_len)
{
    const uint8_t slot = PC_H3_DISPATCH_SLOT;
    HttpReq *r = &http_pool[slot];
    http_reset(slot);

    // Map the semantic request fields into the shared HttpReq (as pc_h2_server does per stream).
    size_t mn = proto_scan_nul(method, sizeof(r->method));
    if (mn >= sizeof(r->method))
    {
        mn = sizeof(r->method) - 1;
    }
    proto_raw_read(r->method, method, mn);
    r->method[mn] = 0;

    // Bounded by everything the request could occupy here, path and query together, rather than by
    // the path field alone: a '?' past the path cap still names a query this slot has room for, and
    // capping the search at the path would drop it while keeping the truncated path.
    const char *q = proto_find(path, sizeof(r->path) + sizeof(r->query), "?", sizeof("?"));
    size_t plen = (q != NULL) ? (size_t)(q - path) : proto_scan_nul(path, sizeof(r->path));
    if (plen >= sizeof(r->path))
    {
        plen = sizeof(r->path) - 1;
    }
    proto_raw_read(r->path, path, plen);
    r->path[plen] = 0;
    r->path_idx = proto_scan_nul(r->path, sizeof(r->path));
    if (q != NULL)
    {
        size_t ql = proto_scan_nul(q + 1, sizeof(r->query));
        if (ql >= sizeof(r->query))
        {
            ql = sizeof(r->query) - 1;
        }
        proto_raw_read(r->query, q + 1, ql);
        r->query[ql] = 0;
        r->query_idx = proto_scan_nul(r->query, sizeof(r->query));
    }

    // :authority maps to Host, the way the h2 bridge does.
    if (authority && authority[0] && r->header_count < MAX_HEADERS)
    {
        Header *h = &r->headers[r->header_count];
        r->header_count++;
        proto_raw_read(h->key, "host", 5);
        size_t vl = proto_scan_nul(authority, sizeof(h->val));
        if (vl >= sizeof(h->val))
        {
            vl = sizeof(h->val) - 1;
        }
        proto_raw_read(h->val, authority, vl);
        h->val[vl] = 0;
    }

    if (body && body_len)
    {
        size_t n = body_len > BODY_BUF_SIZE ? BODY_BUF_SIZE : body_len;
        proto_raw_read(r->body, body, n);
        r->body_len = n;
        r->body[r->body_len] = 0;
        r->body_bytes_read = body_len;
        r->content_length = body_len;
    }
    r->parse_state = PARSE_COMPLETE;

    // Mark the reserved slot as HTTP/3 and install the response sink so send_text() / send_empty() route the
    // response back onto this stream (no TCP pcb here - the sink owns the QUIC framing).
    TcpConn *c = &conn_pool[slot];
    c->h3 = 1;
    c->pc_h3_conn_id = conn_id;
    c->pc_h3_stream = stream_id;
    c->pc_resp_sink = pc_h3_resp_sink;
    c->iface = PC_IFACE_STA;
    Tcp.conn->set_state(slot, CONN_ACTIVE); // reserved slot: no bitmask bit (slot >= MAX_CONNS)
    c->pcb = NULL;

    Http.match_and_execute(slot); // -> handler -> send_text() -> pc_resp_sink -> pc_quic_server_respond()

    // Release the dispatch slot for the next request (a no-response handler simply leaves the stream open).
    c->h3 = 0;
    c->pc_resp_sink = NULL;
    Tcp.conn->set_state(slot, CONN_FREE); // reserved slot: no bitmask bit (slot >= MAX_CONNS)
    http_reset(slot);
}
#endif // PC_ENABLE_HTTP3

#if PC_ENABLE_TLS
proto_bool tls_cert(const uint8_t *cert, size_t cert_len, const uint8_t *key, size_t key_len)
{
    return pc_tls_global_init(cert, cert_len, key, key_len);
}

int32_t listen_tls(uint16_t port)
{
    if (Tcp.listener->reserve(port, PROTO_HTTP, PROTO_TRUE) < 0)
    {
        return (int32_t)PC_ERR_LISTENER_FULL;
    }
    return (int32_t)PC_OK;
}

int32_t begin_tls(uint16_t port, const uint8_t *cert, size_t cert_len, const uint8_t *key, size_t key_len,
                  const WebServerConfig *cfg)
{
    if (!tls_cert(cert, cert_len, key, key_len))
    {
        return (int32_t)PC_ERR_LISTEN_FAILED;
    }
    int32_t rc = listen_tls(port);
    if (rc < 0)
    {
        return rc;
    }
    return proto_begin(cfg);
}

#if PC_ENABLE_MTLS
proto_bool tls_require_client_cert(const uint8_t *ca, size_t ca_len)
{
    return pc_tls_set_client_ca(ca, ca_len);
}

int tls_client_subject(uint8_t slot_id, char *out, size_t out_len)
{
    return pc_tls_peer_subject(slot_id, out, out_len);
}
#endif // PC_ENABLE_MTLS
#endif // PC_ENABLE_TLS

int32_t restart(const WebServerConfig *cfg)
{
    if (Tcp.listener->reserved() == 0)
    {
        return (int32_t)PC_ERR_NO_LISTENERS;
    }
    stop();
    return proto_begin(cfg);
}

void stop(void)
{
#if PROTOCORE_HOT
    // Stop the worker task(s) before tearing down the slots they service.
    pc_workers_stop();
#endif
    Tcp.listener->stop_all();
    Tcp.conn->stop();
    for (uint8_t i = 0; i < MAX_CONNS; i++)
    {
        http_reset(i);
    }
#if PC_ENABLE_WEBSOCKET
    ws_init();
#endif
#if PC_ENABLE_SSE
    pc_sse_init();
#endif
}

/**
 * @brief Fill the fields every route kind shares, whatever its type.
 *
 * The path is stored null-terminated and truncated to MAX_PATH_LEN. Its shape decides two match
 * modes on the spot, so the dispatcher never re-inspects the string: a trailing `*` is a prefix
 * match, and a `/:` anywhere marks a path-parameter route. Regex and the interface filter are set
 * to their inactive defaults for the caller to override.
 *
 * @param r    Route to initialize.
 * @param path URL path to match, e.g. a trailing-star prefix or a `:name` segment.
 */
void fill_route_base(Route *r, const char *path)
{
    // The copy terminates the destination itself and hands back what it wrote, so the length the
    // two shape tests need comes out of the move rather than from a second walk over those bytes.
    size_t len = proto_copy(r->path, path, MAX_PATH_LEN);
    r->is_active = PROTO_TRUE;
    r->is_wildcard = (len > 0 && r->path[len - 1] == '*');
    // Whether, not where: the sieve sweeps the whole field for a fixed cost rather than stopping at
    // the first `/`, which a route path is full of.
    r->is_param = proto_has(r->path, MAX_PATH_LEN, "/:", sizeof("/:"));
    r->is_regex = PROTO_FALSE;
    r->iface_filter = PC_IFACE_ANY;
#if PC_ENABLE_AUTH
    // Stated, not inherited from the zeroed slot: zero is a valid credential id, so a route that
    // registers no credentials has to say so, or the first set anyone registers would guard every
    // route in the table.
    r->auth_id = PC_AUTH_NONE;
#endif
#if PC_ENABLE_WEBSOCKET
    r->ws_id = PC_WS_NONE; // same reason: zero names a real handler set
#endif
#if PC_ENABLE_SSE
    r->sse_id = PC_SSE_NONE; // same reason
#endif
#if PC_ENABLE_FILE_SERVING
    r->mnt_id = PC_MNT_NONE; // same reason
#endif
}

void on_http(const char *path, HttpMethod method, Handler callback)
{
    Route *r = network.route->add();
    if (r == NULL)
    {
        return;
    }

    fill_route_base(r, path);
    r->type = ROUTE_HTTP;
    r->method = method;
    r->callback = callback;
}

void on_http_iface(const char *path, HttpMethod method, Handler callback, pc_iface iface)
{
    Route *r = network.route->add();
    if (r == NULL)
    {
        return;
    }

    fill_route_base(r, path);
    r->type = ROUTE_HTTP;
    r->method = method;
    r->callback = callback;
    r->iface_filter = iface;
}

void set_ap_ip(uint32_t ap_ip)
{
    pc_ap_ip = ap_ip;
}

void on_regex(const char *pattern, HttpMethod method, Handler callback)
{
    Route *r = network.route->add();
    if (r == NULL)
    {
        return;
    }

    fill_route_base(r, pattern);
    r->type = ROUTE_HTTP;
    r->method = method;
    r->callback = callback;
    r->is_regex = PROTO_TRUE;
}

#if PC_ENABLE_AUTH
void on_http_auth(const char *path, HttpMethod method, Handler callback, const char *realm, const char *user,
                  const char *pass, proto_bool digest)
{
    Route *r = network.route->add();
    if (r == NULL)
    {
        return;
    }

    fill_route_base(r, path);
    r->type = ROUTE_HTTP;
    r->method = method;
    r->callback = callback;
    // The credential goes to the module that checks it; the route keeps only the id naming it.
    r->auth_id = Auth.add(realm, user, pass, digest);
}
#endif // PC_ENABLE_AUTH

#if PC_ENABLE_WEBSOCKET
void on_ws(const char *path, WsConnectHandler on_connect, WsMessageHandler on_message, WsCloseHandler on_close)
{
    Route *r = network.route->add();
    if (r == NULL)
    {
        return;
    }

    fill_route_base(r, path);
    r->type = ROUTE_WS;
    r->ws_id = ws_route_add(on_connect, on_message, on_close);
}
#endif // PC_ENABLE_WEBSOCKET

#if PC_ENABLE_SSE
void on_sse(const char *path, SseConnectHandler on_connect)
{
    Route *r = network.route->add();
    if (r == NULL)
    {
        return;
    }

    fill_route_base(r, path);
    r->type = ROUTE_SSE;
    r->sse_id = pc_sse_route_add(on_connect);
}
#endif // PC_ENABLE_SSE

void on_not_found(Handler callback)
{
    Http.set_not_found(callback);
}

// set_cors() / set_cache_control() live in server/response.cpp, with the buffers they fill.

#if PC_ENABLE_HTTP_DELIVERY
proto_bool set_cache_control_swr(uint32_t max_age_s, uint32_t swr_s)
{
    // Build the directive with the RFC 5861 core so the header and the pc_delivery_swr decision
    // can never drift apart.
    char directive[64];
    if (pc_delivery_cache_control(max_age_s, swr_s, directive, sizeof(directive)) == 0)
    {
        return PROTO_FALSE;
    }
    set_cache_control(directive);
    return PROTO_TRUE;
}
#endif




#if PC_ENABLE_WEBSOCKET
void ws_dispatch_message(const WsConn *ws)
{
    for (uint8_t r = 0; r < network.route->count(); r++)
    {
        const Route *rt = network.route->at(r);
        if (rt->type != ROUTE_WS)
        {
            continue;
        }
        WsMessageHandler on_message = ws_route_message(rt->ws_id);
        if (on_message != NULL)
        {
            on_message(ws->ws_id);
            break;
        }
    }
}

void ws_dispatch_close(const WsConn *ws)
{
    for (uint8_t r = 0; r < network.route->count(); r++)
    {
        const Route *rt = network.route->at(r);
        if (rt->type != ROUTE_WS)
        {
            continue;
        }
        WsCloseHandler on_close = ws_route_close(rt->ws_id);
        if (on_close != NULL)
        {
            on_close(ws->ws_id);
            break;
        }
    }
}
#endif // PC_ENABLE_WEBSOCKET

/**
 * @brief Main application tick - tick the session layer then dispatch completed requests.
 *
 * Call this repeatedly from loop(). Each call runs one service_once() pass: a server_tick()
 * (timeout sweeps + event-queue drain), then a poll of every slot this worker owns, which is
 * where a completed request is dispatched and a parse failure is answered.
 *
 * On ESP32 the worker task drives that pass on its own core, so this returns immediately and
 * loop() is free.
 */
void handle(void)
{
#if PROTOCORE_HOT
    if (pc_workers_running())
    {
        return;
    }
#endif
    service_once(0); // the inline path is worker 0: the pools are all its own
}

void service_once(int worker_id)
{
    // Install HTTP's poll so the dispatch loop below pumps it through the uniform
    // ProtoHandler.on_poll seam (see http_poll_slot). Done here rather than only in begin() so a
    // caller that drives service_once() directly still gets it. One pointer store; negligible at
    // poll cadence.
    http_proto_set_poll(pc_http_on_poll);

    server_tick(worker_id);

#if PC_ENABLE_HTTP3
    // Drive the QUIC/HTTP-3 server: ingest queued datagrams, run the engines (which dispatch requests
    // through the route table), flush replies. One worker owns it, so requests stay single-threaded.
    if (worker_id == 0 && s_inst.pc_h3_running)
    {
        pc_quic_server_poll(pc_millis());
    }
#endif

    for (uint8_t i = 0; i < MAX_CONNS; i++)
    {
        // This worker services only the slots it owns (all of them at N=1).
        if (conn_pool[i].owner != worker_id)
        {
            continue;
        }

        // Ack-on-consume: reopen the TCP receive window by whatever any consumer
        // (HTTP/WS/TLS/service) drained from this slot's ring on the previous pass.
        // Transport owns the window math; we just nudge it once per slot per loop.
        Tcp.conn->ack_consumed(i);

        // Every protocol - HTTP included - is pumped through the one uniform ProtoHandler.on_poll
        // seam, so there is no per-protocol branch here. HTTP reaches it via http_proto_set_poll()
        // -> http_poll_slot(); the singleton pollers (SSH etc.) gate on CONN_ACTIVE
        // inside their own on_poll.
        const ProtoHandler *ph = proto_get(conn_pool[i].proto);
        if (ph && ph->on_poll)
        {
            ph->on_poll(i);
        }
    }

    // Run any callbacks app code deferred to this worker (race-free push path).
    pc_worker_run_deferred(worker_id);
}

#if PC_ENABLE_EDGE_CACHE
// Edge-cache async-fetch pump seam (see pc_http_set_edge_poll / services/web/edge_cache/edge_cache_proxy):
// a cache miss suspends the client request and drives the non-blocking origin fetch from this slot's poll.
static proto_bool (*s_edge_poll)(uint8_t slot) = NULL;
void pc_http_set_edge_poll(proto_bool (*fn)(uint8_t slot))
{
    s_edge_poll = fn;
}
#endif

// HTTP's poll pump, installed as the HTTP ProtoHandler's on_poll so the worker dispatch loop pumps
// HTTP through the same uniform seam as every other protocol, with no HTTP special case in the
// loop. Runs the file/chunk send pumps, the WebSocket and SSE drains, the keep-alive re-parse, and
// dispatches a completed request into the route table.
void http_poll_slot(uint8_t i)
{
#if PC_ENABLE_EDGE_CACHE
    // An edge-cache origin fetch in flight for this slot owns it: pump the fetch and skip the rest of the
    // HTTP pipeline until it completes (and hands off to send_chunked for the cached response).
    if (s_edge_poll && s_edge_poll(i))
    {
        return;
    }
#endif
#if PC_ENABLE_FILE_SERVING
    // A file response in flight owns the slot: page out the next window and
    // skip the rest of the pipeline until the whole body has been sent.
    if (pc_file_holds_slot(i))
    {
        file_send_pump(i);
        return;
    }
#endif
    // Likewise a chunked response in flight: pull + frame the next window.
    if (pc_resp_holds_slot(i))
    {
        chunk_send_pump(i);
        return;
    }

#if PC_ENABLE_WEBSOCKET
    // WebSocket slot - drain ring buffer and dispatch ready frames
    WsConn *ws = ws_find(i);
    if (ws)
    {
#if PC_ENABLE_TLS
        if (conn_pool[i].tls)
        {
            // wss://: the rx ring holds ciphertext, so decrypt records here and
            // feed the frame parser, dispatching each completed frame as it
            // finishes (one TLS record may carry several WS frames).
            uint8_t tbuf[256];
            int n;
            while ((n = pc_tls_read(i, tbuf, sizeof(tbuf))) > 0)
            {
                for (int k = 0; k < n; k++)
                {
                    ws_feed_byte(ws, tbuf[k]);
                    if (ws->parse_state == WS_FRAME_READY)
                    {
                        ws_dispatch_message(ws);
                        ws_reset_frame(ws);
                    }
                    else if (ws->parse_state == WS_CLOSED || ws->parse_state == WS_ERROR)
                    {
                        break;
                    }
                }
                if (ws->parse_state == WS_CLOSED || ws->parse_state == WS_ERROR)
                {
                    break;
                }
            }
            if (ws->parse_state == WS_CLOSED || ws->parse_state == WS_ERROR || n < 0)
            {
                ws_dispatch_close(ws);
                ws_free(i);
                Tcp.conn->abort_slot(i); // transport owns TLS-free + detach + reset + RST
                http_reset(i);
            }
            return;
        }
#endif // PC_ENABLE_TLS

        ws_parse(ws);

        if (ws->parse_state == WS_FRAME_READY)
        {
            ws_dispatch_message(ws);
            ws_reset_frame(ws);
        }
        else if (ws->parse_state == WS_CLOSED || ws->parse_state == WS_ERROR)
        {
            ws_dispatch_close(ws);
            ws_free(i);
            // RFC 6455 5.5.1: close the underlying TCP connection after the close
            // handshake. begin_close moves the slot out of CONN_ACTIVE so the
            // post-close bytes are NOT re-parsed as a new HTTP request (the
            // close-frame the WS layer queued still flushes during the dwell).
            Tcp.conn->begin_close(i);
            http_reset(i);
        }
        return; // slot is owned by WS; skip HTTP dispatch
    }
#endif // PC_ENABLE_WEBSOCKET

#if PC_ENABLE_SSE
    // SSE slot - connection stays open, nothing to parse from client
    if (pc_sse_find(i))
    {
        return;
    }
#endif // PC_ENABLE_SSE

#if PC_ENABLE_KEEPALIVE
    // Keep-alive: a slot recycled after a response may already hold the next
    // (pipelined) request in its ring buffer with no new EVT_DATA to trigger a
    // parse. Drain it here each tick so it gets dispatched. TLS slots are
    // skipped - their ring holds ciphertext, decrypted in the session layer.
    if (conn_pool[i].state == CONN_ACTIVE && http_pool[i].parse_state != PARSE_COMPLETE
#if PC_ENABLE_TLS
        && !conn_pool[i].tls
#endif
    )
    {
        http_parse(i);
    }
#endif

#if PC_REQUEST_TIMEOUT_MS > 0
    // Slow-loris defense (the nginx client_header_timeout semantic): bound the request HEADER phase. A
    // connection that sent its first byte but has not completed its request headers within
    // PC_REQUEST_TIMEOUT_MS is answered 408 and closed, freeing the slot. Unlike the idle timeout, req_start_ms
    // is NOT reset by a trickle byte (it is armed once, on the first RX byte), so a drip-fed partial header
    // cannot hold a slot open indefinitely, which is the connection-slot exhaustion this bounds. The deadline is
    // scoped to the header phase (parse_state < PARSE_BODY, since every header state precedes PARSE_BODY in the
    // enum) so it never reaps a legitimate slow body: a large streaming upload sits in PARSE_BODY for its whole
    // duration and is governed by the streaming handler + idle timer, not this deadline. WebSocket / SSE were
    // already returned above.
    if (conn_pool[i].state == CONN_ACTIVE && conn_pool[i].req_start_ms != 0 && http_pool[i].parse_state < PARSE_BODY &&
        (pc_millis() - conn_pool[i].req_start_ms) >= PC_REQUEST_TIMEOUT_MS)
    {
        conn_pool[i].req_start_ms = 0;
        send_text(i, 408, PC_MIME_TEXT_PLAIN, "Request Timeout"); // terminal error response -> connection closes
        return;
    }
#endif

    // HTTP slot
    if (http_pool[i].parse_state == PARSE_COMPLETE)
    {
        conn_pool[i].req_start_ms = 0; // request complete: disarm; the next keep-alive request re-arms on its 1st byte
        Http.match_and_execute(i);
        if (http_pool[i].parse_state == PARSE_COMPLETE)
        {
            http_reset(i);
        }
    }
    else if (http_pool[i].parse_state == PARSE_ERROR)
    {
        send_text(i, 400, PC_MIME_TEXT_PLAIN, "Bad Request");
    }
    else if (http_pool[i].parse_state == PARSE_ENTITY_TOO_LARGE)
    {
        send_text(i, 413, PC_MIME_TEXT_PLAIN, "Payload Too Large");
    }
    else if (http_pool[i].parse_state == PARSE_URI_TOO_LONG)
    {
        send_text(i, 414, PC_MIME_TEXT_PLAIN, "URI Too Long");
    }
}

proto_bool defer(uint8_t slot, pc_deferred_fn fn, void *arg)
{
    if (slot >= MAX_CONNS)
    {
        return PROTO_FALSE;
    }
    // Route to the worker that owns the slot so the callback runs single-threaded
    // alongside that slot's own processing.
    return pc_defer(conn_pool[slot].owner, fn, arg);
}

// ---------------------------------------------------------------------------
// Diagnostic endpoint
// ---------------------------------------------------------------------------

#if PC_ENABLE_DIAG

// The build-info document. Every value is a compile-time constant, so nothing is discovered at
// runtime: each flag selects one of two literals and each sizing constant renders as a decimal.
// A frame spec like every other here, so the conversions come from the shared engine.
static const pc_field DIAG_DOC[] = {
    {PC_FK_LIT, 0, 31, "{\"lib\":\"ProtoCore\",\"features\":{"},
    {PC_FK_LIT, 0, 12, "\"websocket\":"},
    PC_STR,
    {PC_FK_LIT, 0, 7, ",\"sse\":"},
    PC_STR,
    {PC_FK_LIT, 0, 13, ",\"multipart\":"},
    PC_STR,
    {PC_FK_LIT, 0, 16, ",\"file_serving\":"},
    PC_STR,
    {PC_FK_LIT, 0, 8, ",\"auth\":"},
    PC_STR,
    {PC_FK_LIT, 0, 10, ",\"webdav\":"},
    PC_STR,
    {PC_FK_LIT, 0, 8, ",\"coap\":"},
    PC_STR,
    {PC_FK_LIT, 0, 8, ",\"snmp\":"},
    PC_STR,
    {PC_FK_LIT, 0, 9, ",\"opcua\":"},
    PC_STR,
    {PC_FK_LIT, 0, 9, ",\"umati\":"},
    PC_STR,
    {PC_FK_LIT, 0, 10, ",\"modbus\":"},
    PC_STR,
    {PC_FK_LIT, 0, 8, ",\"mqtt\":"},
    PC_STR,
    {PC_FK_LIT, 0, 13, ",\"mtconnect\":"},
    PC_STR,
    {PC_FK_LIT, 0, 9, ",\"redis\":"},
    PC_STR,
    {PC_FK_LIT, 0, 7, ",\"ftp\":"},
    PC_STR,
    {PC_FK_LIT, 0, 8, ",\"smtp\":"},
    PC_STR,
    {PC_FK_LIT, 0, 7, ",\"smb\":"},
    PC_STR,
    {PC_FK_LIT, 0, 10, ",\"syslog\":"},
    PC_STR,
    {PC_FK_LIT, 0, 17, ",\"pc_ntp_server\":"},
    PC_STR,
    {PC_FK_LIT, 0, 14, ",\"dns_server\":"},
    PC_STR,
    {PC_FK_LIT, 0, 8, ",\"nats\":"},
    PC_STR,
    {PC_FK_LIT, 0, 9, ",\"stomp\":"},
    PC_STR,
    {PC_FK_LIT, 0, 10, ",\"statsd\":"},
    PC_STR,
    {PC_FK_LIT, 0, 7, ",\"jwt\":"},
    PC_STR,
    {PC_FK_LIT, 0, 7, ",\"tls\":"},
    PC_STR,
    {PC_FK_LIT, 0, 9, ",\"http2\":"},
    PC_STR,
    {PC_FK_LIT, 0, 9, ",\"http3\":"},
    PC_STR,
    {PC_FK_LIT, 0, 7, ",\"ssh\":"},
    PC_STR,
    {PC_FK_LIT, 0, 14, ",\"ws_deflate\":"},
    PC_STR,
    {PC_FK_LIT, 0, 9, ",\"range\":"},
    PC_STR,
    {PC_FK_LIT, 0, 8, ",\"csrf\":"},
    PC_STR,
    {PC_FK_LIT, 0, 19, ",\"accept_throttle\":"},
    PC_STR,
    {PC_FK_LIT, 0, 19, ",\"per_ip_throttle\":"},
    PC_STR,
    {PC_FK_LIT, 0, 16, ",\"auth_lockout\":"},
    PC_STR,
    {PC_FK_LIT, 0, 12, "},\"config\":{"},
    {PC_FK_LIT, 0, 12, "\"MAX_CONNS\":"},
    PC_U32,
    {PC_FK_LIT, 0, 15, ",\"RX_BUF_SIZE\":"},
    PC_U32,
    {PC_FK_LIT, 0, 17, ",\"BODY_BUF_SIZE\":"},
    PC_U32,
    {PC_FK_LIT, 0, 14, ",\"MAX_ROUTES\":"},
    PC_U32,
    {PC_FK_LIT, 0, 15, ",\"MAX_HEADERS\":"},
    PC_U32,
    {PC_FK_LIT, 0, 16, ",\"MAX_PATH_LEN\":"},
    PC_U32,
    {PC_FK_LIT, 0, 15, ",\"MAX_KEY_LEN\":"},
    PC_U32,
    {PC_FK_LIT, 0, 15, ",\"MAX_VAL_LEN\":"},
    PC_U32,
    {PC_FK_LIT, 0, 17, ",\"MAX_QUERY_LEN\":"},
    PC_U32,
    {PC_FK_LIT, 0, 20, ",\"MAX_QUERY_PARAMS\":"},
    PC_U32,
    {PC_FK_LIT, 0, 19, ",\"CONN_TIMEOUT_MS\":"},
    PC_U32,
    {PC_FK_LIT, 0, 21, ",\"RESP_HDR_BUF_SIZE\":"},
    PC_U32,
    {PC_FK_LIT, 0, 19, ",\"WS_HDR_BUF_SIZE\":"},
    PC_U32,
    {PC_FK_LIT, 0, 21, ",\"CORS_HDR_BUF_SIZE\":"},
    PC_U32,
    {PC_FK_LIT, 0, 19, ",\"EVT_QUEUE_DEPTH\":"},
    PC_U32,
    {PC_FK_LIT, 0, 2, "}}"},
    PC_END,
};

// Worst case: every flag rendering as the longer "false", every size at a uint32_t's ten digits.
#define PC_PLAINTEXT_WORK_DIAG 975
static_assert(PC_PLAINTEXT_WORK_DIAG <= PC_PLAINTEXT_ARENA_SIZE, "diag document exceeds the arena");

void diag(uint8_t slot_id)
{
    // Mark before the borrow and release on every exit: the document is transient, and the
    // per-dispatch reset is only the backstop.
    const size_t mark = pc_plaintext_mark();
    char *doc = (char *)pc_plaintext_alloc(PC_PLAINTEXT_WORK_DIAG, 1);
    if (doc == NULL ||
        pc_frame_build(
            doc, PC_PLAINTEXT_WORK_DIAG, DIAG_DOC, PC_ENABLE_WEBSOCKET ? "true" : "false",
            PC_ENABLE_SSE ? "true" : "false", PC_ENABLE_MULTIPART ? "true" : "false",
            PC_ENABLE_FILE_SERVING ? "true" : "false", PC_ENABLE_AUTH ? "true" : "false",
            PC_ENABLE_WEBDAV ? "true" : "false", PC_ENABLE_COAP ? "true" : "false", PC_ENABLE_SNMP ? "true" : "false",
            PC_ENABLE_OPCUA ? "true" : "false", PC_ENABLE_UMATI ? "true" : "false", PC_ENABLE_MODBUS ? "true" : "false",
            PC_ENABLE_MQTT ? "true" : "false", PC_ENABLE_MTCONNECT ? "true" : "false",
            PC_ENABLE_REDIS ? "true" : "false", PC_ENABLE_FTP ? "true" : "false", PC_ENABLE_SMTP ? "true" : "false",
            PC_ENABLE_SMB ? "true" : "false", PC_ENABLE_SYSLOG ? "true" : "false",
            PC_ENABLE_NTP_SERVER ? "true" : "false", PC_ENABLE_DNS_SERVER ? "true" : "false",
            PC_ENABLE_NATS ? "true" : "false", PC_ENABLE_STOMP ? "true" : "false", PC_ENABLE_STATSD ? "true" : "false",
            PC_ENABLE_JWT ? "true" : "false", PC_ENABLE_TLS ? "true" : "false", PC_ENABLE_HTTP2 ? "true" : "false",
            PC_ENABLE_HTTP3 ? "true" : "false", PC_ENABLE_SSH ? "true" : "false",
            PC_ENABLE_WS_DEFLATE ? "true" : "false", PC_ENABLE_RANGE ? "true" : "false",
            PC_ENABLE_CSRF ? "true" : "false", PC_ENABLE_ACCEPT_THROTTLE ? "true" : "false",
            PC_ENABLE_PER_IP_THROTTLE ? "true" : "false", PC_ENABLE_AUTH_LOCKOUT ? "true" : "false",
            (uint32_t)MAX_CONNS, (uint32_t)RX_BUF_SIZE, (uint32_t)BODY_BUF_SIZE, (uint32_t)MAX_ROUTES,
            (uint32_t)MAX_HEADERS, (uint32_t)MAX_PATH_LEN, (uint32_t)MAX_KEY_LEN, (uint32_t)MAX_VAL_LEN,
            (uint32_t)MAX_QUERY_LEN, (uint32_t)MAX_QUERY_PARAMS, (uint32_t)CONN_TIMEOUT_MS, (uint32_t)RESP_HDR_BUF_SIZE,
            (uint32_t)WS_HDR_BUF_SIZE, (uint32_t)CORS_HDR_BUF_SIZE, (uint32_t)EVT_QUEUE_DEPTH) == 0)
    {
        pc_plaintext_release(mark);
        send_text(slot_id, 503, PC_MIME_TEXT_PLAIN, ""); // fail closed: no partial document reaches the wire
        return;
    }
    send_text(slot_id, 200, PC_MIME_JSON, doc); // reads doc, so it runs before the release
    pc_plaintext_release(mark);
}
#endif

/**
 * @brief Send an HTTP response whose body is a null-terminated string.
 *
 * @param slot_id      Connection slot index.
 * @param code         HTTP status code, e.g. 200.
 * @param content_type MIME type string, e.g. "application/json".
 * @param payload      Null-terminated body string to send; null sends an empty body.
 */
void send_text(uint8_t slot_id, int code, const char *content_type, const char *payload)
{
    // Null-terminated convenience wrapper over the explicit-length send: the only difference between
    // the two is who scans for the length, so text is bin plus one scan rather than a second sender.
    // 0xFFFF is how far the scan is willing to look, not a claim the caller's string is that long:
    // a body is a handler's string of unstated capacity, and the bound is what keeps a missing
    // terminator from becoming an unbounded walk.
    send_bin(slot_id, code, content_type, (const uint8_t *)payload,
             (payload != NULL) ? proto_scan_nul(payload, 0xFFFF) : 0);
}

void send_bin(uint8_t slot_id, int code, const char *content_type, const uint8_t *body, size_t body_len)
{
    if (slot_id >= CONN_POOL_SLOTS)
    {
        return; // guard the public entry: never index conn_pool out of range
    }
    const char *payload = (const char *)body;
    TcpConn *conn = &conn_pool[slot_id];
#if PC_ENABLE_HTTP2 || PC_ENABLE_HTTP3
    // A self-framing protocol (HTTP/2, HTTP/3) installed its own response sink at negotiation /
    // dispatch time; route through it and let it own its framing + connection lifecycle. This runs
    // before the HTTP/1.1 pcb check because that check is a TCP-transport concern (the HTTP/3 slot
    // has no pcb by design, and an h2 connection manages its own).
    if (conn->pc_resp_sink)
    {
        conn->pc_resp_sink(slot_id, code, content_type, payload, body_len);
        return;
    }
#endif
    if (conn->state != CONN_ACTIVE || conn->pcb == NULL)
    {
        http_reset(slot_id);
        return;
    }

    int payload_len = (int)(body_len > 0xFFFF ? 0xFFFF : body_len);

    proto_bool keep;
    const char *cl = pc_resp_conn_hdr(slot_id, &keep);

    char header[RESP_HDR_BUF_SIZE];
    pc_sb sb_header2 = {header, sizeof(header), 0, PROTO_TRUE};
    pc_sb_put(&sb_header2, "HTTP/1.1 ");
    pc_sb_i64(&sb_header2, (int64_t)(code));
    pc_sb_put(&sb_header2, " ");
    pc_sb_put(&sb_header2, Http.status_text(code));
    pc_sb_put(&sb_header2, "\r\nContent-Type: ");
    pc_sb_put(&sb_header2, content_type);
    pc_sb_put(&sb_header2, "\r\nContent-Length: ");
    pc_sb_i64(&sb_header2, (int64_t)(payload_len));
    pc_sb_put(&sb_header2, "\r\n");
    int hlen = (int)pc_sb_finish(&sb_header2);
    hlen = proto_append_resp_trailer(header, sizeof(header), hlen, slot_id, cl);
    if (hlen == 0)
    {
        // The headers do not fit RESP_HDR_BUF_SIZE (an over-long content type, or a custom-header
        // block that filled the buffer). Truncating them would emit a header block with no
        // terminating CRLF and desync the connection, so a fixed reply that always fits goes out
        // instead and the connection closes.
        Tcp.conn->send_flush(slot_id, PC_RESP_HDR_OVERFLOW, (proto_u16)PC_RESP_HDR_OVERFLOW_LEN);
        pc_resp_end(slot_id, 500, 0, PROTO_FALSE, /*pre_flushed=*/PROTO_FALSE);
        return;
    }

    // The slot stays CONN_ACTIVE through the write for both paths; pc_resp_end then
    // begins the CONN_CLOSING dwell on the close path (finalized once ACKed).

    proto_bool head = Http.req_is_head(slot_id);

    // HEAD responses carry the headers (incl. Content-Length) but no body. For a
    // body that fits the header scratch, coalesce headers+body into a single send
    // so the response costs one tcpip_thread round-trip rather than two. The final
    // write carries the flush (Tcp.conn->send_flush) and pc_resp_end skips it, so a
    // small keep-alive response is one marshal (write+output).
    if (!head && payload_len > 0 && (size_t)hlen + (size_t)payload_len <= sizeof(header))
    {
        proto_raw_read(header + hlen, payload, (size_t)payload_len);
        Tcp.conn->send_flush(slot_id, header, (proto_u16)(hlen + payload_len));
    }
    else if (!head && payload_len > 0)
    {
        Tcp.conn->send(slot_id, header, (proto_u16)hlen);
        Tcp.conn->send_flush(slot_id, payload, (proto_u16)payload_len);
    }
    else
    {
        Tcp.conn->send_flush(slot_id, header, (proto_u16)hlen);
    }

    pc_resp_end(slot_id, code, payload_len, keep, /*pre_flushed=*/PROTO_TRUE);
}

/**
 * @brief Send a status-line-and-headers response with `Content-Length: 0`.
 *
 * Used for CORS preflight (204) and any response where only status headers are needed. Takes the
 * same slot lifecycle as send_bin(): a self-framing protocol's sink wins if one is installed, an
 * inactive slot is reset without writing, and pc_resp_end() owns the close-or-recycle decision.
 *
 * @param slot_id Connection slot index.
 * @param code    HTTP status code, e.g. 204.
 */
void send_empty(uint8_t slot_id, int code)
{
    if (slot_id >= CONN_POOL_SLOTS)
    {
        return;
    }
    TcpConn *conn = &conn_pool[slot_id];
#if PC_ENABLE_HTTP2 || PC_ENABLE_HTTP3
    if (conn->pc_resp_sink)
    {
        conn->pc_resp_sink(slot_id, code, "text/plain", "", 0);
        return;
    }
#endif
    if (conn->state != CONN_ACTIVE || conn->pcb == NULL)
    {
        http_reset(slot_id);
        return;
    }

    proto_bool keep;
    const char *cl = pc_resp_conn_hdr(slot_id, &keep);

    char header[RESP_HDR_BUF_SIZE];
    pc_sb sb_header3 = {header, sizeof(header), 0, PROTO_TRUE};
    pc_sb_put(&sb_header3, "HTTP/1.1 ");
    pc_sb_i64(&sb_header3, (int64_t)(code));
    pc_sb_put(&sb_header3, " ");
    pc_sb_put(&sb_header3, Http.status_text(code));
    pc_sb_put(&sb_header3, "\r\nContent-Length: 0\r\n");
    int hlen = (int)pc_sb_finish(&sb_header3);
    hlen = proto_append_resp_trailer(header, sizeof(header), hlen, slot_id, cl);

    Tcp.conn->send_flush(slot_id, header, (proto_u16)hlen);

    pc_resp_end(slot_id, code, 0, keep, /*pre_flushed=*/PROTO_TRUE);
}

void redirect(uint8_t slot_id, int code, const char *location)
{
    if (slot_id >= MAX_CONNS)
    {
        return;
    }
    TcpConn *conn = &conn_pool[slot_id];
    if (conn->state != CONN_ACTIVE || conn->pcb == NULL)
    {
        http_reset(slot_id);
        return;
    }

    // Only the redirect status codes are valid here; anything else → 302.
    switch (code)
    {
    case 301:
    case 302:
    case 303:
    case 307:
    case 308:
        break;
    default:
        code = 302;
        break;
    }

    proto_bool keep;
    const char *cl = pc_resp_conn_hdr(slot_id, &keep);

    char header[RESP_HDR_BUF_SIZE];
    pc_sb sb_header4 = {header, sizeof(header), 0, PROTO_TRUE};
    pc_sb_put(&sb_header4, "HTTP/1.1 ");
    pc_sb_i64(&sb_header4, (int64_t)(code));
    pc_sb_put(&sb_header4, " ");
    pc_sb_put(&sb_header4, Http.status_text(code));
    pc_sb_put(&sb_header4, "\r\nLocation: ");
    pc_sb_put(&sb_header4, location);
    pc_sb_put(&sb_header4, "\r\nContent-Length: 0\r\n");
    int hlen = (int)pc_sb_finish(&sb_header4);
    hlen = proto_append_resp_trailer(header, sizeof(header), hlen, slot_id, cl);

    Tcp.conn->send_flush(slot_id, header, (proto_u16)hlen);

    pc_resp_end(slot_id, code, 0, keep, /*pre_flushed=*/PROTO_TRUE);
}
