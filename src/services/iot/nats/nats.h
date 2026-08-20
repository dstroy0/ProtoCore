// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file nats.h
 * @brief The NATS client protocol (PROTOCORE_ENABLE_NATS): the client-to-server operations a device
 *        writes, and the server-to-client operations it reads.
 *
 * NATS is not an IETF protocol and no RFC governs it. The specification followed here is the NATS
 * project's client protocol reference, "NATS Protocol" (docs.nats.io, Reference > Protocols >
 * Client), whose Overview and one section per operation - INFO, CONNECT, PUB, HPUB, SUB, UNSUB, MSG,
 * HMSG, PING/PONG, +OK/ERR - carry the syntax written below. The wire conventions come from that
 * document's "Protocol conventions" section, kept in the nats-io/nats.docs repository at
 * docs/nats_protocol/nats-protocol.html.
 *
 * Protocol conventions: a space or a tab delimits the fields of a protocol message and repeated
 * whitespace counts as one delimiter; CR LF terminates every protocol message and ends a PUB or MSG
 * payload; subject names, reply subject (INBOX) names included, are case-sensitive, non-empty
 * alphanumeric strings with no embedded whitespace, optionally token-delimited by the dot.
 *
 * The operations, as the reference writes them:
 *
 *     CONNECT {"option_name":option_value,...}<CRLF>
 *     PUB   <subject> [reply-to] <#bytes><CRLF>[payload]<CRLF>
 *     HPUB  <subject> [reply-to] <#header bytes> <#total bytes><CRLF>[headers]<CRLF><CRLF>[payload]<CRLF>
 *     SUB   <subject> [queue group] <sid><CRLF>
 *     UNSUB <sid> [max_msgs]<CRLF>
 *     PING<CRLF>
 *     PONG<CRLF>
 *     INFO  {"option_name":option_value,...}<CRLF>
 *     MSG   <subject> <sid> [reply-to] <#bytes><CRLF>[payload]<CRLF>
 *     HMSG  <subject> <sid> [reply-to] <#header bytes> <#total bytes><CRLF>[headers]<CRLF><CRLF>[payload]<CRLF>
 *     +OK<CRLF>
 *     -ERR <error message><CRLF>
 *
 * #header bytes counts the header section including its terminating CR LF CR LF, and #total bytes
 * counts that section plus the payload, so an HPUB takes the whole section as one span.
 *
 * The Overview states operation names are case insensitive. The parser here matches them in the
 * upper case a server writes.
 *
 * A builder writes one operation into the caller's buffer and reports its length; the parser decodes
 * the operation at the head of the caller's inbound buffer and reports the octets it occupies.
 * Pure: no state, no allocation, no I/O.
 *
 * The module exports one symbol, @ref Nats. Everything in nats.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_NATS_H
#define PROTOCORE_NATS_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_NATS

PROTOCORE_BEGIN_DECLS

/** @brief The server-to-client operation a parse decoded (NATS Protocol). */
typedef enum PROTO_ENUM_PACKED
{
    NATS_OP_MSG,     ///< MSG, or HMSG when the header section is set: subject, sid, reply-to, payload
    NATS_OP_INFO,    ///< INFO: the options JSON lands in @c arg
    NATS_OP_PING,    ///< PING
    NATS_OP_PONG,    ///< PONG
    NATS_OP_OK,      ///< +OK
    NATS_OP_ERR,     ///< -ERR: the error message lands in @c arg
    NATS_OP_UNKNOWN, ///< an operation name this decoder does not carry
} NatsOp;

/**
 * @brief One decoded protocol message. Every span points into the caller's inbound buffer.
 */
typedef struct
{
    NatsOp op;              ///< which operation the control line named
    const char *subject;    ///< MSG / HMSG subject
    size_t subject_len;     ///< its length
    const char *sid;        ///< MSG / HMSG subscription id
    size_t sid_len;         ///< its length
    const char *reply_to;   ///< MSG / HMSG reply-to subject, absent when the field was not written
    size_t reply_to_len;    ///< its length, 0 when absent
    const char *headers;    ///< HMSG header section, NULL for a header-less MSG
    size_t header_bytes;    ///< the #header bytes field, the terminating CR LF CR LF included
    const uint8_t *payload; ///< MSG payload, the header section excluded
    size_t payload_len;     ///< its length: #bytes for a MSG, #total bytes less #header bytes for an HMSG
    const char *arg;        ///< the INFO options JSON or the -ERR error message
    size_t arg_len;         ///< its length
} NatsMsg;

/** @brief Where a builder writes one protocol message. */
typedef struct
{
    char *buf;  ///< the buffer the operation is written into
    size_t cap; ///< octets it holds, the NUL a builder adds when there is room included
} NatsOutArgs;

/** @brief What a CONNECT tells the server about the client. */
typedef struct
{
    const char *options; ///< the JSON object CONNECT carries: {"option_name":option_value,...}
} NatsClientArgs;

/** @brief What a PUB or an HPUB publishes. */
typedef struct
{
    const char *subject;    ///< the subject it publishes to
    const char *reply_to;   ///< the reply-to subject; NULL leaves the optional field off
    const uint8_t *payload; ///< the payload octets
    size_t payload_len;     ///< how many, the #bytes a PUB writes
} NatsPublishArgs;

