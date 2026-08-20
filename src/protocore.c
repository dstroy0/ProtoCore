// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore.c
 * @brief Layer 7 (Application) - HTTP routing and request handler implementation.
 *
 * **Dispatch pipeline (called from handle())**
 * ```
 * handle()
 *   └─ Session.tick()                 ← drain the session event queue
 *   └─ for each slot:
 *        PARSE_COMPLETE          → Http.match_and_execute()
 *        PARSE_ERROR             → send_text(400)
 *        PARSE_ENTITY_TOO_LARGE  → send_text(413)
 *        PARSE_URI_TOO_LONG      → send_text(414)
 * ```
 *
 * **HttpRoute table**
 * Routes are stored in a fixed-size array of `HttpRoute` structs.  Both exact
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
#include "crypto/rng/rng.h"             // Rng: the CSRF secret's seed
#include "mmgr/membuild/membuild.h"     // Sb: the frame builder
#include "mmgr/plaintext/plaintext.h"   // the diag document is borrowed, not a stack array
#include "mmgr/protoframe/protoframe.h" // the diag document is a frame spec, not a concatenation
#include "mmgr/protostr/protostr.h"     // str: the bounded-run walks
#include "mmgr/rawmemcpy/rawmemcpy.h"   // raw.read: every move here is into our own buffer
#include "network_drivers/presentation/http/http.h"
#include "network_drivers/presentation/http/route/http_route.h"
#include "network_drivers/presentation/presentation.h" // http_protocore_set_poll (install the instance-bound HTTP poll)
#include "network_drivers/tls/tls.h"
#include "network_drivers/transport/tcp/common.h"            // TcpConn, conn_pool: the slots this drives
#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool.init: the pool this brings up
#include "network_drivers/transport/tcp/tcp.h"
#include "server/clock/clock.h" // protocore_millis(): the QUIC poll stamp and the request timeout
#include "server/core/proto_handler.h"
#include "server/core/worker/worker.h"
#include "shared/hex/hex.h"
#include "shared/mime/mime.h"
static uint8_t http_delivery_work[16]; // the borrow an entry takes; HttpDelivery never reads it

static uint8_t mnt_work[16]; // the borrow an entry takes; Mnt never reads it

#if PROTOCORE_ENABLE_HTTP2
#include "network_drivers/presentation/http/http2/h2_server/h2_server.h"
#endif
#if PROTOCORE_ENABLE_HTTP3
#include "network_drivers/presentation/http/http3/h3_server/h3_server.h"     // the request seam begin() installs
#include "network_drivers/presentation/http/http3/quic_server/quic_server.h" // protocore_quic_server_begin / _poll
#endif
#if PROTOCORE_ENABLE_HTTP_DELIVERY
#include "services/file_transfer/http_delivery/http_delivery.h" // protocore_delivery_cache_control (SWR directive)
#endif
#if PROTOCORE_ENABLE_CSRF
#include "server/security/csrf/csrf.h"
#endif
#if PROTOCORE_ENABLE_WEBDAV
#include "network_drivers/application/webdav/webdav.h"
#include "server/io/webdav_handler.h" // try_serve_dav()
#endif
#if PROTOCORE_ENABLE_METRICS || PROTOCORE_ENABLE_STATS
#include "network_drivers/application/web_assets/web_assets.h" // PROTOCORE_METRICS_PROM / PROTOCORE_STATS_JSON (generated)
#endif
#if PROTOCORE_HTTP_EMIT_DATE
#if PROTOCORE_ENABLE_TIME_SOURCE
#include "services/timing_position/time_source/time_source.h" // protocore_time_http_date() - any NTP/GPS/RTC/... source
#else
#include "network_drivers/application/ntp_service/ntp_service.h" // protocore_ntp_http_date() - direct NTP (or the host test seam)
#endif
#endif
// No <string.h> and no <stdio.h>: every scan, compare, copy and search on this layer goes through
// mmgr/protostr.h, and nothing here formats. strnlen and the strcasecmp pair are POSIX rather than
// ISO C, so they are absent under -std=c11 on a conforming libc.

// Outbound-transfer state is not held here. Each kind of transfer belongs to the TU that runs it:
// the chunked-send state to server/response.cpp, the file-send state to
// network_drivers/application/file_serving/file_serving.cpp. The poll below asks each owner whether it holds a slot
// instead of reading its state.

