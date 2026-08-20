// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http_client.c
 * @brief Layer 7 - the user agent (RFC 9110 sec 3.5): target URI split, request build, response
 *        frame, and the exchange that runs them over the shared outbound transport.
 *
 * RFC 9112 sec 2.1 gives the message its shape: a start-line, field lines, an empty line, and an
 * optional message body. Each pure call owns one part of that. The exchange runs them in order over
 * ::TcpClient, with the TLS record layer under it when the target URI is https (RFC 9112 sec 9.7).
 */

#include "services/net/http_client/http_client.h"
#include "mmgr/secure/secure.h" // the persistent end this module's key material is taken from

#if PROTOCORE_ENABLE_HTTP_CLIENT

#include "mmgr/membuild/membuild.h" // protocore_sb: the message builder
#include "mmgr/protomem/protomem.h" // mem.cpy / mem.move / mem.cmp / mem.chr
#include "mmgr/protostr/protostr.h" // str.len / diff / starts / digit / to_long
#include "shared/mime/mime.h"       // PROTOCORE_MIME_OCTET_STREAM: the Content-Type default (RFC 9110 sec 8.3)

#if PROTOCORE_HAS_NET_STACK
#include "network_drivers/transport/tcp/client/client.h" // ::TcpClient, the shared outbound transport
#include "server/clock/clock.h"                          // ::Clock and pcdelay
#endif
#if PROTOCORE_ENABLE_HTTP_CLIENT_TLS
#include "network_drivers/tls/tls.h" // the client TLS session (RFC 9112 sec 9.7)
#endif
#if PROTOCORE_ENABLE_HTTP_CLIENT_TLS && PROTOCORE_HAS_VENDOR_TLS
#endif
#ifdef PROTOCORE_HTTP_CLIENT_DEBUG
#include <stdio.h>
#endif

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

// Stage tracing: build with -DPROTOCORE_HTTP_CLIENT_DEBUG to print where an exchange stalls.
#ifdef PROTOCORE_HTTP_CLIENT_DEBUG
#define CL_DBG(...) printf(__VA_ARGS__)
#else
#define CL_DBG(...) ((void)0)
#endif

// The uri-host, the origin-form request target, and the request message the exchange builds.
#define HTTP_CLIENT_HOST_CAP 80
#define HTTP_CLIENT_PATH_CAP 160
#define HTTP_CLIENT_REQ_CAP 768

// ---------------------------------------------------------------------------
// Typedefs
// ---------------------------------------------------------------------------

// --- the borrow, carved above the capability gate ---------------------------
//
// Above it, because the borrow is the MODULE's and not the network's: same size, same offset,
// same alignment, stack or no stack. All of this used to sit inside `#if PROTOCORE_HAS_NET_STACK`
// while http_client.h declares the span unconditionally, so with no stack the module published a
// span nothing defined and a caller asking for its borrow failed to link. oauth2.c worked around
// that by passing NULL in the other arm - a null borrow handed to an entry that dereferences it
// when a stack IS present. Only the entries belong under the capability.
/**
 * @brief The user agent's compile-time storage: one exchange's buffers and its connection.
 *
 * All of it BSS, so an exchange costs no heap and nothing lands on a task stack.
 */
struct HttpClientStorage
{
    uint8_t rx[PROTOCORE_HTTP_CLIENT_BUF_SIZE]; ///< the received message, start-line first (RFC 9112 sec 2.1)
    char host[HTTP_CLIENT_HOST_CAP];            ///< the uri-host a split writes (RFC 9110 sec 4)
    char path[HTTP_CLIENT_PATH_CAP];            ///< the origin-form request target (RFC 9112 sec 3.2.1)
    char req[HTTP_CLIENT_REQ_CAP];              ///< the request message a build writes
    int cid;                                    ///< the outbound connection slot, -1 when none
};

// ---------------------------------------------------------------------------
// The one instance
// ---------------------------------------------------------------------------

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define HTTP_CLIENT_OFF_CTX 0u
static_assert(HTTP_CLIENT_OFF_CTX + sizeof(struct HttpClientStorage) <= PROTOCORE_HTTP_CLIENT_BORROW,
              "PROTOCORE_HTTP_CLIENT_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define HTTP_CLIENT_CTX(w) ((struct HttpClientStorage *)(void *)((w) + HTTP_CLIENT_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_HTTP_CLIENT_BORROW persistent bytes
} HttpClientOwnCtx;
static HttpClientOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_http_client_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_HTTP_CLIENT_BORROW).buf;
        // A borrow arrives zeroed, and these do not start at zero.
        HTTP_CLIENT_CTX(s_own.span)->cid = -1;
    }
    return s_own.span;
}

