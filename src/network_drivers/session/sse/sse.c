// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sse.c
 * @brief Opening and closing one event stream.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SSE

#include "network_drivers/presentation/http/sse/sse.h" // Sse: the stream table and its numbers
#include "network_drivers/session/sse/sse.h"

PROTOCORE_BEGIN_DECLS

// Take a stream for the connection the caller named, then run the route's connect. RFC 4254
// sec 5.1: a side that opens "allocates a local number for the channel"; a table with no free
// number answers the open with a failure rather than a stream.
void protocore_session_sse_open(uint8_t *restrict work)
{
    (void)work;
    Sse.alloc(protocore_sse_span());
    SessionSseV.ok = SseV.conn != NULL;
    if (!SessionSseV.ok)
    {
        return;
    }

    Sse.route_connect(protocore_sse_span());
    if (SseV.handler != NULL)
    {
        SseV.handler(SseV.conn->protocore_sse_id);
    }
}

// Release the stream bound to the connection the caller named. RFC 4254 sec 5.3: the number is
// reusable once the stream is closed.
void protocore_session_sse_close(uint8_t *restrict work)
{
    (void)work;
    Sse.find(protocore_sse_span());
    SessionSseV.ok = SseV.conn != NULL;
    if (!SessionSseV.ok)
    {
        return; // no stream on this connection: nothing to release
    }

    Sse.free(protocore_sse_span());
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
SessionSseVars SessionSseV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSE
