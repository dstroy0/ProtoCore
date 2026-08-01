// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore.cpp
 * @brief Layer 7 (Application) - HTTP routing and request handler implementation.
 *
 * **Dispatch pipeline (called from handle())**
 * ```
 * handle()
 *   └─ server_tick()                 ← drain FreeRTOS event queue
 *   └─ for each slot:
 *        ParseState::PARSE_COMPLETE          → match_and_execute()
 *        ParseState::PARSE_ERROR             → send_text(400)
 *        ParseState::PARSE_ENTITY_TOO_LARGE  → send_text(413)
 *        ParseState::PARSE_URI_TOO_LONG      → send_text(414)
 * ```
 *
 * **Route table**
 * Routes are stored in a fixed-size array of `Route` structs.  Both exact
 * and wildcard (suffix `*`) routes are supported; exact routes always take
 * priority because the loop checks them in insertion order and returns on
 * the first match.
 *
 * **PCB lifecycle / teardown ownership**
 * All TCP I/O and teardown go through the transport-layer connection API
 * (pc_conn_send / pc_conn_flush / pc_conn_begin_close / pc_conn_close /
 * pc_conn_abort_slot), so this layer never calls lwIP or touches the raw
 * `tcp_pcb` directly. The transport owns the teardown order for every close:
 * it detaches the pcb from its lwIP callbacks and sets the slot `ConnState::CONN_FREE`
 * (pcb nulled) BEFORE the FIN/RST, on the captured pcb pointer. This means any
 * lwIP error callback that fires mid-teardown sees the slot as already free and
 * takes no action - preventing a double-free. L7 passes only the slot index:
 * pc_conn_close(slot) for a graceful local close, pc_conn_abort_slot(slot)
 * for a hard RST, pc_conn_begin_close(slot) for the drain-then-close dwell.
 */

#include "protocore.h"
#include "network_drivers/presentation/presentation.h" // http_proto_set_poll (install the instance-bound HTTP poll)
#include "network_drivers/session/proto_handler.h"
#include "network_drivers/session/worker.h"
#include "network_drivers/tls/tls.h"
#include "network_drivers/transport/listener.h"
#include "server/mmgr/plaintext.h" // the diag document is borrowed, not a stack array
#include "server/signaling/route.h"
#include "shared_primitives/frame.h" // the diag document is a frame spec, not a concatenation
#include "shared_primitives/hex.h"
#include "shared_primitives/mime.h"
#include "shared_primitives/strbuf.h" // pc_sb frame builder
#if PC_ENABLE_HTTP2
#include "network_drivers/presentation/http/http2/h2_server.h"
#endif
#if PC_ENABLE_HTTP3
#include "network_drivers/presentation/http/http3/quic_server.h"
#endif
#if PC_ENABLE_WEBSOCKET
#include "crypto/hash/sha1.h"
#include "network_drivers/presentation/codec/base64/base64.h"
#elif PC_ENABLE_AUTH
#include "network_drivers/presentation/codec/base64/base64.h"
#endif
#if PC_ENABLE_AUTH
#include "crypto/hash/sha256.h"
#include "services/system/clock.h" // pc_millis() for the stateless Digest nonce timestamp
#if PC_ENABLE_HTTP_DELIVERY
#include "services/file_transfer/http_delivery/http_delivery.h" // pc_delivery_cache_control (SWR directive)
#endif
#if PC_ENABLE_AUTH_LOCKOUT
#include "services/security/auth_lockout/auth_lockout.h"
#endif
#if PC_ENABLE_FORWARDED_TRUST
#include "services/security/forwarded_trust/forwarded_trust.h"
#endif
#ifdef ARDUINO
#include <esp_system.h> // esp_random() for the Digest nonce CSPRNG
#endif
#endif
#if PC_ENABLE_CSRF
#include "services/security/csrf/csrf.h"
#ifdef ARDUINO
#include <esp_system.h> // esp_random() for the CSRF HMAC secret
#endif
#endif
#if PC_ENABLE_WEBDAV
#include "services/file_transfer/webdav/webdav.h"
#include <time.h> // RFC 1123 Last-Modified formatting
#endif
#if PC_ENABLE_METRICS || PC_ENABLE_STATS
#include "network_drivers/application/web_assets.h" // PC_METRICS_PROM / PC_STATS_JSON (generated)
#endif
#if PC_HTTP_EMIT_DATE
#if PC_ENABLE_TIME_SOURCE
#include "services/timing_position/time_source/time_source.h" // pc_time_http_date() - any NTP/GPS/RTC/... source
#else
#include "services/timing_position/ntp_service/ntp_service.h" // pc_ntp_http_date() - direct NTP (or the host test seam)
#endif
#endif
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Outbound-transfer state is not held here. Each kind of transfer belongs to the TU that runs it:
// the chunked-send state to server/response.cpp, the file-send state to server/file_serving.cpp.
// The poll below asks each owner whether it holds a slot instead of reading its state.

/**
 * @brief Convert an HTTP status code to its standard reason phrase.
 *
 * Covers the 18 codes that arise in typical REST micro-server usage.
 * Unknown codes produce "Unknown" so callers never receive a null pointer.
 *
 * @param code HTTP status integer.
 * @return Pointer to a string-literal reason phrase; never null.
 */
// Response bytes go out via the transport-layer connection I/O API
// (pc_conn_send / pc_conn_flush / pc_conn_close, see tcp.h) so this
// application layer never calls lwIP directly.

const char *status_text(int code)
{
    switch (code)
    {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 204:
        return "No Content";
    case 206:
        return "Partial Content";
#if PC_ENABLE_WEBDAV
    case 207:
        return "Multi-Status";
#endif
    case 301:
        return "Moved Permanently";
    case 302:
        return "Found";
    case 303:
        return "See Other";
    case 304:
        return "Not Modified";
    case 307:
        return "Temporary Redirect";
    case 308:
        return "Permanent Redirect";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 408:
        return "Request Timeout";
    case 409:
        return "Conflict";
#if PC_ENABLE_WEBDAV
    case 412:
        return "Precondition Failed";
    case 423:
        return "Locked";
    case 502:
        return "Bad Gateway";
#endif
    case 413:
        return "Payload Too Large";
    case 414:
        return "URI Too Long";
    case 416:
        return "Range Not Satisfiable";
    case 429:
        return "Too Many Requests";
    case 500:
        return "Internal Server Error";
    case 501:
        return "Not Implemented";
    case 503:
        return "Service Unavailable";
    default:
        return "Unknown";
    }
}

/**
 * @brief Map a method string (from the parsed request line) to an HttpMethod enum.
 *
 * Returns HttpVersion::HTTP_UNKNOWN for any method the server does not implement, so the
 * dispatcher can answer 501 Not Implemented (RFC 7231 §6.5.2) instead of
 * silently treating it as GET.
 *
 * @param m Null-terminated method string, e.g. "POST".
 * @return Matching HttpMethod enum value, or HttpVersion::HTTP_UNKNOWN.
// The server's own state, owned here (internal linkage). It previously carried two pointers to the
// running instance so a trampoline could dispatch back into it; there is no instance to point at,
// because the slot pools were always global singletons and there was only ever one server. The
// trampolines call the functions directly now, and what remains is state nothing outside this file
// reads: who answers a request that matched nothing, where the access log goes, which ports were
// registered, and the HTTP/3 credentials held until begin() binds them.
struct ServerCtx
{
    Handler not_found_handler; ///< Called when no route matches; may be null.
    RequestLogCb log_cb;       ///< Per-request access-log hook; may be null.

    uint16_t listen_ports[MAX_LISTENERS];   ///< Ports registered via listen() / begin_http().
    ConnProto listen_protos[MAX_LISTENERS]; ///< Protocol for each registered listener.
    bool listen_tls[MAX_LISTENERS];         ///< True for TLS listeners (listen_tls()).
    uint8_t listener_count;                  ///< Registered listeners.

#if PC_ENABLE_HTTP3
    bool pc_h3_running;
    const uint8_t *h3_cert;
    size_t h3_cert_len;
    uint8_t h3_seed[32];
    uint16_t h3_port;
    bool h3_enabled;
#endif
};
static ServerCtx s_inst;
 */