// The server's own state, owned here (internal linkage): who answers a request that matched nothing,
// where the access log goes, which ports were registered, and the HTTP/3 credentials held until
// begin() binds them. Nothing outside this file reads any of it.
//
// The three listener arrays are registration intent, not a second copy of listener_pool[]. A port is
// named by listen() before pool_init() has run, so it cannot be bound where it will live yet; begin()
// is what turns each entry into a listener_pool[] binding, and from then on transport owns it.
typedef struct
{
    RequestLogCb log_cb; ///< Per-request access-log hook; may be null.

    uint16_t listen_ports[MAX_LISTENERS];   ///< Ports registered via listen() / begin_http().
    ProtoConn listen_protos[MAX_LISTENERS]; ///< Protocol for each registered listener.
    proto_bool listen_tls[MAX_LISTENERS];   ///< True for TLS listeners (listen_tls()).
    uint8_t listener_count;                 ///< Registered listeners.

#if PROTOCORE_ENABLE_HTTP3
    const uint8_t *h3_cert;
    size_t h3_cert_len;
    uint8_t h3_seed[32];
    uint16_t h3_port;
    proto_bool h3_enabled;
#endif
} ServerCtx;

// Static storage duration zero-initializes every field: no handlers bound, no listeners registered.
static ServerCtx s_inst;

void protocore_server_reset(void)
{
    // The server's state is spread across the files that own it, which is the point - but "start
    // over" is one concern, so it is one call rather than a checklist each caller has to keep in
    // agreement. The blank template lives in rodata, so the reset is a plain copy and never
    // materializes a sizeof(ServerCtx) temporary on the caller's stack.
    static const ServerCtx blank = {0};
    s_inst = blank;
    HttpRoutes.reset(protocore_http_route_span());
    Http.reset(
        protocore_http_span()); // the not-found handler, which answers instead of the built-in 404 while it is set
#if PROTOCORE_ENABLE_AUTH
    // A credential id names a row by index and a route holds that id, so the two tables empty
    // together: routes left behind rows the table has no way to reach, and the table is bounded.
    Auth.reset(protocore_http_auth_span());
#endif
    Mnt.reset(mnt_work); // the same, for the mount id a static or DAV route holds
#if PROTOCORE_ENABLE_WEBSOCKET
    // And again for the ws / sse handler sets: a route holds the id route_add returned, so the
    // tables empty with the routes. Left behind, each re-registration appends a set no route can
    // reach, and both tables are bounded - they fill, route_add starts refusing, and an upgrade
    // then records no handlers at all.
    Ws.route_reset(protocore_ws_span());
#endif
#if PROTOCORE_ENABLE_SSE
    Sse.route_reset(protocore_sse_span());
#endif
    // The tasks the pipeline runs on. While they are up handle() hands the work to them and does
    // nothing itself, so a reset that leaves them running leaves the server serving.
    SessionV.workers->stop(protocore_worker_span());
    protocore_resp_reset();
    protocore_middleware_reset();
    Signal.reset(protocore_signaling_span());
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
    SignalV.put.code = code;
    Signal.put_response(protocore_signaling_span());
    if (s_inst.log_cb)
    {
        const HttpReq *r = &http_pool[slot_id];
        s_inst.log_cb(r->method, r->path, code, body_len);
    }
}

int32_t listen(uint16_t port, ProtoConn proto)
{
    if (s_inst.listener_count >= MAX_LISTENERS)
    {
        return (int32_t)PROTOCORE_ERR_LISTENER_FULL;
    }
    s_inst.listen_ports[s_inst.listener_count] = port;
    s_inst.listen_protos[s_inst.listener_count] = proto;
    s_inst.listen_tls[s_inst.listener_count] = PROTO_FALSE;
    s_inst.listener_count++;
    // Return the listener id (its index), not PROTOCORE_OK: begin() binds listener_pool[i] from
    // s_inst.listen_ports[i] and the accept path stamps that same index onto the slot, so this id is what
    // protocore_relay_publish() / protocore_ssh_forward_begin() must match against. (Errors are negative.)
    return (int32_t)(s_inst.listener_count - 1);
}

#if PROTOCORE_HAS_SCHEDULER
// The worker task's per-tick entry (registered with protocore_workers_start below); ESP32-only, so it is
// compiled only where it is used - on host the pipeline runs inline via handle().
static void protocore_pump_trampoline(int worker_id)
{
    service_once(worker_id);
}
#endif