// ---------------------------------------------------------------------------
// The target URI (RFC 9110 sec 7.1)
// ---------------------------------------------------------------------------

// Split the target URI into the authority to dial and the origin-form request target to send.
// Scheme defaults are RFC 9110 sec 4.2.1 (http, port 80) and sec 4.2.2 (https, port 443); an empty
// path component sends "/" (RFC 9112 sec 3.2.1).
void protocore_http_client_parse_target_uri(uint8_t *restrict work)
{
    (void)work;
    HttpClientVars *ns = &HttpClientV;
    ns->ok = PROTO_FALSE;
    if (!ns->target.url || !ns->target.host || !ns->target.path)
    {
        return;
    }

    const char *p = ns->target.url;
    if (str.starts(p, "https://", sizeof("https://"), PROTO_FALSE))
    {
        ns->target.https = PROTO_TRUE;
        ns->target.port = 443;
        p += sizeof("https://") - 1;
    }
    else if (str.starts(p, "http://", sizeof("http://"), PROTO_FALSE)) // NOSONAR: a user agent dials http:// too
    {
        ns->target.https = PROTO_FALSE;
        ns->target.port = 80;
        p += sizeof("http://") - 1;
    }
    else
    {
        return;
    }

    // uri-host: up to the port delimiter or the path. RFC 9110 sec 4.2.1 rejects an empty one.
    const char *h = p;
    while (*p && *p != ':' && *p != '/')
    {
        p++;
    }
    size_t hlen = (size_t)(p - h);
    if (hlen == 0 || hlen >= ns->target.host_cap)
    {
        return;
    }
    mem.cpy(ns->target.host, h, hlen);
    ns->target.host[hlen] = '\0';

    // Optional ":" port, decimal, bounded by the 16-bit port field.
    if (*p == ':')
    {
        p++;
        if (*p < '0' || *p > '9')
        {
            return;
        }
        uint32_t pn = 0;
        while (*p >= '0' && *p <= '9')
        {
            pn = pn * 10 + (uint32_t)(*p++ - '0');
            if (pn > 65535)
            {
                return;
            }
        }
        ns->target.port = (uint16_t)pn;
    }

    // absolute-path [ "?" query ], or "/" when the URI's path component is empty.
    if (*p == '\0')
    {
        if (ns->target.path_cap < 2)
        {
            return;
        }
        ns->target.path[0] = '/';
        ns->target.path[1] = '\0';
    }
    else
    {
        size_t plen = str.len(p, ns->target.path_cap + 1);
        if (plen >= ns->target.path_cap)
        {
            return;
        }
        mem.cpy(ns->target.path, p, plen + 1);
    }
    ns->ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// The request message (RFC 9112 sec 2.1, sec 3)
// ---------------------------------------------------------------------------

// Write "method SP request-target SP HTTP/1.1" and the field lines, then the content. Host carries
// the port only when it is not the scheme's default (RFC 9110 sec 7.2, sec 4.2.1 / 4.2.2).
void protocore_http_client_build_request(uint8_t *restrict work)
{
    (void)work;
    HttpClientVars *ns = &HttpClientV;
    ns->n = 0;
    if (!ns->request.method || !ns->target.host || !ns->target.path || !ns->request.out || ns->request.cap == 0)
    {
        return;
    }

    const proto_bool default_port =
        (ns->target.https && ns->target.port == 443) || (!ns->target.https && ns->target.port == 80);
    char hosthdr[HTTP_CLIENT_HOST_CAP + 8];
    protocore_sb sb_host = {hosthdr, sizeof(hosthdr), 0, PROTO_TRUE};
    Sb.put(&sb_host, ns->target.host);
    if (!default_port)
    {
        Sb.put(&sb_host, ":");
        Sb.u32(&sb_host, (uint32_t)ns->target.port);
    }
    if (Sb.finish(&sb_host) == 0)
    {
        hosthdr[0] = '\0';
    }

    const proto_bool has_content = (ns->request.body != NULL) && (ns->request.body_len != 0);
    protocore_sb sb_out = {ns->request.out, ns->request.cap, 0, PROTO_TRUE};
    Sb.put(&sb_out, ns->request.method);
    Sb.put(&sb_out, " ");
    Sb.put(&sb_out, ns->target.path);
    Sb.put(&sb_out, " HTTP/1.1\r\nHost: ");
    Sb.put(&sb_out, hosthdr);
    Sb.put(&sb_out, "\r\nUser-Agent: PC");
    if (has_content)
    {
        Sb.put(&sb_out, "\r\nContent-Type: ");
        Sb.put(&sb_out, ns->request.content_type ? ns->request.content_type : PROTOCORE_MIME_OCTET_STREAM);
        Sb.put(&sb_out, "\r\nContent-Length: ");
        Sb.u32(&sb_out, (uint32_t)ns->request.body_len);
    }
    Sb.put(&sb_out, "\r\nConnection: close\r\n\r\n");
    size_t hlen = Sb.finish(&sb_out);

    // The builder latches ok false on the first append that would overflow cap, and reports 0 for a
    // frame that did not fit.
    if (!sb_out.ok)
    {
        return;
    }
    if (!has_content)
    {
        ns->n = hlen;
        return;
    }
    if (hlen + ns->request.body_len > ns->request.cap)
    {
        return;
    }
    mem.cpy(ns->request.out + hlen, ns->request.body, ns->request.body_len);
    ns->n = hlen + ns->request.body_len;
}

// ---------------------------------------------------------------------------
// The response message (RFC 9112 sec 4, sec 5, sec 6.3, sec 7.1)
// ---------------------------------------------------------------------------

// The field-line value for @p name in the field section [buf, end), past the colon and the leading
// OWS, or NULL. RFC 9112 sec 5.1 allows no whitespace between field-name and colon, so the colon
// must sit immediately after the name. Field names are case-insensitive (sec 5).
static const char *field_value(const uint8_t *buf, const uint8_t *end, const char *name)
{
    size_t nlen = str.len(name, (size_t)(end - buf) + 1);
    const uint8_t *p = buf;
    while (p + nlen + 1 < end)
    {
        if (str.diff((const char *)p, name, nlen, PROTO_TRUE) == nlen && p[nlen] == ':')
        {
            const uint8_t *v = p + nlen + 1;
            while (v < end && (*v == ' ' || *v == '\t'))
            {
                v++;
            }
            return (const char *)v;
        }
        while (p < end && *p != '\n')
        {
            p++;
        }
        if (p < end)
        {
            p++;
        }
    }
    return NULL;
}

// RFC 9112 sec 6.3 item 4: chunked frames the body only when it is the final transfer coding. Walks
// the field value to its line end, back over the trailing OWS, and tests the last coding name.
static proto_bool chunked_is_final(const char *value, const char *end)
{
    const char *v = value;
    while (v < end && *v != '\r' && *v != '\n')
    {
        v++;
    }
    while (v > value && (v[-1] == ' ' || v[-1] == '\t'))
    {
        v--;
    }
    const size_t nlen = sizeof("chunked") - 1;
    if ((size_t)(v - value) < nlen)
    {
        return PROTO_FALSE;
    }
    const char *last = v - nlen;
    if (str.diff(last, "chunked", nlen, PROTO_TRUE) != nlen)
    {
        return PROTO_FALSE;
    }
    return (last == value) || last[-1] == ',' || last[-1] == ' ' || last[-1] == '\t';
}

// RFC 9112 sec 7.1.3: decode the chunked coding in place from @p off, chunk-data after chunk-data,
// until the last-chunk. Returns the decoded octet count. chunk-size is hex and a chunk-ext ends it
// at ';' (sec 7.1.1); the running size is clamped to @p len so a large numeral cannot wrap (sec 7.1).
static size_t decode_chunked(uint8_t *buf, size_t len, size_t off)
{
    size_t in = off;
    size_t out = off;
    while (in < len)
    {
        size_t chunk_size = 0;
        proto_bool any = PROTO_FALSE;
        while (in < len && buf[in] != '\r')
        {
            uint8_t c = buf[in++];
            int d;
            if (c >= '0' && c <= '9')
            {
                d = c - '0';
            }
            else if (c >= 'a' && c <= 'f')
            {
                d = c - 'a' + 10;
            }
            else if (c >= 'A' && c <= 'F')
            {
                d = c - 'A' + 10;
            }
            else
            {
                break;
            }
            chunk_size = chunk_size * 16 + (size_t)d;
            if (chunk_size > len)
            {
                chunk_size = len;
            }
            any = PROTO_TRUE;
        }
        while (in < len && buf[in] != '\n')
        {
            in++;
        }
        if (in < len)
        {
            in++;
        }
        if (!any || chunk_size == 0)
        {
            break;
        }
        if (chunk_size > len - in)
        {
            chunk_size = len - in;
        }
        mem.move(buf + out, buf + in, chunk_size);
        out += chunk_size;
        in += chunk_size;
        while (in < len && (buf[in] == '\r' || buf[in] == '\n'))
        {
            in++;
        }
    }
    return out - off;
}

// Read the status-line and frame the message body: chunked when it is the final coding, else
// Content-Length, else the octets received before the close (RFC 9112 sec 6.3 items 4, 6 and 8).
void protocore_http_client_parse_response(uint8_t *restrict work)
{
    (void)work;
    HttpClientVars *ns = &HttpClientV;
    ns->body_off = 0;
    ns->body_len = 0;
    uint8_t *buf = ns->message.buf;
    const size_t len = ns->message.len;
    if (!buf || len < 12 || mem.cmp(buf, "HTTP/1.", 7) != 0)
    {
        ns->status = (int32_t)HTTP_CLIENT_ERR_RESPONSE;
        return;
    }

    // status-line = HTTP-version SP status-code SP [ reason-phrase ], status-code = 3DIGIT.
    const uint8_t *sp = (const uint8_t *)mem.chr(buf, len, ' ');
    if (!sp || sp + 4 > buf + len)
    {
        ns->status = (int32_t)HTTP_CLIENT_ERR_RESPONSE;
        return;
    }
    if (!str.digit((char)sp[1]) || !str.digit((char)sp[2]) || !str.digit((char)sp[3]))
    {
        ns->status = (int32_t)HTTP_CLIENT_ERR_RESPONSE;
        return;
    }
    int32_t status = (sp[1] - '0') * 100 + (sp[2] - '0') * 10 + (sp[3] - '0');
    if (status < 100 || status > 599) // RFC 9110 sec 15: every valid status code is in 100..599
    {
        ns->status = (int32_t)HTTP_CLIENT_ERR_RESPONSE;
        return;
    }

    // The empty line that ends the field section (RFC 9112 sec 2.1).
    uint8_t *hdr_end = NULL;
    for (size_t i = 0; i + 3 < len; i++)
    {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n')
        {
            hdr_end = buf + i;
            break;
        }
    }
    if (!hdr_end)
    {
        ns->status = (int32_t)HTTP_CLIENT_ERR_RESPONSE;
        return;
    }

    const size_t off = (size_t)(hdr_end + 4 - buf);
    const size_t avail = len - off;
    ns->body_off = off;
    ns->status = status;

    const char *te = field_value(buf, hdr_end, "Transfer-Encoding");
    if (te && chunked_is_final(te, (const char *)hdr_end))
    {
        ns->body_len = decode_chunked(buf, len, off);
        return;
    }

    // Transfer-Encoding overrides Content-Length (RFC 9112 sec 6.3 item 3), so this reads only when
    // the chunked branch did not take it. Content-Length = 1*DIGIT (RFC 9110 sec 8.6).
    const char *cl = field_value(buf, hdr_end, "Content-Length");
    if (cl)
    {
        long declared = str.to_long(cl, NULL);
        if (declared < 0)
        {
            declared = 0;
        }
        size_t want = (size_t)declared;
        ns->body_len = (want < avail) ? want : avail;
        return;
    }

    ns->body_len = avail;
}

// ---------------------------------------------------------------------------
// https server authentication (RFC 9110 sec 4.2.2)
// ---------------------------------------------------------------------------
// Without a trust anchor or a pin the exchange is encrypted and the peer unauthenticated. Both are
// no-ops on a build without client TLS.

void protocore_http_client_set_ca(uint8_t *restrict work)
{
    (void)work;
#if PROTOCORE_ENABLE_HTTP_CLIENT_TLS
    protocore_tls_client_set_ca(HttpClientV.verify.ca, HttpClientV.verify.ca_len);
#else
#endif
}

void protocore_http_client_set_pin(uint8_t *restrict work)
{
    (void)work;
#if PROTOCORE_ENABLE_HTTP_CLIENT_TLS
    protocore_tls_client_set_pin(HttpClientV.verify.pin);
#else
#endif
}

void protocore_http_client_clear_verify(uint8_t *restrict work)
{
    (void)work;
#if PROTOCORE_ENABLE_HTTP_CLIENT_TLS
    protocore_tls_client_clear_verify();
#endif
}

// ---------------------------------------------------------------------------
// The exchange (RFC 9110 sec 3.9): active OPEN, request, response
// ---------------------------------------------------------------------------
#if PROTOCORE_HAS_NET_STACK

#if PROTOCORE_ENABLE_HTTP_CLIENT_TLS && PROTOCORE_HAS_VENDOR_TLS
// The record layer's octets over the shared transport: a send queues them, a recv drains the wire
// ring. The engine passes no user pointer, so the slot is read off the one instance.
static int tls_bio_send(void *bio, const unsigned char *buf, size_t len)
{
    (void)bio;
    TcpClientV.cid = HTTP_CLIENT_CTX(protocore_http_client_span())->cid;
    TcpClientV.io.data = buf;
    TcpClientV.io.len = len > 0xFFFF ? 0xFFFF : len;
    TcpClient.send(protocore_tcp_client_span());
    return TcpClientV.ok ? (int)TcpClientV.io.len : -1;
}

static int tls_bio_recv(void *bio, unsigned char *buf, size_t len)
{
    (void)bio;
    TcpClientV.cid = HTTP_CLIENT_CTX(protocore_http_client_span())->cid;
    TcpClientV.io.buf = buf;
    TcpClientV.io.cap = len;
    TcpClient.read(protocore_tcp_client_span());
    if (TcpClientV.n != 0)
    {
        return (int)TcpClientV.n;
    }
    TcpClientV.cid = HTTP_CLIENT_CTX(protocore_http_client_span())->cid;
    TcpClient.is_closed(protocore_tcp_client_span());
    return TcpClientV.ok ? 0 : PROTOCORE_PLATFORM_TLS_WANT_READ;
}
#endif // PROTOCORE_ENABLE_HTTP_CLIENT_TLS && PROTOCORE_HAS_VENDOR_TLS

// The library's monotonic millisecond count. Clock.ms is where the last reading landed, and every
// loop below runs with no dispatch pass in it, so the reading is taken here.
static uint32_t now_ms(void)
{
    Clock.millis(Clock.internal);
    return Clock.ms;
}

// The peer closed and its wire ring is drained: no further octet can arrive (RFC 9112 sec 6.3
// item 8, the close-delimited body).
static proto_bool peer_done(uint8_t *restrict work)
{
    TcpClientV.cid = HTTP_CLIENT_CTX(work)->cid;
    TcpClient.is_closed(protocore_tcp_client_span());
    if (!TcpClientV.ok)
    {
        return PROTO_FALSE;
    }
    TcpClientV.cid = HTTP_CLIENT_CTX(work)->cid;
    TcpClient.available(protocore_tcp_client_span());
    return TcpClientV.n == 0;
}

// Tear the connection down and free the slot (RFC 9112 sec 9.6: the "close" connection option the
// request carried).
static void close_conn(uint8_t *restrict work)
{
    if (HTTP_CLIENT_CTX(work)->cid >= 0)
    {
        TcpClientV.cid = HTTP_CLIENT_CTX(work)->cid;
        TcpClient.close(protocore_tcp_client_span());
        HTTP_CLIENT_CTX(work)->cid = -1;
    }
}

// One exchange: split the target URI, build the request message, open the connection, send, and
// read until the body is framed or the deadline passes. Fills status, body and body_len.
static void exchange(uint8_t *restrict work)
{
    HttpClientVars *ns = &HttpClientV;
    ns->status = 0;
    ns->body = NULL;
    ns->body_len = 0;
    ns->body_off = 0;

    ns->target.host = HTTP_CLIENT_CTX(work)->host;
    ns->target.host_cap = sizeof(HTTP_CLIENT_CTX(work)->host);
    ns->target.path = HTTP_CLIENT_CTX(work)->path;
    ns->target.path_cap = sizeof(HTTP_CLIENT_CTX(work)->path);
    protocore_http_client_parse_target_uri(work);
    if (!ns->ok)
    {
        ns->status = (int32_t)HTTP_CLIENT_ERR_URL;
        return;
    }
    CL_DBG("[hc] host=%s port=%u https=%d path=%s\n", HTTP_CLIENT_CTX(work)->host, (unsigned)ns->target.port,
           (int)ns->target.https, HTTP_CLIENT_CTX(work)->path);
#if !(PROTOCORE_ENABLE_HTTP_CLIENT_TLS && PROTOCORE_HAS_VENDOR_TLS)
    if (ns->target.https)
    {
        ns->status = (int32_t)HTTP_CLIENT_ERR_TLS;
        return;
    }
#endif

    ns->request.out = HTTP_CLIENT_CTX(work)->req;
    ns->request.cap = sizeof(HTTP_CLIENT_CTX(work)->req);
    protocore_http_client_build_request(work);
    const size_t reqlen = ns->n;
    if (reqlen == 0)
    {
        ns->status = (int32_t)HTTP_CLIENT_ERR_URL;
        return;
    }

    const uint32_t deadline = now_ms() + PROTOCORE_HTTP_CLIENT_TIMEOUT_MS;

    // Active OPEN (RFC 9293 sec 3.9.1.1) through the shared transport; the host it dials lives in
    // this module's storage, so it outlives the resolve.
    TcpClientV.dial.host = HTTP_CLIENT_CTX(work)->host;
    TcpClientV.dial.port = ns->target.port;
    TcpClientV.dial.timeout_ms = PROTOCORE_HTTP_CLIENT_TIMEOUT_MS;
    TcpClient.open(protocore_tcp_client_span());
    HTTP_CLIENT_CTX(work)->cid = (int)TcpClientV.i32;
    CL_DBG("[hc] open cid=%d\n", HTTP_CLIENT_CTX(work)->cid);
    if (HTTP_CLIENT_CTX(work)->cid < 0)
    {
        ns->status = (int32_t)HTTP_CLIENT_ERR_CONNECT;
        return;
    }

    // The open resolves and connects a step at a time; the request waits for it to come up.
    for (;;)
    {
        TcpClientV.cid = HTTP_CLIENT_CTX(work)->cid;
        TcpClient.connected(protocore_tcp_client_span());
        if (TcpClientV.ok)
        {
            break;
        }
        TcpClientV.cid = HTTP_CLIENT_CTX(work)->cid;
        TcpClient.is_closed(protocore_tcp_client_span());
        if (TcpClientV.ok)
        {
            close_conn(work);
            ns->status = (int32_t)HTTP_CLIENT_ERR_CONNECT;
            return;
        }
        if ((int32_t)(deadline - now_ms()) <= 0)
        {
            close_conn(work);
            ns->status = (int32_t)HTTP_CLIENT_ERR_TIMEOUT;
            return;
        }
        pcdelay(5);
    }

    size_t msg_len = 0;

#if PROTOCORE_ENABLE_HTTP_CLIENT_TLS && PROTOCORE_HAS_VENDOR_TLS
    if (ns->target.https)
    {
        // RFC 9112 sec 9.7: the user agent is also the TLS client, and the handshake finishes before
        // the request message goes out. All HTTP octets then ride as TLS application data.
        if (!protocore_tls_client_session_begin(HTTP_CLIENT_CTX(work)->host, tls_bio_send, tls_bio_recv))
        {
            close_conn(work);
            ns->status = (int32_t)HTTP_CLIENT_ERR_TLS;
            return;
        }
        protocore_tls_state hs = protocore_tls_client_session_handshake();
        while (hs == PROTOCORE_TLS_BUSY && (int32_t)(deadline - now_ms()) > 0)
        {
            pcdelay(5);
            hs = protocore_tls_client_session_handshake();
        }
        CL_DBG("[hc] tls handshake=%d\n", (int)hs);
        if (hs != PROTOCORE_TLS_READY)
        {
            protocore_tls_client_session_end();
            close_conn(work);
            ns->status = (int32_t)HTTP_CLIENT_ERR_TLS;
            return;
        }
        if (protocore_tls_client_session_write((const uint8_t *)HTTP_CLIENT_CTX(work)->req, reqlen) != (int)reqlen)
        {
            protocore_tls_client_session_end();
            close_conn(work);
            ns->status = (int32_t)HTTP_CLIENT_ERR_SEND;
            return;
        }
        while ((int32_t)(deadline - now_ms()) > 0)
        {
            int got = protocore_tls_client_session_read(HTTP_CLIENT_CTX(work)->rx + msg_len,
                                                        sizeof(HTTP_CLIENT_CTX(work)->rx) - msg_len);
            if (got < 0)
            {
                break; // closure alert or fatal (RFC 9112 sec 9.8)
            }
            msg_len += (size_t)got;
            if (msg_len >= sizeof(HTTP_CLIENT_CTX(work)->rx))
            {
                break;
            }
            if (got == 0)
            {
                if (peer_done(work))
                {
                    break;
                }
                pcdelay(5);
            }
        }
        protocore_tls_client_session_end();
    }
    else
#endif // PROTOCORE_ENABLE_HTTP_CLIENT_TLS && PROTOCORE_HAS_VENDOR_TLS
    {
        TcpClientV.cid = HTTP_CLIENT_CTX(work)->cid;
        TcpClientV.io.data = HTTP_CLIENT_CTX(work)->req;
        TcpClientV.io.len = reqlen;
        TcpClient.send(protocore_tcp_client_span());
        if (!TcpClientV.ok)
        {
            close_conn(work);
            ns->status = (int32_t)HTTP_CLIENT_ERR_SEND;
            return;
        }
        while ((int32_t)(deadline - now_ms()) > 0)
        {
            TcpClientV.cid = HTTP_CLIENT_CTX(work)->cid;
            TcpClientV.io.buf = HTTP_CLIENT_CTX(work)->rx + msg_len;
            TcpClientV.io.cap = sizeof(HTTP_CLIENT_CTX(work)->rx) - msg_len;
            TcpClient.read(protocore_tcp_client_span());
            const size_t got = TcpClientV.n;
            msg_len += got;
            if (msg_len >= sizeof(HTTP_CLIENT_CTX(work)->rx))
            {
                break;
            }
            if (peer_done(work))
            {
                break;
            }
            if (got == 0)
            {
                pcdelay(5);
            }
        }
    }

    CL_DBG("[hc] msg_len=%u\n", (unsigned)msg_len);
    close_conn(work);

    if (msg_len == 0)
    {
        ns->status = (int32_t)HTTP_CLIENT_ERR_TIMEOUT;
        return;
    }

    ns->message.buf = HTTP_CLIENT_CTX(work)->rx;
    ns->message.len = msg_len;
    protocore_http_client_parse_response(work);
    if (ns->status < 0)
    {
        return;
    }
    ns->body = HTTP_CLIENT_CTX(work)->rx + ns->body_off;
}

// RFC 9110 sec 9.3.1: GET requests a transfer of the target resource's selected representation, so
// the request encloses no content.
void protocore_http_client_get(uint8_t *restrict work)
{
    HttpClientV.request.method = "GET";
    HttpClientV.request.content_type = NULL;
    HttpClientV.request.body = NULL;
    HttpClientV.request.body_len = 0;
    exchange(work);
}

// RFC 9110 sec 9.3.3: POST asks the target resource to process the enclosed representation.
void protocore_http_client_post(uint8_t *restrict work)
{
    HttpClientV.request.method = "POST";
    exchange(work);
}

#else // no network stack: the exchange refuses and the pure calls stand alone

void protocore_http_client_get(uint8_t *restrict work)
{
    (void)work;
    HttpClientV.status = (int32_t)HTTP_CLIENT_ERR_CONNECT;
    HttpClientV.body = NULL;
    HttpClientV.body_len = 0;
}

void protocore_http_client_post(uint8_t *restrict work)
{
    (void)work;
    HttpClientV.status = (int32_t)HTTP_CLIENT_ERR_CONNECT;
    HttpClientV.body = NULL;
    HttpClientV.body_len = 0;
}

#endif // PROTOCORE_HAS_NET_STACK

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
HttpClientVars HttpClientV;

#endif // PROTOCORE_ENABLE_HTTP_CLIENT