static HttpMethod parse_method(const char *m)
{
    if (strcmp(m, "GET") == 0)
    {
        return HttpMethod::HTTP_GET;
    }
    if (strcmp(m, "POST") == 0)
    {
        return HttpMethod::HTTP_POST;
    }
    if (strcmp(m, "PUT") == 0)
    {
        return HttpMethod::HTTP_PUT;
    }
    if (strcmp(m, "DELETE") == 0)
    {
        return HttpMethod::HTTP_DELETE;
    }
    if (strcmp(m, "PATCH") == 0)
    {
        return HttpMethod::HTTP_PATCH;
    }
    if (strcmp(m, "HEAD") == 0)
    {
        return HttpMethod::HTTP_HEAD;
    }
    if (strcmp(m, "OPTIONS") == 0)
    {
        return HttpMethod::HTTP_OPTIONS;
    }
    return HttpMethod::HTTP_METHOD_UNKNOWN;
}

/**
 * @brief Canonical method token for an HttpMethod (for the Allow header).
 */
static const char *method_name(HttpMethod m)
{
    switch (m)
    {
    case HttpMethod::HTTP_GET:
        return "GET";
    case HttpMethod::HTTP_POST:
        return "POST";
    case HttpMethod::HTTP_PUT:
        return "PUT";
    case HttpMethod::HTTP_DELETE:
        return "DELETE";
    case HttpMethod::HTTP_PATCH:
        return "PATCH";
    case HttpMethod::HTTP_HEAD:
        return "HEAD";
    case HttpMethod::HTTP_OPTIONS:
        return "OPTIONS";
    default:
        return "";
    }
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

#if PC_ENABLE_KEEPALIVE || PC_ENABLE_WEBSOCKET
// Case-insensitive search for @p token as a comma/space-delimited element of a
// Connection header value (e.g. "keep-alive" in "Keep-Alive, Upgrade"). Shared by
// keep-alive evaluation and the WebSocket Upgrade-token check.
static bool conn_has_token(const char *hdr, const char *token)
{
    if (!hdr)
    {
        return false;
    }
    size_t tlen = strnlen(token, 32);
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
        if (len == tlen && strncasecmp(start, token, tlen) == 0)
        {
            return true;
        }
        if (*p == ',')
        {
            p++;
        }
    }
    return false;
}
#endif // PC_ENABLE_KEEPALIVE || PC_ENABLE_WEBSOCKET

#if PC_ENABLE_KEEPALIVE
bool keepalive_eval(uint8_t slot_id)
{
    HttpReq *req = &http_pool[slot_id];
    // Only a cleanly-parsed request has a known message boundary; errors close.
    if (req->parse_state != ParseState::PARSE_COMPLETE)
    {
        return false;
    }

    const char *c = http_get_header(req, "Connection");
    bool keep;
    if (req->version == HttpVersion::HTTP_11)
    {
        keep = !conn_has_token(c, "close"); // 1.1 default: persistent
    }
    else
    {
        keep = conn_has_token(c, "keep-alive"); // 1.0/unknown default: close
    }
    if (!keep)
    {
        return false;
    }

    // Fairness bound: serve at most PC_KEEPALIVE_MAX_REQUESTS, then close.
    if (++http_req_count[slot_id] >= PC_KEEPALIVE_MAX_REQUESTS)
    {
        return false;
    }
    return true;
}
#endif // PC_ENABLE_KEEPALIVE

// Finish a response: flush, then either begin the graceful ConnState::CONN_CLOSING dwell
// (close path) or leave the slot active for reuse (keep-alive). The HTTP parser
// is reset either way, returning a kept-alive slot to ParseState::PARSE_METHOD ready for the
// next request. The slot stays ConnState::CONN_ACTIVE through the write for BOTH paths
// (callbacks live - the model keep-alive has always used); the close path then
// dwells in ConnState::CONN_CLOSING from here, so the slot is reclaimed only once the peer
// ACKs the response (or the CLOSING timeout fires), not before it is delivered.
// Every response path now addresses the connection by slot alone - the transport
// resolves the pcb internally, the same way the RX read path does (no pcb is
// threaded through the app layer, so the send target can never disagree).
void pc_resp_end(uint8_t slot_id, int code, int body_len, bool keep, bool pre_flushed)
{
    if (!pre_flushed)
    {
        pc_conn_flush(slot_id); // a pre_flushed caller already did tcp_output in its final send
    }
    if (!keep)
    {
        pc_conn_begin_close(slot_id); // ACTIVE -> ConnState::CONN_CLOSING; finalizes on ACK
    }
    note_response(slot_id, code, body_len);
    http_reset(slot_id);
}

