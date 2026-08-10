// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_h2_server.c
 * @brief HTTP/2 engine <-> request-pipeline bridge - implementation. See pc_h2_server.h.
 */

#include "network_drivers/presentation/http/http2/h2_server.h"
#include "mmgr/protomem.h"

#if PC_ENABLE_HTTP2 && PC_ENABLE_TLS

#include "network_drivers/presentation/http/http2/h2_conn.h"
#include "network_drivers/presentation/http/http_parser/http_parser.h"
#include "network_drivers/tls/tls.h"
#include "network_drivers/transport/tcp.h"

// The per-slot engines are large (~28 KB each), so the pool does not fit internal DRAM alongside
// TLS - it lives in PSRAM (PC_H2_POOL_IN_PSRAM). Same mechanism/caveat as the TLS arena: it
// needs a framework built with CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y (the stock
// arduino-esp32 core ships it OFF, so EXT_RAM_BSS_ATTR would no-op); see tools/psram/README.md.
#if PC_H2_POOL_IN_PSRAM && PC_HAS_PSRAM
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

// RFC 9113 sec 8.3 defines five request pseudo-headers and sec 8.3.1 makes three of them mandatory.
// One bit each, so a repeat is an AND that comes back nonzero, the mandatory set is one XOR at the
// end of the block, and "a pseudo-header followed a regular field" is a single bit already being set.
enum
{
    H2_PH_METHOD = 1u << 0,
    H2_PH_SCHEME = 1u << 1,
    H2_PH_PATH = 1u << 2,
    H2_PH_AUTHORITY = 1u << 3,
    H2_PH_PROTOCOL = 1u << 4,
    H2_PH_REQUIRED = H2_PH_METHOD | H2_PH_SCHEME | H2_PH_PATH,
    H2_HDR_REGULAR = 1u << 5, ///< a non-pseudo field has been seen; no pseudo-header may follow
    H2_HDR_BAD = 1u << 6,     ///< the block is malformed (sec 8.2 / 8.3)
};

// HTTP/2 connection pool, owned by one instance (internal linkage): the per-slot H2 connection
// state. One named owner, unreachable from any other translation unit.
typedef struct
{
    H2Conn pool[MAX_CONNS];
    uint8_t hmask[MAX_CONNS]; ///< per-slot header-block bits, cleared when the block is judged
} H2ServerCtx;
static PC_H2_POOL_ATTR H2ServerCtx s_h2;

// The bytes a field name may carry: an RFC 9110 token minus the uppercase letters RFC 9113 sec 8.2.1
// forbids, so lowercase, digits and !#$%&'*+-.^_`|~ - one bit each over the 256 byte values. A byte
// selects its word with >> 5 and its bit with & 31, both of which the compiler emits as shifts.
// ':' is deliberately absent: it is legal only as the sec 8.3 pseudo-header marker at index 0.
static const uint32_t H2_NAME_BYTE_OK[8] = {0x00000000u, 0x03FF6CFAu, 0xC0000000u, 0x57FFFFFFu,
                                            0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u};

static proto_bool name_ok(const char *n, size_t nl)
{
    if (nl == 0)
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i < nl; i++)
    {
        const uint8_t ch = (uint8_t)n[i];
        if (ch == ':')
        {
            if (i != 0)
            {
                return PROTO_FALSE;
            }
            continue;
        }
        if (((H2_NAME_BYTE_OK[ch >> 5] >> (ch & 31u)) & 1u) == 0u)
        {
            return PROTO_FALSE;
        }
    }
    return PROTO_TRUE;
}

// RFC 9113 sec 8.2.1: a field value carries no NUL, CR or LF, and neither leads nor trails with a
// space or a horizontal tab.
static proto_bool value_ok(const char *v, size_t vl)
{
    for (size_t i = 0; i < vl; i++)
    {
        const uint8_t ch = (uint8_t)v[i];
        if (ch == 0x00u || ch == 0x0Au || ch == 0x0Du)
        {
            return PROTO_FALSE;
        }
    }
    if (vl > 0)
    {
        const uint8_t first = (uint8_t)v[0];
        const uint8_t last = (uint8_t)v[vl - 1];
        if (first == ' ' || first == '\t' || last == ' ' || last == '\t')
        {
            return PROTO_FALSE;
        }
    }
    return PROTO_TRUE;
}

// RFC 9113 sec 8.2.2: these are connection-specific and make a message malformed. TE is legal but
// only with the value "trailers".
static proto_bool connection_specific(const char *n, size_t nl)
{
    return (nl == 10 && mem.cmp(n, "connection", 10) == 0) ||
           (nl == 16 && mem.cmp(n, "proxy-connection", 16) == 0) ||
           (nl == 10 && mem.cmp(n, "keep-alive", 10) == 0) ||
           (nl == 17 && mem.cmp(n, "transfer-encoding", 17) == 0) || (nl == 7 && mem.cmp(n, "upgrade", 7) == 0);
}

// The pseudo-header's bit, or 0 for a name that is not one this server knows (sec 8.3: an undefined
// pseudo-header is malformed).
static uint8_t pseudo_bit(const char *n, size_t nl)
{
    if (nl == 7 && mem.cmp(n, ":method", 7) == 0)
    {
        return H2_PH_METHOD;
    }
    if (nl == 7 && mem.cmp(n, ":scheme", 7) == 0)
    {
        return H2_PH_SCHEME;
    }
    if (nl == 5 && mem.cmp(n, ":path", 5) == 0)
    {
        return H2_PH_PATH;
    }
    if (nl == 10 && mem.cmp(n, ":authority", 10) == 0)
    {
        return H2_PH_AUTHORITY;
    }
    if (nl == 9 && mem.cmp(n, ":protocol", 9) == 0)
    {
        return H2_PH_PROTOCOL;
    }
    return 0;
}

