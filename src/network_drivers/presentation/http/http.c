// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http.c
 * @brief The HTTP root. See http.h.
 *
 * The one symbol this file exports is @ref Http.
 */

#include "network_drivers/presentation/http/http.h"
#include "mmgr/membuild.h"  // protocore_sb: the Allow list is appended, not formatted
#include "mmgr/protostr.h"  // str: the bounded-run walks
#include "mmgr/rawmemcpy.h" // proto_raw_read: a captured segment moves into our own buffer
#include "network_drivers/presentation/http/auth/auth.h"
#include "network_drivers/presentation/http/route/http_route.h" // HttpRoutes
#include "network_drivers/transport/tcp/protocol/protocol.h"    // ConnPool: the slot a response writes on
#include "protocore.h"                                          // http_pool, and the request and route widths
#if PROTOCORE_ENABLE_AUTH_LOCKOUT
#include "server/clock/clock.h" // protocore_millis() stamps the attempt the lockout counts
#include "services/security/auth_lockout/auth_lockout.h"
#if PROTOCORE_ENABLE_FORWARDED_TRUST
#include "services/security/forwarded_trust/forwarded_trust.h"
#endif
#endif
#if PROTOCORE_ENABLE_CSRF
#include "services/security/csrf/csrf.h"
#endif

/**
 * @brief The root's compile-time storage: the handlers registered against it.
 *
 * All BSS. The blank template lives in rodata, so a reset is a copy rather than a
 * sizeof(HttpStorage) temporary.
 */
struct HttpStorage
{
    Handler not_found; ///< what a request runs when no route matched
#if PROTOCORE_ENABLE_EDGE_CACHE
    proto_bool (*edge_poll)(uint8_t slot); ///< the edge-cache async-fetch pump
#endif
};

/**
 * @brief The root's state and the calls that reach it - what HttpNs points at.
 *
 * @var HttpInternal::store  the registered handlers
 * @var HttpInternal::ns     the handle a caller sets a call's members on
 */
struct HttpInternal
{
    struct HttpStorage *store;
    HttpNs *ns;
};

static struct HttpStorage s_store;

static struct HttpInternal s_http = {.store = &s_store, .ns = &Http};

static void set_not_found(struct HttpInternal *restrict ctx)
{
    ctx->store->not_found = ctx->ns->cb;
}

// Every other owner protocore_server_reset() calls exposes this; without it a handler registered here
// outlives the reset and answers requests the route table no longer knows about.
static void reset(struct HttpInternal *restrict ctx)
{
    static const struct HttpStorage blank = {0};
    *ctx->store = blank;
}

#if PROTOCORE_ENABLE_EDGE_CACHE
// Edge-cache async-fetch pump seam (see services/web/edge_cache/edge_cache_proxy): a cache miss
// suspends the client request and drives the non-blocking origin fetch from this slot's poll.
static void set_edge_poll(struct HttpInternal *restrict ctx)
{
    ctx->store->edge_poll = ctx->ns->edge_poll;
}
#endif

/**
 * @brief Convert an HTTP status code to its standard reason phrase.
 *
 * Covers 24 codes, plus 4 more (207, 412, 423, 502) with WebDAV built in.
 * Unknown codes produce "Unknown" so callers never receive a null pointer.
 *
 * @param code HTTP status integer.
 * @return Pointer to a string-literal reason phrase; never null.
 */
static void status_text(struct HttpInternal *restrict ctx)
{
    const int code = ctx->ns->code;
    switch (code)
    {
    case 200:
        ctx->ns->text = "OK";
        return;
    case 201:
        ctx->ns->text = "Created";
        return;
    case 204:
        ctx->ns->text = "No Content";
        return;
    case 206:
        ctx->ns->text = "Partial Content";
        return;
#if PROTOCORE_ENABLE_WEBDAV
    case 207:
        ctx->ns->text = "Multi-Status";
        return;
#endif
    case 301:
        ctx->ns->text = "Moved Permanently";
        return;
    case 302:
        ctx->ns->text = "Found";
        return;
    case 303:
        ctx->ns->text = "See Other";
        return;
    case 304:
        ctx->ns->text = "Not Modified";
        return;
    case 307:
        ctx->ns->text = "Temporary Redirect";
        return;
    case 308:
        ctx->ns->text = "Permanent Redirect";
        return;
    case 400:
        ctx->ns->text = "Bad Request";
        return;
    case 401:
        ctx->ns->text = "Unauthorized";
        return;
    case 403:
        ctx->ns->text = "Forbidden";
        return;
    case 404:
        ctx->ns->text = "Not Found";
        return;
    case 405:
        ctx->ns->text = "Method Not Allowed";
        return;
    case 408:
        ctx->ns->text = "Request Timeout";
        return;
    case 409:
        ctx->ns->text = "Conflict";
        return;
#if PROTOCORE_ENABLE_WEBDAV
    case 412:
        ctx->ns->text = "Precondition Failed";
        return;
    case 423:
        ctx->ns->text = "Locked";
        return;
    case 502:
        ctx->ns->text = "Bad Gateway";
        return;
#endif
    case 413:
        ctx->ns->text = "Payload Too Large";
        return;
    case 414:
        ctx->ns->text = "URI Too Long";
        return;
    case 416:
        ctx->ns->text = "Range Not Satisfiable";
        return;
    case 429:
        ctx->ns->text = "Too Many Requests";
        return;
    case 500:
        ctx->ns->text = "Internal Server Error";
        return;
    case 501:
        ctx->ns->text = "Not Implemented";
        return;
    case 503:
        ctx->ns->text = "Service Unavailable";
        return;
    default:
        ctx->ns->text = "Unknown";
        return;
    }
}