// Resolve the Connection response header (and report keep-alive intent) in one
// place so every response path agrees. Keep-alive compiled out always closes.
const char *pc_resp_conn_hdr(uint8_t slot_id, bool *keep_out)
{
    bool keep = false;
#if PC_ENABLE_KEEPALIVE
    keep = keepalive_eval(slot_id);
#else
    (void)slot_id;
#endif
    // The null half cannot fire: every call site passes the address of its own local `keep`. Kept so
    // the signature keeps saying the report-back is optional.
    if (keep_out) // GCOVR_EXCL_BR_LINE  keep_out is never null (see above)
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

int append_resp_trailer(char *buf, size_t cap, int hlen, uint8_t slot_id, const char *cl)
{
    // hlen is the caller's status-line length from pc_sb_finish, which reports 0 for a status line
    // that did not fit. Appending the trailer at offset 0 in that case would emit a response with
    // no status line at all, so 0 propagates as failure and the caller sends a canned reply.
    //
    // The old code clamped an over-long header to cap-1 and sent it. That is worse than sending
    // nothing: a header block cut mid-field has no terminating CRLF, so the peer reads the body as
    // continued headers and the connection desynchronizes. A response either fits or is refused.
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
        pc_sb sb_date_hdr = {date_hdr, sizeof(date_hdr), 0, true};
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
    pc_sb sb411 = {buf + hlen, cap - (size_t)hlen, 0, true};
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
    if (s_inst.listener_count >= MAX_LISTENERS)
    {
        return (int32_t)pc_result::PC_ERR_LISTENER_FULL;
    }
    s_inst.listen_ports[s_inst.listener_count] = port;
    s_inst.listen_protos[s_inst.listener_count] = proto;
    listen_tls[s_inst.listener_count] = false;
    s_inst.listener_count++;
    // Return the listener id (its index), not pc_result::PC_OK: begin() binds listener_pool[i] from
    // s_inst.listen_ports[i] and the accept path stamps that same index onto the slot, so this id is what
    // pc_relay_publish() / pc_ssh_forward_begin() must match against. (Errors are negative.)
    return (int32_t)(s_inst.listener_count - 1);
}

#ifdef ARDUINO
// The worker task's per-tick entry (registered with pc_workers_start below); ESP32-only, so it is
// compiled only where it is used - on host the pipeline runs inline via handle().
static void pc_pump_trampoline(int worker_id)
{
    service_once(worker_id);
}
#endif

#if PC_ENABLE_HTTP3
// The pc_quic_server request seam has no PC type; this trampoline forwards a completed
// HTTP/3 request into the instance's shared route dispatcher (app == the PC *).
static void pc_h3_request_trampoline(void *app, uint32_t conn_id, uint64_t stream_id, const char *method,
                                     const char *path, const char *authority, const uint8_t *body, size_t body_len)
{
    if (app) // GCOVR_EXCL_BR_LINE  app is always the non-null `this` passed to pc_quic_server_begin(); this static
             // trampoline has no other caller, so app==null is unreachable
    {
        ((PC *)app)->dispatch_h3_request(conn_id, stream_id, method, path, authority, body, body_len);
    }
}

// Randomness for the QUIC ephemeral X25519 key, the ServerHello random, and our connection IDs: the
// hardware TRNG on device; a deterministic PRNG on host (test builds carry no security context and
// have no esp_random).
static void pc_h3_rng(uint8_t *out, size_t len)
{
#ifdef ARDUINO
    size_t i = 0;
    while (i < len)
    {
        uint32_t r = esp_random();
        size_t n = (len - i) < 4 ? (len - i) : 4;
        memcpy(out + i, &r, n);
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

// HTTP's poll is instance-bound (it dispatches into this server's routes), so it cannot be a plain
// global on_poll like the singleton protocols. begin() records the serving instance here and installs
// this forwarder as the HTTP ProtoHandler's on_poll, so the worker loop pumps HTTP through the one
// uniform seam. The library serves from a single PC (the slot pools are global singletons),
// which is exactly what this one instance pointer models.
static void pc_http_on_poll(uint8_t slot)
{
    http_poll_slot(slot);
}

int32_t begin(const WebServerConfig *cfg)
{
    if (s_inst.listener_count == 0
#if PC_ENABLE_HTTP3
        && !s_inst.h3_enabled // an HTTP/3-only server binds UDP, not a TCP listener
#endif
    )
        return (int32_t)pc_result::PC_ERR_NO_LISTENERS;
    DeterministicAsyncTCP::pool_init(cfg);
#if PC_ENABLE_AUTH
    regen_digest_secret(); // fresh server keying secret per begin()
#endif
#if PC_ENABLE_CSRF
    {
        // Seed the CSRF HMAC secret from the hardware RNG (a fixed dev secret on
        // native/test builds, which have no esp_random).
        uint8_t sec[32];
#ifdef ARDUINO
        for (int i = 0; i < 8; i++)
        {
            uint32_t r = esp_random();
            memcpy(sec + i * 4, &r, 4);
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
        // GCOVR_EXCL_START  listener_add() only fails on a real lwIP fault - a bind/listen refusal
        // (port already in use) or PCB/queue exhaustion. The host lwIP mock's tcp_new/tcp_bind/
        // tcp_listen_with_backlog always succeed and it exposes no failure hook, so the error return
        // has no host path; it is covered on hardware.
        if (listener_add(i, s_inst.listen_ports[i], s_inst.listen_protos[i], s_inst.listen_tls[i]) < 0)
        {
            return (int32_t)pc_result::PC_ERR_LISTEN_FAILED;
        }
        // GCOVR_EXCL_STOP
    }
#if PC_ENABLE_HTTP3
    // Bind the HTTP/3 QUIC server (UDP on device; on host it is fed via pc_quic_server_ingest). Requests
    // dispatch through this instance's routes via the trampoline; pc_quic_server_poll() runs in service_once.
    if (s_inst.h3_enabled)
    {
        QuicServerConfig h3cfg;
        memset(&h3cfg, 0, sizeof(h3cfg));
        h3cfg.cert_der = s_inst.h3_cert;
        h3cfg.cert_len = s_inst.h3_cert_len;
        memcpy(h3cfg.ed25519_seed, s_inst.h3_seed, sizeof(h3cfg.ed25519_seed));
        h3cfg.rng = pc_h3_rng;
        s_inst.pc_h3_running = pc_quic_server_begin(s_inst.h3_port, &h3cfg, pc_h3_request_trampoline, this);
    }
#endif
#ifdef ARDUINO
    // Routes/listeners are now fixed; start the worker task(s) that drive the
    // pipeline off the user's loop(). On host the pipeline runs inline via handle().
    pc_workers_start(pc_pump_trampoline);
#endif
    return (int32_t)pc_result::PC_OK;
}

int32_t begin_http(uint16_t port, const WebServerConfig *cfg)
{
    int32_t rc = listen(port);
    if (rc < 0)
    {
        return rc;
    }
    return begin(cfg);
}

#if PC_ENABLE_HTTP3
bool pc_h3_cert(const uint8_t *cert_der, size_t cert_len, const uint8_t ed25519_seed[32], uint16_t port)
{
    if (!cert_der || cert_len == 0 || !ed25519_seed)
    {
        return false;
    }
    s_inst.h3_cert = cert_der;
    s_inst.h3_cert_len = cert_len;
    memcpy(s_inst.h3_seed, ed25519_seed, sizeof(s_inst.h3_seed));
    s_inst.h3_port = port;
    s_inst.h3_enabled = true;
    return true;
}

// Response sink for the HTTP/3 dispatch slot: route (code, content_type, body) onto the QUIC stream
// the request arrived on (ids stashed on the slot by dispatch_h3_request). Installed as conn->pc_resp_sink
// so send_text()/send_empty() stay protocol-agnostic.
static bool pc_h3_resp_sink(uint8_t slot, int code, const char *content_type, const char *body, size_t len)
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
    size_t mn = strnlen(method, sizeof(r->method));
    if (mn >= sizeof(r->method))
    {
        mn = sizeof(r->method) - 1;
    }
    memcpy(r->method, method, mn);
    r->method[mn] = 0;

    const char *q = strchr(path, '?');
    size_t plen = q ? (size_t)(q - path) : strnlen(path, sizeof(r->path));
    if (plen >= sizeof(r->path))
    {
        plen = sizeof(r->path) - 1;
    }
    memcpy(r->path, path, plen);
    r->path[plen] = 0;
    r->path_idx = strnlen(r->path, sizeof(r->path));
    if (q)
    {
        size_t ql = strnlen(q + 1, sizeof(r->query));
        if (ql >= sizeof(r->query))
        {
            ql = sizeof(r->query) - 1;
        }
        memcpy(r->query, q + 1, ql);
        r->query[ql] = 0;
        r->query_idx = strnlen(r->query, sizeof(r->query));
    }

    // :authority maps to Host, the way the h2 bridge does.
    if (authority && authority[0] && r->header_count < MAX_HEADERS)
    {
        Header *h = &r->headers[r->header_count++];
        memcpy(h->key, "host", 5);
        size_t vl = strnlen(authority, sizeof(h->val));
        if (vl >= sizeof(h->val))
        {
            vl = sizeof(h->val) - 1;
        }
        memcpy(h->val, authority, vl);
        h->val[vl] = 0;
    }

    if (body && body_len)
    {
        size_t n = body_len > BODY_BUF_SIZE ? BODY_BUF_SIZE : body_len;
        memcpy(r->body, body, n);
        r->body_len = n;
        r->body[r->body_len] = 0;
        r->body_bytes_read = body_len;
        r->content_length = body_len;
    }
    r->parse_state = ParseState::PARSE_COMPLETE;

    // Mark the reserved slot as HTTP/3 and install the response sink so send_text() / send_empty() route the
    // response back onto this stream (no TCP pcb here - the sink owns the QUIC framing).
    TcpConn *c = &conn_pool[slot];
    c->h3 = 1;
    c->pc_h3_conn_id = conn_id;
    c->pc_h3_stream = stream_id;
    c->pc_resp_sink = pc_h3_resp_sink;
    c->iface = pc_iface::PC_IFACE_STA;
    pc_conn_set_state(slot, ConnState::CONN_ACTIVE); // reserved slot: no bitmask bit (slot >= MAX_CONNS)
    c->pcb = nullptr;

    match_and_execute(slot); // -> handler -> send_text() -> pc_resp_sink -> pc_quic_server_respond()

    // Release the dispatch slot for the next request (a no-response handler simply leaves the stream open).
    c->h3 = 0;
    c->pc_resp_sink = nullptr;
    pc_conn_set_state(slot, ConnState::CONN_FREE); // reserved slot: no bitmask bit (slot >= MAX_CONNS)
    http_reset(slot);
}
#endif // PC_ENABLE_HTTP3

#if PC_ENABLE_TLS
bool tls_cert(const uint8_t *cert, size_t cert_len, const uint8_t *key, size_t key_len)
{
    return pc_tls_global_init(cert, cert_len, key, key_len);
}

int32_t listen_tls(uint16_t port)
{
    if (s_inst.listener_count >= MAX_LISTENERS)
    {
        return (int32_t)pc_result::PC_ERR_LISTENER_FULL;
    }
    s_inst.listen_ports[s_inst.listener_count] = port;
    s_inst.listen_protos[s_inst.listener_count] = ConnProto::PROTO_HTTP;
    s_inst.listen_tls[s_inst.listener_count] = true;
    s_inst.listener_count++;
    return (int32_t)pc_result::PC_OK;
}

int32_t begin_tls(uint16_t port, const uint8_t *cert, size_t cert_len, const uint8_t *key, size_t key_len,
                  const WebServerConfig *cfg)
{
    if (!tls_cert(cert, cert_len, key, key_len))
    {
        return (int32_t)pc_result::PC_ERR_LISTEN_FAILED;
    }
    int32_t rc = listen_tls(port);
    if (rc < 0)
    {
        return rc;
    }
    return begin(cfg);
}

#if PC_ENABLE_MTLS
bool tls_require_client_cert(const uint8_t *ca, size_t ca_len)
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
        return (int32_t)pc_result::PC_ERR_NO_LISTENERS;
    }
    stop();
    return begin(cfg);
}

void stop()
{
#ifdef ARDUINO
    // Stop the worker task(s) before tearing down the slots they service.
    pc_workers_stop();
#endif
    listener_stop_all();
    DeterministicAsyncTCP::stop();
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
 * @brief Register a route in the route table.
 *
 * Paths are stored null-terminated and truncated to MAX_PATH_LEN.  The
 * trailing character of the stored path is inspected to detect wildcard
 * routes: any path ending in `*` is treated as a prefix match.
 *
 * Registrations beyond MAX_ROUTES are silently ignored - callers should
 * verify return values if overflow is a concern.
 *
 * @param path     URL path to match, e.g. "/api/*".
 * @param method   HTTP method that triggers this route.
 * @param callback Handler invoked with (slot_id, request).
 */
void fill_route_base(Route *r, const char *path)
{
    strncpy(r->path, path, MAX_PATH_LEN - 1);
    r->path[MAX_PATH_LEN - 1] = '\0';
    r->is_active = true;
    size_t len = strnlen(r->path, MAX_PATH_LEN);
    r->is_wildcard = (len > 0 && r->path[len - 1] == '*');
    r->is_param = (strstr(r->path, "/:") != nullptr);
    r->is_regex = false;
    r->iface_filter = pc_iface::PC_IFACE_ANY;
}

void on_http(const char *path, HttpMethod method, Handler callback)
{
    Route *r = pc_route_add();
    if (r == nullptr)
    {
        return;
    }

    fill_route_base(r, path);
    r->type = RouteType::ROUTE_HTTP;
    r->method = method;
    r->callback = callback;
}

void on_http_iface(const char *path, HttpMethod method, Handler callback, pc_iface iface)
{
    Route *r = pc_route_add();
    if (r == nullptr)
    {
        return;
    }

    fill_route_base(r, path);
    r->type = RouteType::ROUTE_HTTP;
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
    Route *r = pc_route_add();
    if (r == nullptr)
    {
        return;
    }

    fill_route_base(r, pattern);
    r->type = RouteType::ROUTE_HTTP;
    r->method = method;
    r->callback = callback;
    r->is_regex = true;
}

#if PC_ENABLE_AUTH
void on_http_auth(const char *path, HttpMethod method, Handler callback, const char *realm, const char *user,
                  const char *pass, bool digest)
{
    Route *r = pc_route_add();
    if (r == nullptr)
    {
        return;
    }

    fill_route_base(r, path);
    r->type = RouteType::ROUTE_HTTP;
    r->method = method;
    r->callback = callback;
    r->auth_required = true;
    r->auth_digest = digest;
    strncpy(r->auth_realm, realm, MAX_AUTH_LEN - 1);
    r->auth_realm[MAX_AUTH_LEN - 1] = '\0';
    strncpy(r->auth_user, user, MAX_AUTH_LEN - 1);
    r->auth_user[MAX_AUTH_LEN - 1] = '\0';
    strncpy(r->auth_pass, pass, MAX_AUTH_LEN - 1);
    r->auth_pass[MAX_AUTH_LEN - 1] = '\0';
}
#endif // PC_ENABLE_AUTH

#if PC_ENABLE_WEBSOCKET
void on_ws(const char *path, WsConnectHandler on_connect, WsMessageHandler on_message, WsCloseHandler on_close)
{
    Route *r = pc_route_add();
    if (r == nullptr)
    {
        return;
    }

    fill_route_base(r, path);
    r->type = RouteType::ROUTE_WS;
    r->ws_connect = on_connect;
    r->ws_message = on_message;
    r->ws_close = on_close;
}
#endif // PC_ENABLE_WEBSOCKET

#if PC_ENABLE_SSE
void on_sse(const char *path, SseConnectHandler on_connect)
{
    Route *r = pc_route_add();
    if (r == nullptr)
    {
        return;
    }

    fill_route_base(r, path);
    r->type = RouteType::ROUTE_SSE;
    r->pc_sse_connect = on_connect;
}
#endif // PC_ENABLE_SSE

void on_not_found(Handler callback)
{
    s_inst.not_found_handler = callback;
}

/*
 * Enable CORS and pre-build the Access-Control response header block.
 *
 * The header string is constructed once here rather than at response time to
 * avoid repeated snprintf calls on the hot path.  It is stored in
 * `pc_resp_cors_header()[]` and injected verbatim into every response when
 * `pc_resp_cors_enabled()` is true.
 *
 * Passing an empty or null origin disables CORS without clearing the buffer -
 * only the `pc_resp_cors_enabled()` flag matters at dispatch time.
 *
 * @param origin Value for the Access-Control-Allow-Origin header, e.g. "*".
 */
void set_cors(const char *origin)
{
    if (!origin || origin[0] == '\0')
    {
        pc_resp_cors_enabled() = false;
        pc_resp_cors_header()[0] = '\0';
        return;
    }
    pc_sb sb__cors_header_buf = {pc_resp_cors_header(), CORS_HDR_BUF_SIZE, 0, true};
    pc_sb_put(&sb__cors_header_buf, "Access-Control-Allow-Origin: ");
    pc_sb_put(&sb__cors_header_buf, origin);
    pc_sb_put(&sb__cors_header_buf, "\r\nAccess-Control-Allow-Methods: GET, POST, PUT, DELETE, PATCH, HEAD, "
                                    "OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\n");
    if (pc_sb_finish(&sb__cors_header_buf) == 0)
    {
        pc_resp_cors_header()[0] = '\0';
    }
    pc_resp_cors_enabled() = true;
}

void set_cache_control(const char *value)
{
    if (!value || value[0] == '\0')
    {
        pc_resp_cache_control()[0] = '\0';
        return;
    }
    pc_sb sb__cache_control_buf = {pc_resp_cache_control(), CACHE_CONTROL_BUF_SIZE, 0, true};
    pc_sb_put(&sb__cache_control_buf, "Cache-Control: ");
    pc_sb_put(&sb__cache_control_buf, value);
    pc_sb_put(&sb__cache_control_buf, "\r\n");
    if (pc_sb_finish(&sb__cache_control_buf) == 0)
    {
        pc_resp_cache_control()[0] = '\0';
    }
}

#if PC_ENABLE_HTTP_DELIVERY
bool set_cache_control_swr(uint32_t max_age_s, uint32_t swr_s)
{
    // Build the directive with the RFC 5861 core so the header and the pc_delivery_swr decision
    // can never drift apart.
    char directive[64];
    if (pc_delivery_cache_control(max_age_s, swr_s, directive, sizeof(directive)) == 0)
    {
        return false;
    }
    set_cache_control(directive);
    return true;
}
#endif

/**
 * @brief Test whether a route path matches an incoming request path.
 *
 * Exact routes use strcmp (full-string equality).  Wildcard routes use
 * strncmp against the prefix up to (but not including) the trailing `*`.
 *
 * @param route       Registered route path, potentially ending in `*`.
 * @param is_wildcard True when the route was registered with a trailing `*`.
 * @param req_path    Incoming request path from the parsed HTTP request line.
 * @return True if the route matches the request path.
 */
bool path_matches(const char *route, bool is_wildcard, const char *req_path)
{
    if (!is_wildcard)
    {
        return strcmp(route, req_path) == 0;
    }

    // Prefix match: compare everything up to (but not including) the '*'
    size_t prefix_len = strnlen(route, MAX_PATH_LEN) - 1;
    return strncmp(route, req_path, prefix_len) == 0;
}

/**
 * @brief Segment-by-segment match for routes containing `:name` parameters.
 *
 * Walks @p route and @p path one `/`-delimited segment at a time. Literal
 * segments must match exactly; a `:name` segment captures the corresponding
 * path segment into @p req->path_params. Both must contain the same number of
 * segments. No wildcard support (`:name` and trailing `*` are not combined).
 *
 * @return True on a full match (params captured); false otherwise.
 */
// Record one `:name` path parameter (key from the route segment, value from the path segment), each
// truncated to its buffer. No-op once the param table is full. Extracted to keep the matcher loop flat.
static void capture_path_param(HttpReq *req, const char *key, size_t klen, const char *val, size_t vlen)
{
    if (req->path_param_count >= MAX_PATH_PARAMS)
    {
        return;
    }
    QueryParam *qp = &req->path_params[req->path_param_count++];
    if (klen > QUERY_KEY_LEN - 1)
    {
        klen = QUERY_KEY_LEN - 1;
    }
    memcpy(qp->key, key, klen);
    qp->key[klen] = '\0';
    if (vlen > QUERY_VAL_LEN - 1)
    {
        vlen = QUERY_VAL_LEN - 1;
    }
    memcpy(qp->val, val, vlen);
    qp->val[vlen] = '\0';
}

static bool match_path_params(const char *route, const char *path, HttpReq *req)
{
    req->path_param_count = 0;
    const char *r = route;
    const char *p = path;

    while (*r == '/' && *p == '/')
    {
        r++;
        p++;
        const char *rseg = r;
        while (*r && *r != '/')
        {
            r++;
        }
        size_t rlen = (size_t)(r - rseg);
        const char *pseg = p;
        while (*p && *p != '/')
        {
            p++;
        }
        size_t plen = (size_t)(p - pseg);

        if (rlen > 0 && rseg[0] == ':')
        {
            if (plen == 0)
            {
                return false; // a `:name` segment must capture a non-empty value
            }
            capture_path_param(req, rseg + 1, rlen - 1, pseg, plen);
        }
        else if (rlen != plen || strncmp(rseg, pseg, rlen) != 0)
        {
            return false; // literal segment mismatch
        }
    }

    // Both strings must be fully consumed (identical segment counts).
    return (*r == '\0' && *p == '\0');
}

/**
 * @brief Main application tick - tick the session layer then dispatch completed requests.
 *
 * Call this repeatedly from loop().  Each call:
 *   1. Calls server_tick() which runs timeout sweeps + drains the event queue.
 *   2. Walks all slots; any in ParseState::PARSE_COMPLETE is dispatched via match_and_execute().
 *   3. Any slot left in ParseState::PARSE_COMPLETE after dispatch (i.e., callback did not
 *      send a response) is reset so it doesn't block the slot.
 *   4. Any slot in ParseState::PARSE_ERROR receives an automatic 400 response.
 *   5. Any slot in ParseState::PARSE_ENTITY_TOO_LARGE receives an automatic 413 response.
 *   6. Any slot in ParseState::PARSE_URI_TOO_LONG receives an automatic 414 response.
 */
#if PC_ENABLE_WEBSOCKET
void ws_dispatch_message(const WsConn *ws)
{
    for (uint8_t r = 0; r < pc_route_count(); r++)
    {
        if (pc_route_at(r)->type == RouteType::ROUTE_WS && pc_route_at(r)->ws_message)
        {
            pc_route_at(r)->ws_message(ws->ws_id);
            break;
        }
    }
}

void ws_dispatch_close(const WsConn *ws)
{
    for (uint8_t r = 0; r < pc_route_count(); r++)
    {
        if (pc_route_at(r)->type == RouteType::ROUTE_WS && pc_route_at(r)->ws_close)
        {
            pc_route_at(r)->ws_close(ws->ws_id);
            break;
        }
    }
}
#endif // PC_ENABLE_WEBSOCKET

void handle()
{
#ifdef ARDUINO
    // The worker task drives the pipeline on its own core; loop() is freed.
    if (pc_workers_running())
    {
        return;
    }
#endif
    service_once();
}

void service_once(int worker_id)
{
    // Wire HTTP's instance-bound poll to the server currently being serviced, so the dispatch loop
    // pumps HTTP through the uniform ProtoHandler::on_poll seam (see http_poll_slot). Done here (not
    // just begin()) so test paths that drive service_once() directly also install it, and so it always
    // targets the running instance. Two pointer stores; negligible at poll cadence.
    http_proto_set_poll(pc_http_on_poll);

    server_tick(worker_id);

#if PC_ENABLE_HTTP3
    // Drive the QUIC/HTTP-3 server: ingest queued datagrams, run the engines (which dispatch requests
    // through this instance's routes), flush replies. One worker owns it, so requests stay single-threaded.
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
        pc_conn_ack_consumed(i);

        // Every protocol - HTTP included - is pumped through the one uniform ProtoHandler::on_poll
        // seam (no per-protocol branch here). HTTP's poll is instance-bound (it dispatches into this
        // server's routes), installed at begin() via http_proto_set_poll() -> http_poll_slot(); the
        // singleton pollers (SSH etc.) gate on ConnState::CONN_ACTIVE inside their own on_poll.
        const ProtoHandler *ph = proto_get(conn_pool[i].proto);
        if (ph && ph->on_poll)
        {
            ph->on_poll(i);
        }
    }

    // Run any callbacks app code deferred to this worker (race-free push path).
    pc_worker_run_deferred(worker_id);
}

// HTTP's instance-bound poll pump. Installed as the HTTP ProtoHandler's on_poll at begin() (via
// http_proto_set_poll) so the worker dispatch loop pumps HTTP through the same uniform seam as every
// other protocol - no HTTP special case in the loop. Runs the file/chunk send pumps, the WebSocket +
// SSE drains, the keep-alive re-parse, and dispatches a completed request into this server's routes.
#if PC_ENABLE_EDGE_CACHE
// Edge-cache async-fetch pump seam (see pc_http_set_edge_poll / services/web/edge_cache/edge_cache_proxy):
// a cache miss suspends the client request and drives the non-blocking origin fetch from this slot's poll.
static bool (*s_edge_poll)(uint8_t slot) = nullptr;
void pc_http_set_edge_poll(bool (*fn)(uint8_t slot))
{
    s_edge_poll = fn;
}
#endif

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
                    if (ws->parse_state == WsParseState::WS_FRAME_READY)
                    {
                        ws_dispatch_message(ws);
                        ws_reset_frame(ws);
                    }
                    else if (ws->parse_state == WsParseState::WS_CLOSED || ws->parse_state == WsParseState::WS_ERROR)
                    {
                        break;
                    }
                }
                if (ws->parse_state == WsParseState::WS_CLOSED || ws->parse_state == WsParseState::WS_ERROR)
                {
                    break;
                }
            }
            if (ws->parse_state == WsParseState::WS_CLOSED || ws->parse_state == WsParseState::WS_ERROR || n < 0)
            {
                ws_dispatch_close(ws);
                ws_free(i);
                pc_conn_abort_slot(i); // transport owns TLS-free + detach + reset + RST
                http_reset(i);
            }
            return;
        }
