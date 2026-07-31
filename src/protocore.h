// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore.h
 * @brief Layer 7 (Application) - public HTTP routing API.
 *
 * This is the only header most application code needs to include.
 * The full OSI include chain is pulled in automatically:
 * @code
 *   protocore.h                  (L7 Application)
 *     ├── network_drivers/presentation/presentation.h (L6 Presentation)
 *     │       ├── network_drivers/presentation/http/http_parser/http_parser.h (parser types)
 *     │       └── network_drivers/transport/tcp.h      (L4 Transport)
 *     │               └── protocore_config.h   (compile-time config)
 *     └── network_drivers/session/session.h      (L5 Session - event drain)
 * @endcode
 *
 * **Feature flags** - define any of these to 0 before including to strip
 * the feature from the build entirely:
 * @code
 *   #define PC_ENABLE_WEBSOCKET    0
 *   #define PC_ENABLE_SSE          0
 *   #define PC_ENABLE_MULTIPART    0
 *   #define PC_ENABLE_FILE_SERVING 0
 *   #define PC_ENABLE_AUTH         0
 *   #include <protocore.h>
 * @endcode
 *
 * **Determinism guarantees**
 * - All buffers are statically allocated; no heap usage after begin().
 * - Every operation has O(1) or O(MAX_ROUTES) worst-case time.
 * - `handle()` is safe to call every Arduino `loop()` iteration.
 *
 * @author   Douglas Quigg (dstroy0)
 * @date     2026
 * @copyright Copyright (C) 2026 Douglas Quigg (dstroy0). AGPL-3.0-or-later.
 */

#ifndef PROTOCORE_H
#define PROTOCORE_H

#include "network_drivers/presentation/codec/json/json.h"
#include "network_drivers/presentation/presentation.h"
#include "network_drivers/session/session.h"
#include "network_drivers/session/worker.h"
#if PC_ENABLE_WEBSOCKET
#include "network_drivers/presentation/http/websocket/websocket.h"
#endif
#if PC_ENABLE_SSE
#include "network_drivers/presentation/http/sse/sse.h"
#endif
#if PC_ENABLE_MULTIPART
#include "network_drivers/presentation/codec/multipart/multipart.h"
#endif
#include <Arduino.h>
#if PC_ENABLE_FILE_SERVING
#ifdef ARDUINO
#include <FS.h>
#else
#include "FS.h"
#endif
#endif

// ---------------------------------------------------------------------------
// HTTP method enumeration
// ---------------------------------------------------------------------------

/**
 * @brief HTTP request methods supported by the router.
 *
 * Pass one of these values to PC::on() to bind a route to a
 * specific method.  PATCH, HEAD, and OPTIONS were added in v1.0 alongside
 * CORS preflight support.
 */
enum class HttpMethod : uint8_t
{
    HTTP_GET,           ///< Safe, idempotent read
    HTTP_POST,          ///< Non-idempotent create / action
    HTTP_PUT,           ///< Idempotent replace
    HTTP_DELETE,        ///< Idempotent delete
    HTTP_PATCH,         ///< Partial update
    HTTP_HEAD,          ///< Same as GET but no response body
    HTTP_OPTIONS,       ///< Capability query / CORS preflight
    HTTP_METHOD_UNKNOWN ///< Unrecognized method token → 501 Not Implemented
};

// ---------------------------------------------------------------------------
// Handler and route types
// ---------------------------------------------------------------------------

/**
 * @brief Callback signature for HTTP request handlers.
 *
 * The callback receives the connection slot index and a pointer to the
 * fully-parsed request.  Call PC::send() or PC::send_empty()
 * from inside the callback to write a response.
 *
 * @param slot_id  Index into the connection pool (0 … MAX_CONNS-1).
 * @param request  Pointer to the parsed HTTP request.  Valid only during the
 *                 callback; do not cache this pointer.
 *
 * @note If the callback returns without calling send(), the framework will
 *       reset the slot automatically (no response is sent to the client).
 */
typedef void (*Handler)(uint8_t slot_id, HttpReq *request);

/**
 * @brief Resolver for `{{name}}` template placeholders used by send_template().
 *
 * Called with a placeholder name; returns the replacement string, or nullptr
 * to substitute an empty string. The pointer must stay valid for the duration
 * of the send_template() call, and the resolver must be deterministic (it is
 * invoked twice: once to size the body, once to emit it).
 */
typedef const char *(*TemplateVar)(const char *name);

/**
 * @brief Per-request access-log callback (see PC::on_request_log()).
 *
 * Invoked once per response with the request method/path, the HTTP status code,
 * and the response body length in bytes. The strings are valid only for the
 * duration of the call. This is a thin hook - the library does no buffering or
 * formatting; route the data to Serial, syslog, etc. as you see fit.
 */
typedef void (*RequestLogCb)(const char *method, const char *path, int status, int body_len);

/**
 * @brief Outcome of a middleware function (see @ref Middleware).
 *
 * Returning MwResult::MW_NEXT passes the request to the next middleware in the chain and,
 * once the chain is exhausted, on to the matching route handler. Returning
 * MwResult::MW_HALT stops the chain: the route handler is NOT invoked, so a middleware
 * that halts must have already written a response (the dispatcher treats the
 * request as fully handled).
 */
enum class MwResult : uint8_t
{
    MW_NEXT = 0, ///< Continue to the next middleware / the route handler.
    MW_HALT = 1  ///< Stop dispatch; the middleware already sent a response.
};

/**
 * @brief Composable pre-dispatch middleware (see PC::use()).
 *
 * Each registered middleware runs - in registration order - on every request
 * before route matching, receiving the same `(slot_id, request)` pair a handler
 * does. A middleware may inspect the request, queue response headers
 * (PC::add_response_header()), short-circuit by sending a response and
 * returning MwResult::MW_HALT, or fall through with MwResult::MW_NEXT. Middlewares reference the
 * application's server instance the same way handlers do (the global object), so
 * they can call send() / send_empty() to short-circuit.
 *
 * @param slot_id  Connection slot index (0 … MAX_CONNS-1).
 * @param request  Parsed request; valid only during the call (do not cache).
 * @return MwResult::MW_NEXT to continue, MwResult::MW_HALT to stop (response already sent).
 */
typedef MwResult (*Middleware)(uint8_t slot_id, HttpReq *request);

#if PC_ENABLE_WEBSOCKET
/**
 * @brief Callback fired when a WebSocket connection is established.
 *
 * @param ws_id  Index into ws_pool[] for this connection.
 */
typedef void (*WsConnectHandler)(uint8_t ws_id);

/**
 * @brief Callback fired when a WebSocket text or binary frame arrives.
 *
 * The payload is in ws_pool[ws_id].buf, null-terminated.  Length is in
 * ws_pool[ws_id].payload_len.  Opcode is in ws_pool[ws_id].opcode.
 *
 * @param ws_id  Index into ws_pool[].
 */
typedef void (*WsMessageHandler)(uint8_t ws_id);

/**
 * @brief Callback fired when a WebSocket connection closes.
 *
 * @param ws_id  Index into ws_pool[] (slot is still valid during callback).
 */
typedef void (*WsCloseHandler)(uint8_t ws_id);
#endif // PC_ENABLE_WEBSOCKET

#if PC_ENABLE_SSE
/**
 * @brief Callback fired when a new SSE client connects.
 *
 * Use pc_sse_send() inside this callback to push an initial event if needed.
 *
 * @param pc_sse_id  Index into pc_sse_pool[] for this connection.
 */
