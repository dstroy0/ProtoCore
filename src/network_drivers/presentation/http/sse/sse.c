// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sse.c
 * @brief Server-Sent Events connection pool implementation.
 */

#include "sse.h"
#include "mmgr/protomem.h"
#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool: the slot a stream sends on
#include <stdio.h>

SseConn protocore_sse_pool[MAX_SSE_CONNS];

/**
 * @brief The pool's compile-time storage: the streams and the per-route subscribe handlers.
 *
 * A handler belongs here rather than in the route table: a route decides where a request goes, and
 * what runs once a client subscribes is this module's business. A route carries the id that names
 * the set. All BSS.
 */
struct SseStorage
{
    SseConnectHandler on_connect[MAX_ROUTES]; ///< one subscribe handler per route
    uint8_t count;                            ///< how many routes recorded one
    char buf[SSE_BUF_SIZE];                   ///< where a write frames its record
};

/**
 * @brief The pool's state and the calls that reach it - what SseNs points at.
 *
 * @var SseInternal::store  the subscribe handlers and the framing buffer
 * @var SseInternal::ns     the handle a caller sets a call's members on
 */
struct SseInternal
{
    struct SseStorage *store;
    SseNs *ns;
};

static struct SseStorage s_store;

static struct SseInternal s_sse = {.store = &s_store, .ns = &Sse};

static void route_add(struct SseInternal *restrict ctx)
{
    if (ctx->store->count >= MAX_ROUTES)
    {
        ctx->ns->u8 = PROTOCORE_SSE_NONE;
        return;
    }
    ctx->store->on_connect[ctx->store->count] = ctx->ns->route.on_connect;
    ctx->ns->u8 = ctx->store->count++;
}

static void route_connect(struct SseInternal *restrict ctx)
{
    ctx->ns->handler = (ctx->ns->id >= ctx->store->count) ? NULL : ctx->store->on_connect[ctx->ns->id];
}

static void init(struct SseInternal *restrict ctx)
{
    (void)ctx;
    for (int i = 0; i < MAX_SSE_CONNS; i++)
    {
        protocore_sse_pool[i] = (SseConn){0};
        protocore_sse_pool[i].protocore_sse_id = (uint8_t)i;
    }
}

static void alloc(struct SseInternal *restrict ctx)
{
    ctx->ns->conn = NULL;
    for (int i = 0; i < MAX_SSE_CONNS; i++)
    {
        if (!protocore_sse_pool[i].active)
        {
            protocore_sse_pool[i] = (SseConn){0};
            protocore_sse_pool[i].protocore_sse_id = (uint8_t)i;
            protocore_sse_pool[i].slot_id = ctx->ns->slot;
            protocore_sse_pool[i].active = PROTO_TRUE;
            strncpy(protocore_sse_pool[i].path, ctx->ns->route.path, MAX_PATH_LEN - 1);
            protocore_sse_pool[i].path[MAX_PATH_LEN - 1] = '\0';
            ctx->ns->conn = &protocore_sse_pool[i];
            return;
        }
    }
}

static void find(struct SseInternal *restrict ctx)
{
    ctx->ns->conn = NULL;
    for (int i = 0; i < MAX_SSE_CONNS; i++)
    {
        if (protocore_sse_pool[i].active && protocore_sse_pool[i].slot_id == ctx->ns->slot)
        {
            ctx->ns->conn = &protocore_sse_pool[i];
            return;
        }
    }
}

static void release(struct SseInternal *restrict ctx)
{
    for (int i = 0; i < MAX_SSE_CONNS; i++)
    {
        if (protocore_sse_pool[i].active && protocore_sse_pool[i].slot_id == ctx->ns->slot)
        {
            protocore_sse_pool[i] = (SseConn){0};
            protocore_sse_pool[i].protocore_sse_id = (uint8_t)i;
            return;
        }
    }
}

// Append `len` bytes of `src` at *pos if the whole record still leaves room for a trailing NUL (content must
// fit in n-1). Returns false the moment it would not fit, so a record that overflows reports 0 rather than a
// truncated frame. memcpy of a known-length span, no format parsing.
static inline proto_bool sse_append(char *buf, size_t n, size_t *pos, const char *src, size_t len)
{
    if (*pos + len > n - 1)
    {
        return PROTO_FALSE;
    }
    mem.cpy(buf + *pos, src, len);
    *pos += len;
    return PROTO_TRUE;
}

static void format(struct SseInternal *restrict ctx)
{
    ctx->ns->n = 0;
    char *buf = ctx->ns->out.buf;
    const size_t n = ctx->ns->out.cap;
    if (!ctx->ns->event_args.data || n == 0)
    {
        return;
    }

    // WHATWG event-stream field order: event, then id, then data (blank line terminates the record). A
    // branchless memcpy framer - fixed prefixes + strnlen/memcpy of each value + the terminators - instead of
    // three snprintf("%s") calls; ~an order of magnitude cheaper on the Xtensa vsnprintf path, which matters
    // for a high-rate broadcast fan-out (many subscribers). Byte-identical output (test_sse_format).
    // Bounded lengths (strnlen, cap n): a field can never exceed the output buffer (an over-long value makes
    // the append fail and the record report 0), and strnlen never reads past `n` if a value is unterminated.
    size_t pos = 0;
    const char *event = ctx->ns->event_args.event;
    const char *id = ctx->ns->event_args.event_id;
    if (event && (!sse_append(buf, n, &pos, "event: ", 7) || !sse_append(buf, n, &pos, event, strnlen(event, n)) ||
                  !sse_append(buf, n, &pos, "\n", 1)))
    {
        return;
    }
    if (id && (!sse_append(buf, n, &pos, "id: ", 4) || !sse_append(buf, n, &pos, id, strnlen(id, n)) ||
               !sse_append(buf, n, &pos, "\n", 1)))
    {
        return;
    }
    if (!sse_append(buf, n, &pos, "data: ", 6) || !sse_append(buf, n, &pos, ctx->ns->event_args.data, strnlen(ctx->ns->event_args.data, n)) ||
        !sse_append(buf, n, &pos, "\n\n", 2))
    {
        return;
    }

    buf[pos] = '\0'; // pos <= n-1 by construction, so the NUL always fits
    ctx->ns->n = (int)pos;
}

static void write_event(struct SseInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    ConnPool.slot = ctx->ns->stream->slot_id;
    ConnPool.active(ConnPool.internal);
    if (!ConnPool.ok)
    {
        return;
    }

    ctx->ns->out.buf = ctx->store->buf;
    ctx->ns->out.cap = sizeof(ctx->store->buf);
    format(ctx);
    if (ctx->ns->n <= 0)
    {
        return;
    }

    ConnPool.io.data = ctx->store->buf;
    ConnPool.io.len = (proto_u16)ctx->ns->n;
    ConnPool.send(ConnPool.internal);
    ctx->ns->ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
SseNs Sse = {.route_add = route_add,
             .route_connect = route_connect,
             .init = init,
             .alloc = alloc,
             .find = find,
             .free = release,
             .format = format,
             .write = write_event,
             .internal = &s_sse};
