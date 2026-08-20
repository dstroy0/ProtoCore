// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file spa_router.h
 * @brief Single-page-app micro-routing + conditional UI streaming (PROTOCORE_ENABLE_SPA_ROUTER).
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
 * a client route can resolve to PROTOCORE_SPA_SERVE_FALLBACK instead: a plain server-rendered control page
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SPA_ROUTER

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief What to do with a request path. */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_SPA_SERVE_FILE,     ///< a real asset (has a file extension): serve it statically.
    PROTOCORE_SPA_SERVE_SHELL,    ///< a client route (extensionless): serve the SPA shell (index.html).
    PROTOCORE_SPA_PASSTHROUGH,    ///< under the API prefix: let the app's handlers run.
    PROTOCORE_SPA_SERVE_FALLBACK, ///< a client route the SPA cannot serve: serve the no-JS control page.
} protocore_spa_action;

/** @brief What the server currently knows about its ability to serve the SPA. */
typedef struct
{
    const char *api_prefix;      ///< paths under this always pass through; null/empty = none.
    proto_bool shell_available;  ///< is the shell asset actually present and servable?
    proto_bool client_scripting; ///< will the client run the SPA (false = text browser, curl, no-JS)?
    proto_bool degraded;         ///< force the plain control page (recovery mode, failsafe, low memory).
} protocore_spa_ctx;

/** @brief Predicate deciding whether a fragment is part of this render. */
typedef proto_bool (*protocore_ui_when_fn)(void *ctx);

/** @brief One UI panel and the condition under which it is shown. Nothing is copied. */
typedef struct
{
    const char *name;          ///< label, for diagnostics; not emitted.
    const char *html;          ///< the fragment body (borrowed).
    protocore_ui_when_fn when; ///< nullptr = always included.
} protocore_ui_fragment;

/** @brief Cursor over a fragment set. Resumes mid-fragment, so output is chunk-size independent. */
typedef struct
{
    const protocore_ui_fragment *frags;
    size_t count;
    void *ctx;  ///< passed to each predicate.
    size_t idx; ///< next fragment to consider.
    size_t off; ///< bytes of the current fragment already emitted.
    proto_bool done;
} protocore_ui_stream;

/** @brief What has_extension takes: path. */
typedef struct
{
    const char *path;
} SpaRouterHasExtensionArgs;

/** @brief What route takes: path, api_prefix. */
typedef struct
{
    const char *path;       ///< the request path (e.g. "/devices/42", "/app.js", "/api/state")
    const char *api_prefix; ///< a prefix whose paths pass through to handlers (e.g. "/api/"); null/empty = none
} SpaRouterRouteArgs;

/** @brief What route_ex takes: path, ctx. */
typedef struct
{
    const char *path;
    const protocore_spa_ctx *ctx;
} SpaRouterRouteExArgs;

/** @brief What ui_stream_begin takes: s, frags, count, ctx. */
typedef struct
{
    protocore_ui_stream *s;
    const protocore_ui_fragment *frags;
    size_t count;
    void *ctx;
} SpaRouterUiStreamBeginArgs;

/** @brief What ui_stream_next takes: s, out, cap. */
typedef struct
{
    protocore_ui_stream *s;
    char *out;
    size_t cap;
} SpaRouterUiStreamNextArgs;

/** @brief What ui_stream_done takes: s. */
typedef struct
{
    const protocore_ui_stream *s;
} SpaRouterUiStreamDoneArgs;

/**
 * @brief Single-page-app micro-routing + conditional UI streaming (PROTOCORE_ENABLE_SPA_ROUTER). A single-page web UI
 * ...
 *
 * A caller sets the members a call takes, invokes it through ::SpaRouter with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   SpaRouter.has_extension_args.path = ...;
 *   SpaRouter.has_extension(work);
 *   // SpaRouter.ok is what the call reports
 *
 * @var SpaRouterNs::has_extension_args  what has_extension takes: path
 * @var SpaRouterNs::route_args  what route takes: path, api_prefix
 * @var SpaRouterNs::route_ex_args  what route_ex takes: path, ctx
 * @var SpaRouterNs::ui_stream_begin_args  what ui_stream_begin takes: s, frags, count, ctx
 * @var SpaRouterNs::ui_stream_next_args  what ui_stream_next takes: s, out, cap
 * @var SpaRouterNs::ui_stream_done_args  what ui_stream_done takes: s
 * @var SpaRouterNs::ok  a call's true/false outcome
 * @var SpaRouterNs::action  the routing action. "/" (or empty) serves the shell. A path ...
 * @var SpaRouterNs::n  bytes written; 0 when the stream is finished (or on bad args)
 * @var SpaRouterNs::has_extension  true if the last path segment has a file extension (a '.' after the ...
 * @var SpaRouterNs::route  decide how to route path for a single-page app
 * @var SpaRouterNs::route_ex  decide how to route path, choosing the fallback HMI when the SPA ...
 * @var SpaRouterNs::ui_stream_begin  start streaming frags, evaluating each predicate against ctx. ...
 * @var SpaRouterNs::ui_stream_next  emit up to cap bytes of the remaining included fragments into out. ...
 * @var SpaRouterNs::ui_stream_done  true once every included fragment has been emitted
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    SpaRouterHasExtensionArgs has_extension_args;
    SpaRouterRouteArgs route_args;
    SpaRouterRouteExArgs route_ex_args;
    SpaRouterUiStreamBeginArgs ui_stream_begin_args;
    SpaRouterUiStreamNextArgs ui_stream_next_args;
    SpaRouterUiStreamDoneArgs ui_stream_done_args;
    proto_bool ok;
    protocore_spa_action action;
    size_t n;
} SpaRouterVars;

/** @brief The operands and the outcome. */
extern SpaRouterVars SpaRouterV;

/** @brief The entries. */
typedef struct
{
    void (*const has_extension)(uint8_t *restrict work);
    void (*const route)(uint8_t *restrict work);
    void (*const route_ex)(uint8_t *restrict work);
    void (*const ui_stream_begin)(uint8_t *restrict work);
    void (*const ui_stream_next)(uint8_t *restrict work);
    void (*const ui_stream_done)(uint8_t *restrict work);
} SpaRouterNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in SpaRouterV or a region of the borrow at a fixed offset.
void protocore_spa_router_has_extension(uint8_t *restrict work);
void protocore_spa_router_route(uint8_t *restrict work);
void protocore_spa_router_route_ex(uint8_t *restrict work);
void protocore_spa_router_ui_stream_begin(uint8_t *restrict work);
void protocore_spa_router_ui_stream_next(uint8_t *restrict work);
void protocore_spa_router_ui_stream_done(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `SpaRouter.has_extension(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const SpaRouterNs SpaRouter __attribute__((unused)) = {
    .has_extension = protocore_spa_router_has_extension,
    .route = protocore_spa_router_route,
    .route_ex = protocore_spa_router_route_ex,
    .ui_stream_begin = protocore_spa_router_ui_stream_begin,
    .ui_stream_next = protocore_spa_router_ui_stream_next,
    .ui_stream_done = protocore_spa_router_ui_stream_done,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SPA_ROUTER

#endif // PROTOCORE_SPA_ROUTER_H
