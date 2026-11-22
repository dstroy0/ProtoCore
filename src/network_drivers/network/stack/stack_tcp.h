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
 * acknowledges them. It posts one StackTcpEvent for each event of a connection. StackTcpEvent::slot_id is the index of
 * the pool entry.
 *
 * Worker 0 runs the stack. The stack calls ::StackTcpNs::segment after each dispatch that reports TCP. It
 * calls ::StackTcpNs::acknowledge in the TCP_ACK step of the flush, and ::StackTcpNs::service after the
 * flush. Each entry on the namespace runs on worker 0.
 *
 * The worker that owns a pool entry calls protocore_stack_tcp_write and protocore_stack_tcp_close. The two
 * calls put segments on the transmit ring of the entry. They change no other state. The ring is SPSC: the
 * owner is the producer, and worker 0 is the consumer. A segment of zero octets is the close mark. The ring
 * keeps the mark behind the data before it.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_STACK_TCP_H
#define PROTOCORE_STACK_TCP_H

#include "protocore_config.h" // the entry point: the enable gate below, and the knobs of the stack

#if PROTOCORE_ENABLE_IDEMIP

#include "memoria_anularis/memoria_anularis.h"   // the type of each ring of an entry
#include "network_drivers/network/stack/stack.h" // the unit cells that the TCP of the stack calls into

EMBED_BEGIN_DECLS

/** @brief What happened to the connection of one entry. */
EMBED_ENUM_BYTE(StackTcpEventKind,
                STACK_TCP_CONNECT = 0, ///< a passive open completed on the entry
                STACK_TCP_DATA,        ///< text arrived on the receive ring of the entry
                STACK_TCP_DISCONNECT,  ///< the peer sent its FIN
                STACK_TCP_ERROR,       ///< a reset or a timeout ended the connection
);

/**
 * @brief One event of the TCP of the stack, in the event ring.
 *
 * @var StackTcpEvent::data_len the octets of text that a STACK_TCP_DATA put on the receive ring
 * @var StackTcpEvent::type     what happened
 * @var StackTcpEvent::slot_id  the entry
 * @var StackTcpEvent::reserved zeros, so that the record is a power of two in size
 */
typedef struct
{
    uint32_t data_len;
    StackTcpEventKind type;
    uint8_t slot_id;
    uint8_t reserved[2];
} StackTcpEvent;

/** @brief The entries, one for each connection that the stack carries. */
#define PROTOCORE_STACK_TCP_SLOTS PROTOCORE_STACK_TCP_ENTRIES

/** @brief The index that names no pool entry. */
#define PROTOCORE_STACK_TCP_SLOT_NONE 0xFFu

/** @brief The octets in front of the text of each transmit segment: the count of its text octets. */
#define PROTOCORE_STACK_TCP_SEGMENT_HEAD sizeof(uint32_t)

static_assert(PROTOCORE_STACK_TCP_SLOTS < PROTOCORE_STACK_TCP_SLOT_NONE,
              "a pool entry index fits one octet, below PROTOCORE_STACK_TCP_SLOT_NONE");
static_assert((PROTOCORE_STACK_TCP_SEGMENTS & (PROTOCORE_STACK_TCP_SEGMENTS - 1u)) == 0u &&
                  PROTOCORE_STACK_TCP_SEGMENTS != 0u,
              "PROTOCORE_STACK_TCP_SEGMENTS must be a power of two: the segment view of MMgr counts with a mask");

/**
 * @brief What ::StackTcpNs::start takes.
 *
 * @var StackTcpStartArgs::stack  the borrow of the started stack, from protocore_stack_span [BORROWS]. The
 *                                 engine calls its units, and it sends each frame in the transmit buffer that
 *                                 the stack holds.
 * @var StackTcpStartArgs::window the receive window of each connection: the vacant octets of an empty receive
 *                                 ring of a pool entry. 1 through 65535, because the engine announces no window
 *                                 scale.
 */
typedef struct
{
    uint8_t *stack;
    uint32_t window;
} StackTcpStartArgs;

/**
 * @brief What ::StackTcpNs::listen takes: one RFC 9293 sec 3.3.2 passive OPEN on each local address.
 *
 * @var StackTcpListenArgs::port       the local port, 1 through 65535
 * @var StackTcpListenArgs::ip_version 4 or 6
 * @var StackTcpListenArgs::backlog    the connections that the listener keeps in SYN-RECEIVED, 1 or more
 */
typedef struct
{
    uint16_t port;
    uint8_t ip_version;
    uint8_t backlog;
} StackTcpListenArgs;