typedef void (*SseConnectHandler)(uint8_t pc_sse_id);
#endif // PC_ENABLE_SSE

// ---------------------------------------------------------------------------
// Route type discriminator
// ---------------------------------------------------------------------------

/** @brief Discriminates between HTTP, WebSocket, and SSE route entries. */
enum class RouteType : uint8_t
{
    ROUTE_HTTP, ///< Standard HTTP request/response.
#if PC_ENABLE_WEBSOCKET
    ROUTE_WS, ///< WebSocket upgrade route.
#endif
#if PC_ENABLE_SSE
    ROUTE_SSE, ///< Server-Sent Events route.
#endif
#if PC_ENABLE_FILE_SERVING
    ROUTE_STATIC, ///< Static-file subtree mount (serve_static()).
#endif
#if PC_ENABLE_WEBDAV
    ROUTE_DAV, ///< WebDAV subtree mount (dav()).
#endif
};

// ---------------------------------------------------------------------------
// begin() / listen() / restart() result codes
// ---------------------------------------------------------------------------

/**
 * @brief Result codes for listen(), begin(), and restart().
 *
 * Success is a positive value (pc_result::PC_OK). Failures are distinct negative codes
 * so a caller can tell why startup failed.
 */
enum class pc_result : int32_t
{
    PC_OK = 1,                 ///< Success.
    PC_ERR_NO_LISTENERS = -1,  ///< begin() called before any listen() / begin(port).
    PC_ERR_LISTENER_FULL = -2, ///< listen(): listener pool (MAX_LISTENERS) is full.
    PC_ERR_LISTEN_FAILED = -3  ///< A listener failed to open (bind/listen/lwIP error).
};

/**
 * @brief Internal route entry stored in the routing table.
 *
 * Populated by PC::on(), on_ws(), or on_sse().
 * Application code does not interact with this struct directly.
 */
struct Route
{
    char path[MAX_PATH_LEN]; ///< Null-terminated path pattern.
    RouteType type;          ///< HTTP, WS, or SSE.
    HttpMethod method;       ///< HTTP method (RouteType::ROUTE_HTTP only).
    Handler callback;        ///< HTTP handler (RouteType::ROUTE_HTTP only).

#if PC_ENABLE_WEBSOCKET
    WsConnectHandler ws_connect; ///< Fired on upgrade success.
    WsMessageHandler ws_message; ///< Fired on each data frame.
    WsCloseHandler ws_close;     ///< Fired on close.
#endif

#if PC_ENABLE_SSE
    SseConnectHandler pc_sse_connect; ///< Fired when client subscribes.
#endif

#if PC_ENABLE_FILE_SERVING
    fs::FS *static_fs;       ///< Filesystem for RouteType::ROUTE_STATIC (else nullptr).
    const char *static_root; ///< FS root prefix for RouteType::ROUTE_STATIC (must be a persistent string).
#endif

#if PC_ENABLE_AUTH
    bool auth_required;            ///< True when this route requires authentication.
    bool auth_digest;              ///< True for Digest auth; false for Basic.
    char auth_realm[MAX_AUTH_LEN]; ///< WWW-Authenticate realm string.
    char auth_user[MAX_AUTH_LEN];  ///< Required username.
    char auth_pass[MAX_AUTH_LEN];  ///< Required password.
#endif

    bool is_active;        ///< `false` for unused table slots.
    bool is_wildcard;      ///< `true` when path ends with `*`.
    bool is_param;         ///< `true` when the path contains a `:name` segment.
    bool is_regex;         ///< `true` when the path is a regex (see on_regex()).
    pc_iface iface_filter; ///< Interface gate; pc_iface::PC_IFACE_ANY (0) = match any interface.
};

// ---------------------------------------------------------------------------
// Chunked (streaming) response writer
// ---------------------------------------------------------------------------

struct tcp_pcb; // forward decl (full type pulled in via the transport layer)

class PC;

/**
 * @brief Source callback that produces a chunked response body incrementally.
 *
 * Passed to PC::send_chunked() and called repeatedly - possibly across
 * several server loops, as the TCP send window drains - until it returns 0. Each
 * call writes up to @p cap bytes of the next body piece into @p buf and returns
 * the count; the HTTP chunk framing (size line + CRLFs + terminator) is added by
 * the server. Track your position across calls in @p ctx. This pull/generator
 * model lets the server page an arbitrarily large body to the socket in constant
 * memory without ever blocking the worker or truncating at the send window.
 *
 * @warning @p ctx must stay valid until the body is fully sent. A body that fits
 * in a single send window finishes during the send_chunked() call, but a larger
 * one resumes on later loops, so @p ctx must NOT point at the caller's stack: use
 * static / global storage (a per-connection instance if requests can overlap), or
 * generate the body from durable state.
 *
 * @param buf  destination for the next body bytes.
 * @param cap  maximum bytes to write into @p buf on this call.
 * @param ctx  caller state pointer, passed through from send_chunked().
 * @return bytes written into @p buf (<= @p cap), or 0 to end the body.
 */
typedef size_t (*ChunkSource)(uint8_t *buf, size_t cap, void *ctx);

// ---------------------------------------------------------------------------
// PC - the main application class
// ---------------------------------------------------------------------------

/**
 * @class PC
 * @brief Single-port HTTP server with deterministic, zero-allocation execution.
 *
 * ## Typical usage
 * @code
 * PC server;
 *
 * void handle_api(uint8_t slot_id, HttpReq *req) {
 *     server.send(slot_id, 200, "application/json", "{\"ok\":true}");
 * }
 *
 * void setup() {
 *     init_wifi_physical("SSID", "PASSWORD");
 *     server.on("/api/status", HttpMethod::HTTP_GET, handle_api);
 *     server.set_cors("*");
 *     int32_t result = server.begin(80);
 *     if (result < 0) { } // pc_result code: startup failed
 * }
 *
 * void loop() {
 *     server.handle();   // call every iteration - O(MAX_CONNS) per call
 * }
 * @endcode
 *
 * ## Design constraints
 * - Maximum simultaneous connections: `MAX_CONNS` (default 4).
 * - Maximum registered routes: `MAX_ROUTES` (default 16).
 * - Responses are sent synchronously and the TCP connection is closed
 *   immediately after every response (HTTP/1.0 close semantics).
 */
class PC
{
  private:
    Route _routes[MAX_ROUTES]; ///< Flat routing table; searched linearly.
    uint8_t _route_count;      ///< Number of active entries in _routes.

    Handler _not_found_handler; ///< Called when no route matches; may be null.
    bool _cors_enabled;         ///< True after a non-empty set_cors() call.
    RequestLogCb _log_cb;       ///< Per-request access-log hook; may be null.

#if PC_ENABLE_STATS
    uint32_t _stat_requests; ///< Total responses sent.
    uint32_t _stat_2xx;      ///< Responses with a 2xx status.
    uint32_t _stat_4xx;      ///< Responses with a 4xx status.
    uint32_t _stat_5xx;      ///< Responses with a 5xx status.
#endif

    uint16_t _listen_ports[MAX_LISTENERS];   ///< Ports registered via listen() or begin(port).
    ConnProto _listen_protos[MAX_LISTENERS]; ///< Protocol for each registered listener.
    bool _listen_tls[MAX_LISTENERS];         ///< True for TLS listeners (listen_tls()).
    uint8_t _listener_count;                 ///< Number of registered listeners.

