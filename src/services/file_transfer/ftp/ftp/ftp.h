// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ftp.h
 * @brief FTP client wire codec (RFC 959 + RFC 2428 + RFC 3659), PROTOCORE_ENABLE_FTP.
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_FTP

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief First digit of a reply code (1 preliminary, 2 complete, 3 intermediate, 4/5 error), or 0. */
static inline int protocore_ftp_reply_class(int code)
{
    return (code >= 100 && code <= 599) ? code / 100 : 0;
}

/** @brief A 2xx positive-completion reply. */
static inline proto_bool protocore_ftp_reply_ok(int code)
{
    return protocore_ftp_reply_class(code) == 2;
}

/** @brief What build_command takes: buf, cap, verb, arg. */
typedef struct
{
    char *buf;
    size_t cap;
    const char *verb;
    const char *arg; ///< the argument, or nullptr / "" for a bare verb (no trailing space)
} FtpBuildCommandArgs;
/** @brief What build_port takes: buf, cap, ip, port. */
typedef struct
{
    char *buf;
    size_t cap;
    const uint8_t *ip; ///< 4 bytes.
    uint16_t port;
} FtpBuildPortArgs;
/** @brief What build_eprt takes: buf, cap, ip_str, ipv6, port. */
typedef struct
{
    char *buf;
    size_t cap;
    const char *ip_str; ///< dotted-decimal IPv4 or RFC 4291 IPv6 text (copied verbatim)
    proto_bool ipv6;    ///< false => net-prt 1 (IPv4), true => net-prt 2 (IPv6)
    uint16_t port;
} FtpBuildEprtArgs;
/** @brief What parse_reply takes: buf, len, code, consumed. */
typedef struct
{
    const char *buf;
    size_t len;
    int *code;
    size_t *consumed;
} FtpParseReplyArgs;
/** @brief What parse_pasv takes: buf, len, ip, port. */
typedef struct
{
    const char *buf;
    size_t len;
    uint8_t *ip; ///< 4 bytes.
    uint16_t *port;
} FtpParsePasvArgs;
/** @brief What parse_epsv takes: buf, len, port. */
typedef struct
{
    const char *buf;
    size_t len;
    uint16_t *port;
} FtpParseEpsvArgs;
/**
 * @brief FTP client wire codec (RFC 959 + RFC 2428 + RFC 3659), PROTOCORE_ENABLE_FTP.
 *
 * A caller sets the members a call takes, invokes it through ::Ftp with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Ftp.build_command_args.buf = ...;
 *   Ftp.build_command_args.cap = ...;
 *   Ftp.build_command_args.verb = ...;
 *   Ftp.build_command_args.arg = ...;
 *   Ftp.build_command(work);
 *   // Ftp.n is what the call reports
 *
 * @var FtpNs::build_command_args  what build_command takes: buf, cap, verb, arg
 * @var FtpNs::build_port_args  what build_port takes: buf, cap, ip, port
 * @var FtpNs::build_eprt_args  what build_eprt takes: buf, cap, ip_str, ipv6, port
 * @var FtpNs::parse_reply_args  what parse_reply takes: buf, len, code, consumed
 * @var FtpNs::parse_pasv_args  what parse_pasv takes: buf, len, ip, port
 * @var FtpNs::parse_epsv_args  what parse_epsv takes: buf, len, port
 * @var FtpNs::ok  true if a complete reply is present; false if the buffer holds only ...
 * @var FtpNs::n  bytes written (excluding the NUL terminator), or 0 on overflow / ...
 * @var FtpNs::build_command  build a control command line: `VERB<CRLF>` or `VERB<SP>ARG<CRLF>`. ...
 * @var FtpNs::build_port  build an active-mode `PORT h1,h2,h3,h4,p1,p2<CRLF>` from an IPv4 ...
 * @var FtpNs::build_eprt  build an extended active-mode ...
 * @var FtpNs::parse_reply  detect and measure a complete control-channel reply at the head of ...
 * @var FtpNs::parse_pasv  decode the data address from a `227` passive-mode reply. Reads the ...
 * @var FtpNs::parse_epsv  decode the port from a `229` extended-passive reply ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    FtpBuildCommandArgs build_command_args;
    FtpBuildPortArgs build_port_args;
    FtpBuildEprtArgs build_eprt_args;
    FtpParseReplyArgs parse_reply_args;
    FtpParsePasvArgs parse_pasv_args;
    FtpParseEpsvArgs parse_epsv_args;
    proto_bool ok;
    size_t n;
} FtpVars;

/** @brief The operands and the outcome. */
extern FtpVars FtpV;

/** @brief The entries. */
typedef struct
{
    void (*const build_command)(uint8_t *restrict work);
    void (*const build_port)(uint8_t *restrict work);
    void (*const build_eprt)(uint8_t *restrict work);
    void (*const parse_reply)(uint8_t *restrict work);
    void (*const parse_pasv)(uint8_t *restrict work);
    void (*const parse_epsv)(uint8_t *restrict work);
} FtpNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in FtpV or a region of the borrow at a fixed offset.
void protocore_ftp_build_command(uint8_t *restrict work);
void protocore_ftp_build_port(uint8_t *restrict work);
void protocore_ftp_build_eprt(uint8_t *restrict work);
void protocore_ftp_parse_reply(uint8_t *restrict work);
void protocore_ftp_parse_pasv(uint8_t *restrict work);
void protocore_ftp_parse_epsv(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Ftp.build_command(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const FtpNs Ftp __attribute__((unused)) = {
    .build_command = protocore_ftp_build_command,
    .build_port = protocore_ftp_build_port,
    .build_eprt = protocore_ftp_build_eprt,
    .parse_reply = protocore_ftp_parse_reply,
    .parse_pasv = protocore_ftp_parse_pasv,
    .parse_epsv = protocore_ftp_parse_epsv,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_FTP

#endif // PROTOCORE_FTP_H
