// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file spa_router.c
 * @brief Single-page-app micro-routing decision (see spa_router.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SPA_ROUTER

#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h"
#include "server/web/spa_router/spa_router.h"

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_spa_router_has_extension(uint8_t *restrict work);
void protocore_spa_router_route(uint8_t *restrict work);

void protocore_spa_router_has_extension(uint8_t *restrict work)
{
    (void)work;
    const char *path = SpaRouterV.has_extension_args.path;

    if (!path)
    {
        SpaRouterV.ok = PROTO_FALSE;
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
    SpaRouterV.ok = dot && dot != seg && dot[1] != '\0';
}

void protocore_spa_router_route(uint8_t *restrict work)
{
    (void)work;
    const char *path = SpaRouterV.route_args.path;
    const char *api_prefix = SpaRouterV.route_args.api_prefix;

    if (!path || path[0] == '\0' || (path[0] == '/' && path[1] == '\0'))
    {
        SpaRouterV.action = PROTOCORE_SPA_SERVE_SHELL;
        return; // "" or "/" -> the app shell
    }

    if (api_prefix && api_prefix[0])
    {
        size_t pl = str.len(api_prefix, MAX_PATH_LEN + 1);
        if (str.starts(path, api_prefix, pl, PROTO_FALSE))
        {
            SpaRouterV.action = PROTOCORE_SPA_PASSTHROUGH;
            return; // "/api/..." -> handlers
        }
    }

    SpaRouterV.has_extension_args.path = path;
    protocore_spa_router_has_extension(work);
    SpaRouterV.action = SpaRouterV.ok ? PROTOCORE_SPA_SERVE_FILE : PROTOCORE_SPA_SERVE_SHELL;
}

void protocore_spa_router_route_ex(uint8_t *restrict work)
{
    (void)work;
    const char *path = SpaRouterV.route_ex_args.path;
    const protocore_spa_ctx *ctx = SpaRouterV.route_ex_args.ctx;

    if (!ctx)
    {
        SpaRouterV.route_args.path = path;
        SpaRouterV.route_args.api_prefix = NULL;
        protocore_spa_router_route(work);
        return;
    }

    SpaRouterV.route_args.path = path;
    SpaRouterV.route_args.api_prefix = ctx->api_prefix;
    protocore_spa_router_route(work);
    protocore_spa_action a = SpaRouterV.action;
    // Only the shell decision can degrade. An asset request still resolves to the file (the caller
    // reports a real 404 if it is missing), and the API must keep passing through - the fallback
    // page posts to those same endpoints, so cutting them off would make it useless.
    if (a != PROTOCORE_SPA_SERVE_SHELL)
    {
        SpaRouterV.action = a;
        return;
    }
    if (!ctx->shell_available || !ctx->client_scripting || ctx->degraded)
    {
        SpaRouterV.action = PROTOCORE_SPA_SERVE_FALLBACK;
        return;
    }
    SpaRouterV.action = a;
}

// ---------------------------------------------------------------------------
// Conditional UI streaming
// ---------------------------------------------------------------------------

void protocore_spa_router_ui_stream_begin(uint8_t *restrict work)
{
    (void)work;
    protocore_ui_stream *s = SpaRouterV.ui_stream_begin_args.s;
    const protocore_ui_fragment *frags = SpaRouterV.ui_stream_begin_args.frags;
    size_t count = SpaRouterV.ui_stream_begin_args.count;
    void *ctx = SpaRouterV.ui_stream_begin_args.ctx;

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

void protocore_spa_router_ui_stream_next(uint8_t *restrict work)
{
    (void)work;
    protocore_ui_stream *s = SpaRouterV.ui_stream_next_args.s;
    char *out = SpaRouterV.ui_stream_next_args.out;
    size_t cap = SpaRouterV.ui_stream_next_args.cap;

    if (!s || !out || cap == 0 || s->done)
    {
        SpaRouterV.n = 0;
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
    SpaRouterV.n = written;
}

void protocore_spa_router_ui_stream_done(uint8_t *restrict work)
{
    (void)work;
    const protocore_ui_stream *s = SpaRouterV.ui_stream_done_args.s;

    SpaRouterV.ok = !s || s->done;
}

/** @brief The operands and the outcome. */
SpaRouterVars SpaRouterV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SPA_ROUTER
