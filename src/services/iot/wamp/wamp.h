// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wamp.h
 * @brief WAMP (Web Application Messaging Protocol) codec (PROTOCORE_ENABLE_WAMP) - zero-heap
 *        message builders plus a positional element reader, riding the shipped WebSocket layer.
 *
 * WAMP is specified by the WAMP project at wamp-proto.org, not by the IETF. The document is
 * distributed in Internet-Draft format as "WAMP Basic Profile" and carries no RFC number; every
 * section cited in this module is that document's.
 *
 * WAMP sec 3.3: a message is a list whose first element is the message type code, and the
 * application payload is always the tail of that list. SUBSCRIBE is
 * `[SUBSCRIBE, Request|id, Options|dict, Topic|uri]` (sec 3.4.2.3). The builders drive the shared
 * @ref Json writer to emit these lists into the caller's buffer: Options|dict and Details|dict
 * default to `{}`, and Arguments|list / ArgumentsKw|dict are pre-formatted JSON literals or are
 * left off (sec 3.7). The reader is a positional scanner over one received list - the message
 * type code, an id at a position, or a URI - which is what WELCOME, SUBSCRIBED, EVENT, RESULT,
 * INVOCATION and ERROR handling reads.
 *
 * WAMP sec 2.3.1 names the WebSocket subprotocol this JSON serialization rides, `wamp.2.json`,
 * where every frame is a text frame. The connection and the session / subscription / registration
 * tables are the application's; this is the message codec.
 *
 * The module exports one symbol, @ref Wamp. Everything in wamp.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_WAMP_H
#define PROTOCORE_WAMP_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_WAMP

PROTOCORE_BEGIN_DECLS

// Message type codes, WAMP sec 3.5 (the Basic Profile table).
#define WAMP_HELLO 1
#define WAMP_WELCOME 2
#define WAMP_ABORT 3
#define WAMP_GOODBYE 6
#define WAMP_ERROR 8
#define WAMP_PUBLISH 16
#define WAMP_PUBLISHED 17
#define WAMP_SUBSCRIBE 32
#define WAMP_SUBSCRIBED 33
#define WAMP_UNSUBSCRIBE 34
#define WAMP_UNSUBSCRIBED 35
#define WAMP_EVENT 36
#define WAMP_CALL 48
#define WAMP_RESULT 50
#define WAMP_REGISTER 64
#define WAMP_REGISTERED 65
#define WAMP_UNREGISTER 66
#define WAMP_UNREGISTERED 67
#define WAMP_INVOCATION 68
#define WAMP_YIELD 70

/** @brief Where a built message list lands. */
typedef struct
{
    char *buf;  ///< the buffer a build writes the message list into
    size_t cap; ///< how much room it has, the NUL included
} WampOutArgs;

/** @brief The ids a message names: integers in 1..2^53 (WAMP sec 2.1.2). */
typedef struct
{
    uint64_t request;      ///< Request|id, the outgoing request every non-session message carries
    uint64_t subscription; ///< SUBSCRIBED.Subscription|id, what an UNSUBSCRIBE drops (sec 3.4.2.5)
    uint64_t registration; ///< REGISTERED.Registration|id, what an UNREGISTER drops (sec 3.4.3.5)
} WampIdArgs;

/** @brief The URI element a message names (WAMP sec 2.1.1). */
typedef struct
{
    const char *realm;     ///< Realm|uri, the realm a HELLO joins (sec 3.4.1.1)
    const char *reason;    ///< Reason|uri, why a GOODBYE closes the session (sec 3.4.1.4)
    const char *topic;     ///< Topic|uri, what a SUBSCRIBE or a PUBLISH names (sec 3.4.2.1, 3.4.2.3)
    const char *procedure; ///< Procedure|uri, what a CALL or a REGISTER names (sec 3.4.3.1, 3.4.3.3)
} WampUriArgs;

/** @brief The dict and list elements a message carries, each a pre-formatted JSON literal. */
typedef struct
{
    const char *details;      ///< Details|dict of a HELLO or a GOODBYE; NULL emits `{}`
    const char *options;      ///< Options|dict of a SUBSCRIBE, PUBLISH, CALL, REGISTER or YIELD; NULL emits `{}`
    const char *arguments;    ///< Arguments|list, the payload's positional half; NULL leaves the element off
    const char *arguments_kw; ///< ArgumentsKw|dict, its keyword half; NULL leaves the element off
} WampPayloadArgs;

/** @brief One received message list and the element position a read names (WAMP sec 3.3). */
typedef struct
{
    const char *msg; ///< the received message, a NUL-terminated JSON list
    size_t index;    ///< the element a read names, 0 being the message type code
    char *uri_out;   ///< where a URI read copies the element, quotes stripped
    size_t uri_cap;  ///< how much room that has, the NUL included
} WampParseArgs;

/** @brief The codec's calls, described only in wamp.c. */
struct WampInternal;

/**
 * @brief The WAMP message codec: the builders and the positional element reader.
 *
 * A caller sets the members a call takes, invokes it through ::Wamp, and reads the outcome off the
 * same handle.
 *
 * No slot member: the codec owns no rows, so a build names its buffer and a read names its message.
 *
 * @var WampNs::out       the buffer a build writes into
 * @var WampNs::id        the ids a message names (sec 2.1.2)
 * @var WampNs::uri       the URI element a message names (sec 2.1.1)
 * @var WampNs::payload   the Details / Options dicts and the Arguments / ArgumentsKw payload
 * @var WampNs::parse     the received message, the element a read names, and where a URI lands
 * @var WampNs::ok        a call's true/false outcome
 * @var WampNs::n         the byte count a build wrote, or the length of the element @c text names
 * @var WampNs::u64       the id a read reports (sec 2.1.2)
 * @var WampNs::i32       the message type code a read reports (sec 3.5)
 * @var WampNs::text      the raw element a slice names, pointing into @c parse.msg
 * @var WampNs::build_hello        `[HELLO, Realm|uri, Details|dict]` (sec 3.4.1.1)
 * @var WampNs::build_goodbye      `[GOODBYE, Details|dict, Reason|uri]` (sec 3.4.1.4)
 * @var WampNs::build_subscribe    `[SUBSCRIBE, Request|id, Options|dict, Topic|uri]` (sec 3.4.2.3)
 * @var WampNs::build_unsubscribe  `[UNSUBSCRIBE, Request|id, SUBSCRIBED.Subscription|id]` (sec 3.4.2.5)
 * @var WampNs::build_publish      `[PUBLISH, Request|id, Options|dict, Topic|uri]`, the payload
 *                                 appended when set (sec 3.4.2.1)
 * @var WampNs::build_call         `[CALL, Request|id, Options|dict, Procedure|uri]`, the payload
 *                                 appended when set (sec 3.4.3.1)
 * @var WampNs::build_register     `[REGISTER, Request|id, Options|dict, Procedure|uri]` (sec 3.4.3.3)
 * @var WampNs::build_unregister   `[UNREGISTER, Request|id, REGISTERED.Registration|id]` (sec 3.4.3.5)
 * @var WampNs::build_yield        `[YIELD, INVOCATION.Request|id, Options|dict]`, the payload
 *                                 appended when set (sec 3.4.3.8)
 * @var WampNs::element   slice the raw element at @c parse.index into @c text and @c n
 * @var WampNs::get_type  read the message type code into @c i32, naming element 0 itself (sec 3.5)
 * @var WampNs::get_id    read the id at @c parse.index into @c u64 (sec 2.1.2)
 * @var WampNs::get_uri   copy the URI at @c parse.index into @c parse.uri_out, quotes stripped
 * @var WampNs::internal  the codec's calls
 */
typedef struct
{
    WampOutArgs out;         ///< where a built message lands
    WampIdArgs id;           ///< the ids a message names
    WampUriArgs uri;         ///< the URI a message names
    WampPayloadArgs payload; ///< the dicts and the payload lists a message carries
    WampParseArgs parse;     ///< the received message and the element a read names

    proto_bool ok;
    size_t n;
    uint64_t u64;
    int32_t i32;
    const char *text;

    void (*build_hello)(struct WampInternal *ctx);
    void (*build_goodbye)(struct WampInternal *ctx);
    void (*build_subscribe)(struct WampInternal *ctx);
    void (*build_unsubscribe)(struct WampInternal *ctx);
    void (*build_publish)(struct WampInternal *ctx);
    void (*build_call)(struct WampInternal *ctx);
    void (*build_register)(struct WampInternal *ctx);
    void (*build_unregister)(struct WampInternal *ctx);
    void (*build_yield)(struct WampInternal *ctx);

    void (*element)(struct WampInternal *ctx);
    void (*get_type)(struct WampInternal *ctx);
    void (*get_id)(struct WampInternal *ctx);
    void (*get_uri)(struct WampInternal *ctx);

    struct WampInternal *internal;
} WampNs;

/** @brief The one symbol this module exports. */
extern WampNs Wamp;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_WAMP

#endif // PROTOCORE_WAMP_H
