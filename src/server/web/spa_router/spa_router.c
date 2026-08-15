// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file spa_router.c
 * @brief Single-page-app micro-routing decision (see spa_router.h).
 */

#include "server/web/spa_router/spa_router.h"
#include "mmgr/protomem.h"
#include "mmgr/protostr.h"

#if PROTOCORE_ENABLE_SPA_ROUTER

proto_bool protocore_spa_has_extension(const char *path)
{
    if (!path)
    {
        return PROTO_FALSE;
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
    return dot && dot != seg && dot[1] != '\0';
}

protocore_spa_action protocore_spa_route(const char *path, const char *api_prefix)
{
    if (!path || path[0] == '\0' || (path[0] == '/' && path[1] == '\0'))
    {
        return PROTOCORE_SPA_SERVE_SHELL; // "" or "/" -> the app shell
    }

    if (api_prefix && api_prefix[0])
    {
        size_t pl = str.len(api_prefix, MAX_PATH_LEN + 1);
        if (str.starts(path, api_prefix, pl, PROTO_FALSE))
        {
            return PROTOCORE_SPA_PASSTHROUGH; // "/api/..." -> handlers
        }
    }

    return protocore_spa_has_extension(path) ? PROTOCORE_SPA_SERVE_FILE : PROTOCORE_SPA_SERVE_SHELL;
}

protocore_spa_action protocore_spa_route_ex(const char *path, const protocore_spa_ctx *ctx)
{
    if (!ctx)
    {
        return protocore_spa_route(path, NULL);
    }

    protocore_spa_action a = protocore_spa_route(path, ctx->api_prefix);
    // Only the shell decision can degrade. An asset request still resolves to the file (the caller
    // reports a real 404 if it is missing), and the API must keep passing through - the fallback
    // page posts to those same endpoints, so cutting them off would make it useless.
    if (a != PROTOCORE_SPA_SERVE_SHELL)
    {
        return a;
    }
    if (!ctx->shell_available || !ctx->client_scripting || ctx->degraded)
    {
        return PROTOCORE_SPA_SERVE_FALLBACK;
    }
    return a;
}

// ---------------------------------------------------------------------------
// Conditional UI streaming
// ---------------------------------------------------------------------------

void protocore_ui_stream_begin(protocore_ui_stream *s, const protocore_ui_fragment *frags, size_t count, void *ctx)
{
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

size_t protocore_ui_stream_next(protocore_ui_stream *s, char *out, size_t cap)
{
    if (!s || !out || cap == 0 || s->done)
    {
        return 0;
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
    return written;
}

proto_bool protocore_ui_stream_done(const protocore_ui_stream *s)
{
    return !s || s->done;
}

#endif // PROTOCORE_ENABLE_SPA_ROUTER