#endif // PC_ENABLE_TLS

        ws_parse(ws);

        if (ws->parse_state == WsParseState::WS_FRAME_READY)
        {
            ws_dispatch_message(ws);
            ws_reset_frame(ws);
        }
        else if (ws->parse_state == WsParseState::WS_CLOSED || ws->parse_state == WsParseState::WS_ERROR)
        {
            ws_dispatch_close(ws);
            ws_free(i);
            // RFC 6455 5.5.1: close the underlying TCP connection after the close
            // handshake. begin_close moves the slot out of ConnState::CONN_ACTIVE so the
            // post-close bytes are NOT re-parsed as a new HTTP request (the
            // close-frame the WS layer queued still flushes during the dwell).
            pc_conn_begin_close(i);
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
    // (pipelined) request in its ring buffer with no new EvtType::EVT_DATA to trigger a
    // parse. Drain it here each tick so it gets dispatched. TLS slots are
    // skipped - their ring holds ciphertext, decrypted in the session layer.
    if (conn_pool[i].state == ConnState::CONN_ACTIVE && http_pool[i].parse_state != ParseState::PARSE_COMPLETE
#if PC_ENABLE_TLS
        && !conn_pool[i].tls
#endif
    )
        http_parse(i);
#endif

#if PC_REQUEST_TIMEOUT_MS > 0
    // Slow-loris defense (the nginx client_header_timeout semantic): bound the request HEADER phase. A
    // connection that sent its first byte but has not completed its request headers within
    // PC_REQUEST_TIMEOUT_MS is answered 408 and closed, freeing the slot. Unlike the idle timeout, req_start_ms
    // is NOT reset by a trickle byte (it is armed once, on the first RX byte), so a drip-fed partial header
    // cannot hold a slot open indefinitely - the connection-slot DoS the user reproduced. The deadline is
    // scoped to the header phase (parse_state < PARSE_BODY, since every header state precedes PARSE_BODY in the
    // enum) so it never reaps a legitimate slow body: a large streaming upload sits in PARSE_BODY for its whole
    // duration and is governed by the streaming handler + idle timer, not this deadline. WebSocket / SSE were
    // already returned above.
    if (conn_pool[i].state == ConnState::CONN_ACTIVE && conn_pool[i].req_start_ms != 0 &&
        http_pool[i].parse_state < ParseState::PARSE_BODY &&
        (pc_millis() - conn_pool[i].req_start_ms) >= PC_REQUEST_TIMEOUT_MS)
    {
        conn_pool[i].req_start_ms = 0;
        send_text(i, 408, PC_MIME_TEXT_PLAIN, "Request Timeout"); // terminal error response -> connection closes
        return;
    }
#endif

    // HTTP slot
    if (http_pool[i].parse_state == ParseState::PARSE_COMPLETE)
    {
        conn_pool[i].req_start_ms = 0; // request complete: disarm; the next keep-alive request re-arms on its 1st byte
        match_and_execute(i);
        if (http_pool[i].parse_state == ParseState::PARSE_COMPLETE)
        {
            http_reset(i);
        }
    }
    else if (http_pool[i].parse_state == ParseState::PARSE_ERROR)
    {
        send_text(i, 400, PC_MIME_TEXT_PLAIN, "Bad Request");
    }
    else if (http_pool[i].parse_state == ParseState::PARSE_ENTITY_TOO_LARGE)
    {
        send_text(i, 413, PC_MIME_TEXT_PLAIN, "Payload Too Large");
    }
    else if (http_pool[i].parse_state == ParseState::PARSE_URI_TOO_LONG)
    {
        send_text(i, 414, PC_MIME_TEXT_PLAIN, "URI Too Long");
    }
}