// Copies @p n bytes into a NUL-terminated field, truncating to fit, and returns the bytes written.
static size_t set_field(char *dst, size_t cap, const char *src, size_t n)
{
    if (n >= cap)
    {
        n = cap - 1;
    }
    mem.cpy(dst, src, n);
    dst[n] = 0;
    return n;
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

static void cb_header(void *app, uint32_t stream_id, const char *n, size_t nl, const char *v, size_t vl)
{
    (void)stream_id;
    const uint8_t slot = (uint8_t)(uintptr_t)app;
    uint8_t *mask = &s_h2.hmask[slot];
    HttpReq *r = &http_pool[slot];

    if ((*mask & H2_HDR_BAD) != 0)
    {
        return; // condemned already; cb_headers_end resets the stream
    }
    if (!name_ok(n, nl) || !value_ok(v, vl))
    {
        *mask |= H2_HDR_BAD;
        return;
    }

    if (n[0] == ':')
    {
        const uint8_t bit = pseudo_bit(n, nl);
        // sec 8.3: an undefined pseudo-header, a second copy of one already seen, or one that
        // follows a regular field. One AND covers the repeat and the ordering violation together.
        if (bit == 0 || (*mask & (bit | H2_HDR_REGULAR)) != 0)
        {
            *mask |= H2_HDR_BAD;
            return;
        }
        *mask |= bit;
        if (bit == H2_PH_METHOD)
        {
            set_field(r->method, sizeof r->method, v, vl);
        }
        else if (bit == H2_PH_PATH)
        {
            if (vl == 0)
            {
                *mask |= H2_HDR_BAD; // sec 8.3.1: ":path" is never empty for an http/https URI
                return;
            }
            const char *q = (const char *)mem.chr(v, vl, (uint8_t)'?');
            size_t plen = vl;
            if (q)
            {
                plen = (size_t)(q - v);
            }
            r->path_idx = set_field(r->path, sizeof r->path, v, plen);
            if (q)
            {
                r->query_idx = set_field(r->query, sizeof r->query, q + 1, vl - plen - 1);
            }
        }
        else if (bit == H2_PH_AUTHORITY)
        {
            if (r->header_count < MAX_HEADERS)
            {
                Header *h = &r->headers[r->header_count++];
                set_field(h->key, sizeof h->key, "host", 4);
                set_field(h->val, sizeof h->val, v, vl);
            }
        }
        return; // :scheme and :protocol are recorded in the mask, unused by the dispatcher
    }

    *mask |= H2_HDR_REGULAR;
    // sec 8.2.2: the connection-specific fields are malformed here, and TE carries only "trailers".
    if (connection_specific(n, nl))
    {
        *mask |= H2_HDR_BAD;
        return;
    }
    if (nl == 2 && mem.cmp(n, "te", 2) == 0 && !(vl == 8 && mem.cmp(v, "trailers", 8) == 0))
    {
        *mask |= H2_HDR_BAD;
        return;
    }
    if (r->header_count < MAX_HEADERS)
    {
        Header *h = &r->headers[r->header_count++];
        set_field(h->key, sizeof h->key, n, nl);
        set_field(h->val, sizeof h->val, v, vl);
    }
    if (nl == 14 && mem.cmp(n, "content-length", 14) == 0)
    {
        size_t cl = 0;
        for (size_t i = 0; i < vl && v[i] >= '0' && v[i] <= '9'; i++)
        {
            cl = cl * 10 + (size_t)(v[i] - '0');
        }
        r->content_length = cl;
    }
}

static proto_bool cb_headers_end(void *app, uint32_t sid, proto_bool end_stream)
{
    (void)end_stream;
    const uint8_t slot = (uint8_t)(uintptr_t)app;
    const uint8_t mask = s_h2.hmask[slot];
    s_h2.hmask[slot] = 0; // the block is judged here; the next one starts clean

    // sec 8.3.1: ":method", ":scheme" and ":path" are all required, so the XOR of what arrived
    // against the required set is zero exactly when none of the three is missing.
    if (((mask & H2_PH_REQUIRED) ^ H2_PH_REQUIRED) != 0 || (mask & H2_HDR_BAD) != 0)
    {
        http_parser_reset(&http_pool[slot]); // never dispatch a malformed request
        return PROTO_FALSE;                  // the engine resets the stream
    }
    conn_pool[slot].pc_h2_stream = sid;
    http_pool[slot].parse_state = PARSE_COMPLETE; // the worker's handle() loop dispatches it
    return PROTO_TRUE;
}

static void cb_data(void *app, uint32_t stream_id, const uint8_t *data, size_t len, proto_bool end_stream)
{
    (void)stream_id;
    (void)end_stream;
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
    mem.set(&cb, 0, sizeof cb);
    cb.write = cb_write;
    cb.on_header = cb_header;
    cb.on_headers_end = cb_headers_end;
    cb.on_data = cb_data;
    cb.io = (void *)(uintptr_t)slot;
    cb.app = (void *)(uintptr_t)slot;
    s_h2.hmask[slot] = 0;
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
