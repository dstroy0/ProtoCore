// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file spa_router.c
 * @brief Single-page-app micro-routing decision (see spa_router.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SPA_ROUTER

#include "mmgr/protomem.h"
#include "mmgr/protostr.h"
#include "server/web/spa_router/spa_router.h"

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void spa_router_has_extension(uint8_t *restrict work);
static void spa_router_route(uint8_t *restrict work);

static void spa_router_has_extension(uint8_t *restrict work)
{
    (void)work;
    const char *path = SpaRouter.has_extension_args.path;

    if (!path)
    {
        SpaRouter.ok = PROTO_FALSE;
        return;
    }
    // The last '/' opens the final segment, the last '.' inside it marks the extension. find runs
    // forward, so each search resumes past its hit and the last one to return is the rightmost; a
    // path is bounded by MAX_PATH_LEN, so the resumed walks cover it once.
    const size_t n = str.len(path, MAX_PATH_LEN + 1);
    const char *seg = path;
    for (size_t i = 0; i < n; ++i) // each hit advances seg by at least one, so n bounds the trips
    {
        const char *slash = str.find(seg, n - (size_t)(seg - path), "/", sizeof("/"), PROTO_FALSE);
        if (!slash)
        {
            break;
        }
        seg = slash + 1;
    }
    const char *dot = NULL;
    const char *q = seg;
    for (size_t i = 0; i < n; ++i)
    {
        const char *hit = str.find(q, n - (size_t)(q - path), ".", sizeof("."), PROTO_FALSE);
        if (!hit)
        {
            break;
        }
        dot = hit;
        q = hit + 1;
    }
    SpaRouter.ok = dot && dot != seg && dot[1] != '\0';
}

static void spa_router_route(uint8_t *restrict work)
{
    (void)work;
    const char *path = SpaRouter.route_args.path;
    const char *api_prefix = SpaRouter.route_args.api_prefix;

    if (!path || path[0] == '\0' || (path[0] == '/' && path[1] == '\0'))
    {
        SpaRouter.action = PROTOCORE_SPA_SERVE_SHELL;
        return; // "" or "/" -> the app shell
    }

    if (api_prefix && api_prefix[0])
    {
        size_t pl = str.len(api_prefix, MAX_PATH_LEN + 1);
        if (str.starts(path, api_prefix, pl, PROTO_FALSE))
        {
            SpaRouter.action = PROTOCORE_SPA_PASSTHROUGH;
            return; // "/api/..." -> handlers
        }
    }

    SpaRouter.has_extension_args.path = path;
    spa_router_has_extension(work);
    SpaRouter.action = SpaRouter.ok ? PROTOCORE_SPA_SERVE_FILE : PROTOCORE_SPA_SERVE_SHELL;
}

static void spa_router_route_ex(uint8_t *restrict work)
{
    (void)work;
    const char *path = SpaRouter.route_ex_args.path;
    const protocore_spa_ctx *ctx = SpaRouter.route_ex_args.ctx;

    if (!ctx)
    {
        SpaRouter.route_args.path = path;
        SpaRouter.route_args.api_prefix = NULL;
        spa_router_route(work);
        return;
    }

    SpaRouter.route_args.path = path;
    SpaRouter.route_args.api_prefix = ctx->api_prefix;
    spa_router_route(work);
    protocore_spa_action a = SpaRouter.action;
    // Only the shell decision can degrade. An asset request still resolves to the file (the caller
    // reports a real 404 if it is missing), and the API must keep passing through - the fallback
    // page posts to those same endpoints, so cutting them off would make it useless.
    if (a != PROTOCORE_SPA_SERVE_SHELL)
    {
        SpaRouter.action = a;
        return;
    }
    if (!ctx->shell_available || !ctx->client_scripting || ctx->degraded)
    {
        SpaRouter.action = PROTOCORE_SPA_SERVE_FALLBACK;
        return;
    }
    SpaRouter.action = a;
}

// ---------------------------------------------------------------------------
// Conditional UI streaming
// ---------------------------------------------------------------------------

static void spa_router_ui_stream_begin(uint8_t *restrict work)
{
    (void)work;
    protocore_ui_stream *s = SpaRouter.ui_stream_begin_args.s;
    const protocore_ui_fragment *frags = SpaRouter.ui_stream_begin_args.frags;
    size_t count = SpaRouter.ui_stream_begin_args.count;
    void *ctx = SpaRouter.ui_stream_begin_args.ctx;

    if (!s)
    {
        return;
    }
    s->frags = frags;
    s->count = frags ? count : 0;
    s->ctx = ctx;
    s->idx = 0;
    s->off = 0;
    s->done = (s->count == 0);
}

static void spa_router_ui_stream_next(uint8_t *restrict work)
{
    (void)work;
    protocore_ui_stream *s = SpaRouter.ui_stream_next_args.s;
    char *out = SpaRouter.ui_stream_next_args.out;
    size_t cap = SpaRouter.ui_stream_next_args.cap;

    if (!s || !out || cap == 0 || s->done)
    {
        SpaRouter.n = 0;
        return;
    }

    size_t written = 0;
    while (written < cap && s->idx < s->count)
    {
        const protocore_ui_fragment *f = &s->frags[s->idx];
        // Evaluated here rather than at begin(), so a fragment reflects the state that holds when
        // the stream reaches it. Skipping costs nothing - no bytes are emitted for it at all.
        if (!f->html || (f->when && !f->when(s->ctx)))
        {
            s->idx++;
            s->off = 0;
            continue;
        }
        const char *src = f->html + s->off;
        size_t room = cap - written;
        size_t n = 0;
        while (n < room && src[n] != '\0')
        {
            n++;
        }
        mem.cpy(out + written, src, n);
        written += n;
        s->off += n;
        if (src[n] == '\0') // this fragment is fully emitted
        {
            s->idx++;
            s->off = 0;
        }
    }
    if (s->idx >= s->count)
    {
        s->done = PROTO_TRUE;
    }
    SpaRouter.n = written;
}

static void spa_router_ui_stream_done(uint8_t *restrict work)
{
    (void)work;
    const protocore_ui_stream *s = SpaRouter.ui_stream_done_args.s;

    SpaRouter.ok = !s || s->done;
}

SpaRouterNs SpaRouter = {.has_extension = spa_router_has_extension,
                         .route = spa_router_route,
                         .route_ex = spa_router_route_ex,
                         .ui_stream_begin = spa_router_ui_stream_begin,
                         .ui_stream_next = spa_router_ui_stream_next,
                         .ui_stream_done = spa_router_ui_stream_done};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SPA_ROUTER
