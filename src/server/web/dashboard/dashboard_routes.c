// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dashboard_routes.c
 * @brief Dashboard server wiring: page, layout JSON, and SSE value stream.
 *
 * Separated from the host-testable core (dashboard.cpp) so the serializers can be
 * unit-tested without pulling in the server. Requires PROTOCORE_ENABLE_SSE.
 */

#include "mmgr/membuild.h" // protocore_sb frame builder
#include "server/web/dashboard/dashboard.h"

#if PROTOCORE_ENABLE_DASHBOARD

// Dependency (DASHBOARD requires SSE) is enforced centrally in protocore_config.h.

#include "network_drivers/application/web_assets.h" // PROTOCORE_DASHBOARD_PAGE
#include "protocore.h"
#include "shared/mime/mime.h"
#if PROTOCORE_ENABLE_WEBSOCKET
#include "network_drivers/presentation/http/websocket/websocket.h" // ws_pool for inbound control messages
#endif

// All dashboard-routes state, owned by one instance (internal linkage): the server handle and
// the SSE / WebSocket paths, grouped so it is one named owner, unreachable cross-TU. (The route
// handlers are fixed-signature callbacks, so they reach this single owner directly.)
typedef struct
{
    // Whether protocore_dashboard_begin() has run. The route handlers cannot run before it - they exist
    // only because it registered them - but protocore_dashboard_publish() is called by the application on
    // its own schedule, so it can arrive first. This used to be inferred from a stored server
    // pointer being non-null; with the routes registered through free functions there is no pointer
    // to infer it from, and "has this service started" was always the real question.
    proto_bool started;
    char stream_path[MAX_PATH_LEN];
#if PROTOCORE_ENABLE_WEBSOCKET
    char ws_path[MAX_PATH_LEN];
#endif
} DashRoutesCtx;
static DashRoutesCtx s_dashr;

static void dash_page_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    // No instance test: a handler only runs because begin() registered its route.
    send_text(slot_id, 200, PROTOCORE_MIME_TEXT_HTML, PROTOCORE_DASHBOARD_PAGE);
}

static void dash_layout_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    char buf[PROTOCORE_DASHBOARD_JSON_BUF];
    protocore_dashboard_layout_json(buf, sizeof(buf));
    send_text(slot_id, 200, PROTOCORE_MIME_JSON, buf);
}

static void dash_sse_connect(uint8_t protocore_sse_id)
{
    char buf[PROTOCORE_DASHBOARD_JSON_BUF];
    if (protocore_dashboard_values_json(buf, sizeof(buf)) > 0)
    {
        protocore_sse_send(protocore_sse_id, buf, NULL, NULL); // seed the new client with the latest values
    }
}

#if PROTOCORE_ENABLE_WEBSOCKET
static void dash_ws_connect(uint8_t ws_id)
{
    (void)ws_id;
}
static void dash_ws_message(uint8_t ws_id)
{
    // Control widgets send {"k":"<key>","v":<num>}; parse + dispatch to the callback.
    if (ws_id < MAX_WS_CONNS)
    {
        Ws.ws_id = ws_id;
        Ws.payload_of(Ws.internal);
        protocore_dashboard_dispatch_control(Ws.text);
    }
}
static void dash_ws_close(uint8_t ws_id)
{
    (void)ws_id;
}
#endif

void protocore_dashboard_begin(const char *path, const protocore_widget *widgets, uint8_t count)
{
    protocore_dashboard_configure(widgets, count);

    if (!path || !path[0])
    {
        path = "/dashboard";
    }

    char layout_path[MAX_PATH_LEN];
    protocore_sb sb_layout_path = {layout_path, sizeof(layout_path), 0, PROTO_TRUE};
    Sb.put(&sb_layout_path, path);
    Sb.put(&sb_layout_path, "/layout");
    if (Sb.finish(&sb_layout_path) == 0)
    {
        layout_path[0] = '\0';
    }
    protocore_sb sb_stream_path = {s_dashr.stream_path, sizeof(s_dashr.stream_path), 0, PROTO_TRUE};
    Sb.put(&sb_stream_path, path);
    Sb.put(&sb_stream_path, "/stream");
    if (Sb.finish(&sb_stream_path) == 0)
    {
        s_dashr.stream_path[0] = '\0';
    }

    on_http(path, HTTP_GET, dash_page_handler);
    on_http(layout_path, HTTP_GET, dash_layout_handler);
    on_sse(s_dashr.stream_path, dash_sse_connect);
#if PROTOCORE_ENABLE_WEBSOCKET
    protocore_sb sb_ws_path = {s_dashr.ws_path, sizeof(s_dashr.ws_path), 0, PROTO_TRUE};
    Sb.put(&sb_ws_path, path);
    Sb.put(&sb_ws_path, "/ws");
    if (Sb.finish(&sb_ws_path) == 0)
    {
        s_dashr.ws_path[0] = '\0';
    }
    on_ws(s_dashr.ws_path, dash_ws_connect, dash_ws_message, dash_ws_close);
#endif
    s_dashr.started = PROTO_TRUE; // last: publish() is only meaningful once the stream route exists
}

void protocore_dashboard_publish()
{
    if (!s_dashr.started)
    {
        return; // nothing is subscribed until begin() has registered the stream routes
    }
    char buf[PROTOCORE_DASHBOARD_JSON_BUF];
    if (protocore_dashboard_values_json(buf, sizeof(buf)) > 0)
    {
        protocore_sse_broadcast(s_dashr.stream_path, buf, NULL, NULL);
    }
}

#endif // PROTOCORE_ENABLE_DASHBOARD
