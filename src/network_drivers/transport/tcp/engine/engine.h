// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file engine.h
 * @brief Layer 4 (Transport) - the TCP engine over idemIP: listeners, the connections of the pool, and the
 *        segments that idemIP does not send.
 *
 * idemIP runs RFC 9293 sec 3.10 SEGMENT ARRIVES inside Dispatch, and the engine does the rest. It sends
 * each segment that TcpIn asks for. It copies the text of each segment into the receive ring of its pool
 * entry. It sends the octets of the transmit ring of each pool entry, and it frees them when the peer
 * acknowledges them. It posts one TcpEvt for each event of a connection. TcpEvt::slot_id is the index of
 * the pool entry.
 *
 * Worker 0 runs the stack. The stack calls ::TcpEngineNs::segment after each dispatch that reports TCP. It
 * calls ::TcpEngineNs::acknowledge in the TCP_ACK step of the flush, and ::TcpEngineNs::service after the
 * flush. Each entry on the namespace runs on worker 0.
 *
 * The worker that owns a pool entry calls protocore_tcp_engine_write and protocore_tcp_engine_close. The two
 * calls put segments on the transmit ring of the entry. They change no other state. The ring is SPSC: the
 * owner is the producer, and worker 0 is the consumer. A segment of zero octets is the close mark. The ring
 * keeps the mark behind the data before it.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TCP_ENGINE_H
#define PROTOCORE_TCP_ENGINE_H

#include "protocore_config.h" // the entry point: the enable gate below, MAX_CONNS, and the engine knobs

#if PROTOCORE_ENABLE_IDEMIP

#include "memoria_anularis/memoria_anularis.h"     // the type of each ring of a pool entry
#include "network_drivers/network/stack/stack.h"   // the unit cells that the engine calls into
#include "network_drivers/transport/tcp/evt/evt.h" // the record that the engine posts

PROTOCORE_BEGIN_DECLS

/** @brief The pool entries, one for each connection of the pool. */
#define PROTOCORE_TCP_ENGINE_SLOTS MAX_CONNS

/** @brief The index that names no pool entry. */
#define PROTOCORE_TCP_ENGINE_SLOT_NONE 0xFFu

/** @brief The octets in front of the text of each transmit segment: the count of its text octets. */
#define PROTOCORE_TCP_ENGINE_SEGMENT_HEAD sizeof(uint32_t)

static_assert(PROTOCORE_TCP_ENGINE_SLOTS < PROTOCORE_TCP_ENGINE_SLOT_NONE,
              "a pool entry index fits one octet, below PROTOCORE_TCP_ENGINE_SLOT_NONE");
static_assert((PROTOCORE_TCP_ENGINE_SEGMENTS & (PROTOCORE_TCP_ENGINE_SEGMENTS - 1u)) == 0u &&
                  PROTOCORE_TCP_ENGINE_SEGMENTS != 0u,
              "PROTOCORE_TCP_ENGINE_SEGMENTS must be a power of two: the segment view of MMgr counts with a mask");

/**
 * @brief What ::TcpEngineNs::start takes.
 *
 * @var TcpEngineStartArgs::stack  the borrow of the started stack, from protocore_stack_span [BORROWS]. The
 *                                 engine calls its units, and it sends each frame in the transmit buffer that
 *                                 the stack holds.
 * @var TcpEngineStartArgs::window the receive window of each connection: the vacant octets of an empty receive
 *                                 ring of a pool entry. 1 through 65535, because the engine announces no window
 *                                 scale.
 */
typedef struct
{
    uint8_t *stack;
    uint32_t window;
} TcpEngineStartArgs;

/**
 * @brief What ::TcpEngineNs::listen takes: one RFC 9293 sec 3.3.2 passive OPEN on each local address.
 *
 * @var TcpEngineListenArgs::port       the local port, 1 through 65535
 * @var TcpEngineListenArgs::ip_version 4 or 6
 * @var TcpEngineListenArgs::backlog    the connections that the listener keeps in SYN-RECEIVED, 1 or more
 */
typedef struct
{
    uint16_t port;
    uint8_t ip_version;
    uint8_t backlog;
} TcpEngineListenArgs;

/**
 * @brief What ::TcpEngineNs::slot_bind takes: the two rings of one pool entry.
 *
 * @var TcpEngineSlotArgs::receive         the receive ring of the entry, ready and empty. Its vacant octets are
 *                                         the window of ::TcpEngineStartArgs at least [BORROWS].
 * @var TcpEngineSlotArgs::transmit        the storage of the transmit ring, which the engine lays [BORROWS]
 * @var TcpEngineSlotArgs::transmit_buffer the octets of the transmit ring, on a 4-octet boundary [BORROWS]
 * @var TcpEngineSlotArgs::transmit_bytes  the octets at transmit_buffer. The engine splits them into
 *                                         PROTOCORE_TCP_ENGINE_SEGMENTS segments of one size.
 * @var TcpEngineSlotArgs::index           the entry, below PROTOCORE_TCP_ENGINE_SLOTS
 */
typedef struct
{
    mmgr_ring *receive;
    mmgr_ring *transmit;
    uint8_t *transmit_buffer;
    size_t transmit_bytes;
    uint8_t index;
} TcpEngineSlotArgs;

/**
 * @brief What protocore_tcp_engine_write takes.
 *
 * @var TcpEngineWriteArgs::data  the octets to send [BORROWS]
 * @var TcpEngineWriteArgs::len   the count of them
 * @var TcpEngineWriteArgs::index the pool entry that sends them
 */
typedef struct
{
    const uint8_t *data;
    size_t len;
    uint8_t index;
} TcpEngineWriteArgs;

/**
 * @brief What protocore_tcp_engine_close takes.
 *
 * @var TcpEngineCloseArgs::index the pool entry that the owner has no more use for
 */
typedef struct
{
    uint8_t index;
} TcpEngineCloseArgs;

/**
 * @brief What ::TcpEngineNs::segment takes: the frame that the last Dispatch.input read.
 *
 * @var TcpEngineSegmentArgs::frame  the frame, where the receive descriptor holds it [BORROWS]
 * @var TcpEngineSegmentArgs::now_ms the monotonic millisecond count of the tick
 * @var TcpEngineSegmentArgs::netif  the interface that the frame came in on
 */
typedef struct
{
    const uint8_t *frame;
    uint32_t now_ms;
    uint8_t netif;
} TcpEngineSegmentArgs;

/**
 * @brief What ::TcpEngineNs::service takes.
 *
 * @var TcpEngineServiceArgs::now_ms the monotonic millisecond count of the tick
 */
typedef struct
{
    uint32_t now_ms;
} TcpEngineServiceArgs;

/**
 * @brief The operands and the outcome of the entries on the namespace.
 *
 * @var TcpEngineVars::start_args   the stack and the window that ::TcpEngineNs::start takes
 * @var TcpEngineVars::listen_args  the passive OPEN that ::TcpEngineNs::listen takes
 * @var TcpEngineVars::slot_args    the rings that ::TcpEngineNs::slot_bind takes
 * @var TcpEngineVars::segment_args the frame that ::TcpEngineNs::segment takes
 * @var TcpEngineVars::service_args the clock that ::TcpEngineNs::service takes
 * @var TcpEngineVars::event        the record that the last ::TcpEngineNs::event_take read
 * @var TcpEngineVars::sent         the segments that the engine put on a transmit ring since start
 * @var TcpEngineVars::lost_events  the events that found the event ring full since start
 * @var TcpEngineVars::ok           the outcome of the last call
 */
typedef struct
{
    TcpEngineStartArgs start_args;
    TcpEngineListenArgs listen_args;
    TcpEngineSlotArgs slot_args;
    TcpEngineSegmentArgs segment_args;
    TcpEngineServiceArgs service_args;
    TcpEvt event;
    uint32_t sent;
    uint32_t lost_events;
    proto_bool ok;
} TcpEngineVars;

/** @brief The operands and the outcome. */
extern TcpEngineVars TcpEngineV;

/**
 * @brief The entries. Each one runs on worker 0.
 *
 * @var TcpEngineNs::start       clear the pool entries and the event ring, and take the stack and the window
 * @var TcpEngineNs::listen      open one listener on each local address of one IP version
 * @var TcpEngineNs::slot_bind   give one pool entry its two rings
 * @var TcpEngineNs::event_take  read the next event into ::TcpEngineVars::event. False when none waits.
 * @var TcpEngineNs::segment     act on what the last Dispatch.input decided about a TCP segment
 * @var TcpEngineNs::acknowledge send the aggregate ACK that the TCP_ACK step of the flush reports
 * @var TcpEngineNs::service     send the transmit rings, open the receive windows, and end each TIME-WAIT
 *                               whose 2 MSL passed
 */
typedef struct
{
    void (*const start)(uint8_t *work);
    void (*const listen)(uint8_t *work);
    void (*const slot_bind)(uint8_t *work);
    void (*const event_take)(uint8_t *work);
    void (*const segment)(uint8_t *work);
    void (*const acknowledge)(uint8_t *work);
    void (*const service)(uint8_t *work);
} TcpEngineNs;

// The functions that the table binds. The .c defines each one, and each one takes one parameter.
void protocore_tcp_engine_start(uint8_t *work);
void protocore_tcp_engine_listen(uint8_t *work);
void protocore_tcp_engine_slot_bind(uint8_t *work);
void protocore_tcp_engine_event_take(uint8_t *work);
void protocore_tcp_engine_segment(uint8_t *work);
void protocore_tcp_engine_acknowledge(uint8_t *work);
void protocore_tcp_engine_service(uint8_t *work);

/**
 * @brief Puts octets on the transmit ring of a pool entry. The worker that owns the entry calls it.
 *
 * @param[in] args The entry and the octets [BORROWS].
 * @return         The octets that went on the ring. Fewer than args->len where the ring has no free segment.
 */
size_t protocore_tcp_engine_write(const TcpEngineWriteArgs *args);

/**
 * @brief Puts the close mark on the transmit ring of a pool entry. The worker that owns the entry calls it,
 *        once.
 *
 * @param[in] args The entry [BORROWS].
 * @return         True when the mark went on the ring. False where the ring has no free segment.
 * @note The engine sends the FIN after the octets in front of the mark. After a reset, the mark frees the
 *       entry.
 */
proto_bool protocore_tcp_engine_close(const TcpEngineCloseArgs *args);

// The table is `static const` with its initializer here, and not `extern` against a definition in the .c.
// Each translation unit can see the initializer. `TcpEngine.service(work)` thus resolves to a named
// function and becomes a direct call.
static const TcpEngineNs TcpEngine __attribute__((unused)) = {
    .start = protocore_tcp_engine_start,
    .listen = protocore_tcp_engine_listen,
    .slot_bind = protocore_tcp_engine_slot_bind,
    .event_take = protocore_tcp_engine_event_take,
    .segment = protocore_tcp_engine_segment,
    .acknowledge = protocore_tcp_engine_acknowledge,
    .service = protocore_tcp_engine_service,
};

/** @brief The borrow of this module: its context, taken once from the persistent tier. */
uint8_t *protocore_tcp_engine_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_IDEMIP

#endif // PROTOCORE_TCP_ENGINE_H
