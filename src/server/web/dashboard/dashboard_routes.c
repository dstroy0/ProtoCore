// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dashboard_routes.c
 * @brief The dashboard's route callbacks: the page, the layout JSON, the value stream, and the
 *        inbound control socket. See dashboard.h.
 *
 * Only the callbacks live here. They have fixed signatures set by whoever dispatches them, so they
 * hold no state and reach the module through its namespace; the entries that register them are in
 * dashboard.c with the rest of the surface, so the whole namespace is one initializer.
 */

#include "server/web/dashboard/dashboard.h"

#if PROTOCORE_ENABLE_DASHBOARD

// Dependency (DASHBOARD requires SSE) is enforced centrally in protocore_config.h.

#include "network_drivers/application/web_assets/web_assets.h" // PROTOCORE_DASHBOARD_PAGE
#include "protocore.h"
#include "shared/mime/mime.h"
#if PROTOCORE_ENABLE_WEBSOCKET
#include "network_drivers/presentation/http/websocket/websocket.h" // ws_pool for inbound control messages
#endif

void dash_page_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    // No instance test: a handler only runs because begin() registered its route.
    send_text(slot_id, 200, PROTOCORE_MIME_TEXT_HTML, PROTOCORE_DASHBOARD_PAGE);
}

void dash_layout_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    char buf[PROTOCORE_DASHBOARD_JSON_BUF];
    DashboardV.layout_json_args.out = buf;
    DashboardV.layout_json_args.cap = sizeof(buf);
    Dashboard.layout_json(protocore_dashboard_span());
    send_text(slot_id, 200, PROTOCORE_MIME_JSON, buf);
}

void dash_sse_connect(uint8_t protocore_sse_id)
{
    char buf[PROTOCORE_DASHBOARD_JSON_BUF];
    DashboardV.values_json_args.out = buf;
    DashboardV.values_json_args.cap = sizeof(buf);
    Dashboard.values_json(protocore_dashboard_span());
    if (DashboardV.value > 0)
    {
        protocore_sse_send(protocore_sse_id, buf, NULL, NULL); // seed the new client with the latest values
    }
}

#if PROTOCORE_ENABLE_WEBSOCKET
void dash_ws_connect(uint8_t ws_id)
{
    (void)ws_id;
}
void dash_ws_message(uint8_t ws_id)
{
    // Control widgets send {"k":"<key>","v":<num>}; parse + dispatch to the callback.
    if (ws_id < MAX_WS_CONNS)
    {
        WsV.ws_id = ws_id;
        Ws.payload_of(protocore_ws_span());
        DashboardV.dispatch_control_args.msg = WsV.text;
        Dashboard.dispatch_control(protocore_dashboard_span());
    }
}
void dash_ws_close(uint8_t ws_id)
{
    (void)ws_id;
}
#endif

#endif // PROTOCORE_ENABLE_DASHBOARD
