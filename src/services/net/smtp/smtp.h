// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file smtp.h
 * @brief Layer 7 (Application) - the client half of one SMTP session (RFC 5321).
 *
 * RFC 5321 sec 3.1: an SMTP session is initiated when a client opens a connection to a server and
 * the server responds with an opening message. This module drives that session end to end: the 220
 * Greeting (sec 4.2), EHLO (sec 4.1.1.1), an optional in-band TLS upgrade (RFC 3207 sec 4), an
 * optional AUTH exchange (RFC 4954 sec 4), the mail transaction MAIL / RCPT / DATA (sec 3.3,
 * sec 4.1.1.2 - 4.1.1.4), and QUIT (sec 4.1.1.10).
 *
 * What DATA carries is an RFC 5322 message: a From: field (sec 3.6.2), a To: field (sec 3.6.3), a
 * Subject: field (sec 3.6.5), an empty line, and the body (sec 2.1), labeled MIME-Version: 1.0
 * (RFC 2045 sec 4) and text/plain (RFC 2045 sec 5.1). The body is normalized to CRLF line endings
 * and dot-stuffed (RFC 5321 sec 4.5.2) ahead of the "<CRLF>.<CRLF>" end of mail data indication
 * (sec 4.1.1.4).
 *
 * ::SmtpNs::run walks the dialogue over the byte seam in @ref SmtpNs::transport, so a scripted
 * transport runs the whole exchange on the host. ::SmtpNs::send binds that seam to the outbound
 * client transport (::TcpClient) and blocks until the message is accepted.
 *
 * Zero heap; every buffer is a compile-time size (PROTOCORE_SMTP_*). Gated by PROTOCORE_ENABLE_SMTP.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SMTP_H
#define PROTOCORE_SMTP_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths and PROTOCORE_INLINE

#if PROTOCORE_ENABLE_SMTP

PROTOCORE_BEGIN_DECLS

/** @brief How one session ended. 0 is a delivered message; every failure is a distinct code. */
typedef enum PROTO_ENUM_PACKED
{
    SMTP_OK = 0,
    SMTP_ERR_ARG = -1,         ///< host, reverse-path or forward-path was null or empty
    SMTP_ERR_CONNECT = -2,     ///< the transport never came up (name lookup or connect)
    SMTP_ERR_TLS = -3,         ///< the TLS handshake did not complete
    SMTP_ERR_IO = -4,          ///< a send or a recv failed, or a reply never arrived
    SMTP_ERR_PROTOCOL = -5,    ///< the reply code was not the one the step requires (RFC 5321 sec 4.2)
    SMTP_ERR_AUTH = -6,        ///< the AUTH exchange was rejected (RFC 4954 sec 6: 535)
    SMTP_ERR_OVERFLOW = -7,    ///< a command line or the message content outgrew its fixed buffer
    SMTP_ERR_NO_STARTTLS = -8, ///< the EHLO reply carried no STARTTLS keyword (RFC 3207 sec 3)
} SmtpResult;

/** @brief How the channel is secured. */
typedef enum PROTO_ENUM_PACKED
{
    SMTP_PLAIN = 0,    ///< no TLS; credentials and content travel in the clear (port 25)
    SMTP_TLS = 1,      ///< implicit TLS from the first byte: the submissions service (RFC 8314 sec 3.3)
    SMTP_STARTTLS = 2, ///< clear connect, then the in-band upgrade (RFC 3207 sec 4) on submission (RFC 6409 sec 3.1)
} SmtpSecurity;

/**
 * @brief The byte seam the dialogue rides. Every octet leaves and arrives through these two, so the
 * same engine runs over a socket or over a scripted mock.
 *
 * @return send: bytes written, which must equal @p len, or < 0 on error.
 * @return recv: bytes read (> 0), or <= 0 on close, error or timeout.
 */
typedef int (*SmtpSendFn)(void *ctx, const uint8_t *data, size_t len);
typedef int (*SmtpRecvFn)(void *ctx, uint8_t *buf, size_t cap);

/**
 * @brief Upgrade the live channel to TLS in place, after the server's 220 to STARTTLS
 * (RFC 3207 sec 4).
 *
 * Called once, mid-session. Every later send and recv on the same @p ctx carries TLS records, so
 * the switch belongs to the transport and the engine keeps the one pair of function pointers.
 * @return true when the handshake completed.
 */
typedef proto_bool (*SmtpStartTlsFn)(void *ctx);