    /**
     * @brief Pre-built CORS header block injected into every response.
     *
     * Built once by set_cors() to avoid repeated snprintf at dispatch time.
     */
    char _cors_header_buf[CORS_HDR_BUF_SIZE];

    /**
     * @brief Pre-built `Cache-Control: <value>\r\n` line, or "" when unset.
     *
     * Set by set_cache_control(); injected into serve_file() / serve_static()
     * responses beside the ETag. Empty by default (no header emitted).
     */
    char _cache_control_buf[CACHE_CONTROL_BUF_SIZE];

    /**
     * @brief Per-slot buffer for app-supplied custom response headers/cookies.
     *
     * Filled via add_response_header() / set_cookie() during a handler and
     * injected into send() / send_empty() / redirect() just like the CORS
     * block. Cleared at the start of every dispatch so each request begins
     * with no carried-over headers.
     */
    char _extra_hdr[CONN_POOL_SLOTS][EXTRA_HDR_BUF_SIZE];

#if PC_ENABLE_HTTP3
    // HTTP/3 leaf cert + seed and UDP port, held until begin() starts the QUIC server.
    const uint8_t *_h3_cert = nullptr;
    size_t _h3_cert_len = 0;
    uint8_t _h3_seed[32] = {0};
    uint16_t _h3_port = PC_HTTP3_PORT;
    bool _h3_enabled = false;
#endif

    /**
     * @brief Global middleware chain, run in registration order before dispatch.
     *
     * Populated by use(); a middleware returning MwResult::MW_HALT short-circuits the
     * request. An empty chain (the default) adds no per-request work.
     */
    Middleware _middleware[MAX_MIDDLEWARE];
    uint8_t _middleware_count; ///< Number of active entries in _middleware.

    // --- Built-in rate-limit pre-filter (fixed-window counter) ----------------
    uint16_t _rl_max;          ///< Max requests per window; 0 = rate limiting off.
    uint32_t _rl_window_ms;    ///< Window length in milliseconds.
    uint32_t _rl_window_start; ///< millis() at the start of the current window.
    uint16_t _rl_count;        ///< Requests counted in the current window.

    /**
     * @brief Run the global middleware chain for a request.
     * @return true if a middleware returned MwResult::MW_HALT (a response was sent and
     *         dispatch must stop); false to continue to route matching.
     */
    bool run_middleware(uint8_t slot_id, HttpReq *req);

    /**
     * @brief Built-in fixed-window rate-limit check (see enable_rate_limit()).
     * @return true if the request was rejected with 429 (response sent, dispatch
     *         must stop); false when rate limiting is disabled or within budget.
     */
    bool rate_limit_check(uint8_t slot_id);

    /**
     * @brief Evaluate whether a route pattern matches a request path.
     *
     * Wildcard routes end with `*`; the `*` is replaced by a prefix match.
     * Exact routes use strcmp.
     *
     * @param route       Null-terminated route pattern.
     * @param is_wildcard True if route ends with `*`.
     * @param req_path    Null-terminated path from the parsed request.
     * @return True if the route matches the request path.
     */
    static bool path_matches(const char *route, bool is_wildcard, const char *req_path);

    /// @brief Record a response for stats + the access-log hook. Reads method/path from http_pool[slot_id].
    void note_response(uint8_t slot_id, int code, int body_len);

#if PC_ENABLE_KEEPALIVE
    /**
     * @brief Decide whether the current response should keep the connection alive.
     *
     * Only a cleanly-parsed request (ParseState::PARSE_COMPLETE) is eligible: HTTP/1.1 keeps
     * alive unless the client sent `Connection: close`; HTTP/1.0 keeps alive only
     * with `Connection: keep-alive`. On a true return the slot's request tally is
     * incremented; the PC_KEEPALIVE_MAX_REQUESTS-th request returns false so
     * the connection is closed deliberately. Always false with keep-alive off.
     */
    bool keepalive_eval(uint8_t slot_id);
#endif

    /**
     * @brief Finish a response: flush, then close the connection (close path) or
     *        recycle the slot for the next request (keep-alive). Records the
     *        response and resets the HTTP parser either way. Addresses the
     *        connection by slot alone; the transport resolves the pcb internally.
     *
     * @param pre_flushed the caller already emitted the final bytes with pc_conn_send_flush()
     *        (write+tcp_output coalesced into one marshal), so skip the redundant flush here.
     */
    void pc_resp_end(uint8_t slot_id, int code, int body_len, bool keep, bool pre_flushed = false);

    /**
     * @brief Resolve the Connection response header and report keep-alive intent.
     *
     * One owner for the keep-alive decision: returns "Connection: keep-alive\r\n"
     * or "Connection: close\r\n" and, via @p keep_out, whether the slot is kept
     * alive. Always reports close when keep-alive is compiled out.
     */
    const char *pc_resp_conn_hdr(uint8_t slot_id, bool *keep_out);

    /**
     * @brief Append the shared response trailer (CORS block, custom headers, the
     *        Connection header, and the terminating blank line) to a header buffer
     *        already holding the status line and per-response headers. @p hlen is
     *        the current length; returns the new total length.
     */
    int append_resp_trailer(char *buf, size_t cap, int hlen, uint8_t slot_id, const char *cl);

    /// @brief Resume a pending chunked response: pull + frame chunks until the send window is full, finish when
    /// drained.
    void chunk_send_pump(uint8_t slot_id);

#if PC_ENABLE_AUTH
    /// @brief Validate the request's HTTP Basic credentials against route @p r. @return true if authorized.
    static bool check_basic_auth(uint8_t slot_id, HttpReq *req, const Route *r);
    /// @brief Validate an `Authorization: Digest` (RFC 7616, SHA-256, qop=auth) request against route @p r.
    /// @param stale  set true when the credentials verify but the nonce has expired (RFC 7616 3.3): the
    ///               caller reissues a fresh challenge with `stale=true` so the client retries without a
    ///               re-prompt. Left untouched on a credential mismatch or forged nonce.
    bool check_digest_auth(uint8_t slot_id, HttpReq *req, const Route *r, bool *stale);
    /// @brief Send 401 Unauthorized with a Basic or Digest `WWW-Authenticate` challenge per route @p r.
    /// @param stale  emit `stale=true` in the Digest challenge (expired-nonce transparent retry).
    void send_unauth(uint8_t slot_id, const Route *r, bool stale = false);
    /// @brief Per-server Digest keying secret (random at begin()); keys the stateless timestamped nonce.
    uint8_t _digest_secret[16];
    /// @brief (Re)seed the Digest keying secret from the CSPRNG.
    void regen_digest_secret();
    /// @brief Mint a fresh stateless nonce (issue time + keyed MAC) into @p out (needs cap >= 48).
    void make_digest_nonce(char *out, size_t cap);
    /// @brief Verify a client nonce's MAC and freshness. @return true if the MAC is authentic (issued by
    ///        this server); sets @p *expired when the nonce is authentic but older than its lifetime.
    bool verify_digest_nonce(const char *nonce, bool *expired);
#endif

#if PC_ENABLE_FILE_SERVING
    /// @brief Dispatch a RouteType::ROUTE_STATIC match: resolve the FS path and serve it (MIME/index/gzip).
    void serve_static_request(uint8_t slot_id, HttpReq *req, const Route *r);
    /// @brief Open @p fs_path and stream it as 200 with the given type and optional Content-Encoding.
    void serve_file_internal(uint8_t slot_id, bool head, fs::FS &file_sys, const char *fs_path,
                             const char *content_type, const char *content_encoding);
    /// @brief Resume a pending file response: page out one send-buffer window, finishing when drained.
    void file_send_pump(uint8_t slot_id);
#endif

#if PC_ENABLE_WEBDAV
    /// @brief If @p req matches a RouteType::ROUTE_DAV mount, handle it as WebDAV and return true.
    bool try_serve_dav(uint8_t slot_id, HttpReq *req);
    /// @brief Dispatch a WebDAV request against the mount @p r (resolves the FS path, then the method).
    void serve_dav_request(uint8_t slot_id, HttpReq *req, const Route *r);
    /// @brief Send a bodyless WebDAV status with optional extra header lines (each ending in CRLF).
    void dav_send_status(uint8_t slot_id, int code, const char *extra_headers);
#if PC_ENABLE_STREAM_BODY
    /// @brief Stream-begin hook: if @p req is a PUT under a DAV mount, open the file and stream the body.
    bool dav_stream_put_begin(HttpReq *req);
    /// @brief Stream-data hook: write one body chunk to @p req's slot's DAV PUT file.
    void dav_stream_put_data(HttpReq *req, const uint8_t *data, size_t len);
    /// @brief C-callable trampolines (the parser hook takes plain function pointers).
    static bool dav_put_begin_tramp(HttpReq *req);
    static void dav_put_data_tramp(HttpReq *req, const uint8_t *data, size_t len);
    /// @brief Stream-abort hook: close the half-written PUT file if the transfer is torn down early.
    static void dav_put_abort_tramp(HttpReq *req);
#endif
#endif

