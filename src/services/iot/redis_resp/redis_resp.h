// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file redis_resp.h
 * @brief Redis RESP2/RESP3 wire codec (PC_ENABLE_REDIS) - zero-heap command
 *        encoder + reply parser, so a device can talk to a Redis server with the
 *        shipped outbound client transport.
 *
 * RESP (https://redis.io/docs/latest/develop/reference/protocol-spec):
 *  - A command is an array of bulk strings: `*<n>\r\n$<len>\r\n<arg>\r\n...`.
 *  - A RESP2 reply is one of: simple string `+OK\r\n`, error `-ERR ...\r\n`,
 *    integer `:<n>\r\n`, bulk string `$<len>\r\n<bytes>\r\n` (`$-1\r\n` = nil), or
 *    array `*<n>\r\n<elements>` (`*-1\r\n` = nil).
 *  - RESP3 adds: null `_\r\n`, boolean `#t\r\n`/`#f\r\n`, double `,<x>\r\n`, big
 *    number `(<digits>\r\n`, bulk error `!<len>\r\n...`, verbatim string
 *    `=<len>\r\n<fmt>:<text>\r\n`, map `%<pairs>\r\n`, set `~<n>\r\n`, push `><n>\r\n`.
 *
 * The parser is a cursor: it decodes one value at the buffer head and reports how
 * many bytes it consumed; for an aggregate (array / set / push / map) it reports
 * the following child count and the caller parses each child from the remaining
 * bytes (no recursion state, no heap). A map of N pairs reports count 2*N.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_REDIS_RESP_H
#define PROTOCORE_REDIS_RESP_H

#include "protocore_config.h"

#if PC_ENABLE_REDIS

#include <stddef.h>
#include <stdint.h>

/** @brief RESP2/RESP3 reply value types. */
enum class RespType : uint8_t
{
    RESP_SIMPLE,  ///< simple string (+); value in str/str_len
    RESP_ERROR,   ///< error (-); message in str/str_len
    RESP_INTEGER, ///< integer (:); value in ival
    RESP_BULK,    ///< bulk string ($); bytes in str/str_len
    RESP_ARRAY,   ///< array (*); element count in count (parse each from the remainder)
    RESP_NIL,     ///< null bulk string ($-1), null array (*-1), or RESP3 null (_)
    // RESP3 additions:
    RESP_BOOL,       ///< boolean (#); 0/1 in ival
    RESP_DOUBLE,     ///< double (,); value in dval, text in str/str_len
    RESP_BIG_NUMBER, ///< big number ((); digits in str/str_len
    RESP_BULK_ERROR, ///< bulk error (!); message in str/str_len
    RESP_VERBATIM,   ///< verbatim string (=); str includes the 3-char format + ':'
    RESP_MAP,        ///< map (%); count = 2 * pairs = following child count
    RESP_SET,        ///< set (~); element count in count
    RESP_PUSH,       ///< push (>); element count in count
};

/** @brief One decoded RESP value. String fields point INTO the source buffer (not copied). */
struct RespReply
{
    RespType type;
    int64_t ival;    ///< value for RespType::RESP_INTEGER; 0/1 for RespType::RESP_BOOL; child count for aggregates
    double dval;     ///< value for RespType::RESP_DOUBLE (best-effort; str is authoritative)
    const char *str; ///< bytes for simple/error/bulk/big-number/bulk-error/verbatim/double text
    size_t str_len;  ///< length of @ref str
    int64_t count;   ///< child count for RespType::RESP_ARRAY / RespType::RESP_SET / RespType::RESP_PUSH /
                     ///< RespType::RESP_MAP (map = 2*pairs)
};

/**
 * @brief Encode a command (array of bulk strings) into @p buf.
 *
 * @param args     argument byte pointers (argv).
 * @param arg_lens per-arg byte lengths, or nullptr to measure each NUL-terminated arg.
 * @param argc     number of arguments.
 * @return bytes written (excluding any NUL), or 0 on overflow / bad input.
 */
size_t pc_resp_encode_command(char *buf, size_t cap, const char *const *args, const size_t *arg_lens, size_t argc);

/**
 * @brief Parse one RESP value at the head of [buf, buf+len).
 *
 * @param consumed receives the number of bytes the value occupied (so the caller
 *                 can advance to the next value / array element).
 * @return true on a complete value; false if the buffer holds an incomplete or
 *         malformed value (then @p out / @p consumed are unspecified).
 */
bool pc_resp_parse(const uint8_t *buf, size_t len, RespReply *out, size_t *consumed);

#endif // PC_ENABLE_REDIS

#endif // PROTOCORE_REDIS_RESP_H
