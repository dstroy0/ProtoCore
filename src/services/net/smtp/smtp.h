// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file smtp.h
 * @brief Outbound SMTP client (RFC 5321) - send a device email alert.
 *
 * A blocking one-shot: connect, greet, optional AUTH LOGIN, then MAIL FROM / RCPT TO /
 * DATA a plain-text message and QUIT. It rides the shared outbound client transport
 * (`pc_client`), with implicit TLS (SMTPS, typically port 465) when the config sets
 * `tls` and PC_ENABLE_TLS is on. Zero heap; every buffer is a compile-time size
 * (`PC_SMTP_*`). Gated by PC_ENABLE_SMTP.
 *
 * The dialogue itself (smtp_run) is written against a send/recv seam, so the whole
 * protocol exchange - greeting codes, AUTH, dot-stuffing, the terminating `.` - is
 * unit-tested on the host with a scripted mock server, no lwIP or TLS required.
 *
 * "SMS fallback" needs no extra code: most mobile carriers accept an email-to-SMS
 * gateway address (e.g. `5551234567@txt.example.net`) as the recipient.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SMTP_H
#define PROTOCORE_SMTP_H

#include <stddef.h>
#include <stdint.h>

/** @brief Result of an SMTP send. 0 is success; every failure is a distinct negative code. */
enum class SmtpResult : int32_t
{
    SMTP_OK = 0,
    SMTP_ERR_ARG = -1,         ///< a required field (host / from / to) was null or empty
    SMTP_ERR_CONNECT = -2,     ///< could not open the transport (DNS / connect)
    SMTP_ERR_TLS = -3,         ///< the TLS handshake failed (SMTPS)
    SMTP_ERR_IO = -4,          ///< a send/recv failed or the reply timed out
    SMTP_ERR_PROTOCOL = -5,    ///< the server returned an unexpected reply code
    SMTP_ERR_AUTH = -6,        ///< AUTH was rejected (bad user/password)
    SMTP_ERR_OVERFLOW = -7,    ///< a command line or the message exceeded its fixed buffer
    SMTP_ERR_NO_STARTTLS = -8, ///< STARTTLS was required but the server did not advertise it
};

/** @brief How the connection is secured. */
enum class SmtpSecurity : uint8_t
{
    SMTP_PLAIN = 0,    ///< no TLS at all (port 25) - credentials and body travel in the clear.
    SMTP_TLS = 1,      ///< implicit TLS from the first byte (SMTPS, port 465).
    SMTP_STARTTLS = 2, ///< connect in the clear, then upgrade in band (submission, port 587).
};

/**
 * @brief Transport seam for smtp_run(): the engine sends and receives raw bytes only
 * through these, so it can run against a real socket or a test mock.
 *
 * @return send: number of bytes written (must equal @p len), or <0 on error.
 * @return recv: number of bytes read (>0), or <=0 on close / error / timeout.
 */
typedef int (*SmtpSendFn)(void *ctx, const uint8_t *data, size_t len);
typedef int (*SmtpRecvFn)(void *ctx, uint8_t *buf, size_t cap);

/**
 * @brief Upgrade the live connection to TLS in place (RFC 3207), after the server's 220.
 *
 * Called once, mid-dialogue. On success every later send/recv on the same ctx must be
 * encrypted - the engine keeps using the same two function pointers, so the switch belongs to
 * the transport, not to the caller.
 * @return true if the handshake completed.
 */
typedef bool (*SmtpStartTlsFn)(void *ctx);

/** @brief Server address + credentials for one send. Addresses are bare (no angle brackets). */
struct SmtpConfig
{
    const char *host;      ///< server hostname (also the TLS SNI name)
    uint16_t port;         ///< 25 (plain) / 587 (STARTTLS) / 465 (implicit TLS)
    SmtpSecurity security; ///< how to secure the connection
    const char *user;      ///< AUTH LOGIN username (null or empty => skip AUTH)
    const char *pass;      ///< AUTH LOGIN password
    const char *from;      ///< envelope sender + From: header address
    const char *helo;      ///< EHLO domain to announce (null => "esp32")
};

/** @brief One plain-text message. */
struct SmtpMessage
{
    const char *to;      ///< single recipient address (envelope + To: header)
    const char *subject; ///< Subject: header (null => empty)
    const char *body;    ///< plain-text UTF-8 body; LF or CRLF line ends, dot-stuffed for you
};

/**
 * @brief Drive the full SMTP exchange over @p send / @p recv. Pure - no lwIP or TLS -
 * so it is host-testable with a scripted transport.
 *
 * With SmtpSecurity::SMTP_STARTTLS the engine issues STARTTLS after the first EHLO, calls
 * @p starttls to upgrade the transport, and reissues EHLO (RFC 3207 sec 4.2 requires discarding
 * the capabilities learned in the clear). If the server does not advertise STARTTLS it returns
 * SmtpResult::SMTP_ERR_NO_STARTTLS **before** AUTH rather than continuing in the clear - a
 * stripped STARTTLS must not silently downgrade into sending credentials in plaintext.
 * @return SmtpResult::SMTP_OK on a delivered message, else an ::SmtpResult error.
 */
SmtpResult smtp_run(const SmtpConfig *cfg, const SmtpMessage *msg, SmtpSendFn send, SmtpRecvFn recv,
                    SmtpStartTlsFn starttls, void *ctx);

/**
 * @brief Blocking one-shot send over the real transport (pc_client, plus TLS when
 * `cfg->tls`). Opens the connection, runs smtp_run(), and closes.
 * @return SmtpResult::SMTP_OK or an ::SmtpResult error. On non-Arduino (host) builds there is no
 *         lwIP, so this returns SmtpResult::SMTP_ERR_CONNECT; use smtp_run() directly in tests.
 */
SmtpResult smtp_send(const SmtpConfig *cfg, const SmtpMessage *msg);

#endif // PROTOCORE_SMTP_H
