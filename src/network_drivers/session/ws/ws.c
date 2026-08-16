// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ws.c
 * @brief Opening and closing one WebSocket connection.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_WEBSOCKET

#include "network_drivers/presentation/http/websocket/websocket.h" // Ws: the channel table and its numbers
#include "network_drivers/session/ws/ws.h"

PROTOCORE_BEGIN_DECLS

// Take a channel for the connection the caller named, then run the route's connect. RFC 4254
// sec 5.1: a side that opens "allocates a local number for the channel"; a table with no free
// number answers the open with a failure rather than a channel.
static void session_ws_open(uint8_t *restrict work)
{
    (void)work;
    Ws.alloc(protocore_ws_span());
    SessionWs.ok = Ws.found != NULL;
    if (!SessionWs.ok)
    {
        return;
    }

    Ws.route_connect(protocore_ws_span());
    if (Ws.connect_handler != NULL)
    {
        Ws.connect_handler(Ws.found->ws_id);
    }
}

// Run the route's close, then release the channel. RFC 9293 sec 3.6 MUST-12: the application is
// informed that the connection closed, so the order is inform then release - a number freed first
// can be handed to the next connection while the handler still names it. RFC 4254 sec 5.3: the
// number is reusable once the channel is closed.
static void session_ws_close(uint8_t *restrict work)
{
    (void)work;
    Ws.find(protocore_ws_span());
    SessionWs.ok = Ws.found != NULL;
    if (!SessionWs.ok)
    {
        return; // no channel on this connection: nothing to inform and nothing to release
    }

    const uint8_t ws_id = Ws.found->ws_id;
    Ws.id = Ws.found->route_id;
    Ws.route_close(protocore_ws_span());
    if (Ws.close_handler != NULL)
    {
        Ws.close_handler(ws_id);
    }
    Ws.free(protocore_ws_span());
}

// Designated, so a member's position in the struct does not decide what it binds to.
SessionWsNs SessionWs = {.open = session_ws_open, .close = session_ws_close};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_WEBSOCKET
