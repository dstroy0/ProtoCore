// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sse.h
 * @brief Layer 6 (Presentation) -- Server-Sent Events connection pool.
 *
 * SSE (WHATWG HTML Living Standard, Server-sent events; the W3C EventSource
 * API) is a long-lived HTTP GET response with Content-Type: text/event-stream.
 * After the initial headers the connection stays open indefinitely; the server
 * pushes newline-delimited event records at any time.
 *
 * **Event record format** (WHATWG HTML, Server-sent events)
 * ```
 * [event: <name>\n]
 * [id: <id>\n]
 * data: <payload>\n
 * \n
 * ```
 *
 * Each SseConn occupies one TCP slot from conn_pool[] for the lifetime of
 * the subscription.  The total number of simultaneous SSE connections is
 * capped at MAX_SSE_CONNS.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SSE_H
#define PROTOCORE_SSE_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_SSE

PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// Per-connection SSE state
// ---------------------------------------------------------------------------

/**
 * @brief SSE connection state stored in protocore_sse_pool[].
 *
 * Allocated when the SSE handshake (200 + headers) is sent.  slot_id ties
 * this entry back to conn_pool[] and the underlying TCP PCB.
 */
typedef struct
{
    uint8_t protocore_sse_id; ///< Index into protocore_sse_pool[] (set at init).
    uint8_t slot_id;          ///< Owning TCP slot in conn_pool[].
    proto_bool active;        ///< True when this entry is in use.

    /** Path this client subscribed to (for protocore_sse_broadcast() matching). */
    char path[MAX_PATH_LEN];
} SseConn;

/** @brief Pool of SSE connection state, one per MAX_SSE_CONNS. Defined in sse.c. */
extern SseConn protocore_sse_pool[MAX_SSE_CONNS];

/**
 * @brief Callback fired when a new SSE client connects.
 *
 * Use protocore_sse_send() inside this callback to push an initial event if needed.
 *
 * @param protocore_sse_id  Index into protocore_sse_pool[] for this connection.
 */
typedef void (*SseConnectHandler)(uint8_t protocore_sse_id);

/** @brief One subscribe route: the path, and what an open on it runs. */
typedef struct
{
    const char *path;             ///< the path a client subscribed to
    SseConnectHandler on_connect; ///< the subscribe handler a route records
} SseRouteArgs;

/** @brief The fields of one text/event-stream record. */
typedef struct
{
    const char *data;     ///< the event data; required
    const char *event;    ///< the event name; optional
    const char *event_id; ///< the event id; optional
} SseEventArgs;

/** @brief Where a formatted record lands. */
typedef struct
{
    char *buf;  ///< where format writes
    size_t cap; ///< how much room it has
} SseOutArgs;

// ---------------------------------------------------------------------------
// SSE pool API
// ---------------------------------------------------------------------------

/** @brief The id a route carries when it serves no SSE stream. */
#define PROTOCORE_SSE_NONE 0xFFu

/**
 * @brief The event streams this server holds open, and what one carries.
 *
 * A caller sets the members a call takes, invokes it through ::Sse, and reads the outcome off the
 * same handle.
 *
 * @var SseNs::slot        the TCP slot a call acts on
 * @var SseNs::id          the route id a lookup names
 * @var SseNs::route       what one subscribe route records
 * @var SseNs::stream      the stream a write goes to
 * @var SseNs::event_args  the fields of one event record
 * @var SseNs::out         where a format writes
 * @var SseNs::ok          a call's true/false outcome
 * @var SseNs::u8          the route id an add reports, or ::PROTOCORE_SSE_NONE when full
 * @var SseNs::n           bytes format wrote, excluding the terminator
 * @var SseNs::conn        the stream an alloc or a find reports, or NULL
 * @var SseNs::handler     the subscribe handler a lookup reports, or NULL
 * @var SseNs::route_add       record one route's subscribe handler
 * @var SseNs::route_reset     empty the handler table; a route holds the id an add returned, so
 *                            this empties with the routes
 * @var SseNs::route_connect   the subscribe handler an id names
 * @var SseNs::init            set every pool slot inactive; called once from begin()
 * @var SseNs::alloc           take a stream and bind it to a TCP slot
 * @var SseNs::find            the stream bound to a TCP slot
 * @var SseNs::free            release the stream bound to a TCP slot
 * @var SseNs::format          format one event record into out.buf, no transport
 * @var SseNs::write           format one event record and send it to the stream
 *
 * Every entry takes the module's borrow. How those bytes are carved is sse.c's and is never named
 * here. ::protocore_sse_span is where a caller gets one.
 *
 * format emits `event: <event>\n` (if event), `id: <event_id>\n` (if event_id), then
 * `data: <data>\n\n` per the WHATWG event-stream format. It touches no connection state, so it is
 * unit-testable on its own; write wraps it with the send. A caller that needs immediate delivery
 * flushes the connection itself afterwards.
 */
typedef struct
{
    uint8_t slot;    ///< the TCP slot a call acts on
    uint8_t id;      ///< the route id a lookup names
    SseConn *stream; ///< the stream a write goes to

    SseRouteArgs route;      ///< what one subscribe route records
    SseEventArgs event_args; ///< the fields of one event record
    SseOutArgs out;          ///< where a format writes

    proto_bool ok;
    uint8_t u8;
    int n;
    SseConn *conn;
    SseConnectHandler handler;

    void (*const route_add)(uint8_t *restrict work);
    void (*const route_reset)(uint8_t *restrict work);
    void (*const route_connect)(uint8_t *restrict work);
    void (*const init)(uint8_t *restrict work);
    void (*const alloc)(uint8_t *restrict work);
    void (*const find)(uint8_t *restrict work);
    void (*const free)(uint8_t *restrict work);
    void (*const format)(uint8_t *restrict work);
    void (*const write)(uint8_t *restrict work);
} SseNs;

/** @brief The one symbol this module exports. */
extern SseNs Sse;

/** @brief Not an entry: an entry takes a borrow and this is where that borrow comes from. */
uint8_t *protocore_sse_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSE

#endif
