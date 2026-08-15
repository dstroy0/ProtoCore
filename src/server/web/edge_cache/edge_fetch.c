// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file edge_fetch.c
 * @brief CDN edge-cache tier - async origin-fetch engine. See edge_fetch.h.
 */

#include "server/web/edge_cache/edge_fetch.h"
#include "mmgr/protomem.h"
#include "mmgr/protostr.h" // str.has: the chunked token in a folded Transfer-Encoding

#if PROTOCORE_ENABLE_EDGE_CACHE

#include "mmgr/rawmemcpy.h"                       // raw.read: the request into this fetch's buffer
#include "server/web/edge_cache/edge_cache.h"     // edge_header_value
#include "services/net/http_client/http_client.h" // HttpClient.parse_response

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

proto_bool edge_resp_complete(const uint8_t *buf, size_t len, proto_bool conn_closed, size_t *head_len)
{
    size_t h = head_end(buf, len);
    *head_len = h;
    if (h == 0)
    {
        return conn_closed; // no whole header block yet (a closed peer ends the wait)
    }
    char v[24];
    if (edge_header_value((const char *)buf, h, "Content-Length", v, sizeof(v)))
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
            return len >= h + cl;
        }
    }
    char te[40];
    if (edge_header_value((const char *)buf, h, "Transfer-Encoding", te, sizeof(te)) && has_chunked(te))
    {
        return chunked_complete(buf + h, len - h);
    }
    return conn_closed; // close-delimited body
}

void edge_fetch_begin(EdgeFetch *f, const EdgeFetchTransport *t, const char *host, uint16_t port, const void *request,
                      size_t req_len, uint32_t now_ms)
{
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

EdgeFetchStatus edge_fetch_pump(EdgeFetch *f, const EdgeFetchTransport *t, uint32_t now_ms)
{
    if (f->st != EDGE_FETCH_STATUS_PENDING)
    {
        return f->st;
    }

    if (!f->sent)
    {
        if (t->closed(t->ctx, f->cid))
        {
            f->st = EDGE_FETCH_STATUS_FAILED;
            return f->st;
        }
        if (!t->connected(t->ctx, f->cid))
        {
            if (now_ms - f->start_ms >= PROTOCORE_EDGE_FETCH_TIMEOUT_MS)
            {
                f->st = EDGE_FETCH_STATUS_FAILED;
            }
            return f->st;
        }
        if (!t->send(t->ctx, f->cid, f->buf, f->req_len))
        {
            f->st = EDGE_FETCH_STATUS_FAILED;
            return f->st;
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
    if (edge_resp_complete(f->buf, f->got, closed, &hl))
    {
        HttpClient.message.buf = f->buf;
        HttpClient.message.len = f->got;
        HttpClient.parse_response(HttpClient.internal);
        int status = (int)HttpClient.status;
        if (status < 0)
        {
            f->st = EDGE_FETCH_STATUS_FAILED;
            return f->st;
        }
        f->status = status;
        f->head_len = hl;
        f->body_off = HttpClient.body_off;
        f->body_len = HttpClient.body_len;
        f->st = EDGE_FETCH_STATUS_DONE;
        return f->st;
    }
    if (f->got >= sizeof(f->buf)) // full but not complete -> too big to cache
    {
        f->st = EDGE_FETCH_STATUS_OVERSIZE;
        return f->st;
    }
    if (closed) // origin closed before a complete response
    {
        f->st = EDGE_FETCH_STATUS_FAILED;
        return f->st;
    }
    if (now_ms - f->start_ms >= PROTOCORE_EDGE_FETCH_TIMEOUT_MS)
    {
        f->st = EDGE_FETCH_STATUS_FAILED;
        return f->st;
    }
    return EDGE_FETCH_STATUS_PENDING;
}

void edge_fetch_end(EdgeFetch *f, const EdgeFetchTransport *t)
{
    if (f->cid >= 0)
    {
        t->close(t->ctx, f->cid);
        f->cid = -1;
    }
}

#endif // PROTOCORE_ENABLE_EDGE_CACHE
