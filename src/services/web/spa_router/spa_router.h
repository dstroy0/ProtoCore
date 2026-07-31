// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file spa_router.h
 * @brief Single-page-app micro-routing + conditional UI streaming (PC_ENABLE_SPA_ROUTER).
 *
 * A single-page web UI does its own client-side routing: the browser navigates to `/dashboard` or
 * `/devices/42`, but there is no such file on the device - the server must return the app shell
 * (`index.html`) and let the JavaScript router take over, while still serving real asset files
 * (`/app.js`, `/style.css`) and letting API calls (`/api/...`) fall through to their handlers. This is
 * that routing decision: given a request path, return whether to serve the file, serve the shell, or
 * pass through to the app.
 *
 * The rule: a path under a configured API prefix passes through; a path whose last segment has a file
 * extension (a dot) is a real asset request; anything else is a client route and gets the shell. Pure,
 * zero heap, no stdlib, host-testable; the caller wires the result into serve_static / the router.
 *
 * ### The fallback HMI
 *
 * On a machine-control device the SPA is a convenience, not the contract. If the shell asset is
 * missing (a half-finished upload, a wiped filesystem), the client will not run scripts, or the
 * device itself is degraded, an operator still has to be able to see state and actuate something. So
 * a client route can resolve to PC_SPA_SERVE_FALLBACK instead: a plain server-rendered control page
 * needing no JavaScript and no asset files. The API prefix keeps passing through in that mode - a
 * fallback page whose endpoints have stopped answering is decoration.
 *
 * ### Conditional UI streaming
 *
 * That page is assembled from fragments, each with a predicate, and streamed in caller-sized chunks:
 * only the panels whose condition currently holds are emitted, and a page far larger than any single
 * buffer never has to fit in RAM. The same streamer serves any conditional UI - showing an operator
 * only the panels their role, or the machine's current state, warrants.
 */

#ifndef PROTOCORE_SPA_ROUTER_H
#define PROTOCORE_SPA_ROUTER_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_SPA_ROUTER

/** @brief What to do with a request path. */
enum class pc_spa_action : uint8_t
{
    PC_SPA_SERVE_FILE,     ///< a real asset (has a file extension): serve it statically.
    PC_SPA_SERVE_SHELL,    ///< a client route (extensionless): serve the SPA shell (index.html).
    PC_SPA_PASSTHROUGH,    ///< under the API prefix: let the app's handlers run.
    PC_SPA_SERVE_FALLBACK, ///< a client route the SPA cannot serve: serve the no-JS control page.
};

/** @brief True if the last path segment has a file extension (a '.' after the last '/'). */
bool pc_spa_has_extension(const char *path);

/**
 * @brief Decide how to route @p path for a single-page app.
 * @param path       the request path (e.g. "/devices/42", "/app.js", "/api/state").
 * @param api_prefix a prefix whose paths pass through to handlers (e.g. "/api/"); null/empty = none.
 * @return the routing action.
 *
 * "/" (or empty) serves the shell. A path starting with @p api_prefix passes through. A path whose last
 * segment has an extension serves the file; otherwise the shell.
 */
pc_spa_action pc_spa_route(const char *path, const char *api_prefix);

/** @brief What the server currently knows about its ability to serve the SPA. */
struct pc_spa_ctx
{
    const char *api_prefix; ///< paths under this always pass through; null/empty = none.
    bool shell_available;   ///< is the shell asset actually present and servable?
    bool client_scripting;  ///< will the client run the SPA (false = text browser, curl, no-JS)?
    bool degraded;          ///< force the plain control page (recovery mode, failsafe, low memory).
};

/**
 * @brief Decide how to route @p path, choosing the fallback HMI when the SPA cannot serve it.
 *
 * Same rules as pc_spa_route(), with one addition: a request that would serve the shell serves the
 * fallback instead when the shell is unavailable, the client will not run it, or the device is
 * degraded. Asset and API handling are unchanged - in particular the API keeps passing through, so
 * the fallback page's own controls still work.
 */
pc_spa_action pc_spa_route_ex(const char *path, const pc_spa_ctx *ctx);

// ---------------------------------------------------------------------------
// Conditional UI streaming
// ---------------------------------------------------------------------------

/** @brief Predicate deciding whether a fragment is part of this render. */
typedef bool (*pc_ui_when_fn)(void *ctx);

/** @brief One UI panel and the condition under which it is shown. Nothing is copied. */
struct pc_ui_fragment
{
    const char *name;   ///< label, for diagnostics; not emitted.
    const char *html;   ///< the fragment body (borrowed).
    pc_ui_when_fn when; ///< nullptr = always included.
};

/** @brief Cursor over a fragment set. Resumes mid-fragment, so output is chunk-size independent. */
struct pc_ui_stream
{
    const pc_ui_fragment *frags;
    size_t count;
    void *ctx;  ///< passed to each predicate.
    size_t idx; ///< next fragment to consider.
    size_t off; ///< bytes of the current fragment already emitted.
    bool done;
};

/**
 * @brief Start streaming @p frags, evaluating each predicate against @p ctx.
 *
 * Predicates run as the stream reaches each fragment, not all up front, so a long render reflects
 * the state that holds when it gets there.
 */
void pc_ui_stream_begin(pc_ui_stream *s, const pc_ui_fragment *frags, size_t count, void *ctx);

/**
 * @brief Emit up to @p cap bytes of the remaining included fragments into @p out.
 *
 * Skips fragments whose predicate is false and resumes a partially emitted one, so the caller may
 * use any buffer size - including one smaller than a single fragment.
 *
 * @return bytes written; 0 when the stream is finished (or on bad args).
 */
size_t pc_ui_stream_next(pc_ui_stream *s, char *out, size_t cap);

/** @brief True once every included fragment has been emitted. */
bool pc_ui_stream_done(const pc_ui_stream *s);

#endif // PC_ENABLE_SPA_ROUTER
#endif // PROTOCORE_SPA_ROUTER_H
