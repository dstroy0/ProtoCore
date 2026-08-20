// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#if PROTOCORE_ENABLE_REDIS

PROTOCORE_BEGIN_DECLS

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
 */
typedef struct
{
    RespCommandArgs command; ///< what a client sends
    RespOutArgs out;         ///< where the encoded command lands
    RespWireArgs wire;       ///< what a parse reads
    proto_bool ok;
    size_t n;
    RespReply reply;
} RespVars;

/** @brief The operands and the outcome. */
extern RespVars RespV;

/** @brief The entries. */
typedef struct
{
    void (*const encode_command)(uint8_t *restrict work);
    void (*const parse_reply)(uint8_t *restrict work);
} RespNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in RespV or a region of the borrow at a fixed offset.
void protocore_redis_resp_encode_command(uint8_t *restrict work);
void protocore_redis_resp_parse_reply(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Resp.encode_command(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const RespNs Resp __attribute__((unused)) = {
    .encode_command = protocore_redis_resp_encode_command,
    .parse_reply = protocore_redis_resp_parse_reply,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_REDIS

#endif // PROTOCORE_REDIS_RESP_H
