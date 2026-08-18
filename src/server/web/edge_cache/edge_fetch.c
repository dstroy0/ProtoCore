// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file edge_fetch.c
 * @brief CDN edge-cache tier - async origin-fetch engine. See edge_fetch.h.
 */

#include "network_drivers/presentation/http/httpcache/httpcache.h"
#include "protocore_config.h" // the entry point: the enable gate below, and the widths
#include "shared/http_date/http_date.h"

static uint8_t edge_cache_work[16]; // the borrow an entry takes; EdgeCache never reads it

#if PROTOCORE_ENABLE_EDGE_CACHE

#include "mmgr/protomem.h"
#include "mmgr/protostr.h" // str.has: the chunked token in a folded Transfer-Encoding
#include "server/web/edge_cache/edge_fetch.h"

#include "mmgr/rawmemcpy.h"                       // raw.read: the request into this fetch's buffer
#include "server/web/edge_cache/edge_cache.h"     // edge_header_value
#include "services/net/http_client/http_client.h" // HttpClient.parse_response

PROTOCORE_BEGIN_DECLS

// Offset just past the CRLFCRLF header terminator, or 0 if the header block is not complete.
static size_t head_end(const uint8_t *b, size_t n)
{
    for (size_t i = 0; i + 3 < n; i++)
    {
        if (b[i] == '\r' && b[i + 1] == '\n' && b[i + 2] == '\r' && b[i + 3] == '\n')
        {
            return i + 4;
        }
    }
    return 0;
}