/**
 * @brief What ::StackTcpNs::slot_bind takes: the two rings of one pool entry.
 *
 * @var StackTcpSlotArgs::receive         the receive ring of the entry, ready and empty. Its vacant octets are
 *                                         the window of ::StackTcpStartArgs at least [BORROWS].
 * @var StackTcpSlotArgs::transmit        the storage of the transmit ring, which the engine lays [BORROWS]
 * @var StackTcpSlotArgs::transmit_buffer the octets of the transmit ring, on a 4-octet boundary [BORROWS]
 * @var StackTcpSlotArgs::transmit_bytes  the octets at transmit_buffer. The engine splits them into
 *                                         PROTOCORE_STACK_TCP_SEGMENTS segments of one size.
 * @var StackTcpSlotArgs::index           the entry, below PROTOCORE_STACK_TCP_SLOTS
 */
typedef struct
{
    mmgr_ring *receive;
    mmgr_ring *transmit;
    uint8_t *transmit_buffer;
    size_t transmit_bytes;
    uint8_t index;
} StackTcpSlotArgs;

/**
 * @brief What protocore_stack_tcp_write takes.
 *
 * @var StackTcpWriteArgs::data  the octets to send [BORROWS]
 * @var StackTcpWriteArgs::len   the count of them
 * @var StackTcpWriteArgs::index the pool entry that sends them
 */
typedef struct
{
    const uint8_t *data;
    size_t len;
    uint8_t index;
} StackTcpWriteArgs;

/**
 * @brief What protocore_stack_tcp_close takes.
 *
 * @var StackTcpCloseArgs::index the pool entry that the owner has no more use for
 */
typedef struct
{
    uint8_t index;
} StackTcpCloseArgs;

/**
 * @brief What ::StackTcpNs::segment takes: the frame that the last Dispatch.input read.
 *
 * @var StackTcpSegmentArgs::frame  the frame, where the receive descriptor holds it [BORROWS]
 * @var StackTcpSegmentArgs::now_ms the monotonic millisecond count of the tick
 * @var StackTcpSegmentArgs::netif  the interface that the frame came in on
 */
typedef struct
{
    const uint8_t *frame;
    uint32_t now_ms;
    uint8_t netif;
} StackTcpSegmentArgs;

/**
 * @brief What ::StackTcpNs::service takes.
 *
 * @var StackTcpServiceArgs::now_ms the monotonic millisecond count of the tick
 */
typedef struct
{
    uint32_t now_ms;
} StackTcpServiceArgs;

/**
 * @brief The operands and the outcome of the entries on the namespace.
 *
 * @var StackTcpVars::start_args   the stack and the window that ::StackTcpNs::start takes
 * @var StackTcpVars::listen_args  the passive OPEN that ::StackTcpNs::listen takes
 * @var StackTcpVars::slot_args    the rings that ::StackTcpNs::slot_bind takes
 * @var StackTcpVars::segment_args the frame that ::StackTcpNs::segment takes
 * @var StackTcpVars::service_args the clock that ::StackTcpNs::service takes
 * @var StackTcpVars::event        the record that the last ::StackTcpNs::event_take read
 * @var StackTcpVars::sent         the segments that the engine put on a transmit ring since start
 * @var StackTcpVars::lost_events  the events that found the event ring full since start
 * @var StackTcpVars::ok           the outcome of the last call
 */
typedef struct
{
    StackTcpStartArgs start_args;
    StackTcpListenArgs listen_args;
    StackTcpSlotArgs slot_args;
    StackTcpSegmentArgs segment_args;
    StackTcpServiceArgs service_args;
    StackTcpEvent event;
    uint32_t sent;
    uint32_t lost_events;
    embed_bool ok;
} StackTcpVars;

/** @brief The operands and the outcome. */
extern StackTcpVars StackTcpV;

/**
 * @brief The entries. Each one runs on worker 0.
 *
 * @var StackTcpNs::start       clear the pool entries and the event ring, and take the stack and the window
 * @var StackTcpNs::listen      open one listener on each local address of one IP version
 * @var StackTcpNs::slot_bind   give one pool entry its two rings
 * @var StackTcpNs::event_take  read the next event into ::StackTcpVars::event. False when none waits.
 * @var StackTcpNs::segment     act on what the last Dispatch.input decided about a TCP segment
 * @var StackTcpNs::acknowledge send the aggregate ACK that the TCP_ACK step of the flush reports
 * @var StackTcpNs::service     send the transmit rings, open the receive windows, and end each TIME-WAIT
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
} StackTcpNs;

// The functions that the table binds. The .c defines each one, and each one takes one parameter.
void protocore_stack_tcp_start(uint8_t *work);
void protocore_stack_tcp_listen(uint8_t *work);
void protocore_stack_tcp_slot_bind(uint8_t *work);
void protocore_stack_tcp_event_take(uint8_t *work);
void protocore_stack_tcp_segment(uint8_t *work);
void protocore_stack_tcp_acknowledge(uint8_t *work);
void protocore_stack_tcp_service(uint8_t *work);

/**
 * @brief Puts octets on the transmit ring of a pool entry. The worker that owns the entry calls it.
 *
 * @param[in] args The entry and the octets [BORROWS].
 * @return         The octets that went on the ring. Fewer than args->len where the ring has no free segment.
 */