bool defer(uint8_t slot, pc_deferred_fn fn, void *arg)
{
    if (slot >= MAX_CONNS)
    {
        return false;
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
// It is a frame like every other frame here, so the conversions come from the one engine that is
// tested rather than from a private set of preprocessor pastes.
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
    PlaintextScope scope;
    char *doc = (char *)pc_plaintext_alloc(PC_PLAINTEXT_WORK_DIAG, 1);
    if (doc == nullptr ||
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
        send_text(slot_id, 503, PC_MIME_TEXT_PLAIN, ""); // fail closed: no partial document reaches the wire
        return;
    }
    send_text(slot_id, 200, PC_MIME_JSON, doc);
}
#endif

// ---------------------------------------------------------------------------
// Route dispatch
// ---------------------------------------------------------------------------

// True when the request on this slot used the HEAD method, whose response must
// carry the same headers as GET but no message body (RFC 7231 §4.3.2). External
// linkage (declared in server/protocore_internal.h): the split handler TUs call it.
bool req_is_head(uint8_t slot_id)
{
    return strcmp(http_pool[slot_id].method, "HEAD") == 0;
}

// Append a method token to a comma-separated Allow list, de-duplicating.
static void allow_append(char *buf, size_t cap, const char *m)
{
    if (!m[0] || strstr(buf, m))
    {
        return;
    }
    size_t len = strnlen(buf, cap);
    if (len == 0)
    {
        pc_sb sb_buf = {buf, cap, 0, true};
        pc_sb_put(&sb_buf, m);
        if (pc_sb_finish(&sb_buf) == 0)
        {
            buf[0] = '\0';
        }
    }
    else
    {
        pc_sb sb1300 = {buf + len, cap - len, 0, true};
        pc_sb_put(&sb1300, ", ");
        pc_sb_put(&sb1300, m);
        if (pc_sb_finish(&sb1300) == 0)
        {
            sb1300.p[0] = '\0';
        }
    }
}

// Send a terminal text/plain error response that closes the connection: the
// status reason (e.g. "405 Method Not Allowed"), one optional pre-formatted extra
// header (CRLF-terminated, e.g. "Allow: GET\r\n"), then Content-Type/Length and
// "Connection: close". Begins the ConnState::CONN_CLOSING dwell so the bytes drain before
// teardown; HEAD omits the body. One owner for the error-and-close path.
static void send_error_close(uint8_t slot_id, const char *status, const char *extra_hdr, const char *body)
{
    TcpConn *conn = &conn_pool[slot_id];
    if (conn->state != ConnState::CONN_ACTIVE || conn->pcb == nullptr)
    {
        http_reset(slot_id);
        return;
    }

    int blen = (int)strnlen(body, 0xFFFF);
    char header[RESP_HDR_BUF_SIZE];
    // GCOVR_EXCL_BR_START  the null arm of the extra_hdr ternary is unreachable: both callers
    // (send_method_not_allowed, send_too_many_requests) build a non-null header string. Kept so the
    // parameter stays optional for a future caller with nothing extra to add.
    pc_sb sb_header = {header, sizeof(header), 0, true};
    pc_sb_put(&sb_header, "HTTP/1.1 ");
    pc_sb_put(&sb_header, status);
    pc_sb_put(&sb_header, "\r\n");
    pc_sb_put(&sb_header, extra_hdr ? extra_hdr : "");
    pc_sb_put(&sb_header, "Content-Type: ");
    pc_sb_put(&sb_header, PC_MIME_TEXT_PLAIN);
    pc_sb_put(&sb_header, "\r\nContent-Length: ");
    pc_sb_i64(&sb_header, (int64_t)(blen));
    pc_sb_put(&sb_header, "\r\nConnection: close\r\n\r\n");
    int hlen = (int)pc_sb_finish(&sb_header);
    // GCOVR_EXCL_BR_STOP

    // The last write carries the flush (pc_conn_send_flush = write+tcp_output in one marshal), so
    // an error-and-close costs one round-trip fewer - and 4xx/5xx closes are the hot path under a
    // flood (rate-limit / auth-lockout 429s).
    // GCOVR_EXCL_BR_START  the blen == 0 half is unreachable: both callers pass a fixed non-empty
    // reason body ("Method Not Allowed" / "Too Many Requests"). The HEAD half is exercised.
    if (blen > 0 && !req_is_head(slot_id))
    {
        pc_conn_send(slot_id, header, (u16_t)hlen);
        pc_conn_send_flush(slot_id, body, (u16_t)blen);
    }
    else
    {
        pc_conn_send_flush(slot_id, header, (u16_t)hlen);
    }
    // GCOVR_EXCL_BR_STOP
    pc_conn_begin_close(slot_id); // dwell in ConnState::CONN_CLOSING until the response drains
    http_reset(slot_id);
}

// Send 405 Method Not Allowed with the required Allow header (RFC 7231 §6.5.5).
static void send_method_not_allowed(uint8_t slot_id, const char *allow)
{
    char extra[80];
    pc_sb sb_extra = {extra, sizeof(extra), 0, true};
    pc_sb_put(&sb_extra, "Allow: ");
    pc_sb_put(&sb_extra, allow);
    pc_sb_put(&sb_extra, "\r\n");
    if (pc_sb_finish(&sb_extra) == 0)
    {
        extra[0] = '\0';
    }
    send_error_close(slot_id, "405 Method Not Allowed", extra, "Method Not Allowed");
}

#if PC_ENABLE_AUTH_LOCKOUT
// The peer's family-tagged source address for the connection in slot_id (unspecified on native /
// no pcb). Used as the auth-lockout bucket key - the full IPv4 or IPv6 address, so a v6 peer is
// never flattened onto a shared v4 bucket nor folded into a collideable hash.
static pc_ip lockout_client_ip(uint8_t slot_id)
{
    pc_ip ip;
    ip.family = pc_ip_family::PC_IP_NONE;
    pc_conn_remote_addr(slot_id, &ip);
    return ip;
}

// 429 Too Many Requests with Retry-After (auth lockout active). Closes the
// connection - mirrors send_method_not_allowed's PCB lifecycle.
static void send_too_many_requests(uint8_t slot_id, uint32_t retry_after_s)
{
    char extra[40];
    pc_sb sb_extra2 = {extra, sizeof(extra), 0, true};
    pc_sb_put(&sb_extra2, "Retry-After: ");
    pc_sb_u32(&sb_extra2, (uint32_t)((unsigned long)retry_after_s));
    pc_sb_put(&sb_extra2, "\r\n");
    if (pc_sb_finish(&sb_extra2) == 0)
    {
        extra[0] = '\0';
    }
    send_error_close(slot_id, "429 Too Many Requests", extra, "Too Many Requests");
}
#endif // PC_ENABLE_AUTH_LOCKOUT

bool route_admits(const Route *r, uint8_t slot_id, HttpReq *req)
{
    // GCOVR_EXCL_START  unreachable: every entry below route_count was filled by fill_route_base,
    // which sets is_active, and nothing ever clears it - so the dispatcher never walks an inactive
    // slot. Kept because is_active is what makes an unfilled table entry safe to skip.
    if (!r->is_active)
    {
        return false;
    }
    // GCOVR_EXCL_STOP
    bool matched = r->is_regex   ? regex_match(r->path, req->path)
                   : r->is_param ? match_path_params(r->path, req->path, req)
                                 : path_matches(r->path, r->is_wildcard, req->path);
    if (!matched)
    {
        return false;
    }
    // Per-route interface gate: a route bound to STA/AP is invisible on the
    // other interface (falls through to other routes / 404).
    if (r->iface_filter != pc_iface::PC_IFACE_ANY && r->iface_filter != pc_conn_iface(slot_id))
    {
        return false;
    }
    return true;
}

#if PC_ENABLE_CSRF
bool pc_csrf_gate(uint8_t slot_id, HttpReq *req, HttpMethod method)
{
    // Built-in token endpoint: GET /csrf issues a signed token (also set as the
    // csrf cookie) for clients to echo in X-CSRF-Token on state-changing requests.
    if (method == HttpMethod::HTTP_GET && strcmp(req->path, "/csrf") == 0)
    {
        char tok[CSRF_TOKEN_BUF];
        if (pc_csrf_issue(tok, sizeof(tok)) > 0)
        {
            set_cookie(slot_id, "csrf", tok, "Path=/; SameSite=Strict");
            char body[CSRF_TOKEN_BUF + 16];
            pc_sb sb_body = {body, sizeof(body), 0, true};
            pc_sb_put(&sb_body, "{\"token\":\"");
            pc_sb_put(&sb_body, tok);
            pc_sb_put(&sb_body, "\"}");
            if (pc_sb_finish(&sb_body) == 0)
            {
                body[0] = '\0';
            }
            send_text(slot_id, 200, PC_MIME_JSON, body);
        }
        else
        {
            send_text(slot_id, 500, PC_MIME_TEXT_PLAIN, "CSRF unavailable");
        }
        return true;
    }

    // Enforce CSRF on every state-changing method: require a valid signed
    // X-CSRF-Token header (GET / HEAD / OPTIONS are exempt - not state-changing).
    if (method == HttpMethod::HTTP_POST || method == HttpMethod::HTTP_PUT || method == HttpMethod::HTTP_PATCH ||
        method == HttpMethod::HTTP_DELETE)
    {
        const char *tok = http_get_header(req, "X-CSRF-Token");
        if (!tok || !pc_csrf_verify(tok))
        {
            send_text(slot_id, 403, PC_MIME_TEXT_PLAIN, "CSRF token missing or invalid");
            return true;
        }
    }
    return false;
}
#endif // PC_ENABLE_CSRF

#if PC_ENABLE_WEBSOCKET
void handle_ws_route(uint8_t slot_id, HttpReq *req, HttpMethod method, const Route *r)
{
    const char *upgrade_hdr = http_get_header(req, "Upgrade");
    // RFC 6455 4.2.1: a valid handshake needs Upgrade: websocket AND a Connection
    // header that includes the "Upgrade" token.
    bool is_ws_upgrade = (method == HttpMethod::HTTP_GET) && upgrade_hdr &&
                         (strcasecmp(upgrade_hdr, "websocket") == 0) &&
                         conn_has_token(http_get_header(req, "Connection"), "upgrade");
    if (!is_ws_upgrade)
    {
        send_text(slot_id, 400, PC_MIME_TEXT_PLAIN, "WebSocket upgrade required");
        return;
    }
    // RFC 6455 §4.2.1: only version 13 is supported; otherwise 426.
    const char *ws_ver = http_get_header(req, "Sec-WebSocket-Version");
    if (!ws_ver || strcmp(ws_ver, "13") != 0)
    {
        ws_send_version_required(slot_id);
        return;
    }
    // A failed upgrade here means a malformed/oversized Sec-WebSocket-Key (a
    // client error, RFC 6455 4.2.1), so answer 400 rather than 503.
    if (!ws_do_upgrade(slot_id, req, r->ws_connect))
    {
        send_text(slot_id, 400, PC_MIME_TEXT_PLAIN, "Bad WebSocket handshake");
    }
}
#endif // PC_ENABLE_WEBSOCKET

#if PC_ENABLE_AUTH
bool authorize_request(uint8_t slot_id, HttpReq *req, const Route *r)
{
#if PC_ENABLE_AUTH_LOCKOUT
    pc_ip cip = lockout_client_ip(slot_id);
#if PC_ENABLE_FORWARDED_TRUST
    // Behind a trusted reverse proxy, key the lockout on the original client (the proxy's Forwarded /
    // X-Forwarded-For), not the proxy's shared TCP address. Ignored for a direct/untrusted peer, so a
    // spoofed header can neither evade a lockout nor frame another address.
    {
        char fbuf[PC_IP_STR_MAX];
        const char *fwd = http_forwarded_client(req, fbuf, sizeof(fbuf), nullptr) ? fbuf : nullptr;
        pc_ip eff;
        pc_forwarded_effective_ip(&cip, fwd, &eff);
        cip = eff;
    }
#endif
    uint32_t now = (uint32_t)millis();
    uint32_t remain = auth_lockout_remaining_ms(&cip, now);
    if (remain > 0)
    {
        // Address is locked out: 429 + Retry-After, no credential check.
        send_too_many_requests(slot_id, (remain + 999) / 1000);
        return false;
    }
#endif
    bool stale = false;
    bool ok = r->auth_digest ? check_digest_auth(slot_id, req, r, &stale) : check_basic_auth(slot_id, req, r);
#if PC_ENABLE_AUTH_LOCKOUT
    // A stale-nonce retry carries valid credentials, so it is not a failed
    // attempt: don't count it toward the lockout (nor reset the counter).
    if (ok)
    {
        auth_lockout_succeed(&cip);
    }
    else if (!stale)
    {
        auth_lockout_fail(&cip, now);
    }
#endif
    if (!ok)
    {
        send_unauth(slot_id, r, stale);
        return false;
    }
    return true;
}
#endif // PC_ENABLE_AUTH

bool dispatch_matched_route(uint8_t slot_id, HttpReq *req, HttpMethod method, Route *r, bool *path_matched,
                            char *allow_buf, size_t allow_cap)
{
#if PC_ENABLE_WEBSOCKET
    if (r->type == RouteType::ROUTE_WS)
    {
        handle_ws_route(slot_id, req, method, r);
        return true;
    }
#endif // PC_ENABLE_WEBSOCKET

#if PC_ENABLE_SSE
    if (r->type == RouteType::ROUTE_SSE)
    {
        if (!pc_sse_do_upgrade(slot_id, req, r->pc_sse_connect))
        {
            send_text(slot_id, 503, PC_MIME_TEXT_PLAIN, "Service Unavailable");
        }
        return true;
    }
#endif // PC_ENABLE_SSE

#if PC_ENABLE_FILE_SERVING
    if (r->type == RouteType::ROUTE_STATIC)
    {
        // Static mounts answer GET (and HEAD via GET); other methods → 405.
        if (method != HttpMethod::HTTP_GET && method != HttpMethod::HTTP_HEAD)
        {
            *path_matched = true;
            allow_append(allow_buf, allow_cap, "GET");
            allow_append(allow_buf, allow_cap, "HEAD");
            return false;
        }
        serve_static_request(slot_id, req, r);
        return true;
    }
#endif // PC_ENABLE_FILE_SERVING

    // RouteType::ROUTE_HTTP - a HEAD request is served by the GET handler with the
    // response body suppressed (RFC 7231 §4.3.2).
    bool method_ok = (r->method == method) || (method == HttpMethod::HTTP_HEAD && r->method == HttpMethod::HTTP_GET);
    if (!method_ok)
    {
        // Path matches but method differs - record it for a 405 + Allow.
        *path_matched = true;
        allow_append(allow_buf, allow_cap, method_name(r->method));
        // A GET route also answers HEAD, so advertise it in Allow.
        if (r->method == HttpMethod::HTTP_GET)
        {
            allow_append(allow_buf, allow_cap, "HEAD");
        }
        return false;
    }
#if PC_ENABLE_AUTH
    if (r->auth_required && !authorize_request(slot_id, req, r))
    {
        return true; // 401/429 already sent
    }
#endif // PC_ENABLE_AUTH
    r->callback(slot_id, req);
    return true;
}

void match_and_execute(uint8_t slot_id)
{
    HttpReq *req = &http_pool[slot_id];
    HttpMethod method = parse_method(req->method);

    // Start each request with no carried-over custom response headers or
    // captured path parameters.
    pc_resp_extra_hdr(slot_id)[0] = '\0';
    req->path_param_count = 0;

    // Built-in rate limiter first (cheapest rejection under flood), then the
    // user middleware chain. Either may short-circuit with a response.
    if (rate_limit_check(slot_id))
    {
        return;
    }
    if (run_middleware(slot_id, req))
    {
        return;
    }

#if PC_ENABLE_WEBDAV
    // A WebDAV mount owns its whole subtree and every method on it (including
    // PROPFIND/MKCOL/etc., which parse_method() does not recognize), so intercept
    // before the unknown-method 501 and the normal route loop.
    if (try_serve_dav(slot_id, req))
    {
        return;
    }
#endif

    // CORS preflight
    if (method == HttpMethod::HTTP_OPTIONS && pc_resp_cors_enabled())
    {
        send_empty(slot_id, 204);
        return;
    }

#if PC_ENABLE_CSRF
    if (pc_csrf_gate(slot_id, req, method))
    {
        return;
    }
#endif

    // RFC 7230 §3.3.1: reject Transfer-Encoding
    if (http_get_header(req, "Transfer-Encoding") != nullptr)
    {
        send_text(slot_id, 501, PC_MIME_TEXT_PLAIN, "Not Implemented");
        return;
    }

    // RFC 7231 §6.5.2: a method the server does not implement → 501.
    if (method == HttpMethod::HTTP_METHOD_UNKNOWN)
    {
        send_text(slot_id, 501, PC_MIME_TEXT_PLAIN, "Not Implemented");
        return;
    }

    // For RFC 7231 §6.5.5: if a path matches but no method does, answer 405
    // with an Allow header listing the methods registered for that path.
    bool path_matched = false;
    char allow_buf[64];
    allow_buf[0] = '\0';

    for (uint8_t i = 0; i < pc_route_count(); i++)
    {
        Route *r = pc_route_at(i);
        if (!route_admits(r, slot_id, req))
        {
            continue;
        }
        if (dispatch_matched_route(slot_id, req, method, r, &path_matched, allow_buf, sizeof(allow_buf)))
        {
            return;
        }
    }

    // Path existed but the method was not allowed (RFC 7231 §6.5.5).
    if (path_matched)
    {
        send_method_not_allowed(slot_id, allow_buf);
        return;
    }

    if (s_inst.not_found_handler)
    {
        s_inst.not_found_handler(slot_id, req);
    }
    else
    {
        send_text(slot_id, 404, PC_MIME_TEXT_PLAIN, "Not Found");
    }
}

/*
 * Build and transmit an HTTP response with a body.
 *
 * Uses a 512-byte stack buffer for headers.  CORS headers are appended when
 * `pc_resp_cors_enabled()`.  The slot is freed (state → ConnState::CONN_FREE, pcb → nullptr)
 * *before* the tcp_write + tcp_close sequence to ensure any error callback
 * that lwIP fires during the write sees the slot as already released.
 *
 * If the slot's connection is not active (e.g., already timed-out or the
 * PCB is null) the slot is reset and the function returns without writing.
 *
 * @param slot_id      Connection slot index.
 * @param code         HTTP status code, e.g. 200.
 * @param content_type MIME type string, e.g. "application/json".
 * @param payload      Null-terminated body string to send.
 */
void send_text(uint8_t slot_id, int code, const char *content_type, const char *payload)
{
    // Null-terminated convenience wrapper over the explicit-length send.
    send_text(slot_id, code, content_type, (const uint8_t *)payload, payload ? strnlen(payload, 0xFFFF) : 0);
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
    if (conn->state != ConnState::CONN_ACTIVE || conn->pcb == nullptr)
    {
        http_reset(slot_id);
        return;
    }

    int payload_len = (int)(body_len > 0xFFFF ? 0xFFFF : body_len);

    bool keep;
    const char *cl = pc_resp_conn_hdr(slot_id, &keep);

    char header[RESP_HDR_BUF_SIZE];
    pc_sb sb_header2 = {header, sizeof(header), 0, true};
    pc_sb_put(&sb_header2, "HTTP/1.1 ");
    pc_sb_i64(&sb_header2, (int64_t)(code));
    pc_sb_put(&sb_header2, " ");
    pc_sb_put(&sb_header2, status_text(code));
    pc_sb_put(&sb_header2, "\r\nContent-Type: ");
    pc_sb_put(&sb_header2, content_type);
    pc_sb_put(&sb_header2, "\r\nContent-Length: ");
    pc_sb_i64(&sb_header2, (int64_t)(payload_len));
    pc_sb_put(&sb_header2, "\r\n");
    int hlen = (int)pc_sb_finish(&sb_header2);
    hlen = append_resp_trailer(header, sizeof(header), hlen, slot_id, cl);
    if (hlen == 0)
    {
        // The headers do not fit RESP_HDR_BUF_SIZE (an over-long content type, or a custom-header
        // block that filled the buffer). Truncating them would emit a header block with no
        // terminating CRLF and desync the connection, so a fixed reply that always fits goes out
        // instead and the connection closes.
        pc_conn_send_flush(slot_id, PC_RESP_HDR_OVERFLOW, (u16_t)PC_RESP_HDR_OVERFLOW_LEN);
        pc_resp_end(slot_id, 500, 0, false);
        return;
    }

    // The slot stays ConnState::CONN_ACTIVE through the write for both paths; pc_resp_end then
    // begins the ConnState::CONN_CLOSING dwell on the close path (finalized once ACKed).

    bool head = req_is_head(slot_id);

    // HEAD responses carry the headers (incl. Content-Length) but no body. For a
    // body that fits the header scratch, coalesce headers+body into a single send
    // so the response costs one tcpip_thread round-trip instead of two. The final
    // write also carries the flush (pc_conn_send_flush), so pc_resp_end skips it -
    // a keep-alive small response is now one marshal (write+output) instead of two.
    if (!head && payload_len > 0 && (size_t)hlen + (size_t)payload_len <= sizeof(header))
    {
        memcpy(header + hlen, payload, (size_t)payload_len);
        pc_conn_send_flush(slot_id, header, (u16_t)(hlen + payload_len));
    }
    else if (!head && payload_len > 0)
    {
        pc_conn_send(slot_id, header, (u16_t)hlen);
        pc_conn_send_flush(slot_id, payload, (u16_t)payload_len);
    }
    else
    {
        pc_conn_send_flush(slot_id, header, (u16_t)hlen);
    }

    pc_resp_end(slot_id, code, payload_len, keep, /*pre_flushed=*/true);
}

/*
 * Build and transmit an HTTP response with no body.
 *
 * Used for CORS preflight (204) and any response where only status headers
 * are needed.  Behaves identically to send_text() regarding slot lifecycle and
 * PCB ownership transfer - the slot is freed before the lwIP write call.
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
    if (conn->state != ConnState::CONN_ACTIVE || conn->pcb == nullptr)
    {
        http_reset(slot_id);
        return;
    }

    bool keep;
    const char *cl = pc_resp_conn_hdr(slot_id, &keep);

    char header[RESP_HDR_BUF_SIZE];
    pc_sb sb_header3 = {header, sizeof(header), 0, true};
    pc_sb_put(&sb_header3, "HTTP/1.1 ");
    pc_sb_i64(&sb_header3, (int64_t)(code));
    pc_sb_put(&sb_header3, " ");
    pc_sb_put(&sb_header3, status_text(code));
    pc_sb_put(&sb_header3, "\r\nContent-Length: 0\r\n");
    int hlen = (int)pc_sb_finish(&sb_header3);
    hlen = append_resp_trailer(header, sizeof(header), hlen, slot_id, cl);

    pc_conn_send_flush(slot_id, header, (u16_t)hlen);

    pc_resp_end(slot_id, code, 0, keep, /*pre_flushed=*/true);
}

