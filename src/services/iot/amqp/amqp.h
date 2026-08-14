// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file amqp.h
 * @brief AMQP 0-9-1 wire-level framing (PROTOCORE_ENABLE_AMQP): the frames a client builds and the
 *        frames it reads back, into and out of caller buffers.
 *
 * The governing document is not an IETF RFC. It is "AMQP, Advanced Message Queuing Protocol,
 * Protocol Specification, Version 0-9-1, 13 November 2008", published by the AMQP Working Group.
 * Every section number below is that document's. AMQP 1.0 is a separate OASIS Standard (OASIS
 * Advanced Message Queuing Protocol (AMQP) Version 1.0 Part 2: Transport, OASIS Standard,
 * 29 October 2012) whose frame carries an 8 octet fixed header and no frame-end octet; nothing
 * here speaks it.
 *
 * Sec 4.2.2, the 8 octets a client opens the connection with:
 * @code
 *   protocol-header  = literal-AMQP protocol-id protocol-version
 *   literal-AMQP     = %d65.77.81.80              ; "AMQP"
 *   protocol-id      = %d0
 *   protocol-version = %d0.9.1
 * @endcode
 *
 * Sec 2.3.5 and sec 4.2.3, every frame after it, the size field counting the payload alone:
 * @code
 *   type(1)  channel(2)  size(4)  payload(size)  frame-end(1)
 * @endcode
 * with type an octet, channel and size held high byte first (sec 4.2.5.1 holds every integer in
 * network byte order), and `frame-end = %xCE` (sec 4.2.1). Sec 4.2.3 states the frame-end octet
 * MUST always be %xCE and that a peer MUST check it before decoding the frame, which is the check
 * ::AmqpNs::parse_frame makes.
 *
 * Sec 4.2.3 names the four frame types METHOD, HEADER, BODY and HEARTBEAT. Its prose gives
 * HEARTBEAT as type 4, while the sec 4.2.1 grammar reads `heartbeat = %d8 %d0 %d0 frame-end` and
 * the specification's own constant table reads `frame-heartbeat = 8`. @ref AMQP_FRAME_HEARTBEAT
 * takes 8.
 *
 * A method payload is `class-id(2) method-id(2)` then the method's arguments (sec 2.3.5.1, sec
 * 4.2.4). A content header payload is `class-id(2) weight(2) body-size(8) property-flags(2)` then
 * the property list (sec 4.2.6.1), the weight field unused and zero, the body size the sum of the
 * body sizes of the content body frames that follow. A heartbeat carries channel 0 and an empty
 * payload (sec 4.2.7).
 *
 * The arguments in a method payload and the property list in a content header are the
 * application's octets; this module frames them and validates the framing.
 *
 * The module exports one symbol, @ref Amqp. Everything in amqp.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_AMQP_H
#define PROTOCORE_AMQP_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_AMQP

// Frame types, octet 0 of a frame (sec 4.2.3).
#define AMQP_FRAME_METHOD 1    ///< method frame
#define AMQP_FRAME_HEADER 2    ///< content header frame
#define AMQP_FRAME_BODY 3      ///< content body frame
#define AMQP_FRAME_HEARTBEAT 8 ///< heartbeat frame

#define AMQP_FRAME_END 0xCE   ///< `frame-end = %xCE` (sec 4.2.1)
#define AMQP_FRAME_OVERHEAD 8 ///< type(1) + channel(2) + size(4) + frame-end(1)

/** @brief Where a build writes its frame. */
typedef struct
{
    uint8_t *buf; ///< the buffer the octets land in
    size_t cap;   ///< how many octets it holds
} AmqpOutArgs;

/** @brief The octets a parse reads, one frame at their head. */
typedef struct
{
    const uint8_t *buf; ///< the first octet of a frame
    size_t len;         ///< how many octets are buffered from there
} AmqpInArgs;

/** @brief Sec 4.2.3: the type octet and the channel a frame header carries. */
typedef struct
{
    uint16_t channel; ///< 0 for frames global to the connection, 1..65535 otherwise
    uint8_t type;     ///< METHOD, HEADER, BODY or HEARTBEAT
} AmqpFrameArgs;

/** @brief Sec 4.2.3: the payload the size field counts, the frame-end octet excluded. */
typedef struct
{
    const uint8_t *data; ///< the payload octets; a parse points this into @ref AmqpInArgs::buf
    size_t len;          ///< how many of them there are
} AmqpPayloadArgs;

