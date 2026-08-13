// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file web_terminal.c
 * @brief Browser web-serial terminal over WebSocket (PROTOCORE_ENABLE_WEB_TERMINAL).
 */

#include "services/web/web_terminal/web_terminal.h"
#include "mmgr/membuild.h" // protocore_sb frame builder
#include "protocore.h"     // MAX_PATH_LEN, MAX_WS_CONNS, HttpReq, send_text

#if PROTOCORE_ENABLE_WEB_TERMINAL

// Dependency (WEB_TERMINAL requires WEBSOCKET) is enforced centrally in protocore_config.h.

#include "network_drivers/application/web_assets.h" // PROTOCORE_TERMINAL_PAGE
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
static WebTerminalCtx s_term;

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
    // ws_id always addresses a real pool slot: the WebSocket layer numbers ws_pool[i].ws_id = i for
    // i < MAX_WS_CONNS and dispatches every route callback as cb(ws->ws_id), so the bound check
    // cannot fail. Same reasoning for the ws_id checks in term_ws_message / term_ws_close below.
    if (ws_id < MAX_WS_CONNS)
    {
        s_term.is_client[ws_id] = PROTO_TRUE;
    }
    // As in term_page_handler: this handler is only reachable once begin() registered the route.
    ws_send_text(ws_id, "ProtoCore terminal ready\n");
}

static void term_ws_message(uint8_t ws_id)
{
    // Branch-excluded for the ws_id bound only (see term_ws_connect); the s_term.cb arms are both
    // exercised by the suite (with and without a registered command callback).
    if (s_term.cb && ws_id < MAX_WS_CONNS)
    {
        s_term.cb(ws_payload(ws_id), ws_id);
    }
}

static void term_ws_close(uint8_t ws_id)
{
    if (ws_id < MAX_WS_CONNS)
    {
        s_term.is_client[ws_id] = PROTO_FALSE;
    }
}

// ---- public API -----------------------------------------------------------

void protocore_web_terminal_begin(const char *path)
{
    for (uint8_t i = 0; i < MAX_WS_CONNS; i++)
    {
        s_term.is_client[i] = PROTO_FALSE;
    }

    if (!path || !path[0])
    {
        path = "/terminal";
    }
    protocore_sb sb_ws_path = {s_term.ws_path, sizeof(s_term.ws_path), 0, PROTO_TRUE};
    protocore_sb_put(&sb_ws_path, path);
    protocore_sb_put(&sb_ws_path, "/ws");
    if (protocore_sb_finish(&sb_ws_path) == 0)
    {
        s_term.ws_path[0] = '\0';
    }

    on_http(path, HTTP_GET, term_page_handler);
    on_ws(s_term.ws_path, term_ws_connect, term_ws_message, term_ws_close);
}

void protocore_web_terminal_on_command(TermCommandCb cb)
{
    s_term.cb = cb;
}

void protocore_web_terminal_print(const char *s)
{
    if (!s)
    {
        return;
    }
    for (uint8_t i = 0; i < MAX_WS_CONNS; i++)
    {
        if (s_term.is_client[i] && ws_active(i))
        {
            ws_send_text(i, s);
        }
    }
}

void protocore_web_terminal_println(const char *s)
{
    char buf[TERM_TX_BUF_SIZE];
    protocore_sb sb_buf = {buf, sizeof(buf), 0, PROTO_TRUE};
    protocore_sb_put(&sb_buf, s ? s : "");
    protocore_sb_put(&sb_buf, "\n");
    if (protocore_sb_finish(&sb_buf) == 0)
    {
        buf[0] = '\0';
    }
    protocore_web_terminal_print(buf);
}

uint8_t protocore_web_terminal_client_count()
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < MAX_WS_CONNS; i++)
    {
        if (s_term.is_client[i] && ws_active(i))
        {
            n++;
        }
    }
    return n;
}

#endif // PROTOCORE_ENABLE_WEB_TERMINAL
