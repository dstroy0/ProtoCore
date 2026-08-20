// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file web_terminal.c
 * @brief Browser web-serial terminal over WebSocket (PROTOCORE_ENABLE_WEB_TERMINAL).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_WEB_TERMINAL

#include "mmgr/membuild/membuild.h" // protocore_sb frame builder
#include "mmgr/secure/secure.h"     // the persistent end this module's state is taken from
#include "protocore.h"              // MAX_PATH_LEN, MAX_WS_CONNS, HttpReq, send_text
#include "server/web/web_terminal/web_terminal.h"

PROTOCORE_BEGIN_DECLS

// Dependency (WEB_TERMINAL requires WEBSOCKET) is enforced centrally in protocore_config.h.

#include "network_drivers/application/web_assets/web_assets.h" // PROTOCORE_TERMINAL_PAGE
#include "shared/mime/mime.h"

// ---------------------------------------------------------------------------
// State (all static / BSS - no heap)
// ---------------------------------------------------------------------------
// All web-terminal state, owned by one instance (internal linkage): the server handle, the
// command callback, the WebSocket path, and which ws slots are terminal browsers. Grouped so
// it is one named owner, unreachable cross-TU. (The route/ws handlers are fixed-signature
// callbacks, so they reach this single owner directly.)
typedef struct
{
    TermCommandCb cb;
    char ws_path[MAX_PATH_LEN];
    proto_bool is_client[MAX_WS_CONNS]; // which ws slots are terminal browsers
} WebTerminalCtx;

// Static storage duration zero-initializes every field: cb is null, ws_path is empty, and no slot
// is marked a terminal client until a WebSocket connects.
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define WEB_TERMINAL_OFF_CTX 0u
static_assert(WEB_TERMINAL_OFF_CTX + sizeof(WebTerminalCtx) <= PROTOCORE_WEB_TERMINAL_BORROW,
              "PROTOCORE_WEB_TERMINAL_BORROW is short of the module context - raise it in protocore_config.h, which\n"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(
    WEB_TERMINAL_OFF_CTX % _Alignof(WebTerminalCtx) == 0,
    "WEB_TERMINAL_OFF_CTX is not a multiple of alignof(WebTerminalCtx) - WEB_TERMINAL_CTX() would return a misaligned "
    "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define WEB_TERMINAL_CTX(w) ((WebTerminalCtx *)(void *)((w) + WEB_TERMINAL_OFF_CTX))

// ---- internal route handlers ----------------------------------------------

static void term_page_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    // This route only exists once protocore_web_terminal_begin() has installed it, so there is nothing to
    // test: the handler running IS the proof the service started.
    send_text(slot_id, 200, PROTOCORE_MIME_TEXT_HTML, PROTOCORE_TERMINAL_PAGE);
}

static void term_ws_connect(uint8_t ws_id)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the accessor
    // rather than a parameter.
    uint8_t *restrict work = protocore_web_terminal_span();
    // ws_id always addresses a real pool slot: the WebSocket layer numbers ws_pool[i].ws_id = i for
    // i < MAX_WS_CONNS and dispatches every route callback as cb(ws->ws_id), so the bound check
    // cannot fail. Same reasoning for the ws_id checks in term_ws_message / term_ws_close below.
    if (ws_id < MAX_WS_CONNS)
    {
        WEB_TERMINAL_CTX(work)->is_client[ws_id] = PROTO_TRUE;
    }
    // As in term_page_handler: this handler is only reachable once begin() registered the route.
    ws_send_text(ws_id, "ProtoCore terminal ready\n");
}

static void term_ws_message(uint8_t ws_id)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the accessor
    // rather than a parameter.
    uint8_t *restrict work = protocore_web_terminal_span();
    // Branch-excluded for the ws_id bound only (see term_ws_connect); the cb arms are both
    // exercised by the suite (with and without a registered command callback).
    if (WEB_TERMINAL_CTX(work)->cb && ws_id < MAX_WS_CONNS)
    {
        WsV.ws_id = ws_id;
        Ws.payload_of(protocore_ws_span());
        WEB_TERMINAL_CTX(work)->cb(WsV.text, ws_id);
    }
}