size_t protocore_stack_tcp_write(const StackTcpWriteArgs *args);

/**
 * @brief Puts the close mark on the transmit ring of a pool entry. The worker that owns the entry calls it,
 *        once.
 *
 * @param[in] args The entry [BORROWS].
 * @return         True when the mark went on the ring. False where the ring has no free segment.
 * @note The engine sends the FIN after the octets in front of the mark. After a reset, the mark frees the
 *       entry.
 */
embed_bool protocore_stack_tcp_close(const StackTcpCloseArgs *args);

/**
 * @brief The two ends of the connection of one entry.
 *
 * @var StackTcpEnds::local_ip    the local IPv4 address, network order
 * @var StackTcpEnds::remote_ip   the IPv4 address of the peer, network order
 * @var StackTcpEnds::local_port  the local port
 * @var StackTcpEnds::remote_port the port of the peer
 */
typedef struct
{
    uint8_t local_ip[4];
    uint8_t remote_ip[4];
    uint16_t local_port;
    uint16_t remote_port;
} StackTcpEnds;

/** @brief The text octets that the transmit ring of an open entry takes now. 0 for an entry with no room. */
size_t protocore_stack_tcp_room(uint8_t index);

/** @brief The segments on the transmit ring of an entry that the peer did not acknowledge yet. */
size_t protocore_stack_tcp_queued(uint8_t index);

/** @brief The text octets of an entry that the peer acknowledged, since the connection opened. */
uint32_t protocore_stack_tcp_released(uint8_t index);

/** @brief The two ends of the connection of an entry in @p ends. False for an entry with no TCB. */
embed_bool protocore_stack_tcp_ends(uint8_t index, StackTcpEnds *ends);

/** @brief RFC 9293 sec 3.10.4 ABORT: a reset, and the entry goes back at once. No event follows. */
void protocore_stack_tcp_abort(uint8_t index);

/**
 * @brief What an active OPEN takes.
 *
 * @var StackTcpConnectArgs::local_ip    the local IPv4 address, network order
 * @var StackTcpConnectArgs::remote_ip   the IPv4 address of the peer, network order
 * @var StackTcpConnectArgs::next_hop    the hardware address of the next hop to the peer
 * @var StackTcpConnectArgs::now_ms      the monotonic millisecond count of the caller
 * @var StackTcpConnectArgs::random_word a random word, which the choice of the ephemeral port takes
 * @var StackTcpConnectArgs::remote_port the port of the peer
 * @var StackTcpConnectArgs::netif       the interface of the connection
 */
typedef struct
{
    const uint8_t *local_ip;
    const uint8_t *remote_ip;
    const uint8_t *next_hop;
    uint32_t now_ms;
    uint32_t random_word;
    uint16_t remote_port;
    uint8_t netif;
} StackTcpConnectArgs;

/**
 * @brief RFC 9293 sec 3.10.1 active OPEN: the SYN goes out, and the entry waits in SYN-SENT.
 *
 * STACK_TCP_CONNECT follows once the handshake completes, and STACK_TCP_ERROR follows a reset or a timeout.
 *
 * @return the entry of the connection, or PROTOCORE_STACK_TCP_SLOT_NONE where no entry or no TCB is free
 */
uint8_t protocore_stack_tcp_connect(const StackTcpConnectArgs *args);

// The table is `static const` with its initializer here, and not `extern` against a definition in the .c.
// Each translation unit can see the initializer. `StackTcp.service(work)` thus resolves to a named
// function and becomes a direct call.
static const StackTcpNs StackTcp __attribute__((unused)) = {
    .start = protocore_stack_tcp_start,
    .listen = protocore_stack_tcp_listen,
    .slot_bind = protocore_stack_tcp_slot_bind,
    .event_take = protocore_stack_tcp_event_take,
    .segment = protocore_stack_tcp_segment,
    .acknowledge = protocore_stack_tcp_acknowledge,
    .service = protocore_stack_tcp_service,
};

/** @brief The borrow of this module: its context, taken once from the persistent tier. */
uint8_t *protocore_stack_tcp_span(void);

EMBED_END_DECLS

#endif // PROTOCORE_ENABLE_IDEMIP

#endif // PROTOCORE_STACK_TCP_H
