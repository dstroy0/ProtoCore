// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http.c
 * @brief The HTTP root. See http.h.
 *
 * The one symbol this file exports is @ref Http.
 */

#include "network_drivers/presentation/http/http.h"
#include "mmgr/membuild.h"            // pc_sb: the Allow list is appended, not formatted
#include "mmgr/rawmemcpy.h"           // proto_raw_read: a captured segment moves into our own buffer
#include "protocore.h"                // http_pool, and the request and route widths
#include "network_drivers/presentation/http/auth/auth.h"
#include "shared_primitives/runops.h" // every scan and compare here

// The root's one file-scope mutable: the handler a request runs when no route matched.
typedef struct
{
    Handler not_found;
} HttpCtx;
static HttpCtx s_http;

static void set_not_found(Handler cb)
{
    s_http.not_found = cb;
}

/**
 * @brief Convert an HTTP status code to its standard reason phrase.
 *
 * Covers 24 codes, plus 4 more (207, 412, 423, 502) with WebDAV built in.
 * Unknown codes produce "Unknown" so callers never receive a null pointer.
 *
 * @param code HTTP status integer.
 * @return Pointer to a string-literal reason phrase; never null.
 */
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
 * Returns HTTP_METHOD_UNKNOWN for any method the server does not implement, so the
 * dispatcher can answer 501 Not Implemented (RFC 7231 §6.5.2) instead of
 * silently treating it as GET.
 *
 * @param m Null-terminated method string, e.g. "POST".
 * @return Matching HttpMethod enum value, or HTTP_METHOD_UNKNOWN.
 */
HttpMethod parse_method(const char *m)
{
    // Each compare is bounded by the token it is comparing against, not by the buffer @p m came
    // from: one more byte than the literal is already enough to decide, because a longer method
    // scans to the bound without finding its terminator and fails on length before any byte is
    // compared. That keeps this function honest about a caller it does not otherwise constrain.
    if (proto_eq_str(m, "GET", sizeof("GET")))
    {
        return HTTP_GET;
    }
    if (proto_eq_str(m, "POST", sizeof("POST")))
    {
        return HTTP_POST;
    }
    if (proto_eq_str(m, "PUT", sizeof("PUT")))
    {
        return HTTP_PUT;
    }
    if (proto_eq_str(m, "DELETE", sizeof("DELETE")))
    {
        return HTTP_DELETE;
    }
    if (proto_eq_str(m, "PATCH", sizeof("PATCH")))
    {
        return HTTP_PATCH;
    }
    if (proto_eq_str(m, "HEAD", sizeof("HEAD")))
    {
        return HTTP_HEAD;
    }
    if (proto_eq_str(m, "OPTIONS", sizeof("OPTIONS")))
    {
        return HTTP_OPTIONS;
    }
    return HTTP_METHOD_UNKNOWN;
}

/**
 * @brief Canonical method token for an HttpMethod (for the Allow header).
 */
const char *method_name(HttpMethod m)
{
    switch (m)
    {
    case HTTP_GET:
        return "GET";
    case HTTP_POST:
        return "POST";
    case HTTP_PUT:
        return "PUT";
    case HTTP_DELETE:
        return "DELETE";
    case HTTP_PATCH:
        return "PATCH";
    case HTTP_HEAD:
        return "HEAD";
    case HTTP_OPTIONS:
        return "OPTIONS";
    default:
        return "";
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
proto_bool path_matches(const char *route, proto_bool is_wildcard, const char *req_path)
{
    if (!is_wildcard)
    {
        return proto_eq_str(route, req_path, MAX_PATH_LEN);
    }

    // Prefix match: compare everything up to (but not including) the '*'. A first difference AT the
    // bound is the whole prefix agreeing, which is what the scan returns when it never parts.
    size_t prefix_len = proto_scan_nul(route, MAX_PATH_LEN) - 1;
    return proto_diff(route, req_path, prefix_len) == prefix_len;
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
proto_bool match_path_params(const char *route, const char *path, HttpReq *req)
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
                return PROTO_FALSE; // a `:name` segment must capture a non-empty value
            }
            capture_path_param(req, rseg + 1, rlen - 1, pseg, plen);
        }
        else if (rlen != plen || proto_diff(rseg, pseg, rlen) != rlen)
        {
            return PROTO_FALSE; // literal segment mismatch
        }
    }

    // Both strings must be fully consumed (identical segment counts).
    return (*r == '\0' && *p == '\0');
}