    /**
     * @brief Look up and invoke the first matching route for the given slot.
     *
     * If CORS is enabled and the method is OPTIONS, the preflight is
     * short-circuited here with a 204 response.  If no route matches, the
     * not-found handler is invoked (or a default 404 is sent).
     *
     * @param slot_id Connection slot to dispatch.
     */
    void match_and_execute(uint8_t slot_id);

    /// @brief Route-selection predicate: true if route @p r is active, its path pattern matches
    ///        @p req, and its interface filter admits this slot's connection. Matching a param route
    ///        captures its path parameters into @p req as a side effect (as the inline match did).
    bool route_admits(const Route *r, uint8_t slot_id, HttpReq *req) const;

    /// @brief Dispatch a route whose path + interface already matched (WS/SSE/STATIC/HTTP + auth).
    /// @return true when a response was sent (the caller stops); false to keep scanning later routes,
    ///         with @p path_matched / @p allow_buf updated for a possible 405.
    bool dispatch_matched_route(uint8_t slot_id, HttpReq *req, HttpMethod method, Route *r, bool *path_matched,
                                char *allow_buf, size_t allow_cap);

#if PC_ENABLE_CSRF
    /// @brief Built-in CSRF gate: serve the `GET /csrf` token endpoint and enforce a valid
    ///        `X-CSRF-Token` on every state-changing method. @return true if a response was sent.
    bool pc_csrf_gate(uint8_t slot_id, HttpReq *req, HttpMethod method);
#endif

#if PC_ENABLE_WEBSOCKET
    /// @brief Complete (or reject) a RouteType::ROUTE_WS handshake per RFC 6455 §4.2.1. Always responds.
    void handle_ws_route(uint8_t slot_id, HttpReq *req, HttpMethod method, const Route *r);
#endif

#if PC_ENABLE_AUTH
    /// @brief Enforce route @p r's auth (lockout gate + Digest/Basic credential check, with lockout
    ///        accounting). @return true if authorized; on failure the 401/429 is already sent.
    bool authorize_request(uint8_t slot_id, HttpReq *req, const Route *r);
#endif

#if PC_ENABLE_WEBSOCKET
    /// @brief Invoke the registered WS message handler for a completed frame on @p ws.
    void ws_dispatch_message(const WsConn *ws) const;
    /// @brief Invoke the registered WS close handler for @p ws.
    void ws_dispatch_close(const WsConn *ws) const;
#endif

  public:
    /**
     * @brief Construct a PC with an empty routing table.
     *
     * All route slots are marked inactive.  CORS is disabled.  The
     * not-found handler is null (falls back to built-in 404 response).
     */
    PC();

    /**
     * @brief Register a port to listen on when begin() is called.
     *
     * Call this before begin() for each port you want the server to accept
     * connections on.  The @p proto argument tells the session layer which
     * protocol handler to invoke for events on this port.
     *
     * For the common single-HTTP-port case, prefer `begin(80)` which calls
     * this internally.  Use the explicit listen() + begin() form when you
     * need multiple ports (e.g., HTTP on 80 and Telnet on 23).
     *
     * @code
     * server.listen(80, ConnProto::PROTO_HTTP);
     * server.listen(23, ConnProto::PROTO_TELNET);
     * server.begin();
     * @endcode
     *
     * @param port  TCP port to open.
     * @param proto Application protocol; defaults to ConnProto::PROTO_HTTP.
     * @return the listener id (a non-negative index) on success - pass it to
     *         pc_relay_publish() / pc_ssh_forward_begin(); pc_result::PC_ERR_LISTENER_FULL if the pool is
     * full.
     */
    int32_t listen(uint16_t port, ConnProto proto = ConnProto::PROTO_HTTP);

    /**
     * @brief Initialize all connection slots and open all registered listeners.
     *
     * Resets the HTTP parser pool, calls DeterministicAsyncTCP::pool_init(),
     * then calls listener_add() for each port registered via listen().
     * Requires at least one prior listen() call.  For the common single-port
     * case use begin(port, cfg) instead.
     *
     * @param cfg  Optional runtime configuration.  Pass nullptr for defaults.
     * @return pc_result::PC_OK on success; pc_result::PC_ERR_NO_LISTENERS if no ports were
     *         registered; pc_result::PC_ERR_LISTEN_FAILED if a listener could not open.
     */
    int32_t begin(const WebServerConfig *cfg = nullptr);

    /**
     * @brief Convenience overload: register @p port as HTTP and start listening.
     *
     * Equivalent to `listen(port); begin(cfg);`.  Preserved for backward
     * compatibility with single-port sketches.
     *
     * @param port TCP port to listen on (typically 80).
     * @param cfg  Optional runtime configuration.  Pass nullptr for defaults.
     * @return pc_result::PC_OK on success; a negative pc_result on failure.
     */
    int32_t begin(uint16_t port, const WebServerConfig *cfg = nullptr);

#if PC_ENABLE_TLS
    /**
     * @brief Load the TLS server certificate + private key (call before begin).
     *
     * Initializes the static-pool mbedTLS engine. Required before any TLS
     * listener will complete a handshake. PEM buffers must include the trailing
     * NUL in the length; DER is also accepted.
     *
     * @return true on success; false if the cert/key/pool setup failed.
     */
    bool tls_cert(const uint8_t *cert, size_t cert_len, const uint8_t *key, size_t key_len);

    /**
     * @brief Register a TLS (HTTPS) HTTP listener on @p port (typically 443).
     *
     * Like listen() but connections accepted here run a TLS handshake first.
     * Call tls_cert() first, then begin(). @return pc_result::PC_OK or an error code.
     */
    int32_t listen_tls(uint16_t port);

