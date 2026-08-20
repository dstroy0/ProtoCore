// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sse.c
 * @brief Server-Sent Events connection pool implementation.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SSE

#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h"
#include "mmgr/secure/secure.h"                              // the persistent end this module's state is taken from
#include "mmgr/span/span.h"                                  // span.ok: whether the pool had the bytes
#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool: the slot a stream sends on
#include "sse.h"

SseConn protocore_sse_pool[MAX_SSE_CONNS];

// The per-route subscribe handlers and the framing buffer. A handler belongs here rather than in
// the route table: a route decides where a request goes, and what runs once a client subscribes is
// this module's business. A route carries the id that names the set. Only what is not derivable:
// the regions live at fixed offsets in the caller's borrow, so the macro below computes them from
// the pointer rather than the context storing them.
typedef struct
{
    SseConnectHandler on_connect[MAX_ROUTES]; ///< one subscribe handler per route
    uint8_t count;                            ///< how many routes recorded one
    char buf[SSE_BUF_SIZE];                   ///< where a write frames its record
} SseCtx;

// The caller's borrow, split: the context at its offset. One pointer arrives and every region is
// that pointer plus a compile-time offset, so the assert below proves the span covers them before
// anything runs.
#define SSE_OFF_CTX 0u
static_assert(SSE_OFF_CTX + sizeof(SseCtx) <= PROTOCORE_SSE_BORROW,
              "PROTOCORE_SSE_BORROW is short of the module context - raise it in protocore_config.h,"
              " which sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(SSE_OFF_CTX % _Alignof(SseCtx) == 0,
              "SSE_OFF_CTX is not a multiple of alignof(SseCtx) - SSE_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define SSE_CTX(w) ((SseCtx *)(void *)((w) + SSE_OFF_CTX))

static void route_add(uint8_t *restrict work)
{
    if (SSE_CTX(work)->count >= MAX_ROUTES)
    {
        Sse.u8 = PROTOCORE_SSE_NONE;
        return;
    }
    SSE_CTX(work)->on_connect[SSE_CTX(work)->count] = Sse.route.on_connect;
    Sse.u8 = SSE_CTX(work)->count++;
}

// Empty the handler table. A route holds the id an add returned, so this belongs with whatever
// empties the routes - otherwise every re-registration appends a handler nothing can reach any
// more, and the table, which is bounded, fills and starts refusing.
static void route_reset(uint8_t *restrict work)
{
    SSE_CTX(work)->count = 0;
}

static void route_connect(uint8_t *restrict work)
{
    Sse.handler = (Sse.id >= SSE_CTX(work)->count) ? NULL : SSE_CTX(work)->on_connect[Sse.id];
}

static void init(uint8_t *restrict work)
{
    (void)work;
    for (int i = 0; i < MAX_SSE_CONNS; i++)
    {
        protocore_sse_pool[i] = (SseConn){0};
        protocore_sse_pool[i].protocore_sse_id = (uint8_t)i;
    }
}

static void alloc(uint8_t *restrict work)
{
    (void)work;
    Sse.conn = NULL;
    for (int i = 0; i < MAX_SSE_CONNS; i++)
    {
        if (!protocore_sse_pool[i].active)
        {
            protocore_sse_pool[i] = (SseConn){0};
            protocore_sse_pool[i].protocore_sse_id = (uint8_t)i;
            protocore_sse_pool[i].slot_id = Sse.slot;
            protocore_sse_pool[i].active = PROTO_TRUE;
            str.copy(protocore_sse_pool[i].path, Sse.route.path, sizeof(protocore_sse_pool[i].path));
            protocore_sse_pool[i].path[MAX_PATH_LEN - 1] = '\0';
            Sse.conn = &protocore_sse_pool[i];
            return;
        }
    }
}

static void find(uint8_t *restrict work)
{
    (void)work;
    Sse.conn = NULL;
    for (int i = 0; i < MAX_SSE_CONNS; i++)
    {
        if (protocore_sse_pool[i].active && protocore_sse_pool[i].slot_id == Sse.slot)
        {
            Sse.conn = &protocore_sse_pool[i];
            return;
        }
    }
}

static void release(uint8_t *restrict work)
{
    (void)work;
    for (int i = 0; i < MAX_SSE_CONNS; i++)
    {
        if (protocore_sse_pool[i].active && protocore_sse_pool[i].slot_id == Sse.slot)
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

static void format(uint8_t *restrict work)
{
    (void)work;
    Sse.n = 0;
    char *buf = Sse.out.buf;
    const size_t n = Sse.out.cap;
    if (!Sse.event_args.data || n == 0)
    {
        return;
    }

    // WHATWG event-stream field order: event, then id, then data (blank line terminates the record). A
    // branchless memcpy framer - fixed prefixes + str.len/memcpy of each value + the terminators - instead of
    // three snprintf("%s") calls; ~an order of magnitude cheaper on the Xtensa vsnprintf path, which matters
    // for a high-rate broadcast fan-out (many subscribers). Byte-identical output (test_sse_format).
    // Bounded lengths (str.len, cap n): a field can never exceed the output buffer (an over-long value makes
    // the append fail and the record report 0), and str.len never reads past `n` if a value is unterminated.
    size_t pos = 0;
    const char *event = Sse.event_args.event;
    const char *id = Sse.event_args.event_id;
    if (event && (!sse_append(buf, n, &pos, "event: ", 7) || !sse_append(buf, n, &pos, event, str.len(event, n)) ||
                  !sse_append(buf, n, &pos, "\n", 1)))
    {
        return;
    }
    if (id && (!sse_append(buf, n, &pos, "id: ", 4) || !sse_append(buf, n, &pos, id, str.len(id, n)) ||
               !sse_append(buf, n, &pos, "\n", 1)))
    {
        return;
    }
    if (!sse_append(buf, n, &pos, "data: ", 6) ||
        !sse_append(buf, n, &pos, Sse.event_args.data, str.len(Sse.event_args.data, n)) ||
        !sse_append(buf, n, &pos, "\n\n", 2))
    {
        return;
    }

    buf[pos] = '\0'; // pos <= n-1 by construction, so the NUL always fits
    Sse.n = (int)pos;
}

static void write_event(uint8_t *restrict work)
{
    Sse.ok = PROTO_FALSE;
    ConnPool.slot = Sse.stream->slot_id;
    ConnPool.active(protocore_conn_pool_span());
    if (!ConnPool.ok)
    {
        return;
    }

    Sse.out.buf = SSE_CTX(work)->buf;
    Sse.out.cap = sizeof(SSE_CTX(work)->buf);
    format(work);
    if (Sse.n <= 0)
    {
        return;
    }

    ConnPool.io.data = SSE_CTX(work)->buf;
    ConnPool.io.len = (proto_u16)Sse.n;
    ConnPool.send(protocore_conn_pool_span());
    Sse.ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_sse_span(void)
{
    static uint8_t *s_span;
    if (s_span == NULL)
    {
        s_span = protocore_secure_persist_span(PROTOCORE_SSE_BORROW).buf;
    }
    return s_span;
}

SseNs Sse = {.route_add = route_add,
             .route_reset = route_reset,
             .route_connect = route_connect,
             .init = init,
             .alloc = alloc,
             .find = find,
             .free = release,
             .format = format,
             .write = write_event};

#endif // PROTOCORE_ENABLE_SSE