/**
 * @brief Map a method string (from the parsed request line) to an HttpMethod enum.
 *
 * Returns HTTP_METHOD_UNKNOWN for any method the server does not implement, so the
 * dispatcher can answer 501 Not Implemented (RFC 7231 §6.5.2) instead of
 * silently treating it as GET.
 *
 * @param m Null-terminated method string, e.g. "POST".
 * @return Matching HttpMethod enum value, or HTTP_METHOD_UNKNOWN.
 */
static void parse_method(struct HttpInternal *restrict ctx)
{
    const char *m = ctx->ns->method_args.token;
    // Each compare is bounded by the token it is comparing against, not by the buffer @p m came
    // from: one more byte than the literal is already enough to decide, because a longer method
    // scans to the bound without finding its terminator and fails on length before any byte is
    // compared. That keeps this function honest about a caller it does not otherwise constrain.
    if (str.eq(m, "GET", sizeof("GET"), PROTO_FALSE))
    {
        ctx->ns->method_of = HTTP_GET;
        return;
    }
    if (str.eq(m, "POST", sizeof("POST"), PROTO_FALSE))
    {
        ctx->ns->method_of = HTTP_POST;
        return;
    }
    if (str.eq(m, "PUT", sizeof("PUT"), PROTO_FALSE))
    {
        ctx->ns->method_of = HTTP_PUT;
        return;
    }
    if (str.eq(m, "DELETE", sizeof("DELETE"), PROTO_FALSE))
    {
        ctx->ns->method_of = HTTP_DELETE;
        return;
    }
    if (str.eq(m, "PATCH", sizeof("PATCH"), PROTO_FALSE))
    {
        ctx->ns->method_of = HTTP_PATCH;
        return;
    }
    if (str.eq(m, "HEAD", sizeof("HEAD"), PROTO_FALSE))
    {
        ctx->ns->method_of = HTTP_HEAD;
        return;
    }
    if (str.eq(m, "OPTIONS", sizeof("OPTIONS"), PROTO_FALSE))
    {
        ctx->ns->method_of = HTTP_OPTIONS;
        return;
    }
    ctx->ns->method_of = HTTP_METHOD_UNKNOWN;
        return;
}

/**
 * @brief Canonical method token for an HttpMethod (for the Allow header).
 */
static void method_name(struct HttpInternal *restrict ctx)
{
    const HttpMethod m = ctx->ns->method_args.method;
    switch (m)
    {
    case HTTP_GET:
        ctx->ns->text = "GET";
        return;
    case HTTP_POST:
        ctx->ns->text = "POST";
        return;
    case HTTP_PUT:
        ctx->ns->text = "PUT";
        return;
    case HTTP_DELETE:
        ctx->ns->text = "DELETE";
        return;
    case HTTP_PATCH:
        ctx->ns->text = "PATCH";
        return;
    case HTTP_HEAD:
        ctx->ns->text = "HEAD";
        return;
    case HTTP_OPTIONS:
        ctx->ns->text = "OPTIONS";
        return;
    default:
        ctx->ns->text = "";
        return;
    }
}

/**
 * @brief Test whether a route path matches an incoming request path.
 *
 * An exact route has to match the whole path.  A wildcard route matches when
 * the path agrees with everything up to (but not including) the trailing `*`.
 *
 * @param route       Registered route path, potentially ending in `*`.
 * @param is_wildcard True when the route was registered with a trailing `*`.
 * @param req_path    Incoming request path from the parsed HTTP request line.
 * @return True if the route matches the request path.
 */
