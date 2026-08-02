// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_h2_server.cpp
 * @brief HTTP/2 engine <-> request-pipeline bridge - implementation. See pc_h2_server.h.
 */

#include "network_drivers/presentation/http/http2/h2_server.h"

#if PC_ENABLE_HTTP2 && PC_ENABLE_TLS

#include "network_drivers/presentation/http/http2/h2_conn.h"
#include "network_drivers/presentation/http/http_parser/http_parser.h"
#include "network_drivers/tls/tls.h"
#include "network_drivers/transport/tcp.h"
#include <string.h>

// The per-slot engines are large (~28 KB each), so the pool does not fit internal DRAM alongside
// TLS - it lives in PSRAM (PC_H2_POOL_IN_PSRAM). Same mechanism/caveat as the TLS arena: it
// needs a framework built with CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y (the stock
// arduino-esp32 core ships it OFF, so EXT_RAM_BSS_ATTR would no-op); see tools/psram/README.md.
#if PC_H2_POOL_IN_PSRAM && PROTOCORE_HOT
#include <esp_attr.h> // pulls in sdkconfig.h -> CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#if !defined(CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY)
#error                                                                                                                 \
    "PC_H2_POOL_IN_PSRAM needs a framework built with CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y. The stock arduino-esp32 core ships it OFF, so EXT_RAM_BSS_ATTR silently no-ops and the pool would overflow internal DRAM. Rebuild the core (tools/psram/README.md) or unset PC_H2_POOL_IN_PSRAM."
#endif
#if defined(EXT_RAM_BSS_ATTR)
#define PC_H2_POOL_ATTR EXT_RAM_BSS_ATTR // IDF v5 / arduino-esp32 3.x
#elif defined(EXT_RAM_ATTR)
#define PC_H2_POOL_ATTR EXT_RAM_ATTR // IDF v4 / arduino-esp32 2.x
#else
#define PC_H2_POOL_ATTR
#endif
#else
#define PC_H2_POOL_ATTR
#endif

// HTTP/2 connection pool, owned by one instance (internal linkage): the per-slot H2 connection
// state. One named owner, unreachable from any other translation unit.
typedef struct
{
    H2Conn pool[MAX_CONNS];
} H2ServerCtx;
static PC_H2_POOL_ATTR H2ServerCtx s_h2;

static void set_field(char *dst, size_t cap, const char *src, size_t n)
{
    if (n >= cap)
    {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

// --- engine callbacks (io / app carry the slot index) ---------------------------------------

static void cb_write(void *io, const uint8_t *data, size_t len)
{
    uint8_t slot = (uint8_t)(uintptr_t)io;
    size_t off = 0;
    while (off < len)
    {
        int w = pc_tls_write(slot, data + off, len - off);
        if (w <= 0)
        {
            break; // error / would-block: best-effort for this path
        }
        off += (size_t)w;
    }
}

static void cb_header(void *app, uint32_t, const char *n, size_t nl, const char *v, size_t vl)
{
    HttpReq *r = &http_pool[(uint8_t)(uintptr_t)app];
    if (nl == 7 && memcmp(n, ":method", 7) == 0)
    {
        set_field(r->method, sizeof r->method, v, vl);
    }
    else if (nl == 5 && memcmp(n, ":path", 5) == 0)
    {
        const char *q = (const char *)memchr(v, '?', vl);
        size_t plen = q ? (size_t)(q - v) : vl;
        set_field(r->path, sizeof r->path, v, plen);
        r->path_idx = strnlen(r->path, sizeof r->path);
        if (q)
        {
            set_field(r->query, sizeof r->query, q + 1, vl - plen - 1);
            r->query_idx = strnlen(r->query, sizeof r->query);
        }
    }
    else if (nl == 10 && memcmp(n, ":authority", 10) == 0)
    {
        if (r->header_count < MAX_HEADERS)
        {
            Header *h = &r->headers[r->header_count++];
            set_field(h->key, sizeof h->key, "host", 4);
            set_field(h->val, sizeof h->val, v, vl);
        }
    }
    else if (nl >= 1 && n[0] == ':')
    {
        // other pseudo-header (:scheme, :protocol) - not needed by the dispatcher
    }
    else
    {
        if (r->header_count < MAX_HEADERS)
        {
            Header *h = &r->headers[r->header_count++];
            set_field(h->key, sizeof h->key, n, nl);
            set_field(h->val, sizeof h->val, v, vl);
        }
        if (nl == 14 && memcmp(n, "content-length", 14) == 0)
        {
            size_t cl = 0;
            for (size_t i = 0; i < vl && v[i] >= '0' && v[i] <= '9'; i++)
            {
                cl = cl * 10 + (size_t)(v[i] - '0');
            }
            r->content_length = cl;
        }
    }
}

static void cb_headers_end(void *app, uint32_t sid, proto_bool)
{
    uint8_t slot = (uint8_t)(uintptr_t)app;
    conn_pool[slot].pc_h2_stream = sid;
    http_pool[slot].parse_state = PARSE_COMPLETE; // the worker's handle() loop dispatches it
}

static void cb_data(void *app, uint32_t, const uint8_t *data, size_t len, proto_bool)
{
    HttpReq *r = &http_pool[(uint8_t)(uintptr_t)app];
    for (size_t i = 0; i < len && r->body_len < BODY_BUF_SIZE; i++)
    {
        r->body[r->body_len++] = data[i];
    }
    r->body[r->body_len] = 0;
    r->body_bytes_read += len;
}

void pc_h2_server_open(uint8_t slot)
{
    H2Callbacks cb;
    memset(&cb, 0, sizeof cb);
    cb.write = cb_write;
    cb.on_header = cb_header;
    cb.on_headers_end = cb_headers_end;
    cb.on_data = cb_data;
    cb.io = (void *)(uintptr_t)slot;
    cb.app = (void *)(uintptr_t)slot;
    pc_h2_conn_init(&s_h2.pool[slot], &cb); // emits our SETTINGS through cb_write
    http_parser_reset(&http_pool[slot]);
}

void pc_h2_server_data(uint8_t slot)
{
    uint8_t buf[512];
    int n;
    while ((n = pc_tls_read(slot, buf, sizeof buf)) > 0)
    {
        if (!pc_h2_conn_recv(&s_h2.pool[slot], buf, (size_t)n))
        {
            pc_h2_conn_goaway(&s_h2.pool[slot], 1 /* PROTOCOL_ERROR */);
            return;
        }
    }
}

proto_bool pc_h2_server_respond(uint8_t slot, int code, const char *content_type, const char *body, size_t len)
{
    proto_bool ok = pc_h2_conn_respond(&s_h2.pool[slot], conn_pool[slot].pc_h2_stream, code, content_type, body, len);
    http_parser_reset(&http_pool[slot]); // ready for the next stream; keep the connection open
    return ok;
}

void pc_h2_server_close(uint8_t slot)
{
    conn_pool[slot].h2 = 0;
    conn_pool[slot].pc_h2_checked = 0;
    conn_pool[slot].pc_resp_sink = NULL;
}

#endif // PC_ENABLE_HTTP2 && PC_ENABLE_TLS