/** @brief The header section an HPUB carries (NATS Protocol, HPUB). */
typedef struct
{
    const char *block; ///< the section: the NATS/1.0 version line, name: value lines, CR LF CR LF
    size_t bytes;      ///< its length, the #header bytes field, the terminator included
} NatsHeadersArgs;

/** @brief The subscription a SUB opens and an UNSUB ends. */
typedef struct
{
    const char *subject;     ///< SUB: the subject the subscription matches
    const char *queue_group; ///< SUB: the queue group; NULL leaves the optional field off
    const char *sid;         ///< the alphanumeric subscription id the client generates
    uint32_t max_msgs;       ///< UNSUB: messages to deliver before the subscription ends
    proto_bool with_max;     ///< UNSUB: write max_msgs; false ends the subscription at once
} NatsSubscriptionArgs;

/** @brief The inbound octets a parse reads. */
typedef struct
{
    const char *buf; ///< the receive buffer, one protocol message at its head
    size_t len;      ///< octets buffered
} NatsInboundArgs;

/**
 * @brief The NATS client protocol codec.
 *
 * A caller sets the members a call takes, invokes it through ::Nats, and reads the outcome off the
 * same handle.
 *
 * No slot member: the codec keeps no rows, so no call names one.
 *
 * A builder reports the octets it wrote in @c n and clears @c ok when the operation did not fit
 * @c out.cap or an argument it needs is absent. It NUL-terminates when a byte is left over, and the
 * reported length excludes that NUL.
 *
 * parse reports true once the whole operation is buffered: the control line for every operation,
 * and the payload plus its trailing CR LF for a MSG or an HMSG. An operation name it does not carry
 * reports ::NATS_OP_UNKNOWN and consumes the control line, which is what the server answers with
 * `-ERR 'Unknown Protocol Operation'`.
 *
 * No storage member: every octet a call touches belongs to the caller, so nothing survives a call.
 *
 * @var NatsNs::out           where a builder writes the operation
 * @var NatsNs::client        the options a CONNECT declares
 * @var NatsNs::publish       the subject, reply-to and payload a PUB or an HPUB carries
 * @var NatsNs::headers       the header section an HPUB carries
 * @var NatsNs::subscription  the subscription a SUB opens or an UNSUB ends
 * @var NatsNs::in            the inbound octets a parse reads
 * @var NatsNs::ok            a call's true/false outcome
 * @var NatsNs::n             octets a builder wrote, 0 when it wrote none
 * @var NatsNs::consumed      octets the decoded operation occupies, so a caller can advance @c in.buf
 * @var NatsNs::msg           the decoded operation, its spans pointing into @c in.buf
 * @var NatsNs::connect       write `CONNECT <options>` from @c client into @c out
 * @var NatsNs::pub           write `PUB <subject> [reply-to] <#bytes>` and the payload from @c publish
 * @var NatsNs::hpub          the same with @c headers ahead of the payload, both lengths written
 * @var NatsNs::sub           write `SUB <subject> [queue group] <sid>` from @c subscription
 * @var NatsNs::unsub         write `UNSUB <sid> [max_msgs]` from @c subscription
 * @var NatsNs::ping          write `PING`
 * @var NatsNs::pong          write `PONG`
 * @var NatsNs::parse         decode the operation at the head of @c in into @c msg
 */
typedef struct
{
    NatsOutArgs out;                   ///< where a builder writes
    NatsClientArgs client;             ///< what a CONNECT declares
    NatsPublishArgs publish;           ///< what a PUB or an HPUB carries
    NatsHeadersArgs headers;           ///< the header section an HPUB carries
    NatsSubscriptionArgs subscription; ///< what a SUB opens or an UNSUB ends
    NatsInboundArgs in;                ///< what a parse reads
    proto_bool ok;
    size_t n;
    size_t consumed;
    NatsMsg msg;
} NatsVars;

/** @brief The operands and the outcome. */
extern NatsVars NatsV;

/** @brief The entries. */
typedef struct
{
    void (*const connect)(uint8_t *restrict work);
    void (*const pub)(uint8_t *restrict work);
    void (*const hpub)(uint8_t *restrict work);
    void (*const sub)(uint8_t *restrict work);
    void (*const unsub)(uint8_t *restrict work);
    void (*const ping)(uint8_t *restrict work);
    void (*const pong)(uint8_t *restrict work);
    void (*const parse)(uint8_t *restrict work);
} NatsNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in NatsV or a region of the borrow at a fixed offset.
void protocore_nats_connect(uint8_t *restrict work);
void protocore_nats_pub(uint8_t *restrict work);
void protocore_nats_hpub(uint8_t *restrict work);
void protocore_nats_sub(uint8_t *restrict work);
void protocore_nats_unsub(uint8_t *restrict work);
void protocore_nats_ping(uint8_t *restrict work);
void protocore_nats_pong(uint8_t *restrict work);
void protocore_nats_parse(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Nats.connect(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const NatsNs Nats __attribute__((unused)) = {
    .connect = protocore_nats_connect,
    .pub = protocore_nats_pub,
    .hpub = protocore_nats_hpub,
    .sub = protocore_nats_sub,
    .unsub = protocore_nats_unsub,
    .ping = protocore_nats_ping,
    .pong = protocore_nats_pong,
    .parse = protocore_nats_parse,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_NATS

#endif // PROTOCORE_NATS_H