void redirect(uint8_t slot_id, int code, const char *location)
{
    if (slot_id >= MAX_CONNS)
    {
        return;
    }
    TcpConn *conn = &conn_pool[slot_id];
    if (conn->state != ConnState::CONN_ACTIVE || conn->pcb == nullptr)
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

    bool keep;
    const char *cl = pc_resp_conn_hdr(slot_id, &keep);

    char header[RESP_HDR_BUF_SIZE];
    pc_sb sb_header4 = {header, sizeof(header), 0, true};
    pc_sb_put(&sb_header4, "HTTP/1.1 ");
    pc_sb_i64(&sb_header4, (int64_t)(code));
    pc_sb_put(&sb_header4, " ");
    pc_sb_put(&sb_header4, status_text(code));
    pc_sb_put(&sb_header4, "\r\nLocation: ");
    pc_sb_put(&sb_header4, location);
    pc_sb_put(&sb_header4, "\r\nContent-Length: 0\r\n");
    int hlen = (int)pc_sb_finish(&sb_header4);
    hlen = append_resp_trailer(header, sizeof(header), hlen, slot_id, cl);

    pc_conn_send_flush(slot_id, header, (u16_t)hlen);

    pc_resp_end(slot_id, code, 0, keep, /*pre_flushed=*/true);
}