// True when the request on this slot used the HEAD method, whose response must
// carry the same headers as GET but no message body (RFC 7231 §4.3.2). External
// linkage (declared in protocore.h): the split handler TUs call it.
proto_bool req_is_head(uint8_t slot_id)
{
    return proto_eq_str(http_pool[slot_id].method, "HEAD", sizeof("HEAD"));
}

// Append a method token to a comma-separated Allow list, de-duplicating.
void allow_append(char *buf, size_t cap, const char *m)
{
    // method_name() hands back one of the seven method literals, so the longest of them is the
    // bound on @p m - the Allow buffer's capacity is the bound on `buf` and says nothing about it.
    //
    // The search runs to the NUL, not to the capacity: the caller sets only buf[0], so every byte
    // past the text is whatever the stack held. Scanning those could match a method that was never
    // appended and return early, and the Allow header would silently lose one.
    size_t len = proto_scan_nul(buf, cap);
    if (!m[0] || proto_has(buf, len, m, sizeof("OPTIONS")))
    {
        return;
    }
    if (len == 0)
    {
        pc_sb sb_buf = {buf, cap, 0, PROTO_TRUE};
        pc_sb_put(&sb_buf, m);
        if (pc_sb_finish(&sb_buf) == 0)
        {
            buf[0] = '\0';
        }
    }
    else
    {
        pc_sb sb1300 = {buf + len, cap - len, 0, PROTO_TRUE};
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
// "Connection: close". Begins the CONN_CLOSING dwell so the bytes drain before
// teardown; HEAD omits the body. One owner for the error-and-close path.
static void send_error_close(uint8_t slot_id, const char *status, const char *extra_hdr, const char *body)
{
    TcpConn *conn = &conn_pool[slot_id];
    if (conn->state != CONN_ACTIVE || conn->pcb == NULL)
    {
        http_reset(slot_id);
        return;
    }

    int blen = (int)proto_scan_nul(body, 0xFFFF);
    char header[RESP_HDR_BUF_SIZE];
    // (send_method_not_allowed, send_too_many_requests) build a non-null header string. Kept so the
    // parameter stays optional for a future caller with nothing extra to add.
    pc_sb sb_header = {header, sizeof(header), 0, PROTO_TRUE};
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

    // The last write carries the flush: Tcp.conn->send_flush is write+tcp_output in one marshal, so
    // the response leaves in a single trip whether or not a body follows the header.
    if (blen > 0 && !Http.req_is_head(slot_id))
    {
        Tcp.conn->send(slot_id, header, (proto_u16)hlen);
        Tcp.conn->send_flush(slot_id, body, (proto_u16)blen);
    }
    else
    {
        Tcp.conn->send_flush(slot_id, header, (proto_u16)hlen);
    }
    Tcp.conn->begin_close(slot_id); // dwell in CONN_CLOSING until the response drains
    http_reset(slot_id);
}

// Send 405 Method Not Allowed with the required Allow header (RFC 7231 §6.5.5).
static void send_method_not_allowed(uint8_t slot_id, const char *allow)
{
    char extra[80];
    pc_sb sb_extra = {extra, sizeof(extra), 0, PROTO_TRUE};
    pc_sb_put(&sb_extra, "Allow: ");
    pc_sb_put(&sb_extra, allow);
    pc_sb_put(&sb_extra, "\r\n");
    if (pc_sb_finish(&sb_extra) == 0)
    {
        extra[0] = '\0';
    }
    send_error_close(slot_id, "405 Method Not Allowed", extra, "Method Not Allowed");
}

// The peer's family-tagged source address for the connection in slot_id (unspecified on native /
// no pcb). Used as the auth-lockout bucket key - the full IPv4 or IPv6 address, so a v6 peer is
// never flattened onto a shared v4 bucket nor folded into a collideable hash.
static pc_ip lockout_client_ip(uint8_t slot_id)
{
    pc_ip ip;
    ip.family = PC_IP_NONE;
    Tcp.conn->remote_addr(slot_id, &ip);
    return ip;
}

// 429 Too Many Requests with Retry-After (auth lockout active). Closes the
// connection - mirrors send_method_not_allowed's PCB lifecycle.
static void send_too_many_requests(uint8_t slot_id, uint32_t retry_after_s)
{
    char extra[40];
    pc_sb sb_extra2 = {extra, sizeof(extra), 0, PROTO_TRUE};
    pc_sb_put(&sb_extra2, "Retry-After: ");
    pc_sb_u32(&sb_extra2, (uint32_t)((unsigned long)retry_after_s));
    pc_sb_put(&sb_extra2, "\r\n");
    if (pc_sb_finish(&sb_extra2) == 0)
    {
        extra[0] = '\0';
    }
    send_error_close(slot_id, "429 Too Many Requests", extra, "Too Many Requests");
}

static proto_bool route_admits(const Route *r, uint8_t slot_id, HttpReq *req)
{
    if (!r->is_active)
    {
        return PROTO_FALSE;
    }
    proto_bool matched = r->is_regex   ? regex_match(r->path, req->path)
                         : r->is_param ? Http.match_path_params(r->path, req->path, req)
                                       : Http.path_matches(r->path, r->is_wildcard, req->path);
    if (!matched)
    {
        return PROTO_FALSE;
    }
    // Per-route interface gate: a route bound to STA/AP is invisible on the
    // other interface (falls through to other routes / 404).
    if (r->iface_filter != PC_IFACE_ANY && r->iface_filter != pc_conn_iface(slot_id))
    {
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

static proto_bool pc_csrf_gate(uint8_t slot_id, HttpReq *req, HttpMethod method)
{
    // Built-in token endpoint: GET /csrf issues a signed token (also set as the
    // csrf cookie) for clients to echo in X-CSRF-Token on state-changing requests.
    if (method == HTTP_GET && proto_eq_str(req->path, "/csrf", sizeof("/csrf")))
    {
        char tok[CSRF_TOKEN_BUF];
        if (pc_csrf_issue(tok, sizeof(tok)) > 0)
        {
            set_cookie(slot_id, "csrf", tok, "Path=/; SameSite=Strict");
            char body[CSRF_TOKEN_BUF + 16];
            pc_sb sb_body = {body, sizeof(body), 0, PROTO_TRUE};
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
        return PROTO_TRUE;
    }

    // Enforce CSRF on every state-changing method: require a valid signed
    // X-CSRF-Token header (GET / HEAD / OPTIONS are exempt - not state-changing).
    if (method == HTTP_POST || method == HTTP_PUT || method == HTTP_PATCH || method == HTTP_DELETE)
    {
        const char *tok = http_get_header(req, "X-CSRF-Token");
        if (!tok || !pc_csrf_verify(tok))
        {
            send_text(slot_id, 403, PC_MIME_TEXT_PLAIN, "CSRF token missing or invalid");
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

static void handle_ws_route(uint8_t slot_id, HttpReq *req, HttpMethod method, const Route *r)
{
    const char *upgrade_hdr = http_get_header(req, "Upgrade");
    // RFC 6455 4.2.1: a valid handshake needs Upgrade: websocket AND a Connection
    // header that includes the "Upgrade" token.
    proto_bool is_ws_upgrade = (method == HTTP_GET) && (upgrade_hdr != NULL) &&
                               proto_eq_str_ci(upgrade_hdr, "websocket", sizeof("websocket")) &&
                               pc_http_conn_has_token(http_get_header(req, "Connection"), "upgrade");
    if (!is_ws_upgrade)
    {
        send_text(slot_id, 400, PC_MIME_TEXT_PLAIN, "WebSocket upgrade required");
        return;
    }
    // RFC 6455 §4.2.1: only version 13 is supported; otherwise 426.
    const char *ws_ver = http_get_header(req, "Sec-WebSocket-Version");
    if (ws_ver == NULL || !proto_eq_str(ws_ver, "13", sizeof("13")))
    {
        ws_send_version_required(slot_id);
        return;
    }
    // A failed upgrade here means a malformed/oversized Sec-WebSocket-Key (a
    // client error, RFC 6455 4.2.1), so answer 400 rather than 503.
    if (!ws_do_upgrade(slot_id, req, ws_route_connect(r->ws_id)))
    {
        send_text(slot_id, 400, PC_MIME_TEXT_PLAIN, "Bad WebSocket handshake");
    }
}

static proto_bool proto_authorize_request(uint8_t slot_id, HttpReq *req, const Route *r)
{
#if PC_ENABLE_AUTH_LOCKOUT
    pc_ip cip = lockout_client_ip(slot_id);
#if PC_ENABLE_FORWARDED_TRUST
    // Behind a trusted reverse proxy, key the lockout on the original client (the proxy's Forwarded /
    // X-Forwarded-For), not the proxy's shared TCP address. Ignored for a direct/untrusted peer, so a
    // spoofed header can neither evade a lockout nor frame another address.
    {
        char fbuf[PC_IP_STR_MAX];
        const char *fwd = http_forwarded_client(req, fbuf, sizeof(fbuf), NULL) ? fbuf : NULL;
        pc_ip eff;
        pc_forwarded_effective_ip(&cip, fwd, &eff);
        cip = eff;
    }
#endif
    uint32_t now = (uint32_t)pc_millis();
    uint32_t remain = auth_lockout_remaining_ms(&cip, now);
    if (remain > 0)
    {
        // Address is locked out: 429 + Retry-After, no credential check.
        send_too_many_requests(slot_id, (remain + 999) / 1000);
        return PROTO_FALSE;
    }
#endif
    proto_bool stale = PROTO_FALSE;
    proto_bool ok = Auth.check(slot_id, req, r->auth_id, &stale);
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
        Auth.challenge(slot_id, r->auth_id, stale);
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

static proto_bool dispatch_matched_route(uint8_t slot_id, HttpReq *req, HttpMethod method, Route *r, proto_bool *path_matched,
                                  char *allow_buf, size_t allow_cap)
{
#if PC_ENABLE_WEBSOCKET
    if (r->type == ROUTE_WS)
    {
        handle_ws_route(slot_id, req, method, r);
        return PROTO_TRUE;
    }
#endif // PC_ENABLE_WEBSOCKET

#if PC_ENABLE_SSE
    if (r->type == ROUTE_SSE)
    {
        if (!pc_sse_do_upgrade(slot_id, req, pc_sse_route_connect(r->sse_id)))
        {
            send_text(slot_id, 503, PC_MIME_TEXT_PLAIN, "Service Unavailable");
        }
        return PROTO_TRUE;
    }
#endif // PC_ENABLE_SSE

#if PC_ENABLE_FILE_SERVING
    if (r->type == ROUTE_STATIC)
    {
        // Static mounts answer GET (and HEAD via GET); other methods → 405.
        if (method != HTTP_GET && method != HTTP_HEAD)
        {
            *path_matched = PROTO_TRUE;
            Http.allow_append(allow_buf, allow_cap, "GET");
            Http.allow_append(allow_buf, allow_cap, "HEAD");
            return PROTO_FALSE;
        }
        serve_static_request(slot_id, req, r);
        return PROTO_TRUE;
    }
#endif // PC_ENABLE_FILE_SERVING

    // ROUTE_HTTP - a HEAD request is served by the GET handler with the
    // response body suppressed (RFC 7231 §4.3.2).
    proto_bool method_ok = (r->method == method) || (method == HTTP_HEAD && r->method == HTTP_GET);
    if (!method_ok)
    {
        // Path matches but method differs - record it for a 405 + Allow.
        *path_matched = PROTO_TRUE;
        Http.allow_append(allow_buf, allow_cap, Http.method_name(r->method));
        // A GET route also answers HEAD, so advertise it in Allow.
        if (r->method == HTTP_GET)
        {
            Http.allow_append(allow_buf, allow_cap, "HEAD");
        }
        return PROTO_FALSE;
    }
#if PC_ENABLE_AUTH
    if (r->auth_id != PC_AUTH_NONE && !proto_authorize_request(slot_id, req, r))
    {
        return PROTO_TRUE; // 401/429 already sent
    }
#endif // PC_ENABLE_AUTH
    r->callback(slot_id, req);
    return PROTO_TRUE;
}

void match_and_execute(uint8_t slot_id)
{
    HttpReq *req = &http_pool[slot_id];
    HttpMethod method = Http.parse_method(req->method);

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
    // PROPFIND/MKCOL/etc., which Http.parse_method() does not recognize), so intercept
    // before the unknown-method 501 and the normal route loop.
    if (try_serve_dav(slot_id, req))
    {
        return;
    }
#endif

    // CORS preflight
    if (method == HTTP_OPTIONS && pc_resp_cors_enabled())
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
    if (http_get_header(req, "Transfer-Encoding") != NULL)
    {
        send_text(slot_id, 501, PC_MIME_TEXT_PLAIN, "Not Implemented");
        return;
    }

    // RFC 7231 §6.5.2: a method the server does not implement → 501.
    if (method == HTTP_METHOD_UNKNOWN)
    {
        send_text(slot_id, 501, PC_MIME_TEXT_PLAIN, "Not Implemented");
        return;
    }

    // For RFC 7231 §6.5.5: if a path matches but no method does, answer 405
    // with an Allow header listing the methods registered for that path.
    proto_bool path_matched = PROTO_FALSE;
    char allow_buf[64];
    allow_buf[0] = '\0';

    for (uint8_t i = 0; i < network.route->count(); i++)
    {
        Route *r = network.route->at(i);
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

    if (s_http.not_found != NULL)
    {
        s_http.not_found(slot_id, req);
    }
    else
    {
        send_text(slot_id, 404, PC_MIME_TEXT_PLAIN, "Not Found");
    }
}

const HttpNs Http = {status_text,       parse_method, method_name,      path_matches,
                     match_path_params, req_is_head,  allow_append,     match_and_execute, set_not_found};
