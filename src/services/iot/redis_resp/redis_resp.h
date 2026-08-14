// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file redis_resp.h
 * @brief RESP, the Redis serialization protocol: the command encoder and the reply parser
 *        (PROTOCORE_ENABLE_REDIS).
 *
 * The governing specification is not an IETF document and carries no RFC number. It is the Redis
 * project's "Redis serialization protocol specification"
 * (https://redis.io/docs/latest/develop/reference/protocol-spec), whose RESP3 half is also
 * specified in redis/redis-specifications, protocol/RESP3.md. Section names below are that
 * document's headings.
 *
 * "Sending commands to a Redis server": a client sends the server "an array consisting of only bulk
 * strings", the first bulk string being the command's name and the rest its arguments, so
 * `LLEN mylist` goes out as `*2\r\n$4\r\nLLEN\r\n$6\r\nmylist\r\n`. An encode builds that array.
 *
 * "RESP protocol description" gives the first byte of every type. RESP2 (Redis 2.0): Simple strings
 * `+`, Simple errors `-`, Integers `:`, Bulk strings `$` with Null bulk strings `$-1\r\n`, and
 * Arrays `*` with Null arrays `*-1\r\n`. RESP3 (Redis 6.0, negotiated by the HELLO command of
 * "Client handshake") adds Nulls `_`, Booleans `#`, Doubles `,`, Big numbers `(`, Bulk errors `!`,
 * Verbatim strings `=`, Maps `%`, Sets `~` and Pushes `>`. Any other first byte, the Attributes
 * type `|` included, fails the parse.
 *
 * The parser is a cursor. One call decodes the value at the head of the buffered octets and reports
 * how many octets that value occupied. An aggregate header (Arrays, Sets, Pushes, Maps) reports the
 * header alone and the count of children that follow, and the caller parses each child from the
 * remaining octets: a map of N entries reports 2*N children, one per key and one per value. Nothing
 * recurses and nothing is allocated.
 *
 * A decoded string points into the buffer that was parsed and is never copied, so a caller walking
 * an aggregate takes what it needs from @ref RespNs::reply before the next parse overwrites it.
 *
 * The module exports one symbol, @ref Resp. Everything in redis_resp.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_REDIS_RESP_H
#define PROTOCORE_REDIS_RESP_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_REDIS

/** @brief The decoded type, named after the "RESP protocol description" table of first bytes. */
typedef enum PROTO_ENUM_PACKED
{
    RESP_SIMPLE_STRING,   ///< Simple strings (+); the text in str/str_len
    RESP_SIMPLE_ERROR,    ///< Simple errors (-); the message in str/str_len
    RESP_INTEGER,         ///< Integers (:); the signed 64-bit value in ival
    RESP_BULK_STRING,     ///< Bulk strings ($); the octets in str/str_len
    RESP_ARRAY,           ///< Arrays (*); the element count in count
    RESP_NULL,            ///< Null bulk strings ($-1), Null arrays (*-1), and RESP3 Nulls (_)
    RESP_BOOLEAN,         ///< Booleans (#); 0 or 1 in ival
    RESP_DOUBLE,          ///< Doubles (,); the text in str/str_len and its value in dval
    RESP_BIG_NUMBER,      ///< Big numbers ((); the digits in str/str_len
    RESP_BULK_ERROR,      ///< Bulk errors (!); the message in str/str_len
    RESP_VERBATIM_STRING, ///< Verbatim strings (=); str holds the 3-octet encoding, ':' and the data
    RESP_MAP,             ///< Maps (%); count = 2 * entries
    RESP_SET,             ///< Sets (~); the element count in count
    RESP_PUSH,            ///< Pushes (>); the element count in count
} RespType;

/** @brief One decoded value. Every string member points into the parsed octets; nothing is copied. */
typedef struct
{
    RespType type;   ///< which type the first byte named
    int64_t ival;    ///< the Integers value, 0 or 1 for Booleans, the child count for an aggregate
    double dval;     ///< the Doubles value, decoded from str, which stays authoritative
    const char *str; ///< the octets of a string, an error, a big number, a verbatim string or a double
    size_t str_len;  ///< how many octets @ref RespReply::str holds
    int64_t count;   ///< children following an Arrays, Sets, Pushes or Maps header (Maps = 2 * entries)
} RespReply;

/** @brief "Sending commands to a Redis server": the array of bulk strings a client sends. */
typedef struct
{
    const char *const *argv; ///< the bulk strings, the command's name first, then its arguments
    const size_t *argv_len;  ///< per-string octet counts, or NULL to measure each NUL-terminated string
    size_t argc;             ///< how many bulk strings the array carries
} RespCommandArgs;

/** @brief Where an encoded command lands. */
typedef struct
{
    char *buf;  ///< the buffer an encode writes the command into
    size_t cap; ///< how much room it has, the terminating NUL included
} RespOutArgs;

/** @brief The buffered reply octets a parse reads. */
typedef struct
{
    const uint8_t *buf; ///< the head of the octets still to decode
    size_t len;         ///< how many of them are buffered
} RespWireArgs;

/** @brief The codec's own state and the calls that reach it, described only in redis_resp.c. */
struct RespInternal;

/**
 * @brief The RESP codec: one command out, one value in.
 *
 * A caller sets the members a call takes, invokes it through ::Resp, and reads the outcome off the
 * same handle.
 *
 * No slot member: the codec holds no rows, so no call names one.
 *
 * @var RespNs::command  the array of bulk strings an encode builds ("Sending commands to a Redis server")
 * @var RespNs::out      the buffer that array is written into
 * @var RespNs::wire     the buffered reply octets a parse decodes from
 * @var RespNs::ok       a call's true/false outcome
 * @var RespNs::n        the octets an encode wrote excluding the NUL, or the octets a parse consumed, 0 on failure
 * @var RespNs::reply    the value a parse decoded
 * @var RespNs::encode_command  build `*<argc>\r\n$<len>\r\n<arg>\r\n...` from @c command into @c out
 * @var RespNs::parse_reply     decode the one value at the head of @c wire into @c reply
 * @var RespNs::internal        the codec's state and the calls that reach it
 */
typedef struct
{
    RespCommandArgs command; ///< what a client sends
    RespOutArgs out;         ///< where the encoded command lands
    RespWireArgs wire;       ///< what a parse reads

    proto_bool ok;
    size_t n;
    RespReply reply;

    void (*encode_command)(struct RespInternal *ctx);
    void (*parse_reply)(struct RespInternal *ctx);

    struct RespInternal *internal;
} RespNs;

/** @brief The one symbol this module exports. */
extern RespNs Resp;

#endif // PROTOCORE_ENABLE_REDIS

PROTOCORE_END_DECLS

#endif // PROTOCORE_REDIS_RESP_H
