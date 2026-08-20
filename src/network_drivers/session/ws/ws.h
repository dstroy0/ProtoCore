// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ws.h
 * @brief Layer 5 (Session) - opening and closing a WebSocket connection.
 *
 * RFC 4254 sec 5 states the shape: "Multiple channels are multiplexed into a single connection",
 * either side "allocates a local number for the channel" to open one, and a party "may then reuse
 * the channel number" once it is closed. Sequencing that open and that close is this layer's.
 *
 * RFC 9293 sec 3.6 MUST-12 is why the close is one call rather than each caller's checklist: "If
 * the local TCP connection is closed by the remote side due to a FIN or RST received from the
 * remote side, then the local application MUST be informed whether it closed normally or was
 * aborted." A close that releases the number without informing the application leaves that
 * application holding state for a connection that no longer exists.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SESSION_WS_H
#define PROTOCORE_SESSION_WS_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_WEBSOCKET

PROTOCORE_BEGIN_DECLS

/**
 * @brief Opening and closing one WebSocket connection.
 *
 * @var SessionWsNs::ok     whether the channel opened, or whether a close had one to release
 * @var SessionWsNs::open   take a channel for the named connection and run the route's connect
 * @var SessionWsNs::close  run the route's close, then release the channel for reuse
 *
 * No storage and no members naming the connection: the caller has already named it on ::Ws, and
 * this sequences what happens to it. A second copy would be a second answer to "which connection".
 */
typedef struct
{
    proto_bool ok; ///< whether the channel opened, or whether a close had one to release
} SessionWsVars;

/** @brief The operands and the outcome. */
extern SessionWsVars SessionWsV;

/** @brief The entries. */
typedef struct
{
    void (*const open)(uint8_t *restrict work);
    void (*const close)(uint8_t *restrict work);
} SessionWsNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in SessionWsV or a region of the borrow at a fixed offset.
void protocore_ws_open(uint8_t *restrict work);
void protocore_ws_close(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `SessionWs.open(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const SessionWsNs SessionWs __attribute__((unused)) = {
    .open = protocore_ws_open,
    .close = protocore_ws_close,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_WEBSOCKET

#endif
