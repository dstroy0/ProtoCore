// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file stomp.h
 * @brief STOMP 1.2 frame codec (PC_ENABLE_STOMP) - zero-heap frame builder + parser,
 *        so a device can talk to a STOMP broker (ActiveMQ / RabbitMQ / Artemis) over the
 *        shipped outbound client transport (TCP, or STOMP-over-WebSocket via the WS client).
 *
 * STOMP 1.2 (https://stomp.github.io/stomp-specification-1.2.html) frame:
 * @code
 *   COMMAND\n
 *   header:value\n
 *   header:value\n
 *   \n
 *   body^@            // body ends at the NUL octet (0x00)
 * @endcode
 *  - The command and header lines end with EOL (`\n`, optionally preceded by `\r`).
 *  - A blank line separates the headers from the body; the body ends at a NUL.
 *  - When a `content-length` header is present the body is exactly that many octets
 *    (so it may itself contain NULs); otherwise the body runs to the first NUL.
 *  - In header keys/values these octets are escaped: `\r`(CR) `\n`(LF) `\c`(:) `\\`(\\).
 *
 * The parser is non-mutating: it reports the command, header key/value slices, and body
 * as pointers INTO the source buffer (header values are still escaped - decode one with
 * @ref pc_stomp_unescape when needed). The builder escapes header keys/values for you.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_STOMP_H
#define PROTOCORE_STOMP_H

#include "protocore_config.h"

#if PC_ENABLE_STOMP

#include <stddef.h>
#include <stdint.h>

/** @brief One parsed header line: key/value slices point INTO the source buffer (raw, still escaped). */
struct StompHeader
{
    const char *key;
    size_t key_len;
    const char *val;
    size_t val_len;
};

/** @brief One parsed STOMP frame. All pointers reference the source buffer (nothing copied). */
struct StompFrame
{
    const char *command; ///< command verb (e.g. "MESSAGE"); not NUL-terminated
    size_t command_len;
    StompHeader headers[PC_STOMP_MAX_HEADERS];
    size_t header_count; ///< number of parsed headers (capped at PC_STOMP_MAX_HEADERS)
    const char *body;    ///< frame body (may contain NULs when content-length is given)
    size_t body_len;
};

/**
 * @brief Build a STOMP frame: `COMMAND\n` + each `key:value\n` (keys/values escaped) + `\n` + body + NUL.
 *
 * @param header_keys NUL-terminated header names (argv-style), length @p nheaders.
 * @param header_vals NUL-terminated header values, parallel to @p header_keys.
 * @param body        body bytes, or nullptr for an empty body.
 * @param body_len    body length in bytes.
 * @return bytes written (including the terminating NUL), or 0 on overflow / bad input.
 */
size_t pc_stomp_build_frame(char *buf, size_t cap, const char *command, const char *const *header_keys,
                            const char *const *header_vals, size_t nheaders, const char *body, size_t body_len);

/**
 * @brief Parse one STOMP frame at the head of [buf, buf+len).
 *
 * Leading EOL octets (broker heart-beats / inter-frame newlines) are skipped and counted
 * in @p consumed.
 *
 * @param consumed receives the bytes the frame occupied (past its terminating NUL), so the
 *                 caller can advance to the next frame.
 * @return true on a complete frame; false if the buffer holds an incomplete or malformed
 *         frame (then @p out / @p consumed are unspecified).
 */
bool pc_stomp_parse_frame(const char *buf, size_t len, StompFrame *out, size_t *consumed);

/**
 * @brief Find a header by name; returns the RAW (still escaped) value slice.
 * @return true and fills @p val / @p val_len on the first match (per spec), else false.
 */
bool pc_stomp_header(const StompFrame *f, const char *name, const char **val, size_t *val_len);

/**
 * @brief Decode STOMP 1.2 header escapes (`\r` `\n` `\c` `\\`) from @p src into @p dst.
 * @return decoded length, or 0 on overflow or an invalid escape sequence.
 */
size_t pc_stomp_unescape(char *dst, size_t cap, const char *src, size_t src_len);

#endif // PC_ENABLE_STOMP

#endif // PROTOCORE_STOMP_H
