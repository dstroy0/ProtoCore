// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sse.h
 * @brief Layer 5 (Session) - opening and closing an event stream.
 *
 * RFC 4254 sec 5 states the shape a multiplexed stream takes: a side that opens "allocates a local
 * number for the channel", and a party "may then reuse the channel number" once it is closed.
 * Sequencing that open and that close is this layer's.
 *
 * RFC 9293 sec 3.6 MUST-12 is why the close is one call rather than each caller's checklist: "If
 * the local TCP connection is closed by the remote side due to a FIN or RST received from the
 * remote side, then the local application MUST be informed whether it closed normally or was
 * aborted."
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SESSION_SSE_H
#define PROTOCORE_SESSION_SSE_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_SSE

PROTOCORE_BEGIN_DECLS

/**
 * @brief Opening and closing one event stream.
 *
 * @var SessionSseNs::ok     whether the stream opened, or whether a close had one to release
 * @var SessionSseNs::open   take a stream for the named connection and run the route's connect
 * @var SessionSseNs::close  release the stream bound to the named connection
 *
 * No storage and no members naming the connection: the caller has already named it on ::Sse, and
 * this sequences what happens to it. A second copy would be a second answer to "which connection".
 */
typedef struct
{
    proto_bool ok; ///< whether the stream opened, or whether a close had one to release
} SessionSseVars;

/** @brief The operands and the outcome. */
extern SessionSseVars SessionSseV;

/** @brief The entries. */
typedef struct
{
    void (*const open)(uint8_t *restrict work);
    void (*const close)(uint8_t *restrict work);
} SessionSseNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in SessionSseV or a region of the borrow at a fixed offset.
void protocore_session_sse_open(uint8_t *restrict work);
void protocore_session_sse_close(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `SessionSse.open(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const SessionSseNs SessionSse __attribute__((unused)) = {
    .open = protocore_session_sse_open,
    .close = protocore_session_sse_close,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSE

#endif