    /**
     * @brief Convenience: load cert/key, register a TLS listener, and start.
     *
     * Equivalent to `tls_cert(...); listen_tls(port); begin(cfg);`.
     *
     * @param port     TLS port (typically 443).
     * @param cert     Server certificate (chain).
     * @param cert_len Length incl. trailing NUL for PEM.
     * @param key      Server private key.
     * @param key_len  Length incl. trailing NUL for PEM.
     * @param cfg      Optional runtime config.
     * @return pc_result::PC_OK on success; a negative code, or pc_result::PC_ERR_LISTEN_FAILED
     * if the TLS engine could not initialize.
     */
    int32_t begin_tls(uint16_t port, const uint8_t *cert, size_t cert_len, const uint8_t *key, size_t key_len,
                      const WebServerConfig *cfg = nullptr);

#if PC_ENABLE_MTLS
    /**
     * @brief Require a verified client certificate (mTLS).
     *
     * Call after tls_cert() (or begin_tls()) and before connections arrive. Sets
     * @p ca as the trust anchor and switches the handshake to require a client
     * certificate chaining to it; a client that presents none, or an untrusted
     * one, is rejected during the handshake.
     *
     * @param ca     CA certificate (chain).
     * @param ca_len Length incl. trailing NUL for PEM.
     * @return true on success; false if the engine is not ready or the CA failed
     *         to parse.
     */
    bool tls_require_client_cert(const uint8_t *ca, size_t ca_len);

    /**
     * @brief Copy the connecting client's verified certificate subject DN.
     *
     * Use inside a handler to identify the mTLS peer (e.g. for authorization or
     * logging). Valid only on a TLS connection whose handshake required and
     * verified a client cert.
     *
     * @param slot_id  Connection slot (the handler's id).
     * @param out      Destination buffer (always NUL-terminated on success).
     * @param out_len  Capacity of @p out.
     * @return subject length written, or <0 if there is no verified client cert.
     */
    int tls_client_subject(uint8_t slot_id, char *out, size_t out_len);
#endif // PC_ENABLE_MTLS
#endif // PC_ENABLE_TLS

#if PC_ENABLE_HTTP3
    /**
     * @brief Enable the HTTP/3 (QUIC) server: load its Ed25519 leaf certificate + key and choose the
     * UDP port. Call before begin(); begin() then binds the port and serves HTTP/3 through the same
     * routes as HTTP/1.1 and HTTP/2. @p cert_der is a DER X.509 leaf whose public key is the Ed25519
     * key matching @p ed25519_seed (its 32-byte private seed). @return true if stored.
     *
     * Profile: TLS_AES_128_GCM_SHA256 + X25519 + Ed25519 (a client offering none of these is refused).
     */
    bool pc_h3_cert(const uint8_t *cert_der, size_t cert_len, const uint8_t ed25519_seed[32],
                    uint16_t port = PC_HTTP3_PORT);

    /**
     * @brief Internal: run a completed HTTP/3 request through the shared route dispatcher on the
     * reserved conn-pool slot (called by the pc_quic_server request trampoline, not by app code). The
     * response routes back to @p stream_id on @p conn_id via send() -> pc_quic_server_respond.
     */
    void dispatch_h3_request(uint32_t conn_id, uint64_t stream_id, const char *method, const char *path,
                             const char *authority, const uint8_t *body, size_t body_len);
#endif // PC_ENABLE_HTTP3

    /**
     * @brief Gracefully stop the server.
     *
     * Aborts all active connections, closes the listener, frees the event
     * queue, and resets all HTTP parser slots.  The WiFi and TCP/IP stack
     * remain active.  Call begin() or restart() to bring the server back up.
     */
    void stop();

    /**
     * @brief Hard-reset all connections and re-open all registered listeners.
     *
     * Equivalent to stop() followed by begin(cfg) using the ports and protocols
     * registered via listen() (or the port passed to begin(port)).  The WiFi
     * and TCP/IP stack are not touched.
     *
     * Calling restart() before any listen() / begin(port) has no effect and
     * returns -1.
     *
     * @param cfg Optional new runtime configuration.  Pass nullptr to reuse
     *            the compile-time default (CONN_TIMEOUT_MS).
     */
    int32_t restart(const WebServerConfig *cfg = nullptr);

    /**
     * @brief Register a route handler.
     *
     * Routes are matched in registration order (first match wins).
     * A trailing `*` in @p path enables prefix matching: `"/api/"` followed by `*`
     * matches `"/api/users"`, `"/api/devices"`, etc.
     *
     * @param path     URL path pattern, e.g. `"/api/status"`, or a prefix ending in a `*` wildcard.
     *                 Must be ≤ `MAX_PATH_LEN - 1` characters.
     * @param method   HTTP method this route accepts.
     * @param callback Function called when this route is matched.
     *
     * @note Registering more than MAX_ROUTES routes silently drops extras.
     */
    void on(const char *path, HttpMethod method, Handler callback);

    /**
     * @brief Register a route that only matches on a specific network interface.
     *
     * Identical to on(path, method, callback) but the route is invisible unless
     * the request arrived on @p iface (pc_iface::PC_IFACE_STA or pc_iface::PC_IFACE_AP). A
     * non-matching interface falls through to other routes / 404, so you can,
     * e.g., expose a provisioning UI only on the softAP and the app API only on
     * the station link. Requires set_ap_ip() to have been called so connections
     * can be classified.
     *
     * @param path     URL path pattern.
     * @param method   HTTP method.
     * @param callback Handler invoked on a match.
     * @param iface    pc_iface::PC_IFACE_STA or pc_iface::PC_IFACE_AP (pc_iface::PC_IFACE_ANY = no filter).
     */
    void on(const char *path, HttpMethod method, Handler callback, pc_iface iface);

    /**
     * @brief Register a route whose path is a regular expression.
     *
     * The whole request path must match @p pattern (implicitly anchored). The
     * matcher is a small, bounded, allocation-free backtracker supporting:
     * `.` (any char), `*` `+` `?` quantifiers, character classes `[...]` /
     * `[^...]` with `a-z` ranges, the shorthands `\d \w \s` (and `\D \W \S`),
     * and `\` to escape a metacharacter. It is **non-capturing** and has no
     * groups `()` or alternation `|` - use `:name` path parameters (see the
     * other on() overload notes / http_get_param) when you need to capture.
     * Matching is bounded by RE_MAX_STEPS and fails closed past that budget.
     *
     * @code
     *   server.on_regex("/sensor/[0-9]+", HttpMethod::HTTP_GET, handle_sensor);
     *   server.on_regex("/img/.+\\.png", HttpMethod::HTTP_GET, handle_png);
     * @endcode
     *
     * @param pattern  Regex the full path must match (stored, <= MAX_PATH_LEN-1).
     * @param method   HTTP method.
     * @param callback Handler invoked on a match.
     */
    void on_regex(const char *pattern, HttpMethod method, Handler callback);

    /**
     * @brief Tell the server the softAP IPv4 address for STA/AP route filtering.
     *
     * Each accepted connection is tagged pc_iface::PC_IFACE_AP when its local IP equals
     * @p ap_ip, else pc_iface::PC_IFACE_STA. Call once after starting the softAP, e.g.
     * `server.set_ap_ip(pc_net_ap_ip())` (already network byte order).
     * Without it, every connection is treated as pc_iface::PC_IFACE_STA.
     *
     * @param ap_ip softAP IPv4 address in network byte order (0 to clear).
     */
    void set_ap_ip(uint32_t ap_ip);

#if PC_ENABLE_AUTH
    /**
     * @brief Register a route handler protected by HTTP authentication.
     *
     * If the request does not include valid credentials, the library sends
     * `401 Unauthorized` with the appropriate `WWW-Authenticate` challenge
     * (`Basic`, or `Digest` with SHA-256 + `qop=auth` per RFC 7616) and the
     * callback is not invoked.
     *
     * @param path     URL path pattern.
     * @param method   HTTP method.
     * @param callback Handler invoked only on successful authentication.
     * @param realm    WWW-Authenticate realm displayed by the browser.
     * @param user     Required username.
     * @param pass     Required password.
     * @param digest   When true, use Digest authentication instead of Basic.
     */
    void on(const char *path, HttpMethod method, Handler callback, const char *realm, const char *user,
            const char *pass, bool digest = false);
#endif // PC_ENABLE_AUTH

#if PC_ENABLE_FILE_SERVING
    /**
     * @brief Serve a file from any Arduino-compatible filesystem.
     *
     * Opens @p fs_path on @p file_sys, sends HTTP 200 with the appropriate
     * headers (Content-Type, Content-Length), and streams the file body in
     * FILE_CHUNK_SIZE chunks via tcp_write().  Sends 404 if the file cannot
     * be opened.
     *
     * @param slot_id      Connection slot index.
     * @param file_sys     Filesystem reference (e.g. SPIFFS, LittleFS).
     * @param fs_path      Path to the file on the filesystem.
     * @param content_type MIME type string, e.g. "text/html".
     */
    void serve_file(uint8_t slot_id, fs::FS &file_sys, const char *fs_path, const char *content_type);

    /**
     * @brief Mount a filesystem subtree at a URL prefix (one-call static serving).
     *
     * Registers a wildcard route so every request under @p url_prefix is served
     * from @p fs_root on @p file_sys. The request path beyond the prefix is
     * appended to @p fs_root; a request ending in `/` (or exactly the prefix)
     * serves `index.html`. Content-Type is auto-detected from the extension
     * (see mime_type()). If the client sends `Accept-Encoding: gzip` and a
     * `<path>.gz` exists, the pre-compressed file is served with
     * `Content-Encoding: gzip`. Paths containing `..` are rejected (404).
     *
     * Only GET and HEAD are served; other methods get 405.
     *
     * @code
     * server.serve_static("/", LittleFS, "/www");      // SPA from flash
     * server.serve_static("/assets/", LittleFS, "/assets");
     * @endcode
     *
     * @param url_prefix  URL prefix to mount (with or without a trailing `*`).
     * @param file_sys    Filesystem reference (must outlive the server).
     * @param fs_root     Root directory on the filesystem (persistent string).
     */
    void serve_static(const char *url_prefix, fs::FS &file_sys, const char *fs_root);
#endif // PC_ENABLE_FILE_SERVING

#if PC_ENABLE_WEBDAV
    /**
     * @brief Mount a filesystem subtree as a WebDAV share (RFC 4918).
     *
     * Registers a wildcard route so every request under @p url_prefix is handled
     * as WebDAV against @p fs_root on @p file_sys. The supported methods are
     * OPTIONS, PROPFIND (Depth 0/1), GET, HEAD, PUT, DELETE, MKCOL, COPY, MOVE,
     * and advisory LOCK/UNLOCK; a client such as rclone, cadaver, curl, or a
     * mounted network drive can browse and edit files. The request path beyond
     * the prefix is appended to @p fs_root (paths containing `..` are rejected).
     *
     * Limits (see PC_ENABLE_WEBDAV): PROPFIND builds a 207 into a
     * PC_WEBDAV_BUF_SIZE buffer and lists at most PC_WEBDAV_MAX_ENTRIES
     * children; PUT buffers the body (bounded by BODY_BUF_SIZE); COPY handles
     * files (not collections); locks are advisory (issued, not enforced);
     * PROPPATCH is unsupported. Combine with per-route auth and HTTPS before
     * exposing a writable share.
     *
     * @code
     * server.dav("/dav", LittleFS, "/dav");   // dav://<ip>/dav -> /dav on flash
     * @endcode
     *
     * @param url_prefix URL prefix to mount (with or without a trailing `*`).
     * @param file_sys   Filesystem reference (must outlive the server).
     * @param fs_root    Root directory on the filesystem (persistent string).
     */
    void dav(const char *url_prefix, fs::FS &file_sys, const char *fs_root);
#endif // PC_ENABLE_WEBDAV

    /**
     * @brief Register a fallback handler for unmatched requests.
     *
     * Called instead of sending a built-in 404 when no route matches.
     * The callback may call send() to return a custom error page.
     *
     * @param callback Handler to invoke on a 404 condition.
     */
    void on_not_found(Handler callback);

    /**
     * @brief Install a per-request access-log callback (one hook, no buffering).
     *
     * @p cb is invoked once per response with the method, path, status code, and
     * response body length. Pass nullptr to remove. See @ref RequestLogCb.
     */
    void on_request_log(RequestLogCb cb);

    /**
     * @brief Register a middleware to run before every request is dispatched.
     *
     * Middlewares run in registration order (see @ref Middleware) ahead of route
     * matching, after the built-in rate-limit check. Up to MAX_MIDDLEWARE may be
     * registered; further calls are ignored. Use this to add cross-cutting
     * behavior - request logging, custom auth, header injection, feature gating -
     * composed independently of individual routes.
     *
     * @code
     *   static MwResult log_mw(uint8_t slot, HttpReq *req) {
     *       Serial.printf("%s %s\n", req->method, req->path);
     *       return MwResult::MW_NEXT;                  // fall through to the handler
     *   }
     *   server.use(log_mw);
     * @endcode
     *
     * @param mw Middleware function pointer (must not be nullptr).
     */
    void use(Middleware mw);

    /**
     * @brief Enable a built-in fixed-window request rate limiter.
     *
     * Counts all incoming requests in a sliding fixed window; once more than
     * @p max_requests arrive within @p window_ms the server answers further
     * requests in that window with `429 Too Many Requests` (plus a `Retry-After`
     * header) instead of dispatching them. The check runs before the middleware
     * chain and route matching, so it bounds work under flood. State is a few
     * per-server counters (no heap, no per-IP table) - a global throttle suited
     * to a small device behind a trusted LAN. For connection-level flood defense
     * see also `PC_ENABLE_ACCEPT_THROTTLE`.
     *
     * @param max_requests Requests allowed per window. Pass 0 to disable.
     * @param window_ms    Window length in milliseconds (must be > 0).
     */
    void enable_rate_limit(uint16_t max_requests, uint32_t window_ms);

#if PC_ENABLE_STATS
    /**
     * @brief Send a JSON runtime-stats snapshot and close the connection.
     *
     * Body: uptime_ms, total requests, 2xx/4xx/5xx counts, active connection-pool
     * slots, and (on ESP32) free heap. Wire it to a route:
     * @code
     *   server.on("/stats", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.stats(id); });
     * @endcode
     *
     * @param slot_id Connection slot to respond on.
     */
    void stats(uint8_t slot_id);
#endif

#if PC_ENABLE_METRICS
    /**
     * @brief Respond with runtime metrics in Prometheus text exposition format.
     *
     * Convenience for a `/metrics` route: emits the stats counters as Prometheus
     * gauges/counters (Content-Type `text/plain; version=0.0.4`) so a Prometheus
     * server can scrape the device.
     * @code
     *   server.on("/metrics", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.metrics(id); });
     * @endcode
     *
     * @param slot_id Connection slot to respond on.
     */
    void metrics(uint8_t slot_id);
#endif

    /**
     * @brief Enable CORS by pre-building the Access-Control headers.
     *
     * Once called, every response produced by send() and send_empty()
     * includes the CORS headers.  OPTIONS requests are intercepted and
     * answered with 204 automatically (preflight short-circuit).
     *
     * @param origin `Access-Control-Allow-Origin` value, e.g. `"*"` or
     *               `"https://example.com"`.  Pass `""` to disable CORS.
     */
    void set_cors(const char *origin);

    /**
     * @brief Set the `Cache-Control` header emitted for static files.
     *
     * Applies to serve_file() / serve_static() responses (beside the ETag), so
     * browsers can cache assets and revalidate cheaply with `If-None-Match`.
     * Examples: `"no-cache"` (cache but always revalidate), `"max-age=3600"`,
     * `"public, max-age=31536000, immutable"`. Pass `""` / `nullptr` to disable.
     *
     * @param value `Cache-Control` directive, or empty/null to emit no header.
     */
    void set_cache_control(const char *value);

#if PC_ENABLE_HTTP_DELIVERY
    /**
     * @brief Set `Cache-Control` to an RFC 5861 stale-while-revalidate policy.
     *
     * Emits `public, max-age=N, stale-while-revalidate=M` on served files. Within `max_age_s` a
     * client uses its copy outright; for `swr_s` beyond that it may serve the stale copy *and*
     * refresh in the background - so a sleepy or busy device never blocks the page, and the visitor
     * after it gets the fresh copy. Pass `swr_s` 0 for a plain `max-age`.
     *
     * @return true if the directive was built and applied.
     */
    bool set_cache_control_swr(uint32_t max_age_s, uint32_t swr_s);
#endif

    /**
     * @brief Drive the server - call every Arduino `loop()` iteration.
     *
     * On ESP32 `begin()` spawns the server worker task(s) (see PC_WORKER_COUNT),
     * which run the pipeline on their own core; `handle()` is then a no-op and your
     * `loop()` is free for application code. On host builds (and if no worker task
     * is running) `handle()` drives one service iteration inline, so existing
     * sketches and the native tests keep working unchanged.
     *
     * One service iteration (see service_once()):
     * 1. Calls `DeterministicAsyncTCP::check_timeouts()` to kill stale
     *    connections.
     * 2. Drains the event queue (connections, data, disconnects, errors).
     * 3. Scans all connection slots for `ParseState::PARSE_COMPLETE` requests and
     *    dispatches them to the matching route handler.
     * 4. Auto-sends 400 for any slot stuck in `ParseState::PARSE_ERROR`.
     * 5. Auto-sends 413 for any slot stuck in `ParseState::PARSE_ENTITY_TOO_LARGE`.
     * 6. Auto-sends 414 for any slot stuck in `ParseState::PARSE_URI_TOO_LONG`.
     *
     * Threading note: with the worker task running, route/WS/SSE handlers execute
     * in the worker task. Do server I/O from handlers; pushing from `loop()` (e.g.
     * SSE broadcast on a timer) runs concurrently with the worker and is made
     * thread-safe in a later phase.
     */
    void handle();

    /**
     * @brief Run exactly one service iteration for worker @p worker_id (the body
     *        driven by that worker's task, or by handle() when no task is running).
     *
     * Services only the connection slots owned by @p worker_id, so multiple workers
     * run disjoint slot sets in parallel. At PC_WORKER_COUNT=1 worker 0 owns
     * every slot. Public so the worker task can invoke it; application code should
     * call handle() rather than this directly.
     */
    void service_once(int worker_id = 0);

    /**
     * @brief The instance-bound HTTP poll pump for one slot (the HTTP ProtoHandler's on_poll).
     *
     * Installed into the HTTP handler at begin() via http_proto_set_poll() so the worker dispatch
     * loop pumps HTTP through the same uniform ProtoHandler seam as every other protocol - there is no
     * HTTP special case in the loop. Runs the file/chunk send pumps, the WebSocket + SSE drains, the
     * keep-alive re-parse, and dispatches a completed request into this server's routes. Public only so
     * the poll trampoline can reach it (like service_once); application code never calls it directly.
     * @param slot_id Connection slot to pump.
     */
    void http_poll_slot(uint8_t slot_id);

    /**
     * @brief Run @p fn(@p arg) on the worker that owns connection @p slot.
     *
     * The thread-safe way to push to a connection from outside a handler - e.g. an
     * SSE broadcast or a ws_send from loop() or a sensor task. Calling the send API
     * directly from another task would race the worker that owns the slot; instead
     * wrap the send in @p fn and defer it, and it runs single-threaded in the
     * owning worker's context. @p arg must stay valid until the callback runs. On
     * host builds (no worker task) it runs inline immediately.
     *
     * @return false if the slot is invalid or the worker's defer queue is full.
     */
    bool defer(uint8_t slot, pc_deferred_fn fn, void *arg) const;

    /**
     * @brief Send an HTTP response with a body and close the connection.
     *
     * Writes status line, Content-Type, Content-Length, optional CORS
     * headers, and the payload; then calls tcp_close (tcp_abort on failure).
     * Always calls http_reset() at the end to free the parser slot.
     *
     * @param slot_id      Connection slot index returned by the router.
     * @param code         HTTP status code (200, 404, 500, …).
     * @param content_type MIME type string, e.g. `"application/json"`.
     * @param payload      Null-terminated response body.
     *
     * @note If the underlying PCB has already been freed (e.g. by a
     *       concurrent timeout), this function is a no-op that just
     *       resets the slot.
     */
    void send(uint8_t slot_id, int code, const char *content_type, const char *payload);

    /**
     * @brief Send an HTTP response with an explicit-length (possibly binary) body.
     *
     * Same as send() above but the body length is given, so the body may contain NUL
     * bytes (protobuf, gRPC-web frames, octet-stream, images). @p body_len is bounded
     * by the single-write limit (65535); larger bodies need the chunked/file path.
     *
     * @param slot_id      Connection slot index returned by the router.
     * @param code         HTTP status code.
     * @param content_type MIME type string, e.g. `"application/grpc-web+proto"`.
     * @param body         Response body (may contain NULs); not required to be terminated.
     * @param body_len     Number of body octets.
     */
    void send(uint8_t slot_id, int code, const char *content_type, const uint8_t *body, size_t body_len);

    /**
     * @brief Send a headers-only HTTP response and close the connection.
     *
     * Equivalent to send() with an empty body and Content-Length: 0.
     * Useful for 204 No Content, 304 Not Modified, HEAD responses, and
     * CORS preflight replies.
     *
     * @param slot_id Connection slot index.
     * @param code    HTTP status code.
     */
    void send_empty(uint8_t slot_id, int code);

    /**
     * @brief Send an HTTP redirect (Location header, empty body) and close.
     *
     * Convenience for the common `/`→`/index.html` or canonical-host case,
     * previously hand-rolled via send_empty() plus a manual Location header.
     *
     * @param slot_id  Connection slot index.
     * @param code     Redirect status: 301, 302, 303, 307, or 308. Any other
     *                 value is treated as 302 Found.
     * @param location Value for the `Location` response header.
     */
    void redirect(uint8_t slot_id, int code, const char *location);

    /**
     * @brief Send a response body with `{{name}}` placeholders substituted.
     *
     * Streams @p tmpl to the client, replacing each `{{name}}` token with the
     * string returned by @p resolver (nullptr → empty). The body is never
     * buffered whole: it is walked twice - once to compute Content-Length, once
     * to write - so memory use is constant regardless of body size. A `{{` with
     * no matching `}}` (or a name longer than 32 chars) is emitted literally.
     *
     * @param slot_id      Connection slot index.
     * @param code         HTTP status code.
     * @param content_type Response Content-Type.
     * @param tmpl         Null-terminated template text.
     * @param resolver     Placeholder resolver (see TemplateVar), or nullptr.
     */
    void send_template(uint8_t slot_id, int code, const char *content_type, const char *tmpl, TemplateVar resolver);

