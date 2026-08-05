// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ftp.h
 * @brief FTP client wire codec (RFC 959 + RFC 2428 + RFC 3659), PC_ENABLE_FTP.
 *
 * The pure protocol layer of an FTP client: build control-channel commands, parse the
 * (possibly multiline) 3-digit reply, and decode the PASV / EPSV data-channel address the
 * server hands back. A device can then push/pull files - e.g. drip a `.nc` program to a CNC
 * controller's FTP program store (Fanuc / Haas / Mazak / Heidenhain all expose one), fetch a
 * config, or archive a log. No heap, no stdlib; the two sockets (control + data) are the
 * application's - this is only the bytes on the wire, so it is fully host-testable.
 *
 * FTP replies (RFC 959 sec 4.2): a single line is `NNN<SP>text<CRLF>`; a multiline reply is
 * `NNN-text<CRLF>` continuation lines `... <CRLF>` and a final `NNN<SP>text<CRLF>` (the same
 * code followed by a space marks the end). Passive mode: `227 ...(h1,h2,h3,h4,p1,p2)` gives the
 * data address (ip = h1.h2.h3.h4, port = p1*256+p2); extended passive `229 ...(|||port|)`
 * (RFC 2428) gives just the port on the control host.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_FTP_H
#define PROTOCORE_FTP_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_FTP

/**
 * @brief Build a control command line: `VERB<CRLF>` or `VERB<SP>ARG<CRLF>`.
 *
 * Covers every simple verb (USER, PASS, TYPE, CWD, CDUP, PASV, EPSV, RETR, STOR, APPE, LIST,
 * NLST, DELE, MKD, RMD, PWD, SIZE, REST, RNFR, RNTO, SYST, FEAT, NOOP, QUIT, ...). The verb and
 * arg are copied verbatim; the caller supplies well-formed values (no embedded CR/LF).
 *
 * @param arg the argument, or nullptr / "" for a bare verb (no trailing space).
 * @return bytes written (excluding the NUL terminator), or 0 on overflow / bad input.
 */
size_t pc_ftp_build_command(char *buf, size_t cap, const char *verb, const char *arg);

/**
 * @brief Build an active-mode `PORT h1,h2,h3,h4,p1,p2<CRLF>` from an IPv4 address + port.
 * @return bytes written (excluding NUL), or 0 on overflow.
 */
size_t pc_ftp_build_port(char *buf, size_t cap, const uint8_t ip[4], uint16_t port);

/**
 * @brief Build an extended active-mode `EPRT<SP>|net-prt|net-addr|port|<CRLF>` (RFC 2428).
 * @param ip_str dotted-decimal IPv4 or RFC 4291 IPv6 text (copied verbatim).
 * @param ipv6   false => net-prt 1 (IPv4), true => net-prt 2 (IPv6).
 * @return bytes written (excluding NUL), or 0 on overflow.
 */
size_t pc_ftp_build_eprt(char *buf, size_t cap, const char *ip_str, proto_bool ipv6, uint16_t port);

/**
 * @brief Detect and measure a complete control-channel reply at the head of @p buf.
 *
 * Handles single-line and multiline replies. On a complete reply, @p code receives the 3-digit
 * reply code and @p consumed the byte count the reply occupied (so the caller can advance past it
 * and keep any pipelined bytes).
 *
 * @return true if a complete reply is present; false if the buffer holds only a partial reply
 *         (need more bytes) or a malformed head (then @p code / @p consumed are unspecified).
 */
proto_bool pc_ftp_parse_reply(const char *buf, size_t len, int *code, size_t *consumed);

/**
 * @brief Decode the data address from a `227` passive-mode reply.
 *
 * Reads the `(h1,h2,h3,h4,p1,p2)` tuple anywhere in the reply text; ip = h1.h2.h3.h4,
 * port = p1*256 + p2. Each field must be 0-255.
 *
 * @return true on a well-formed tuple, false otherwise (then @p ip / @p port are unspecified).
 */
proto_bool pc_ftp_parse_pasv(const char *buf, size_t len, uint8_t ip[4], uint16_t *port);

/**
 * @brief Decode the port from a `229` extended-passive reply `(<d><d><d>port<d>)` (RFC 2428).
 *
 * The data connection uses the control connection's host; only the port is carried.
 *
 * @return true on a well-formed reply, false otherwise (then @p port is unspecified).
 */
proto_bool pc_ftp_parse_epsv(const char *buf, size_t len, uint16_t *port);

/** @brief First digit of a reply code (1 preliminary, 2 complete, 3 intermediate, 4/5 error), or 0. */
static inline int pc_ftp_reply_class(int code)
{
    return (code >= 100 && code <= 599) ? code / 100 : 0;
}

/** @brief A 2xx positive-completion reply. */
static inline proto_bool pc_ftp_reply_ok(int code)
{
    return pc_ftp_reply_class(code) == 2;
}

#endif // PC_ENABLE_FTP

PROTO_END_DECLS

#endif // PROTOCORE_FTP_H