static proto_bool hex_val(uint8_t c, int *v)
{
    if (c >= '0' && c <= '9')
    {
        *v = c - '0';
        return PROTO_TRUE;
    }
    c |= 0x20;
    if (c >= 'a' && c <= 'f')
    {
        *v = c - 'a' + 10;
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

// After the final zero-length chunk's size line (offset j), true once the trailer section reaches the
// empty line (CRLF) that ends the message; false while the buffer is still short.
static proto_bool chunked_trailer_complete(const uint8_t *b, size_t n, size_t j)
{
    for (;;)
    {
        if (j + 1 < n && b[j] == '\r' && b[j + 1] == '\n')
        {
            return PROTO_TRUE;
        }
        size_t k = j;
        while (k < n && b[k] != '\n')
        {
            k++;
        }
        if (k >= n)
        {
            return PROTO_FALSE;
        }
        j = k + 1;
    }
}

// True if the chunked body @p b[0..n) reaches its terminating zero-length chunk + trailer CRLF.
static proto_bool chunked_complete(const uint8_t *b, size_t n)
{
    size_t i = 0;
    for (;;)
    {
        size_t sz = 0;
        size_t j = i;
        proto_bool any = PROTO_FALSE;
        int v = 0;
        while (j < n && hex_val(b[j], &v))
        {
            sz = sz * 16 + (size_t)v;
            j++;
            any = PROTO_TRUE;
        }
        if (!any)
        {
            return PROTO_FALSE;
        }
        while (j < n && b[j] != '\n') // skip chunk extensions to the size-line LF
        {
            j++;
        }
        if (j >= n)
        {
            return PROTO_FALSE;
        }
        j++; // past LF
        if (sz == 0)
        {
            return chunked_trailer_complete(b, n, j);
        }
        size_t next = j + sz + 2; // chunk data + trailing CRLF
        if (next > n)
        {
            return PROTO_FALSE;
        }
        i = next;
    }
}

// Case-insensitive "does @p s contain 'chunked'".
static proto_bool has_chunked(const char *s)
{
    char low[40];
    size_t i = 0;
    // The `i + 1 < sizeof(low)` bound has no false arm to reach: has_chunked has internal linkage
    // and its one caller passes `char te[40]`, which edge_header_value NUL-terminates inside its
    // own capacity - so s is at most 39 characters and s[i] always short-circuits first. The bound
    // stays as the guard that keeps that true if the caller's buffer ever grows.
    for (; s[i] && i + 1 < sizeof(low); i++)
    {
        low[i] = (s[i] >= 'A' && s[i] <= 'Z') ? (char)(s[i] + 32) : s[i];
    }
    low[i] = '\0';
    return str.has(low, sizeof(low), "chunked", sizeof("chunked"), PROTO_FALSE);
}

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void edge_fetch_edge_resp_complete(uint8_t *restrict work);

static void edge_fetch_edge_resp_complete(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = EdgeFetcher.edge_resp_complete_args.buf;
    size_t len = EdgeFetcher.edge_resp_complete_args.len;
    proto_bool conn_closed = EdgeFetcher.edge_resp_complete_args.conn_closed;
    size_t *head_len = EdgeFetcher.edge_resp_complete_args.head_len;

    size_t h = head_end(buf, len);
    *head_len = h;
    if (h == 0)
    {
        EdgeFetcher.ok = conn_closed;
        return; // no whole header block yet (a closed peer ends the wait)
    }
    char v[24];
    EdgeCache.header_value_args.hdrs = (const char *)buf;
    EdgeCache.header_value_args.len = h;
    EdgeCache.header_value_args.name = "Content-Length";
    EdgeCache.header_value_args.out = v;
    EdgeCache.header_value_args.out_cap = sizeof(v);
    EdgeCache.header_value(edge_cache_work);
    if (EdgeCache.ok)
    {
        size_t cl = 0;
        proto_bool any = PROTO_FALSE;
        for (const char *p = v; *p >= '0' && *p <= '9'; p++)
        {
            cl = cl * 10 + (size_t)(*p - '0');
            any = PROTO_TRUE;
        }
        if (any)
        {
            EdgeFetcher.ok = len >= h + cl;
            return;
        }
    }
    char te[40];
    EdgeCache.header_value_args.hdrs = (const char *)buf;
    EdgeCache.header_value_args.len = h;
    EdgeCache.header_value_args.name = "Transfer-Encoding";
    EdgeCache.header_value_args.out = te;
    EdgeCache.header_value_args.out_cap = sizeof(te);
    EdgeCache.header_value(edge_cache_work);
    if (EdgeCache.ok && has_chunked(te))
    {
        EdgeFetcher.ok = chunked_complete(buf + h, len - h);
        return;
    }
    EdgeFetcher.ok = conn_closed;
    return; // close-delimited body
}

static void edge_fetch_begin(uint8_t *restrict work)
{
    (void)work;
    EdgeFetch *f = EdgeFetcher.begin_args.f;
    const EdgeFetchTransport *t = EdgeFetcher.begin_args.t;
    const char *host = EdgeFetcher.begin_args.host;
    uint16_t port = EdgeFetcher.begin_args.port;
    const void *request = EdgeFetcher.begin_args.request;
    size_t req_len = EdgeFetcher.begin_args.req_len;
    uint32_t now_ms = EdgeFetcher.begin_args.now_ms;

    mem.set(f, 0, sizeof(*f));
    f->cid = -1;
    f->start_ms = now_ms;
    f->st = EDGE_FETCH_STATUS_PENDING;
    if (request == NULL || req_len == 0 || req_len > sizeof(f->buf))
    {
        f->st = EDGE_FETCH_STATUS_FAILED;
        return;
    }
    f->cid = t->open(t->ctx, host, port, PROTOCORE_EDGE_FETCH_TIMEOUT_MS);
    if (f->cid < 0)
    {
        f->st = EDGE_FETCH_STATUS_FAILED;
        return;
    }
    // The connection is not up yet. Nothing has arrived to occupy the response buffer, so the
    // request waits at the head of it until the pump finds the transport connected.
    raw.read(f->buf, request, req_len);
    f->req_len = (uint32_t)req_len;
}

static void edge_fetch_pump(uint8_t *restrict work)
{
    (void)work;
    EdgeFetch *f = EdgeFetcher.pump_args.f;
    const EdgeFetchTransport *t = EdgeFetcher.pump_args.t;
    uint32_t now_ms = EdgeFetcher.pump_args.now_ms;

    if (f->st != EDGE_FETCH_STATUS_PENDING)
    {
        EdgeFetcher.status = f->st;
        return;
    }

    if (!f->sent)
    {
        if (t->closed(t->ctx, f->cid))
        {
            f->st = EDGE_FETCH_STATUS_FAILED;
            EdgeFetcher.status = f->st;
            return;
        }
        if (!t->connected(t->ctx, f->cid))
        {
            if (now_ms - f->start_ms >= PROTOCORE_EDGE_FETCH_TIMEOUT_MS)
            {
                f->st = EDGE_FETCH_STATUS_FAILED;
            }
            EdgeFetcher.status = f->st;
            return;
        }
        if (!t->send(t->ctx, f->cid, f->buf, f->req_len))
        {
            f->st = EDGE_FETCH_STATUS_FAILED;
            EdgeFetcher.status = f->st;
            return;
        }
        f->sent = PROTO_TRUE;
        f->got = 0; // the buffer goes back to taking the response
    }

    while (f->got < sizeof(f->buf))
    {
        size_t n = t->read(t->ctx, f->cid, f->buf + f->got, sizeof(f->buf) - f->got);
        if (n == 0)
        {
            break;
        }
        f->got += n;
    }
    proto_bool closed = t->closed(t->ctx, f->cid);

    size_t hl = 0;
    EdgeFetcher.edge_resp_complete_args.buf = f->buf;
    EdgeFetcher.edge_resp_complete_args.len = f->got;
    EdgeFetcher.edge_resp_complete_args.conn_closed = closed;
    EdgeFetcher.edge_resp_complete_args.head_len = &hl;
    edge_fetch_edge_resp_complete(work);
    if (EdgeFetcher.ok)
    {
        HttpClient.message.buf = f->buf;
        HttpClient.message.len = f->got;
        // parse_response reads the caller's buffer and holds nothing, so it takes no borrow.
        HttpClient.parse_response(NULL);
        int status = (int)HttpClient.status;
        if (status < 0)
        {
            f->st = EDGE_FETCH_STATUS_FAILED;
            EdgeFetcher.status = f->st;
            return;
        }
        f->status = status;
        f->head_len = hl;
        f->body_off = HttpClient.body_off;
        f->body_len = HttpClient.body_len;
        f->st = EDGE_FETCH_STATUS_DONE;
        EdgeFetcher.status = f->st;
        return;
    }
    if (f->got >= sizeof(f->buf)) // full but not complete -> too big to cache
    {
        f->st = EDGE_FETCH_STATUS_OVERSIZE;
        EdgeFetcher.status = f->st;
        return;
    }
    if (closed) // origin closed before a complete response
    {
        f->st = EDGE_FETCH_STATUS_FAILED;
        EdgeFetcher.status = f->st;
        return;
    }
    if (now_ms - f->start_ms >= PROTOCORE_EDGE_FETCH_TIMEOUT_MS)
    {
        f->st = EDGE_FETCH_STATUS_FAILED;
        EdgeFetcher.status = f->st;
        return;
    }
    EdgeFetcher.status = EDGE_FETCH_STATUS_PENDING;
}

static void edge_fetch_end(uint8_t *restrict work)
{
    (void)work;
    EdgeFetch *f = EdgeFetcher.end_args.f;
    const EdgeFetchTransport *t = EdgeFetcher.end_args.t;

    if (f->cid >= 0)
    {
        t->close(t->ctx, f->cid);
        f->cid = -1;
    }
}

EdgeFetchNs EdgeFetcher = {.begin = edge_fetch_begin,
                           .pump = edge_fetch_pump,
                           .end = edge_fetch_end,
                           .edge_resp_complete = edge_fetch_edge_resp_complete};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_EDGE_CACHE
