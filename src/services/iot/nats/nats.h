// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file nats.h
 * @brief NATS client protocol codec (PC_ENABLE_NATS) - zero-heap builder + parser for the
 *        text-based NATS pub/sub protocol, so a device can be a NATS client over the shipped
 *        outbound client transport.
 *
 * NATS is a small, line-oriented protocol; every control line ends with CRLF and fields are
 * space-separated:
 * @code
 *   CONNECT {json}                                  // client -> server
 *   PUB <subject> [reply-to] <#bytes>\r\n<payload>  // client -> server
 *   SUB <subject> [queue] <sid>                     // client -> server
 *   UNSUB <sid> [max]                               // client -> server
 *   MSG <subject> <sid> [reply-to] <#bytes>\r\n<payload>  // server -> client
 *   PING / PONG / +OK / -ERR <msg> / INFO {json}
 * @endcode
 * Only PUB and MSG carry a payload (the byte count precedes a CRLF, then that many payload
 * octets, then a trailing CRLF).
 *
 * The builders write a control line (plus payload) into a caller buffer; the parser decodes
 * one inbound message at the buffer head and reports the bytes consumed. Line formats per the
 * NATS client protocol reference.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_NATS_H
#define PROTOCORE_NATS_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_NATS

// ---- builders (return bytes written, or 0 on overflow / bad input) ----

/** @brief CONNECT: `CONNECT <options_json>\r\n`. */
size_t pc_nats_build_connect(char *buf, size_t cap, const char *options_json);

/** @brief PUB: `PUB <subject> [reply_to] <len>\r\n<payload>\r\n` (@p reply_to may be null). */
size_t pc_nats_build_pub(char *buf, size_t cap, const char *subject, const char *reply_to, const uint8_t *payload,
                         size_t payload_len);

/**
 * @brief HPUB (headers publish, NATS 2.2+): `HPUB <subject> [reply_to] <hdr_len> <total_len>\r\n<headers>
 *        <payload>\r\n`. @p headers is the pre-formatted header block (e.g. `NATS/1.0\r\nKey: Value\r\n\r\n`),
 *        which must be non-empty; the two length fields are the header-block length and header+payload.
 */
size_t pc_nats_build_hpub(char *buf, size_t cap, const char *subject, const char *reply_to, const char *headers,
                          size_t headers_len, const uint8_t *payload, size_t payload_len);

/** @brief SUB: `SUB <subject> [queue] <sid>\r\n` (@p queue may be null). */
size_t pc_nats_build_sub(char *buf, size_t cap, const char *subject, const char *queue, const char *sid);

/** @brief UNSUB: `UNSUB <sid> [max]\r\n` (@p with_max controls the optional max-messages field). */
size_t pc_nats_build_unsub(char *buf, size_t cap, const char *sid, uint32_t max_msgs, proto_bool with_max);

/** @brief PING: `PING\r\n`. */
size_t pc_nats_build_ping(char *buf, size_t cap);

/** @brief PONG: `PONG\r\n`. */
size_t pc_nats_build_pong(char *buf, size_t cap);

/** @brief Inbound message kind. */
typedef enum PROTO_ENUM_PACKED
{
    NATS_MSG,  ///< a delivered message (subject/sid/reply/payload set)
    NATS_INFO, ///< server INFO (arg = the JSON)
    NATS_PING,
    NATS_PONG,
    NATS_OK,      ///< +OK
    NATS_ERR,     ///< -ERR (arg = the message)
    NATS_UNKNOWN, ///< unrecognized verb
} NatsMsgType;

/** @brief One parsed inbound protocol message. String/payload fields point INTO the buffer. */
typedef struct
{
    NatsMsgType type;
    const char *subject; ///< MSG only
    size_t subject_len;
    const char *sid; ///< MSG only
    size_t sid_len;
    const char *reply; ///< MSG reply-to (may be empty)
    size_t reply_len;
    const uint8_t *payload; ///< MSG payload
    size_t payload_len;
    const char *headers; ///< HMSG header block (`NATS/1.0\r\n...`); nullptr for a header-less MSG
    size_t headers_len;
    const char *arg; ///< INFO / -ERR argument (the rest of the control line)
    size_t arg_len;
} NatsMsg;

/**
 * @brief Parse one inbound message at the head of [buf, buf+len).
 * @param consumed receives the message length (control line + any payload) so the caller can advance.
 * @return true on a complete message; false if the control line or the MSG payload is not yet buffered.
 */
proto_bool pc_nats_parse(const char *buf, size_t len, NatsMsg *out, size_t *consumed);

#endif // PC_ENABLE_NATS

PROTO_END_DECLS

#endif // PROTOCORE_NATS_H