/** @brief RFC 5321 sec 3.1: the server a session is opened with, and how it is secured. */
typedef struct
{
    const char *host;        ///< the server it dials; also the TLS SNI name
    uint16_t port;           ///< 25, submission 587 (RFC 6409 sec 3.1), submissions 465 (RFC 8314 sec 3.3)
    SmtpSecurity security;   ///< how the channel is secured
    const char *client_name; ///< the Domain the EHLO argument carries (RFC 5321 sec 4.1.1.1)
} SmtpSessionArgs;
/**
 * @brief RFC 4954 sec 4: the identity the AUTH exchange presents.
 *
 * The mechanism is AUTH LOGIN: the username then the password, each base64 (RFC 4648 sec 4) on its
 * own line, each answering a 334 challenge. LOGIN is not defined by any RFC; the IANA SASL
 * Mechanisms registry carries it with usage OBSOLETE, referencing draft-murchison-sasl-login-00.
 * RFC 4954 sec 4 defines the AUTH verb and the 334 / 235 replies the exchange uses.
 */
typedef struct
{
    const char *user; ///< the authentication identity; null or empty skips AUTH entirely
    const char *pass; ///< its password
} SmtpAuthArgs;
/** @brief RFC 5321 sec 3.3: the two paths one mail transaction names. Bare mailboxes, no brackets. */
typedef struct
{
    const char *reverse_path; ///< the sender mailbox MAIL carries (RFC 5321 sec 4.1.1.2)
    const char *forward_path; ///< the recipient mailbox RCPT carries (RFC 5321 sec 4.1.1.3)
} SmtpEnvelopeArgs;
/** @brief RFC 5322: the message DATA carries. Nothing the envelope reads. */
typedef struct
{
    const char *subject; ///< the Subject: field body (RFC 5322 sec 3.6.5); null writes an empty one
    const char *body;    ///< the body (RFC 5322 sec 2.1); LF or CRLF ends, dot-stuffed on the way out
} SmtpContentArgs;
/** @brief The seam the octets move through, and the transport state handed back to it. */
typedef struct
{
    SmtpSendFn send;         ///< writes octets to the server
    SmtpRecvFn recv;         ///< reads octets from the server
    SmtpStartTlsFn starttls; ///< upgrades the channel in place (RFC 3207 sec 4); null when it cannot
    void *ctx;               ///< the transport's own handle, passed back to all three
} SmtpTransportArgs;
/**
 * @brief The SMTP client.
 *
 * A caller sets the members a call takes, invokes it through ::Smtp, and reads the outcome off the
 * same handle. There is no slot member: the module drives one session at a time, so no call has a
 * row to name.
 *
 * @var SmtpNs::session   the server a session is opened with (RFC 5321 sec 3.1)
 * @var SmtpNs::auth      the identity the AUTH exchange presents (RFC 4954 sec 4)
 * @var SmtpNs::envelope  the reverse-path and forward-path of the transaction (RFC 5321 sec 3.3)
 * @var SmtpNs::content   the RFC 5322 message DATA carries
 * @var SmtpNs::transport the seam the octets move through
 * @var SmtpNs::ok        a call's true/false outcome: the message was accepted
 * @var SmtpNs::result    the same outcome as a distinct ::SmtpResult code
 * @var SmtpNs::code      the reply code of the last reply read (RFC 5321 sec 4.2)
 * @var SmtpNs::run       walk the whole session over the seam in @c transport
 * @var SmtpNs::send      open the outbound client transport, walk the session, close
 */
typedef struct
{
    SmtpSessionArgs session;     ///< the server a session is opened with
    SmtpAuthArgs auth;           ///< the identity AUTH presents
    SmtpEnvelopeArgs envelope;   ///< the two paths of one mail transaction
    SmtpContentArgs content;     ///< the message DATA carries
    SmtpTransportArgs transport; ///< the seam the octets move through
    proto_bool ok;
    SmtpResult result;
    int16_t code;
} SmtpVars;

/** @brief The operands and the outcome. */
extern SmtpVars SmtpV;

/** @brief The entries. */
typedef struct
{
    void (*const run)(uint8_t *restrict work);
    void (*const send)(uint8_t *restrict work);
} SmtpNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in SmtpV or a region of the borrow at a fixed offset.
void protocore_smtp_run(uint8_t *restrict work);
void protocore_smtp_send(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Smtp.run(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const SmtpNs Smtp __attribute__((unused)) = {
    .run = protocore_smtp_run,
    .send = protocore_smtp_send,
};

/**
 * @brief The PROTOCORE_SMTP_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_smtp_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SMTP

#endif // PROTOCORE_SMTP_H