static void path_matches(struct HttpInternal *restrict ctx)
{
    const char *route = ctx->ns->route_args.route;
    const proto_bool is_wildcard = ctx->ns->route_args.is_wildcard;
    const char *req_path = ctx->ns->route_args.path;
    if (!is_wildcard)
    {
        return str.eq(route, req_path, MAX_PATH_LEN, PROTO_FALSE);
    }

    // Prefix match: compare everything up to (but not including) the '*'. A first difference AT the
    // bound is the whole prefix agreeing, which is what the scan returns when it never parts.
    size_t prefix_len = str.len(route, MAX_PATH_LEN) - 1;
    return str.diff(route, req_path, prefix_len, PROTO_FALSE) == prefix_len;
}

// Record one `:name` path parameter (key from the route segment, value from the path segment).
// No-op once the param table is full.
static void capture_path_param(HttpReq *req, const char *key, size_t klen, const char *val, size_t vlen)
{
    if (req->path_param_count >= MAX_PATH_PARAMS)
    {
        return;
    }
    QueryParam *qp = &req->path_params[req->path_param_count];
    req->path_param_count++;
    if (klen > QUERY_KEY_LEN - 1)
    {
        klen = QUERY_KEY_LEN - 1;
    }
    proto_raw_read(qp->key, key, klen);
    qp->key[klen] = '\0';
    if (vlen > QUERY_VAL_LEN - 1)
    {
        vlen = QUERY_VAL_LEN - 1;
    }
    proto_raw_read(qp->val, val, vlen);
    qp->val[vlen] = '\0';
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
static void match_path_params(struct HttpInternal *restrict ctx)
{
    const char *route = ctx->ns->route_args.route;
    const char *path = ctx->ns->route_args.path;
    HttpReq *req = ctx->ns->route_args.req;
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
                ctx->ns->ok = PROTO_FALSE;
        return; // a `:name` segment must capture a non-empty value
            }
            capture_path_param(req, rseg + 1, rlen - 1, pseg, plen);
        }
        else if (rlen != plen || str.diff(rseg, pseg, rlen, PROTO_FALSE) != rlen)
        {
            ctx->ns->ok = PROTO_FALSE;
        return; // literal segment mismatch
        }
    }

    // Both strings must be fully consumed (identical segment counts).
    return (*r == '\0' && *p == '\0');
}

// True when the request on this slot used the HEAD method, whose response must
// carry the same headers as GET but no message body (RFC 7231 §4.3.2). External
// linkage (declared in protocore.h): the split handler TUs call it.
static void req_is_head(struct HttpInternal *restrict ctx)
{
    ctx->ns->ok = str.eq(http_pool[ctx->ns->slot].method, "HEAD", sizeof("HEAD"), PROTO_FALSE);
}

// Append a method token to a comma-separated Allow list, de-duplicating.
static void allow_append(struct HttpInternal *restrict ctx)
{
    char *buf = ctx->ns->allow.buf;
    const size_t cap = ctx->ns->allow.cap;
    const char *m = ctx->ns->method_args.token;
    // method_name() hands back one of the seven method literals, so the longest of them is the
    // bound on @p m - the Allow buffer's capacity is the bound on `buf` and says nothing about it.
    //
    // The search runs to the NUL, not to the capacity: the caller sets only buf[0], so every byte
    // past the text is whatever the stack held. Scanning those could match a method that was never
    // appended and return early, and the Allow header would silently lose one.
    size_t len = str.len(buf, cap);
    if (!m[0] || str.has(buf, len, m, sizeof("OPTIONS"), PROTO_FALSE))
    {
        return;
    }
    if (len == 0)
    {
        protocore_sb sb_buf = {buf, cap, 0, PROTO_TRUE};
        protocore_sb_put(&sb_buf, m);
        if (protocore_sb_finish(&sb_buf) == 0)
        {
            buf[0] = '\0';
        }
    }
    else
    {
        protocore_sb sb1300 = {buf + len, cap - len, 0, PROTO_TRUE};
        protocore_sb_put(&sb1300, ", ");
        protocore_sb_put(&sb1300, m);
        if (protocore_sb_finish(&sb1300) == 0)
        {
            sb1300.p[0] = '\0';
        }
    }
}

// Send a terminal text/plain error response that closes the connection: the
// status reason (e.g. "405 Method Not Allowed"), one optional pre-formatted extra
// header (CRLF-terminated, e.g. "Allow: GET\r\n"), then Content-Type/Length and
// "Connection: close". Begins the CONN_CLOSING dwell so the bytes drain before
// teardown; HEAD omits the body. One owner for the error-and-close path.
static void send_error_close(uint8_t slot_id, const char *status, const char *extra_hdr, const char *body)
{
    if (conn->state != CONN_ACTIVE || conn->pcb == NULL)
    {
        http_reset(slot_id);
        return;
    }

    int blen = (int)str.len(body, 0xFFFF);
    char header[RESP_HDR_BUF_SIZE];
    // (send_method_not_allowed, send_too_many_requests) build a non-null header string. Kept so the
    // parameter stays optional for a future caller with nothing extra to add.
    protocore_sb sb_header = {header, sizeof(header), 0, PROTO_TRUE};
    protocore_sb_put(&sb_header, "HTTP/1.1 ");
    protocore_sb_put(&sb_header, status);
    protocore_sb_put(&sb_header, "\r\n");
    protocore_sb_put(&sb_header, extra_hdr ? extra_hdr : "");
    protocore_sb_put(&sb_header, "Content-Type: ");
    protocore_sb_put(&sb_header, PROTOCORE_MIME_TEXT_PLAIN);
    protocore_sb_put(&sb_header, "\r\nContent-Length: ");
    protocore_sb_i64(&sb_header, (int64_t)(blen));
    protocore_sb_put(&sb_header, "\r\nConnection: close\r\n\r\n");
    int hlen = (int)protocore_sb_finish(&sb_header);

    // The last write carries the flush: send_flush is write+output in one marshal, so
    // the response leaves in a single trip whether or not a body follows the header.
    Http.slot = slot_id;
    Http.req_is_head(Http.internal);
    if (blen > 0 && !Http.ok)
    {
        ConnPool.slot = slot_id;
        ConnPool.io.data = header;
        ConnPool.io.len = (proto_u16)hlen;
        ConnPool.send(ConnPool.internal);
        ConnPool.io.data = body;
        ConnPool.io.len = (proto_u16)blen;
        ConnPool.send_flush(ConnPool.internal);
    }
    else
    {
        ConnPool.slot = slot_id;
        ConnPool.io.data = header;
        ConnPool.io.len = (proto_u16)hlen;
        ConnPool.send_flush(ConnPool.internal);
    }
    ConnPool.slot = slot_id;
    ConnPool.begin_close(ConnPool.internal); // dwell in CONN_CLOSING until the response drains
    http_reset(slot_id);
}

// Send 405 Method Not Allowed with the required Allow header (RFC 7231 §6.5.5).
static void send_method_not_allowed(uint8_t slot_id, const char *allow)
{
    char extra[80];
    protocore_sb sb_extra = {extra, sizeof(extra), 0, PROTO_TRUE};
    protocore_sb_put(&sb_extra, "Allow: ");
    protocore_sb_put(&sb_extra, allow);
    protocore_sb_put(&sb_extra, "\r\n");
    if (protocore_sb_finish(&sb_extra) == 0)
    {
        extra[0] = '\0';
    }
    send_error_close(slot_id, "405 Method Not Allowed", extra, "Method Not Allowed");
}

#if PROTOCORE_ENABLE_AUTH_LOCKOUT
// The peer's family-tagged source address for the connection in slot_id (unspecified on native /
// no pcb). Used as the auth-lockout bucket key - the full IPv4 or IPv6 address, so a v6 peer is
// never flattened onto a shared v4 bucket nor folded into a collideable hash.
static protocore_ip lockout_client_ip(uint8_t slot_id)
{
    protocore_ip ip;
    ip.family = PROTOCORE_IP_NONE;
    ConnPool.slot = slot_id;
    ConnPool.out = &ip;
    ConnPool.remote_addr(ConnPool.internal);
    return ip;
}

// 429 Too Many Requests with Retry-After (auth lockout active). Closes the
// connection - mirrors send_method_not_allowed's PCB lifecycle.
static void send_too_many_requests(uint8_t slot_id, uint32_t retry_after_s)
{
    char extra[40];
    protocore_sb sb_extra2 = {extra, sizeof(extra), 0, PROTO_TRUE};
    protocore_sb_put(&sb_extra2, "Retry-After: ");
    protocore_sb_u32(&sb_extra2, (uint32_t)((unsigned long)retry_after_s));
    protocore_sb_put(&sb_extra2, "\r\n");
    if (protocore_sb_finish(&sb_extra2) == 0)
    {
        extra[0] = '\0';
    }
    send_error_close(slot_id, "429 Too Many Requests", extra, "Too Many Requests");
}
#endif // PROTOCORE_ENABLE_AUTH_LOCKOUT