    /**
     * @brief Stream a response body of unknown length via chunked transfer.
     *
     * Writes the status line and headers (including `Transfer-Encoding: chunked`,
     * plus any CORS / queued custom headers), then pulls the body from @p source
     * one piece at a time, adding the chunk framing and the terminating chunk. The
     * body is never buffered whole and the send paces with the TCP window - paging
     * across server loops as it drains - so output size is unbounded in constant
     * memory and a body larger than the send buffer is never truncated. This is the
     * complement to send(), which needs the full payload up front. A HEAD request
     * sends the headers only (@p source is not called).
     *
     * @param slot_id      Connection slot index.
     * @param code         HTTP status code.
     * @param content_type Response Content-Type.
     * @param source       Generator that produces the body (must not be nullptr).
     * @param ctx          Opaque state handed to @p source; see @ref ChunkSource
     *                     for the lifetime requirement (must outlive the response).
     */
    void send_chunked(uint8_t slot_id, int code, const char *content_type, ChunkSource source, void *ctx = nullptr);

    /**
     * @brief Queue a custom response header for the next send on this slot.
     *
     * Call from inside a handler before send() / send_empty() / redirect().
     * The header is appended to a fixed per-slot buffer (EXTRA_HDR_BUF_SIZE)
     * and emitted verbatim as `Name: value\r\n`. Headers that would overflow
     * the buffer are dropped whole (never truncated mid-line). The buffer is
     * cleared automatically at the start of each request.
     *
     * @param slot_id Connection slot index.
     * @param name    Header field name (no `:` or CRLF).
     * @param value   Header field value (no CRLF).
     */
    void add_response_header(uint8_t slot_id, const char *name, const char *value);

    /**
     * @brief Queue a `Set-Cookie` response header for the next send on this slot.
     *
     * Emits `Set-Cookie: name=value\r\n`, or `Set-Cookie: name=value; attrs\r\n`
     * when @p attrs is non-null (e.g. `"Path=/; HttpOnly; Max-Age=3600"`).
     * Shares the per-slot buffer with add_response_header().
     *
     * @param slot_id Connection slot index.
     * @param name    Cookie name.
     * @param value   Cookie value.
     * @param attrs   Optional `;`-separated attribute string, or nullptr.
     */
    void set_cookie(uint8_t slot_id, const char *name, const char *value, const char *attrs = nullptr);

    /**
     * @brief Discard any headers/cookies queued for this slot.
     *
     * @param slot_id Connection slot index.
     */
    void clear_response_headers(uint8_t slot_id);

    /**
     * @brief Guess a `Content-Type` from a path's file extension.
     *
     * Small static extension→type table covering the common web asset types
     * (html, css, js, json, svg, png, jpg, gif, ico, txt, wasm, woff2, …).
     * Case-insensitive on the extension. Falls back to
     * `"application/octet-stream"` when the extension is unknown or absent.
     *
     * @param path  File path or name (e.g. "/css/site.css").
     * @return Static content-type string (never null).
     */
    static const char *mime_type(const char *path);

#if PC_ENABLE_DIAG
    /**
     * @brief Send the diagnostic JSON and close the connection.
     *
     * Responds with 200 application/json containing the compile-time feature
     * flags and all capacity constants.  Only available when
     * PC_ENABLE_DIAG is set to 1 - disable before deploying to production.
     *
     * @param slot_id Connection slot index.
     */
    void diag(uint8_t slot_id);
#endif

#if PC_ENABLE_WEBSOCKET
    // -----------------------------------------------------------------------
    // WebSocket API
    // -----------------------------------------------------------------------

    /**
     * @brief Register a WebSocket upgrade route.
     *
     * When a GET request arrives for @p path with `Upgrade: websocket`, the
     * library performs the RFC 6455 handshake automatically and fires
     * @p on_connect.  Subsequent frames fire @p on_message.  Closing the
     * connection fires @p on_close.
     *
     * Ping frames are answered with Pong automatically; no handler needed.
     *
     * @param path        URL path the client connects to, e.g. `"/ws"`.
     * @param on_connect  Fired once when the handshake completes.  May be nullptr.
     * @param on_message  Fired for each text or binary frame.  Must not be nullptr.
     * @param on_close    Fired when the connection closes.  May be nullptr.
     */
    void on_ws(const char *path, WsConnectHandler on_connect, WsMessageHandler on_message, WsCloseHandler on_close);

    /**
     * @brief Send a text frame to a WebSocket client.
     *
     * @param ws_id    Index into ws_pool[] (from the WsConnectHandler or WsMessageHandler).
     * @param text     Null-terminated UTF-8 string to send.
     */
    void ws_send_text(uint8_t ws_id, const char *text);

    /**
     * @brief Send a binary frame to a WebSocket client.
     *
     * @param ws_id    Index into ws_pool[].
     * @param data     Payload bytes.
     * @param len      Payload length in bytes; must be <= WS_FRAME_SIZE.
     */
    void ws_send_binary(uint8_t ws_id, const uint8_t *data, uint16_t len);

    /**
     * @brief Initiate a graceful WebSocket close.
     *
     * Sends a Close frame with WsCloseCode::WS_CLOSE_NORMAL and marks the slot WsParseState::WS_CLOSED.
     * The on_close handler fires on the next handle() call.
     *
     * @param ws_id  Index into ws_pool[].
     */
    void ws_disconnect(uint8_t ws_id);
#endif // PC_ENABLE_WEBSOCKET

#if PC_ENABLE_SSE
    // -----------------------------------------------------------------------
    // Server-Sent Events API
    // -----------------------------------------------------------------------

    /**
     * @brief Register a Server-Sent Events endpoint.
     *
     * When a GET request arrives for @p path, the library sends the SSE
     * headers and keeps the connection open.  @p on_connect fires so the
     * handler can push an initial event with pc_sse_send().
     *
     * @param path        URL path, e.g. `"/events"`.
     * @param on_connect  Fired when a client subscribes.  May be nullptr.
     */
    void on_sse(const char *path, SseConnectHandler on_connect);

    /**
     * @brief Push an event to one SSE client.
     *
     * Formats and sends `event: ...\ndata: ...\nid: ...\n\n` to the client
     * on @p pc_sse_id.  Any field may be nullptr to omit it from the output.
     * The data field is required; passing nullptr sends nothing.
     *
     * @param pc_sse_id  Index into pc_sse_pool[].
     * @param data    Event data string (required).
     * @param event   Optional event name (sets the `event:` field).
     * @param id      Optional event ID (sets the `id:` field).
     */
    void pc_sse_send(uint8_t pc_sse_id, const char *data, const char *event = nullptr, const char *id = nullptr);

    /**
     * @brief Push an event to all connected SSE clients on a given path.
     *
     * Iterates pc_sse_pool[] and calls pc_sse_send() for every active client
     * whose path matches @p path.
     *
     * @param path   SSE endpoint path, e.g. `"/events"`.
     * @param data   Event data string.
     * @param event  Optional event name.
     * @param id     Optional event ID.
     */
    void pc_sse_broadcast(const char *path, const char *data, const char *event = nullptr, const char *id = nullptr);
#endif // PC_ENABLE_SSE
};

#endif