#if PROTOCORE_ENABLE_HTTP3

#endif // PROTOCORE_ENABLE_HTTP3

// Installed by begin() as the HTTP ProtoHandler's on_poll, so the worker loop pumps HTTP through
// the same uniform seam as every other protocol. The ProtoHandler seam takes a plain slot, which is
// all http_poll_slot() needs: the route table and the slot pools are single global owners, so there
// is no per-server context to thread through.
static void protocore_http_on_poll(uint8_t slot)
{
    HttpV.slot = slot;
    Http.poll_slot(protocore_http_span());
}

int32_t proto_begin(const WebServerConfig *cfg)
{
    if (s_inst.listener_count == 0
#if PROTOCORE_ENABLE_HTTP3
        && !s_inst.h3_enabled // an HTTP/3-only server binds UDP, not a TCP listener
#endif
    )
    {
        return (int32_t)PROTOCORE_ERR_NO_LISTENERS;
    }
    // The connection's idle deadline is its lifetime, which this layer owns; the pool is told the
    // number rather than handed the config to read it out of.
    SessionV.conn_timeout_ms = (cfg != NULL) ? cfg->conn_timeout_ms : CONN_TIMEOUT_MS;
    ConnPoolV.life.conn_timeout_ms = SessionV.conn_timeout_ms;
    ConnPool.init(protocore_conn_pool_span());
#if PROTOCORE_ENABLE_AUTH
    // Fresh server keying secret per begin(). The secret it writes lives in the auth module's own
    // borrow, and the hash that derives it runs out of the region beside it, so one span serves both.
    Auth.rekey(protocore_http_auth_span());
#endif
#if PROTOCORE_ENABLE_CSRF
    {
        // Seed the CSRF HMAC secret from the generator, which binds and seeds itself on first use
        // and redraws from the platform on its own schedule.
        uint8_t sec[32];
        RngV.fill_args.out = sec;
        RngV.fill_args.len = sizeof(sec);
        Rng.fill(protocore_rng_span());
        CsrfV.secret_args.secret = sec;
        CsrfV.secret_args.len = sizeof(sec);
        Csrf.set_secret(protocore_csrf_span());
    }
#endif
    for (uint8_t i = 0; i < MAX_CONNS; i++)
    {
        HttpConnV.slot = i;
        HttpConn.reset(protocore_http_conn_span());
    }
#if PROTOCORE_ENABLE_WEBSOCKET
    Ws.init(protocore_ws_span());
#endif
#if PROTOCORE_ENABLE_SSE
    Sse.init(protocore_sse_span());
#endif
    for (uint8_t i = 0; i < s_inst.listener_count; i++)
    {
        TcpListenerV.idx = i;
        TcpListenerV.bind.port = s_inst.listen_ports[i];
        TcpListenerV.bind.proto = s_inst.listen_protos[i];
        TcpListenerV.bind.tls = s_inst.listen_tls[i];
        TcpListener.add(protocore_tcp_listener_span());
        if (TcpListenerV.i32 < 0)
        {
            return (int32_t)PROTOCORE_ERR_LISTEN_FAILED;
        }
    }
#if PROTOCORE_ENABLE_HTTP3
    // Bind the HTTP/3 QUIC server's UDP port. Requests dispatch through the route table via the
    // trampoline; protocore_quic_server_poll() runs in service_once.
    if (s_inst.h3_enabled)
    {
        QuicServerConfig h3cfg = {0};
        h3cfg.cert_der = s_inst.h3_cert;
        h3cfg.cert_len = s_inst.h3_cert_len;
        raw.read(h3cfg.ed25519_seed, s_inst.h3_seed, sizeof(h3cfg.ed25519_seed));
        h3cfg.rng = protocore_h3_server_rng;
        // No app pointer: the trampoline dispatches through the global route table. The QUIC server
        // records whether it came up and protocore_quic_server_poll() reads its own answer.
        QuicServer.begin_args.port = s_inst.h3_port;
        QuicServer.begin_args.cfg = &h3cfg;
        QuicServer.begin_args.on_request = protocore_h3_server_request;
        QuicServer.begin_args.app = NULL;
        QuicServer.begin(protocore_quic_server_span());
    }
#endif
#if PROTOCORE_HAS_SCHEDULER
    // Routes/listeners are now fixed; start the worker task(s) that drive the
    // pipeline off the user's loop(). On host the pipeline runs inline via handle().
    WorkersV.pump = protocore_pump_trampoline;
    SessionV.workers->start(protocore_worker_span());
#endif
    return (int32_t)PROTOCORE_OK;
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

#if PROTOCORE_ENABLE_HTTP3
proto_bool protocore_h3_cert(const uint8_t *cert_der, size_t cert_len, const uint8_t ed25519_seed[32], uint16_t port)
{
    if (!cert_der || cert_len == 0 || !ed25519_seed)
    {
        return PROTO_FALSE;
    }
    s_inst.h3_cert = cert_der;
    s_inst.h3_cert_len = cert_len;
    raw.read(s_inst.h3_seed, ed25519_seed, sizeof(s_inst.h3_seed));
    s_inst.h3_port = port;
    s_inst.h3_enabled = PROTO_TRUE;
    return PROTO_TRUE;
}

#endif // PROTOCORE_ENABLE_HTTP3

#if PROTOCORE_ENABLE_TLS
proto_bool tls_cert(const uint8_t *cert, size_t cert_len, const uint8_t *key, size_t key_len)
{
    return protocore_tls_global_init(cert, cert_len, key, key_len);
}

int32_t listen_tls(uint16_t port)
{
    if (s_inst.listener_count >= MAX_LISTENERS)
    {
        return (int32_t)PROTOCORE_ERR_LISTENER_FULL;
    }
    s_inst.listen_ports[s_inst.listener_count] = port;
    s_inst.listen_protos[s_inst.listener_count] = PROTO_HTTP;
    s_inst.listen_tls[s_inst.listener_count] = PROTO_TRUE;
    s_inst.listener_count++;
    return (int32_t)PROTOCORE_OK;
}

int32_t begin_tls(uint16_t port, const uint8_t *cert, size_t cert_len, const uint8_t *key, size_t key_len,
                  const WebServerConfig *cfg)
{
    if (!tls_cert(cert, cert_len, key, key_len))
    {
        return (int32_t)PROTOCORE_ERR_LISTEN_FAILED;
    }
    int32_t rc = listen_tls(port);
    if (rc < 0)
    {
        return rc;
    }
    return proto_begin(cfg);
}

#if PROTOCORE_ENABLE_MTLS
proto_bool tls_require_client_cert(const uint8_t *ca, size_t ca_len)
{
    return protocore_tls_set_client_ca(ca, ca_len);
}

int tls_client_subject(uint8_t slot_id, char *out, size_t out_len)
{
    return protocore_tls_peer_subject(slot_id, out, out_len);
}
#endif // PROTOCORE_ENABLE_MTLS
#endif // PROTOCORE_ENABLE_TLS

int32_t restart(const WebServerConfig *cfg)
{
    if (s_inst.listener_count == 0)
    {
        return (int32_t)PROTOCORE_ERR_NO_LISTENERS;
    }
    stop();
    return proto_begin(cfg);
}

void stop(void)
{
#if PROTOCORE_HAS_SCHEDULER
    // Stop the worker task(s) before tearing down the slots they service.
    SessionV.workers->stop(protocore_worker_span());
#endif
    TcpListener.stop_all(protocore_tcp_listener_span());
    ConnPool.stop(protocore_conn_pool_span());
    for (uint8_t i = 0; i < MAX_CONNS; i++)
    {
        HttpConnV.slot = i;
        HttpConn.reset(protocore_http_conn_span());
    }
#if PROTOCORE_ENABLE_WEBSOCKET
    Ws.init(protocore_ws_span());
#endif
#if PROTOCORE_ENABLE_SSE
    Sse.init(protocore_sse_span());
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
 * @param r    HttpRoute to initialize.
 * @param path URL path to match, e.g. a trailing-star prefix or a `:name` segment.
 */
void fill_route_base(HttpRoute *r, const char *path)
{
    // The copy terminates the destination itself and hands back what it wrote, so the length the
    // two shape tests need comes out of the move rather than from a second walk over those bytes.
    size_t len = str.copy(r->path, path, MAX_PATH_LEN);
    r->is_active = PROTO_TRUE;
    r->is_wildcard = (len > 0 && r->path[len - 1] == '*');
    // Whether, not where: the sieve sweeps the whole field for a fixed cost rather than stopping at
    // the first `/`, which a route path is full of.
    r->is_param = str.has(r->path, MAX_PATH_LEN, "/:", sizeof("/:"), PROTO_FALSE);
    r->is_regex = PROTO_FALSE;
    r->iface_filter = PROTOCORE_IF_ANY;
#if PROTOCORE_ENABLE_AUTH
    // Stated, not inherited from the zeroed slot: zero is a valid credential id, so a route that
    // registers no credentials has to say so, or the first set anyone registers would guard every
    // route in the table.
    r->auth_id = PROTOCORE_AUTH_NONE;
#endif
#if PROTOCORE_ENABLE_WEBSOCKET
    r->ws_id = PROTOCORE_WS_NONE; // same reason: zero names a real handler set
#endif
#if PROTOCORE_ENABLE_SSE
    r->sse_id = PROTOCORE_SSE_NONE; // same reason
#endif
#if PROTOCORE_ENABLE_FILE_SERVING
    r->mnt_id = PROTOCORE_MNT_NONE; // same reason
#endif
}

void on_http(const char *path, HttpMethod method, Handler callback)
{
    HttpRoutes.add(protocore_http_route_span());
    HttpRoute *r = HttpRoutesV.ptr;
    if (r == NULL)
    {
        return;
    }

    fill_route_base(r, path);
    r->type = ROUTE_HTTP;
    r->method = method;
    r->callback = callback;
}

void on_http_iface(const char *path, HttpMethod method, Handler callback, protocore_if_kind iface)
{
    HttpRoutes.add(protocore_http_route_span());
    HttpRoute *r = HttpRoutesV.ptr;
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
    protocore_ap_ip = ap_ip;
}

void on_regex(const char *pattern, HttpMethod method, Handler callback)
{
    HttpRoutes.add(protocore_http_route_span());
    HttpRoute *r = HttpRoutesV.ptr;
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

#if PROTOCORE_ENABLE_AUTH
void on_http_auth(const char *path, HttpMethod method, Handler callback, const char *realm, const char *user,
                  const char *pass, proto_bool digest)
{
    HttpRoutes.add(protocore_http_route_span());
    HttpRoute *r = HttpRoutesV.ptr;
    if (r == NULL)
    {
        return;
    }

    fill_route_base(r, path);
    r->type = ROUTE_HTTP;
    r->method = method;
    r->callback = callback;
    // The credential goes to the module that checks it; the route keeps only the id naming it.
    AuthV.cred.realm = realm;
    AuthV.cred.user = user;
    AuthV.cred.pass = pass;
    AuthV.cred.digest = digest;
    Auth.add(protocore_http_auth_span());
    r->auth_id = AuthV.u8;
}
#endif // PROTOCORE_ENABLE_AUTH

#if PROTOCORE_ENABLE_WEBSOCKET
void on_ws(const char *path, WsConnectHandler on_connect, WsMessageHandler on_message, WsCloseHandler on_close)
{
    HttpRoutes.add(protocore_http_route_span());
    HttpRoute *r = HttpRoutesV.ptr;
    if (r == NULL)
    {
        return;
    }

    fill_route_base(r, path);
    r->type = ROUTE_WS;
    WsV.route.on_connect = on_connect;
    WsV.route.on_message = on_message;
    WsV.route.on_close = on_close;
    Ws.route_add(protocore_ws_span());
    r->ws_id = WsV.u8;
}
#endif // PROTOCORE_ENABLE_WEBSOCKET

#if PROTOCORE_ENABLE_SSE
void on_sse(const char *path, SseConnectHandler on_connect)
{
    HttpRoutes.add(protocore_http_route_span());
    HttpRoute *r = HttpRoutesV.ptr;
    if (r == NULL)
    {
        return;
    }

    fill_route_base(r, path);
    r->type = ROUTE_SSE;
    SseV.route.on_connect = on_connect;
    Sse.route_add(protocore_sse_span());
    r->sse_id = SseV.u8;
}
#endif // PROTOCORE_ENABLE_SSE

void on_not_found(Handler callback)
{
    HttpV.cb = callback;
    Http.set_not_found(protocore_http_span());
}

// set_cors() / set_cache_control() live in server/response.cpp, with the buffers they fill.

#if PROTOCORE_ENABLE_HTTP_DELIVERY
proto_bool set_cache_control_swr(uint32_t max_age_s, uint32_t swr_s)
{
    // Build the directive with the RFC 5861 core so the header and the protocore_delivery_swr decision
    // can never drift apart.
    char directive[64];
    HttpDeliveryV.cache_control_args.max_age_s = max_age_s;
    HttpDeliveryV.cache_control_args.swr_s = swr_s;
    HttpDeliveryV.cache_control_args.out = directive;
    HttpDeliveryV.cache_control_args.cap = sizeof(directive);
    HttpDelivery.cache_control(http_delivery_work);
    if (HttpDeliveryV.n == 0)
    {
        return PROTO_FALSE;
    }
    set_cache_control(directive);
    return PROTO_TRUE;
}
#endif

#if PROTOCORE_ENABLE_WEBSOCKET
void ws_dispatch_message(const WsConn *ws)
{
    HttpRoutes.count(protocore_http_route_span());
    for (uint8_t r = 0; r < HttpRoutesV.value; r++)
    {
        HttpRoutesV.at_args.i = r;
        HttpRoutes.at(protocore_http_route_span());
        const HttpRoute *rt = HttpRoutesV.ptr;
        if (rt->type != ROUTE_WS)
        {
            continue;
        }
        WsV.id = rt->ws_id;
        Ws.route_message(protocore_ws_span());
        if (WsV.message_handler != NULL)
        {
            WsV.message_handler(ws->ws_id);
            break;
        }
    }
}

void ws_dispatch_close(const WsConn *ws)
{
    HttpRoutes.count(protocore_http_route_span());
    for (uint8_t r = 0; r < HttpRoutesV.value; r++)
    {
        HttpRoutesV.at_args.i = r;
        HttpRoutes.at(protocore_http_route_span());
        const HttpRoute *rt = HttpRoutesV.ptr;
        if (rt->type != ROUTE_WS)
        {
            continue;
        }
        WsV.id = rt->ws_id;
        Ws.route_close(protocore_ws_span());
        if (WsV.close_handler != NULL)
        {
            WsV.close_handler(ws->ws_id);
            break;
        }
    }
}
#endif // PROTOCORE_ENABLE_WEBSOCKET

/**
 * @brief Main application tick - tick the session layer then dispatch completed requests.
 *
 * Call this repeatedly from loop(). Each call runs one service_once() pass: a Session.tick()
 * (timeout sweeps + event-queue drain), then a poll of every slot this worker owns, which is
 * where a completed request is dispatched and a parse failure is answered.
 *
 * On ESP32 the worker task drives that pass on its own core, so this returns immediately and
 * loop() is free.
 */
void handle(void)
{
#if PROTOCORE_HAS_SCHEDULER
    SessionV.workers->running(protocore_worker_span());
    if (WorkersV.ok)
    {
        return;
    }
#endif
    service_once(0); // the inline path is worker 0: the pools are all its own
}

void service_once(int worker_id)
{
    // The iteration's stamp. One read of the source per pass, before anything reads the time, so
    // every step of the pass measures against the same instant. A caller that needs the live value
    // - a latency measurement, an elapsed time - calls Clock.millis() again and reads the delta.
    Clock.millis(Clock.internal);

    // Install HTTP's poll so the dispatch loop below pumps it through the uniform
    // ProtoHandler.on_poll seam (see http_poll_slot). Done here rather than only in begin() so a
    // caller that drives service_once() directly still gets it. One pointer store; negligible at
    // poll cadence.
    HttpConnV.poll = protocore_http_on_poll;
    HttpConn.set_poll(protocore_http_conn_span());

    SessionV.worker_id = worker_id;
    Session.tick(protocore_session_span());

#if PROTOCORE_ENABLE_HTTP3
    // Drive the QUIC/HTTP-3 server: ingest queued datagrams, run the engines (which dispatch requests
    // through the route table), flush replies. One worker owns it, so requests stay single-threaded.
    if (worker_id == 0)
    {
        QuicServer.now_ms = Clock.ms;
        QuicServer.poll(protocore_quic_server_span());
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
        ConnPoolV.slot = i;
        ConnPool.ack_consumed(protocore_conn_pool_span());

        // Every protocol - HTTP included - is pumped through the one uniform ProtoHandler.on_poll
        // seam, so there is no per-protocol branch here. HTTP reaches it via http_protocore_set_poll()
        // -> http_poll_slot(); the singleton pollers (SSH etc.) gate on CONN_ACTIVE
        // inside their own on_poll.
        SessionV.proto->proto = conn_pool[i].proto;
        SessionV.proto->get(protocore_session_span());
        const ProtoHandler *ph = SessionV.proto->handler;
        if (ph && ph->on_poll)
        {
            ph->on_poll(i);
        }
    }

    // Run any callbacks app code deferred to this worker (race-free push path).
    WorkersV.worker_id = worker_id;
    SessionV.workers->run_deferred(protocore_worker_span());
}

proto_bool defer(uint8_t slot, protocore_deferred_fn fn, void *arg)
{
    if (slot >= MAX_CONNS)
    {
        return PROTO_FALSE;
    }
    // HttpRoute to the worker that owns the slot so the callback runs single-threaded
    // alongside that slot's own processing.
    WorkersV.worker_id = conn_pool[slot].owner;
    WorkersV.defer_args.fn = fn;
    WorkersV.defer_args.arg = arg;
    SessionV.workers->defer(protocore_worker_span());
    return WorkersV.ok;
}

// ---------------------------------------------------------------------------
// Diagnostic endpoint
// ---------------------------------------------------------------------------

#if PROTOCORE_ENABLE_DIAG

// The build-info document. Every value is a compile-time constant, so nothing is discovered at
// runtime: each flag selects one of two literals and each sizing constant renders as a decimal.
// A frame spec like every other here, so the conversions come from the shared engine.

// A flag indexes this; !!flag is 0 or 1, so the selection is a load rather than a branch.
static const char *const PROTOCORE_DIAG_BOOL[2] = {"false", "true"};

static const protocore_field DIAG_DOC[] = {
    {PROTOCORE_FK_LIT, 0, 31, "{\"lib\":\"ProtoCore\",\"features\":{"},
    {PROTOCORE_FK_LIT, 0, 12, "\"websocket\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 7, ",\"sse\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 13, ",\"multipart\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 16, ",\"file_serving\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 8, ",\"auth\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 10, ",\"webdav\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 8, ",\"coap\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 8, ",\"snmp\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 9, ",\"opcua\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 9, ",\"umati\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 10, ",\"modbus\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 8, ",\"mqtt\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 13, ",\"mtconnect\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 9, ",\"redis\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 7, ",\"ftp\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 8, ",\"smtp\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 7, ",\"smb\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 10, ",\"syslog\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 24, ",\"protocore_ntp_server\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 14, ",\"dns_server\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 8, ",\"nats\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 9, ",\"stomp\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 10, ",\"statsd\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 7, ",\"jwt\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 7, ",\"tls\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 9, ",\"http2\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 9, ",\"http3\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 7, ",\"ssh\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 14, ",\"ws_deflate\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 9, ",\"range\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 8, ",\"csrf\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 19, ",\"accept_throttle\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 19, ",\"per_ip_throttle\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 16, ",\"auth_lockout\":"},
    PROTOCORE_STR,
    {PROTOCORE_FK_LIT, 0, 12, "},\"config\":{"},
    {PROTOCORE_FK_LIT, 0, 12, "\"MAX_CONNS\":"},
    PROTOCORE_U32,
    {PROTOCORE_FK_LIT, 0, 15, ",\"RX_BUF_SIZE\":"},
    PROTOCORE_U32,
    {PROTOCORE_FK_LIT, 0, 17, ",\"BODY_BUF_SIZE\":"},
    PROTOCORE_U32,
    {PROTOCORE_FK_LIT, 0, 14, ",\"MAX_ROUTES\":"},
    PROTOCORE_U32,
    {PROTOCORE_FK_LIT, 0, 15, ",\"MAX_HEADERS\":"},
    PROTOCORE_U32,
    {PROTOCORE_FK_LIT, 0, 16, ",\"MAX_PATH_LEN\":"},
    PROTOCORE_U32,
    {PROTOCORE_FK_LIT, 0, 15, ",\"MAX_KEY_LEN\":"},
    PROTOCORE_U32,
    {PROTOCORE_FK_LIT, 0, 15, ",\"MAX_VAL_LEN\":"},
    PROTOCORE_U32,
    {PROTOCORE_FK_LIT, 0, 17, ",\"MAX_QUERY_LEN\":"},
    PROTOCORE_U32,
    {PROTOCORE_FK_LIT, 0, 20, ",\"MAX_QUERY_PARAMS\":"},
    PROTOCORE_U32,
    {PROTOCORE_FK_LIT, 0, 19, ",\"CONN_TIMEOUT_MS\":"},
    PROTOCORE_U32,
    {PROTOCORE_FK_LIT, 0, 21, ",\"RESP_HDR_BUF_SIZE\":"},
    PROTOCORE_U32,
    {PROTOCORE_FK_LIT, 0, 19, ",\"WS_HDR_BUF_SIZE\":"},
    PROTOCORE_U32,
    {PROTOCORE_FK_LIT, 0, 21, ",\"CORS_HDR_BUF_SIZE\":"},
    PROTOCORE_U32,
    {PROTOCORE_FK_LIT, 0, 19, ",\"EVT_QUEUE_DEPTH\":"},
    PROTOCORE_U32,
    {PROTOCORE_FK_LIT, 0, 2, "}}"},
    PROTOCORE_END,
};

// Worst case: every flag rendering as the longer "false", every size at a uint32_t's ten digits.
#define PROTOCORE_PLAINTEXT_WORK_DIAG 975
static_assert(PROTOCORE_PLAINTEXT_WORK_DIAG <= PROTOCORE_PLAINTEXT_ARENA_SIZE, "diag document exceeds the arena");

void diag(uint8_t slot_id)
{
    // Mark before the borrow and release on every exit: the document is transient, and the
    // per-dispatch reset is only the backstop.
    const size_t mark = protocore_plaintext_mark();
    char *doc = (char *)protocore_plaintext_alloc(PROTOCORE_PLAINTEXT_WORK_DIAG, 1);
    if (doc == NULL ||
        frame.build(doc, PROTOCORE_PLAINTEXT_WORK_DIAG, DIAG_DOC,
                    (const protocore_fval[]){PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_WEBSOCKET]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_SSE]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_MULTIPART]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_FILE_SERVING]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_AUTH]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_WEBDAV]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_COAP]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_SNMP]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_OPCUA]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_UMATI]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_MODBUS]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_MQTT]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_MTCONNECT]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_REDIS]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_FTP]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_SMTP]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_SMB]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_SYSLOG]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_NTP_SERVER]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_DNS_SERVER]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_NATS]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_STOMP]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_STATSD]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_JWT]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_TLS]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_HTTP2]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_HTTP3]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_SSH]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_WS_DEFLATE]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_RANGE]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_CSRF]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_ACCEPT_THROTTLE]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_PER_IP_THROTTLE]),
                                             PROTOCORE_VSTR(PROTOCORE_DIAG_BOOL[!!PROTOCORE_ENABLE_AUTH_LOCKOUT]),
                                             PROTOCORE_VU32((uint32_t)MAX_CONNS),
                                             PROTOCORE_VU32((uint32_t)RX_BUF_SIZE),
                                             PROTOCORE_VU32((uint32_t)BODY_BUF_SIZE),
                                             PROTOCORE_VU32((uint32_t)MAX_ROUTES),
                                             PROTOCORE_VU32((uint32_t)MAX_HEADERS),
                                             PROTOCORE_VU32((uint32_t)MAX_PATH_LEN),
                                             PROTOCORE_VU32((uint32_t)MAX_KEY_LEN),
                                             PROTOCORE_VU32((uint32_t)MAX_VAL_LEN),
                                             PROTOCORE_VU32((uint32_t)MAX_QUERY_LEN),
                                             PROTOCORE_VU32((uint32_t)MAX_QUERY_PARAMS),
                                             PROTOCORE_VU32((uint32_t)CONN_TIMEOUT_MS),
                                             PROTOCORE_VU32((uint32_t)RESP_HDR_BUF_SIZE),
                                             PROTOCORE_VU32((uint32_t)WS_HDR_BUF_SIZE),
                                             PROTOCORE_VU32((uint32_t)CORS_HDR_BUF_SIZE),
                                             PROTOCORE_VU32((uint32_t)EVT_QUEUE_DEPTH)},
                    49) == 0)
    {
        protocore_plaintext_release(mark);
        send_text(slot_id, 503, PROTOCORE_MIME_TEXT_PLAIN, ""); // fail closed: no partial document reaches the wire
        return;
    }
    send_text(slot_id, 200, PROTOCORE_MIME_JSON, doc); // reads doc, so it runs before the release
    protocore_plaintext_release(mark);
}
#endif