static proto_bool route_admits(const HttpRoute *r, uint8_t slot_id, HttpReq *req)
{
    if (!r->is_active)
    {
        return PROTO_FALSE;
    }
    proto_bool matched = r->is_regex   ? regex_match(r->path, req->path)
                         : (Http.route = r->path, Http.path = req->path, Http.req = req,
                            Http.is_wildcard = r->is_wildcard,
                            r->is_param ? Http.match_path_params(Http.internal) : Http.path_matches(Http.internal),
                            Http.ok);
    if (!matched)
    {
        return PROTO_FALSE;
    }
    // Per-route interface gate: a route bound to STA/AP is invisible on the
    // other interface (falls through to other routes / 404).
    ConnPool.slot = slot_id;
    ConnPool.iface(ConnPool.internal);
    if (r->iface_filter != PROTOCORE_IF_ANY && r->iface_filter != ConnPool.if_kind)
    {
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

#if PROTOCORE_ENABLE_CSRF
static proto_bool protocore_csrf_gate(uint8_t slot_id, HttpReq *req, HttpMethod method)
{
    // Built-in token endpoint: GET /csrf issues a signed token (also set as the
    // csrf cookie) for clients to echo in X-CSRF-Token on state-changing requests.
    if (method == HTTP_GET && str.eq(req->path, "/csrf", sizeof("/csrf"), PROTO_FALSE))
    {
        char tok[CSRF_TOKEN_BUF];
        if (protocore_csrf_issue(tok, sizeof(tok)) > 0)
        {
            set_cookie(slot_id, "csrf", tok, "Path=/; SameSite=Strict");
            char body[CSRF_TOKEN_BUF + 16];
            protocore_sb sb_body = {body, sizeof(body), 0, PROTO_TRUE};
            protocore_sb_put(&sb_body, "{\"token\":\"");
            protocore_sb_put(&sb_body, tok);
            protocore_sb_put(&sb_body, "\"}");
            if (protocore_sb_finish(&sb_body) == 0)
            {
                body[0] = '\0';
            }
            send_text(slot_id, 200, PROTOCORE_MIME_JSON, body);
        }
        else
        {
            send_text(slot_id, 500, PROTOCORE_MIME_TEXT_PLAIN, "CSRF unavailable");
        }
        return PROTO_TRUE;
    }

    // Enforce CSRF on every state-changing method: require a valid signed
    // X-CSRF-Token header (GET / HEAD / OPTIONS are exempt - not state-changing).
    if (method == HTTP_POST || method == HTTP_PUT || method == HTTP_PATCH || method == HTTP_DELETE)
    {
        const char *tok = http_get_header(req, "X-CSRF-Token");
        if (!tok || !protocore_csrf_verify(tok))
        {
            send_text(slot_id, 403, PROTOCORE_MIME_TEXT_PLAIN, "CSRF token missing or invalid");
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}
#endif // PROTOCORE_ENABLE_CSRF

#if PROTOCORE_ENABLE_WEBSOCKET
static void handle_ws_route(uint8_t slot_id, HttpReq *req, HttpMethod method, const HttpRoute *r)
{
    const char *upgrade_hdr = http_get_header(req, "Upgrade");
    // RFC 6455 4.2.1: a valid handshake needs Upgrade: websocket AND a Connection
    // header that includes the "Upgrade" token.
    proto_bool is_ws_upgrade = (method == HTTP_GET) && (upgrade_hdr != NULL) &&
                               str.eq(upgrade_hdr, "websocket", sizeof("websocket"), PROTO_TRUE) &&
                               protocore_http_conn_has_token(http_get_header(req, "Connection"), "upgrade");
    if (!is_ws_upgrade)
    {
        send_text(slot_id, 400, PROTOCORE_MIME_TEXT_PLAIN, "WebSocket upgrade required");
        return;
    }
    // RFC 6455 §4.2.1: only version 13 is supported; otherwise 426.
    const char *ws_ver = http_get_header(req, "Sec-WebSocket-Version");
    if (ws_ver == NULL || !str.eq(ws_ver, "13", sizeof("13"), PROTO_FALSE))
    {
        ws_send_version_required(slot_id);
        return;
    }
    // A failed upgrade here means a malformed/oversized Sec-WebSocket-Key (a
    // client error, RFC 6455 4.2.1), so answer 400 rather than 503.
    if (!ws_do_upgrade(slot_id, req, ws_route_connect(r->ws_id)))
    {
        send_text(slot_id, 400, PROTOCORE_MIME_TEXT_PLAIN, "Bad WebSocket handshake");
    }
}
#endif // PROTOCORE_ENABLE_WEBSOCKET

#if PROTOCORE_ENABLE_AUTH
static proto_bool proto_authorize_request(uint8_t slot_id, HttpReq *req, const HttpRoute *r)
{
#if PROTOCORE_ENABLE_AUTH_LOCKOUT
    protocore_ip cip = lockout_client_ip(slot_id);
#if PROTOCORE_ENABLE_FORWARDED_TRUST
    // Behind a trusted reverse proxy, key the lockout on the original client (the proxy's Forwarded /
    // X-Forwarded-For), not the proxy's shared TCP address. Ignored for a direct/untrusted peer, so a
    // spoofed header can neither evade a lockout nor frame another address.
    {
        char fbuf[PROTOCORE_IP_STR_MAX];
        const char *fwd = http_forwarded_client(req, fbuf, sizeof(fbuf), NULL) ? fbuf : NULL;
        protocore_ip eff;
        protocore_forwarded_effective_ip(&cip, fwd, &eff);
        cip = eff;
    }
#endif
    uint32_t now = (uint32_t)protocore_millis();
    uint32_t remain = auth_lockout_remaining_ms(&cip, now);
    if (remain > 0)
    {
        // Address is locked out: 429 + Retry-After, no credential check.
        send_too_many_requests(slot_id, (remain + 999) / 1000);
        return PROTO_FALSE;
    }
#endif
    // One borrow for this request's auth decision: the digest hashes and the challenge's nonce work
    // out of it, and it goes back before the handler runs. The pool resolves the slot from the calling
    // worker, so two workers never share these bytes.
    size_t auth_mark = protocore_secure_mark();
    protocore_span auth_ws = protocore_secure_span(PROTOCORE_SHA256_BORROW, _Alignof(uint32_t));
    if (!protocore_span_ok(auth_ws))
    {
        protocore_secure_release(auth_mark);
        return PROTO_FALSE; // pool exhausted: fail closed
    }
    proto_bool stale = PROTO_FALSE;
    proto_bool ok = Auth.check(auth_ws.buf, slot_id, req, r->auth_id, &stale);
#if PROTOCORE_ENABLE_AUTH_LOCKOUT
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
        Auth.challenge(auth_ws.buf, slot_id, r->auth_id, stale);
        protocore_secure_release(auth_mark);
        return PROTO_FALSE;
    }
    protocore_secure_release(auth_mark);
    return PROTO_TRUE;
}
#endif // PROTOCORE_ENABLE_AUTH

static proto_bool dispatch_matched_route(uint8_t slot_id, HttpReq *req, HttpMethod method, HttpRoute *r,
                                         proto_bool *path_matched, char *allow_buf, size_t allow_cap)
{
#if PROTOCORE_ENABLE_WEBSOCKET
    if (r->type == ROUTE_WS)
    {
        handle_ws_route(slot_id, req, method, r);
        return PROTO_TRUE;
    }
#endif // PROTOCORE_ENABLE_WEBSOCKET

#if PROTOCORE_ENABLE_SSE
    if (r->type == ROUTE_SSE)
    {
        if (!protocore_sse_do_upgrade(slot_id, req, protocore_sse_route_connect(r->sse_id)))
        {
            send_text(slot_id, 503, PROTOCORE_MIME_TEXT_PLAIN, "Service Unavailable");
        }
        return PROTO_TRUE;
    }
#endif // PROTOCORE_ENABLE_SSE

#if PROTOCORE_ENABLE_FILE_SERVING
    if (r->type == ROUTE_STATIC)
    {
        // Static mounts answer GET (and HEAD via GET); other methods → 405.
        if (method != HTTP_GET && method != HTTP_HEAD)
        {
            *path_matched = PROTO_TRUE;
            Http.buf = allow_buf;
            Http.cap = allow_cap;
            Http.token = "GET";
            Http.allow_append(Http.internal);
            Http.token = "HEAD";
            Http.allow_append(Http.internal);
            return PROTO_FALSE;
        }
        serve_static_request(slot_id, req, r);
        return PROTO_TRUE;
    }
#endif // PROTOCORE_ENABLE_FILE_SERVING

    // ROUTE_HTTP - a HEAD request is served by the GET handler with the
    // response body suppressed (RFC 7231 §4.3.2).
    proto_bool method_ok = (r->method == method) || (method == HTTP_HEAD && r->method == HTTP_GET);
    if (!method_ok)
    {
        // Path matches but method differs - record it for a 405 + Allow.
        *path_matched = PROTO_TRUE;
        Http.method = r->method;
        Http.method_name(Http.internal);
        Http.buf = allow_buf;
        Http.cap = allow_cap;
        Http.token = Http.text;
        Http.allow_append(Http.internal);
        // A GET route also answers HEAD, so advertise it in Allow.
        if (r->method == HTTP_GET)
        {
            Http.buf = allow_buf;
            Http.cap = allow_cap;
            Http.token = "HEAD";
            Http.allow_append(Http.internal);
        }
        return PROTO_FALSE;
    }
#if PROTOCORE_ENABLE_AUTH
    if (r->auth_id != PROTOCORE_AUTH_NONE && !proto_authorize_request(slot_id, req, r))
    {
        return PROTO_TRUE; // 401/429 already sent
    }
#endif // PROTOCORE_ENABLE_AUTH
    r->callback(slot_id, req);
    return PROTO_TRUE;
}

static void match_and_execute(struct HttpInternal *restrict ctx)
{
    const uint8_t slot_id = ctx->ns->slot;
    HttpReq *req = &http_pool[slot_id];
    Http.token = req->method;
    Http.parse_method(Http.internal);
    HttpMethod method = Http.method_of;

    // Start each request with no carried-over custom response headers or
    // captured path parameters.
    protocore_resp_extra_hdr(slot_id)[0] = '\0';
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

#if PROTOCORE_ENABLE_WEBDAV
    // A WebDAV mount owns its whole subtree and every method on it (including
    // PROPFIND/MKCOL/etc., which Http.parse_method() does not recognize), so intercept
    // before the unknown-method 501 and the normal route loop.
    if (try_serve_dav(slot_id, req))
    {
        return;
    }
#endif

    // CORS preflight
    if (method == HTTP_OPTIONS && protocore_resp_cors_enabled())
    {
        send_empty(slot_id, 204);
        return;
    }

#if PROTOCORE_ENABLE_CSRF
    if (protocore_csrf_gate(slot_id, req, method))
    {
        return;
    }
#endif

    // RFC 7230 §3.3.1: reject Transfer-Encoding
    if (http_get_header(req, "Transfer-Encoding") != NULL)
    {
        send_text(slot_id, 501, PROTOCORE_MIME_TEXT_PLAIN, "Not Implemented");
        return;
    }

    // RFC 7231 §6.5.2: a method the server does not implement → 501.
    if (method == HTTP_METHOD_UNKNOWN)
    {
        send_text(slot_id, 501, PROTOCORE_MIME_TEXT_PLAIN, "Not Implemented");
        return;
    }

    // For RFC 7231 §6.5.5: if a path matches but no method does, answer 405
    // with an Allow header listing the methods registered for that path.
    proto_bool path_matched = PROTO_FALSE;
    char allow_buf[64];
    allow_buf[0] = '\0';

    for (uint8_t i = 0; i < HttpRoutes.count(); i++)
    {
        HttpRoute *r = HttpRoutes.at(i);
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

    if (ctx->store->not_found != NULL)
    {
        ctx->store->not_found(slot_id, req);
    }
    else
    {
        send_text(slot_id, 404, PROTOCORE_MIME_TEXT_PLAIN, "Not Found");
    }
}

// HTTP's poll pump, installed as the HTTP ProtoHandler's on_poll so the worker dispatch loop pumps
// HTTP through the same uniform seam as every other protocol, with no HTTP special case in the
// loop. Runs the file/chunk send pumps, the WebSocket and SSE drains, the keep-alive re-parse, and
// dispatches a completed request into the route table.
static void poll_slot(struct HttpInternal *restrict ctx)
{
    const uint8_t i = ctx->ns->slot;
#if PROTOCORE_ENABLE_EDGE_CACHE
    // An edge-cache origin fetch in flight for this slot owns it: pump the fetch and skip the rest of the
    // HTTP pipeline until it completes (and hands off to send_chunked for the cached response).
    if (ctx->store->edge_poll != NULL && ctx->store->edge_poll(i))
    {
        return;
    }
#endif
#if PROTOCORE_ENABLE_FILE_SERVING
    // A file response in flight owns the slot: page out the next window and
    // skip the rest of the pipeline until the whole body has been sent.
    if (protocore_file_holds_slot(i))
    {
        file_send_pump(i);
        return;
    }
#endif
    // Likewise a chunked response in flight: pull + frame the next window.
    if (protocore_resp_holds_slot(i))
    {
        chunk_send_pump(i);
        return;
    }

#if PROTOCORE_ENABLE_WEBSOCKET
    // WebSocket slot - drain ring buffer and dispatch ready frames
    WsConn *ws = ws_find(i);
    if (ws)
    {
#if PROTOCORE_ENABLE_TLS
        ConnPool.slot = i;
        ConnPool.tls(ConnPool.internal);
        if (ConnPool.ok)
        {
            // wss://: the bytes are ciphertext, so decrypt records here and
            // feed the frame parser, dispatching each completed frame as it
            // finishes (one TLS record may carry several WS frames).
            uint8_t tbuf[256];
            int n;
            while ((n = protocore_tls_read(i, tbuf, sizeof(tbuf))) > 0)
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
                ConnPool.slot = i;
                ConnPool.abort_slot(ConnPool.internal); // transport owns TLS-free + detach + reset + RST
                http_reset(i);
            }
            return;
        }
#endif // PROTOCORE_ENABLE_TLS

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
            ConnPool.slot = i;
            ConnPool.begin_close(ConnPool.internal);
            http_reset(i);
        }
        return; // slot is owned by WS; skip HTTP dispatch
    }
#endif // PROTOCORE_ENABLE_WEBSOCKET

#if PROTOCORE_ENABLE_SSE
    // SSE slot - connection stays open, nothing to parse from client
    if (protocore_sse_find(i))
    {
        return;
    }
#endif // PROTOCORE_ENABLE_SSE

#if PROTOCORE_ENABLE_KEEPALIVE
    // Keep-alive: a slot recycled after a response may already hold the next
    // (pipelined) request in its ring buffer with no new EVT_DATA to trigger a
    // parse. Drain it here each tick so it gets dispatched. TLS slots are
    // skipped - their ring holds ciphertext, decrypted in the session layer.
    ConnPool.slot = i;
    ConnPool.active(ConnPool.internal);
    proto_bool live = ConnPool.ok;
#if PROTOCORE_ENABLE_TLS
    ConnPool.tls(ConnPool.internal);
    live = live && !ConnPool.ok;
#endif
    if (live && http_pool[i].parse_state != PARSE_COMPLETE)
    {
        HttpConn.slot = i;
        HttpConn.parse(HttpConn.internal);
    }
#endif

#if PROTOCORE_REQUEST_TIMEOUT_MS > 0
    // Slow-loris defense (the nginx client_header_timeout semantic): bound the request HEADER phase. A
    // connection that sent its first byte but has not completed its request headers within
    // PROTOCORE_REQUEST_TIMEOUT_MS is answered 408 and closed, freeing the slot. Unlike the idle timeout, req_start_ms
    // is NOT reset by a trickle byte (it is armed once, on the first RX byte), so a drip-fed partial header
    // cannot hold a slot open indefinitely, which is the connection-slot exhaustion this bounds. The deadline is
    // scoped to the header phase (parse_state < PARSE_BODY, since every header state precedes PARSE_BODY in the
    // enum) so it never reaps a legitimate slow body: a large streaming upload sits in PARSE_BODY for its whole
    // duration and is governed by the streaming handler + idle timer, not this deadline. WebSocket / SSE were
    // already returned above.
    ConnPool.slot = i;
    ConnPool.active(ConnPool.internal);
    if (ConnPool.ok && http_req_start_ms[i] != 0 && http_pool[i].parse_state < PARSE_BODY &&
        (protocore_millis() - http_req_start_ms[i]) >= PROTOCORE_REQUEST_TIMEOUT_MS)
    {
        http_req_start_ms[i] = 0;
        send_text(i, 408, PROTOCORE_MIME_TEXT_PLAIN, "Request Timeout"); // terminal error response -> connection closes
        return;
    }
#endif

    // HTTP slot
    if (http_pool[i].parse_state == PARSE_COMPLETE)
    {
        http_req_start_ms[i] = 0; // request complete: disarm; the next keep-alive request re-arms on its 1st byte
        Http.match_and_execute(i);
        if (http_pool[i].parse_state == PARSE_COMPLETE)
        {
            http_reset(i);
        }
    }
    else if (http_pool[i].parse_state == PARSE_ERROR)
    {
        send_text(i, 400, PROTOCORE_MIME_TEXT_PLAIN, "Bad Request");
    }
    else if (http_pool[i].parse_state == PARSE_ENTITY_TOO_LARGE)
    {
        send_text(i, 413, PROTOCORE_MIME_TEXT_PLAIN, "Payload Too Large");
    }
    else if (http_pool[i].parse_state == PARSE_URI_TOO_LONG)
    {
        send_text(i, 414, PROTOCORE_MIME_TEXT_PLAIN, "URI Too Long");
    }
}

// Designated, so a member's position in the struct does not decide what it binds to.
HttpNs Http = {.status_text = status_text,
               .parse_method = parse_method,
               .method_name = method_name,
               .path_matches = path_matches,
               .match_path_params = match_path_params,
               .req_is_head = req_is_head,
               .allow_append = allow_append,
               .match_and_execute = match_and_execute,
               .set_not_found = set_not_found,
               .poll_slot = poll_slot,
               .reset = reset,
#if PROTOCORE_ENABLE_EDGE_CACHE
               .set_edge_poll = set_edge_poll,
#endif
               .internal = &s_http};
