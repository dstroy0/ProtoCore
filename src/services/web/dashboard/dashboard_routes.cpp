// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dashboard_routes.cpp
 * @brief Dashboard server wiring: page, layout JSON, and SSE value stream.
 *
 * Separated from the host-testable core (dashboard.cpp) so the serializers can be
 * unit-tested without pulling in the server. Requires PC_ENABLE_SSE.
 */

#include "services/web/dashboard/dashboard.h"
#include "shared_primitives/strbuf.h" // pc_sb frame builder

#if PC_ENABLE_DASHBOARD

// Dependency (DASHBOARD requires SSE) is enforced centrally in protocore_config.h.

#include "network_drivers/application/web_assets.h" // PC_DASHBOARD_PAGE
#include "protocore.h"
#include "shared_primitives/mime.h"
#include <stdio.h>
#if PC_ENABLE_WEBSOCKET
#include "network_drivers/presentation/http/websocket/websocket.h" // ws_pool for inbound control messages
#endif

// All dashboard-routes state, owned by one instance (internal linkage): the server handle and
// the SSE / WebSocket paths, grouped so it is one named owner, unreachable cross-TU. (The route
// handlers are fixed-signature callbacks, so they reach this single owner directly.)
struct DashRoutesCtx
{
    PC *srv = nullptr;
    char stream_path[MAX_PATH_LEN] = {0};
#if PC_ENABLE_WEBSOCKET
    char ws_path[MAX_PATH_LEN] = {0};
#endif
};
static DashRoutesCtx s_dashr;

static void dash_page_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    if (s_dashr.srv)
    {
        s_dashr.srv->send(slot_id, 200, PC_MIME_TEXT_HTML, PC_DASHBOARD_PAGE);
    }
}

static void dash_layout_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    char buf[PC_DASHBOARD_JSON_BUF];
    pc_dashboard_layout_json(buf, sizeof(buf));
    if (s_dashr.srv)
    {
        s_dashr.srv->send(slot_id, 200, PC_MIME_JSON, buf);
    }
}

static void dash_sse_connect(uint8_t pc_sse_id)
{
    char buf[PC_DASHBOARD_JSON_BUF];
    if (s_dashr.srv && pc_dashboard_values_json(buf, sizeof(buf)) > 0)
    {
        s_dashr.srv->pc_sse_send(pc_sse_id, buf); // seed the new client with the latest values
    }
}

#if PC_ENABLE_WEBSOCKET
static void dash_ws_connect(uint8_t ws_id)
{
    (void)ws_id;
}
static void dash_ws_message(uint8_t ws_id)
{
    // Control widgets send {"k":"<key>","v":<num>}; parse + dispatch to the callback.
    if (ws_id < MAX_WS_CONNS)
    {
        pc_dashboard_dispatch_control(ws_payload(ws_id));
    }
}
static void dash_ws_close(uint8_t ws_id)
{
    (void)ws_id;
}
#endif

void pc_dashboard_begin(PC &server, const char *path, const pc_widget *widgets, uint8_t count)
{
    s_dashr.srv = &server;
    pc_dashboard_configure(widgets, count);

    if (!path || !path[0])
    {
        path = "/dashboard";
    }

    char layout_path[MAX_PATH_LEN];
    pc_sb sb_layout_path = {layout_path, sizeof(layout_path), 0, true};
    pc_sb_put(&sb_layout_path, path);
    pc_sb_put(&sb_layout_path, "/layout");
    if (pc_sb_finish(&sb_layout_path) == 0)
    {
        layout_path[0] = '\0';
    }
    pc_sb sb_stream_path = {s_dashr.stream_path, sizeof(s_dashr.stream_path), 0, true};
    pc_sb_put(&sb_stream_path, path);
    pc_sb_put(&sb_stream_path, "/stream");
    if (pc_sb_finish(&sb_stream_path) == 0)
    {
        s_dashr.stream_path[0] = '\0';
    }

    server.on(path, HttpMethod::HTTP_GET, dash_page_handler);
    server.on(layout_path, HttpMethod::HTTP_GET, dash_layout_handler);
    server.on_sse(s_dashr.stream_path, dash_sse_connect);
#if PC_ENABLE_WEBSOCKET
    pc_sb sb_ws_path = {s_dashr.ws_path, sizeof(s_dashr.ws_path), 0, true};
    pc_sb_put(&sb_ws_path, path);
    pc_sb_put(&sb_ws_path, "/ws");
    if (pc_sb_finish(&sb_ws_path) == 0)
    {
        s_dashr.ws_path[0] = '\0';
    }
    server.on_ws(s_dashr.ws_path, dash_ws_connect, dash_ws_message, dash_ws_close);
#endif
}

void pc_dashboard_publish()
{
    if (!s_dashr.srv)
    {
        return;
    }
    char buf[PC_DASHBOARD_JSON_BUF];
    if (pc_dashboard_values_json(buf, sizeof(buf)) > 0)
    {
        s_dashr.srv->pc_sse_broadcast(s_dashr.stream_path, buf);
    }
}

#endif // PC_ENABLE_DASHBOARD