static void term_ws_close(uint8_t ws_id)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the accessor
    // rather than a parameter.
    uint8_t *restrict work = protocore_web_terminal_span();
    if (ws_id < MAX_WS_CONNS)
    {
        WEB_TERMINAL_CTX(work)->is_client[ws_id] = PROTO_FALSE;
    }
}

// ---- public API -----------------------------------------------------------

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_WEB_TERMINAL_BORROW persistent bytes
} WebTerminalOwnCtx;
static WebTerminalOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_web_terminal_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_WEB_TERMINAL_BORROW).buf;
    }
    return s_own.span;
}

void protocore_web_terminal_begin(uint8_t *restrict work)
{
    (void)work;
    const char *path = WebTerminalV.begin_args.path;

    for (uint8_t i = 0; i < MAX_WS_CONNS; i++)
    {
        WEB_TERMINAL_CTX(work)->is_client[i] = PROTO_FALSE;
    }

    if (!path || !path[0])
    {
        path = "/terminal";
    }
    protocore_sb sb_ws_path = {WEB_TERMINAL_CTX(work)->ws_path, sizeof(WEB_TERMINAL_CTX(work)->ws_path), 0, PROTO_TRUE};
    Sb.put(&sb_ws_path, path);
    Sb.put(&sb_ws_path, "/ws");
    if (Sb.finish(&sb_ws_path) == 0)
    {
        WEB_TERMINAL_CTX(work)->ws_path[0] = '\0';
    }

    on_http(path, HTTP_GET, term_page_handler);
    on_ws(WEB_TERMINAL_CTX(work)->ws_path, term_ws_connect, term_ws_message, term_ws_close);
}

void protocore_web_terminal_on_command(uint8_t *restrict work)
{
    (void)work;
    TermCommandCb cb = WebTerminalV.on_command_args.cb;

    WEB_TERMINAL_CTX(work)->cb = cb;
}

void protocore_web_terminal_print(uint8_t *restrict work)
{
    (void)work;
    const char *s = WebTerminalV.print_args.s;

    if (!s)
    {
        return;
    }
    for (uint8_t i = 0; i < MAX_WS_CONNS; i++)
    {
        if (WEB_TERMINAL_CTX(work)->is_client[i])
        {
            WsV.ws_id = i;
            Ws.active(protocore_ws_span());
            if (WsV.ok)
            {
                ws_send_text(i, s);
            }
        }
    }
}

void protocore_web_terminal_println(uint8_t *restrict work)
{
    (void)work;
    const char *s = WebTerminalV.println_args.s;

    char buf[TERM_TX_BUF_SIZE];
    protocore_sb sb_buf = {buf, sizeof(buf), 0, PROTO_TRUE};
    Sb.put(&sb_buf, s ? s : "");
    Sb.put(&sb_buf, "\n");
    WebTerminalV.print_args.s = buf;
    protocore_web_terminal_print(work);
    if (Sb.finish(&sb_buf) == 0)
    {
        buf[0] = '\0';
    }
}

void protocore_web_terminal_client_count(uint8_t *restrict work)
{
    (void)work;
    WebTerminalV.value = 0;

    uint8_t n = 0;
    for (uint8_t i = 0; i < MAX_WS_CONNS; i++)
    {
        if (WEB_TERMINAL_CTX(work)->is_client[i])
        {
            WsV.ws_id = i;
            Ws.active(protocore_ws_span());
            if (WsV.ok)
            {
                n++;
            }
        }
    }
    WebTerminalV.value = n;
    return;
}

/** @brief The operands and the outcome. */
WebTerminalVars WebTerminalV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_WEB_TERMINAL