/** @brief Sec 4.2.4: a method payload, `class-id method-id *amqp-field`. */
typedef struct
{
    const uint8_t *args; ///< the encoded method arguments
    size_t args_len;     ///< their octet count
    uint16_t class_id;   ///< the class the method belongs to
    uint16_t method_id;  ///< the method within that class
} AmqpMethodArgs;

/** @brief Sec 4.2.6.1: a content header payload, less the unused weight field. */
typedef struct
{
    uint64_t body_size;           ///< total octets of the content body frames that follow
    const uint8_t *property_list; ///< the encoded values of the set property flags
    size_t property_list_len;     ///< their octet count
    uint16_t class_id;            ///< matches the class-id of the method frame it follows
    uint16_t property_flags;      ///< bit 15 marks the first property, bit 0 marks a further flags field
} AmqpContentArgs;

/** @brief The codec's calls and the handle they read, described only in amqp.c. */
struct AmqpInternal;

/**
 * @brief The AMQP 0-9-1 frame codec.
 *
 * A caller sets the members a call takes, invokes it through ::Amqp, and reads the outcome off the
 * same handle. A build reads @c out and writes @c n; a parse reads @c in and writes @c frame,
 * @c payload and @c consumed. @c payload carries a build's payload in and a parse's payload out, so
 * a parse_frame of a METHOD frame hands parse_method its payload with nothing to move.
 *
 * No slot member: the codec holds no rows, so no call names one.
 *
 * @var AmqpNs::out       the buffer a build writes its frame into
 * @var AmqpNs::in        the octets a parse reads a frame from
 * @var AmqpNs::frame     the type and channel of the frame (sec 4.2.3)
 * @var AmqpNs::payload   the payload octets the size field counts (sec 4.2.3)
 * @var AmqpNs::method    the class-id, method-id and arguments of a method payload (sec 4.2.4)
 * @var AmqpNs::content   the class-id, body size, property flags and property list of a content
 *                        header payload (sec 4.2.6.1)
 * @var AmqpNs::ok        a call's true/false outcome
 * @var AmqpNs::n         the octets a build wrote, 0 when it wrote none
 * @var AmqpNs::consumed  the whole frame length a parse read, for advancing over it
 * @var AmqpNs::protocol_header      write the 8 octet protocol-header "AMQP" 0 0 9 1 (sec 4.2.2)
 * @var AmqpNs::build_frame          frame @c payload under @c frame.type and @c frame.channel
 * @var AmqpNs::build_method         frame a METHOD payload from @c method (sec 4.2.4)
 * @var AmqpNs::build_content_header frame a HEADER payload from @c content, weight 0 (sec 4.2.6.1)
 * @var AmqpNs::build_heartbeat      frame a heartbeat, channel 0, empty payload (sec 4.2.7)
 * @var AmqpNs::parse_frame          split one frame off @c in, the %xCE frame-end checked first
 * @var AmqpNs::parse_method         split @c payload into class-id, method-id and arguments
 * @var AmqpNs::internal             the calls and the handle they read
 */
typedef struct
{
    AmqpOutArgs out;         ///< where a build writes
    AmqpInArgs in;           ///< what a parse reads
    AmqpFrameArgs frame;     ///< the frame header fields
    AmqpPayloadArgs payload; ///< the framed octets
    AmqpMethodArgs method;   ///< a method payload's fields
    AmqpContentArgs content; ///< a content header payload's fields

    proto_bool ok;
    size_t n;
    size_t consumed;

    void (*protocol_header)(struct AmqpInternal *ctx);
    void (*build_frame)(struct AmqpInternal *ctx);
    void (*build_method)(struct AmqpInternal *ctx);
    void (*build_content_header)(struct AmqpInternal *ctx);
    void (*build_heartbeat)(struct AmqpInternal *ctx);
    void (*parse_frame)(struct AmqpInternal *ctx);
    void (*parse_method)(struct AmqpInternal *ctx);

    struct AmqpInternal *internal;
} AmqpNs;

/** @brief The one symbol this module exports. */
extern AmqpNs Amqp;

#endif // PROTOCORE_ENABLE_AMQP

PROTOCORE_END_DECLS

#endif // PROTOCORE_AMQP_H
