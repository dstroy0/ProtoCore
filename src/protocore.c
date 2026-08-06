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
#include "network_drivers/presentation/http/http.h"
#include "mmgr/frame.h"     // the diag document is a frame spec, not a concatenation
#include "mmgr/membuild.h"  // pc_sb frame builder
#include "mmgr/plaintext.h" // the diag document is borrowed, not a stack array
#include "mmgr/rawmemcpy.h" // proto_raw_read: every move here is into our own buffer
#include "network_drivers/presentation/http/route/http_route.h"
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
#include "network_drivers/presentation/http/http3/h3_server.h"   // the request seam begin() installs
#include "network_drivers/presentation/http/http3/quic_server.h" // pc_quic_server_begin / _poll
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

// The server's own state, owned here (internal linkage): who answers a request that matched nothing,
// where the access log goes, which ports were registered, and the HTTP/3 credentials held until
// begin() binds them. Nothing outside this file reads any of it.
//
// The three listener arrays are registration intent, not a second copy of listener_pool[]. A port is
// named by listen() before pool_init() has run, so it cannot be bound where it will live yet; begin()
// is what turns each entry into a listener_pool[] binding, and from then on transport owns it.
typedef struct
{
    RequestLogCb log_cb;       ///< Per-request access-log hook; may be null.

    uint16_t listen_ports[MAX_LISTENERS];   ///< Ports registered via listen() / begin_http().
    ConnProto listen_protos[MAX_LISTENERS]; ///< Protocol for each registered listener.
    proto_bool listen_tls[MAX_LISTENERS];   ///< True for TLS listeners (listen_tls()).
    uint8_t listener_count;                 ///< Registered listeners.

#if PC_ENABLE_HTTP3
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
    HttpRoutes.reset();
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

int32_t listen(uint16_t port, ConnProto proto)
{
    if (s_inst.listener_count >= MAX_LISTENERS)
    {
        return (int32_t)PC_ERR_LISTENER_FULL;
    }
    s_inst.listen_ports[s_inst.listener_count] = port;
    s_inst.listen_protos[s_inst.listener_count] = proto;
    s_inst.listen_tls[s_inst.listener_count] = PROTO_FALSE;
    s_inst.listener_count++;
    // Return the listener id (its index), not PC_OK: begin() binds listener_pool[i] from
    // s_inst.listen_ports[i] and the accept path stamps that same index onto the slot, so this id is what
    // pc_relay_publish() / pc_ssh_forward_begin() must match against. (Errors are negative.)
    return (int32_t)(s_inst.listener_count - 1);
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

#endif // PC_ENABLE_HTTP3

// Installed by begin() as the HTTP ProtoHandler's on_poll, so the worker loop pumps HTTP through
// the same uniform seam as every other protocol. The ProtoHandler seam takes a plain slot, which is
// all http_poll_slot() needs: the route table and the slot pools are single global owners, so there
// is no per-server context to thread through.
static void pc_http_on_poll(uint8_t slot)
{
    Http.poll_slot(slot);
}

int32_t proto_begin(const WebServerConfig *cfg)
{
    if (s_inst.listener_count == 0
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
    for (uint8_t i = 0; i < s_inst.listener_count; i++)
    {
        if (listener_add(i, s_inst.listen_ports[i], s_inst.listen_protos[i], s_inst.listen_tls[i]) < 0)
        {
            return (int32_t)PC_ERR_LISTEN_FAILED;
        }
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
        h3cfg.rng = pc_h3_server_rng;
        // No app pointer: the trampoline dispatches through the global route table. The QUIC server
        // records whether it came up and pc_quic_server_poll() reads its own answer.
        (void)pc_quic_server_begin(s_inst.h3_port, &h3cfg, pc_h3_server_request, NULL);
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

#endif // PC_ENABLE_HTTP3

#if PC_ENABLE_TLS
proto_bool tls_cert(const uint8_t *cert, size_t cert_len, const uint8_t *key, size_t key_len)
{
    return pc_tls_global_init(cert, cert_len, key, key_len);
}

int32_t listen_tls(uint16_t port)
{
    if (s_inst.listener_count >= MAX_LISTENERS)
    {
        return (int32_t)PC_ERR_LISTENER_FULL;
    }
    s_inst.listen_ports[s_inst.listener_count] = port;
    s_inst.listen_protos[s_inst.listener_count] = PROTO_HTTP;
    s_inst.listen_tls[s_inst.listener_count] = PROTO_TRUE;
    s_inst.listener_count++;
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
    if (s_inst.listener_count == 0)
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
 * @param r    HttpRoute to initialize.
 * @param path URL path to match, e.g. a trailing-star prefix or a `:name` segment.
 */
void fill_route_base(HttpRoute *r, const char *path)
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
    HttpRoute *r = HttpRoutes.add();
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
    HttpRoute *r = HttpRoutes.add();
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
    HttpRoute *r = HttpRoutes.add();
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
    HttpRoute *r = HttpRoutes.add();
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
    HttpRoute *r = HttpRoutes.add();
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
    HttpRoute *r = HttpRoutes.add();
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
    for (uint8_t r = 0; r < HttpRoutes.count(); r++)
    {
        const HttpRoute *rt = HttpRoutes.at(r);
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
    for (uint8_t r = 0; r < HttpRoutes.count(); r++)
    {
        const HttpRoute *rt = HttpRoutes.at(r);
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
    if (worker_id == 0)
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

proto_bool defer(uint8_t slot, pc_deferred_fn fn, void *arg)
{
    if (slot >= MAX_CONNS)
    {
        return PROTO_FALSE;
    }
    // HttpRoute to the worker that owns the slot so the callback runs single-threaded
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

