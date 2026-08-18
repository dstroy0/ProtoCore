// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ftp.c
 * @brief FTP client wire codec implementation (see ftp.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_FTP

#include "ftp.h"
#include "mmgr/protomem.h"
#include "mmgr/protostr.h"

PROTOCORE_BEGIN_DECLS

static const size_t FTP_SENT = (size_t)-1; // "overflowed" sentinel threaded through the emitters

// Append raw bytes; propagates the overflow sentinel.
static size_t protocore_ftp_emit(char *buf, size_t cap, size_t n, const char *s, size_t slen)
{
    // Overflow-safe bound: n <= cap is invariant (every non-sentinel return is <= cap), but guard
    // n > cap explicitly so cap - n provably cannot underflow; written as subtraction so a huge
    // slen cannot wrap n + slen.
    if (n == FTP_SENT || n > cap || slen > cap - n)
    {
        return FTP_SENT; // caller passes n from 0 or a prior emit's return,
                         // which the invariant above keeps <= cap
    }
    // The guard above proves n + slen <= cap, so this write stays inside buf[0, cap). S3519 can't link
    // buf's size to the separate cap parameter and follows an infeasible path (same FP as mms.cpp).
    mem.cpy(buf + n, s, slen); // NOSONAR - bound proven above; analyzer follows an infeasible path
    return n + slen;
}

// Append an unsigned decimal; propagates the overflow sentinel.
static size_t protocore_ftp_emit_uint(char *buf, size_t cap, size_t n, unsigned v)
{
    if (n == FTP_SENT)
    {
        return FTP_SENT;
    }
    char rev[10];
    int ri = 0;
    if (v == 0)
    {
        rev[ri++] = '0';
    }
    else
    {
        while (v)
        {
            rev[ri++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    if ((size_t)ri > cap - n) // n <= cap invariant (checked above); subtraction form can't overflow
    {
        return FTP_SENT;
    }
    for (int k = 0; k < ri; k++)
    {
        buf[n + k] = rev[ri - 1 - k];
    }
    return n + (size_t)ri;
}

// Finish: on no overflow and room for the NUL, terminate and return the length; else 0.
static size_t protocore_ftp_finish(char *buf, size_t cap, size_t n)
{
    if (n == FTP_SENT || n >= cap) // no room for the NUL (n == cap); n <= cap invariant avoids n + 1 overflow
    {
        return 0;
    }
    buf[n] = 0;
    return n;
}

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void ftp_build_command(uint8_t *restrict work)
{
    (void)work;
    char *buf = Ftp.build_command_args.buf;
    size_t cap = Ftp.build_command_args.cap;
    const char *verb = Ftp.build_command_args.verb;
    const char *arg = Ftp.build_command_args.arg;

    if (!buf || !verb || !verb[0])
    {
        Ftp.n = 0;
        return;
    }
    size_t n = 0;
    n = protocore_ftp_emit(buf, cap, n, verb, str.len(verb, cap));
    if (arg && arg[0])
    {
        n = protocore_ftp_emit(buf, cap, n, " ", 1);
        n = protocore_ftp_emit(buf, cap, n, arg, str.len(arg, cap));
    }
    n = protocore_ftp_emit(buf, cap, n, "\r\n", 2);
    Ftp.n = protocore_ftp_finish(buf, cap, n);
}

static void ftp_build_port(uint8_t *restrict work)
{
    (void)work;
    char *buf = Ftp.build_port_args.buf;
    size_t cap = Ftp.build_port_args.cap;
    const uint8_t *ip = Ftp.build_port_args.ip;
    uint16_t port = Ftp.build_port_args.port;

    if (!buf || !ip)
    {
        Ftp.n = 0;
        return;
    }
    size_t n = 0;
    n = protocore_ftp_emit(buf, cap, n, "PORT ", 5);
    for (int i = 0; i < 4; i++)
    {
        n = protocore_ftp_emit_uint(buf, cap, n, ip[i]);
        n = protocore_ftp_emit(buf, cap, n, ",", 1);
    }
    n = protocore_ftp_emit_uint(buf, cap, n, (unsigned)(port >> 8));
    n = protocore_ftp_emit(buf, cap, n, ",", 1);
    n = protocore_ftp_emit_uint(buf, cap, n, (unsigned)(port & 0xFF));
    n = protocore_ftp_emit(buf, cap, n, "\r\n", 2);
    Ftp.n = protocore_ftp_finish(buf, cap, n);
}

static void ftp_build_eprt(uint8_t *restrict work)
{
    (void)work;
    char *buf = Ftp.build_eprt_args.buf;
    size_t cap = Ftp.build_eprt_args.cap;
    const char *ip_str = Ftp.build_eprt_args.ip_str;
    proto_bool ipv6 = Ftp.build_eprt_args.ipv6;
    uint16_t port = Ftp.build_eprt_args.port;

    if (!buf || !ip_str || !ip_str[0])
    {
        Ftp.n = 0;
        return;
    }
    size_t n = 0;
    n = protocore_ftp_emit(buf, cap, n, "EPRT |", 6);
    n = protocore_ftp_emit(buf, cap, n, ipv6 ? "2" : "1", 1);
    n = protocore_ftp_emit(buf, cap, n, "|", 1);
    n = protocore_ftp_emit(buf, cap, n, ip_str, str.len(ip_str, cap));
    n = protocore_ftp_emit(buf, cap, n, "|", 1);
    n = protocore_ftp_emit_uint(buf, cap, n, port);
    n = protocore_ftp_emit(buf, cap, n, "|\r\n", 3);
    Ftp.n = protocore_ftp_finish(buf, cap, n);
}

static proto_bool protocore_ftp_is_3digit(const char *p)
{
    return p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9' && p[2] >= '0' && p[2] <= '9';
}

static int protocore_ftp_code3(const char *p)
{
    return (p[0] - '0') * 100 + (p[1] - '0') * 10 + (p[2] - '0');
}

// Index just past the LF of the line starting at @p start, or 0 if the line is not yet complete.
static size_t protocore_ftp_line_end(const char *buf, size_t len, size_t start)
{
    for (size_t i = start; i < len; i++)
    {
        if (buf[i] == '\n')
        {
            return i + 1;
        }
    }
    return 0;
}

static void ftp_parse_reply(uint8_t *restrict work)
{
    (void)work;
    const char *buf = Ftp.parse_reply_args.buf;
    size_t len = Ftp.parse_reply_args.len;
    int *code = Ftp.parse_reply_args.code;
    size_t *consumed = Ftp.parse_reply_args.consumed;

    if (!buf || len < 4 || !protocore_ftp_is_3digit(buf))
    {
        Ftp.ok = PROTO_FALSE;
        return;
    }
    int first = protocore_ftp_code3(buf);
    char sep = buf[3];

    if (sep == ' ')
    {
        size_t eol = protocore_ftp_line_end(buf, len, 0);
        if (!eol)
        {
            Ftp.ok = PROTO_FALSE; // line not fully received
            return;
        }
        *code = first;
        *consumed = eol;
        Ftp.ok = PROTO_TRUE;
        return;
    }
    if (sep != '-')
    {
        Ftp.ok = PROTO_FALSE; // malformed: the separator must be SP or '-'
        return;
    }

    // Multiline: end at the first line that begins with the same code followed by a space.
    size_t pos = protocore_ftp_line_end(buf, len, 0);
    if (!pos)
    {
        Ftp.ok = PROTO_FALSE;
        return;
    }
    while (pos < len)
    {
        if (len - pos >= 4 && protocore_ftp_is_3digit(buf + pos) && protocore_ftp_code3(buf + pos) == first &&
            buf[pos + 3] == ' ')
        {
            size_t eol = protocore_ftp_line_end(buf, len, pos);
            if (!eol)
            {
                Ftp.ok = PROTO_FALSE; // terminator line not fully received
                return;
            }
            *code = first;
            *consumed = eol;
            Ftp.ok = PROTO_TRUE;
            return;
        }
        size_t eol = protocore_ftp_line_end(buf, len, pos);
        if (!eol)
        {
            Ftp.ok = PROTO_FALSE; // partial continuation line; need more
            return;
        }
        pos = eol;
    }
    Ftp.ok = PROTO_FALSE; // no terminator yet
}

static void ftp_parse_pasv(uint8_t *restrict work)
{
    (void)work;
    const char *buf = Ftp.parse_pasv_args.buf;
    size_t len = Ftp.parse_pasv_args.len;
    uint8_t *ip = Ftp.parse_pasv_args.ip;
    uint16_t *port = Ftp.parse_pasv_args.port;

    if (!buf || !ip || !port)
    {
        Ftp.ok = PROTO_FALSE;
        return;
    }
    size_t i = 0;
    while (i < len && buf[i] != '(')
    {
        i++;
    }
    if (i >= len)
    {
        Ftp.ok = PROTO_FALSE;
        return;
    }
    i++; // past '('

    unsigned nums[6];
    for (int ni = 0; ni < 6; ni++)
    {
        if (i >= len || buf[i] < '0' || buf[i] > '9')
        {
            Ftp.ok = PROTO_FALSE; // the guard above guarantees at least one digit in this field
            return;
        }
        unsigned v = 0;
        while (i < len && buf[i] >= '0' && buf[i] <= '9')
        {
            v = v * 10 + (unsigned)(buf[i] - '0');
            if (v > 255)
            {
                Ftp.ok = PROTO_FALSE;
                return;
            }
            i++;
        }
        nums[ni] = v;
        if (ni < 5)
        {
            if (i >= len || buf[i] != ',')
            {
                Ftp.ok = PROTO_FALSE;
                return;
            }
            i++;
        }
    }
    ip[0] = (uint8_t)nums[0];
    ip[1] = (uint8_t)nums[1];
    ip[2] = (uint8_t)nums[2];
    ip[3] = (uint8_t)nums[3];
    *port = (uint16_t)(nums[4] * 256 + nums[5]);
    Ftp.ok = PROTO_TRUE;
}

static void ftp_parse_epsv(uint8_t *restrict work)
{
    (void)work;
    const char *buf = Ftp.parse_epsv_args.buf;
    size_t len = Ftp.parse_epsv_args.len;
    uint16_t *port = Ftp.parse_epsv_args.port;

    if (!buf || !port)
    {
        Ftp.ok = PROTO_FALSE;
        return;
    }
    size_t i = 0;
    while (i < len && buf[i] != '(')
    {
        i++;
    }
    if (i >= len)
    {
        Ftp.ok = PROTO_FALSE;
        return;
    }
    i++; // past '('
    if (i >= len)
    {
        Ftp.ok = PROTO_FALSE;
        return;
    }
    char d = buf[i]; // the delimiter (RFC 2428 recommends '|')

    // Skip the 3 leading delimiters (empty net-prt + net-addr fields) to reach the port field.
    int seen = 0;
    while (i < len && seen < 3)
    {
        if (buf[i] == d)
        {
            seen++;
        }
        i++;
    }
    if (seen < 3)
    {
        Ftp.ok = PROTO_FALSE;
        return;
    }

    if (i >= len || buf[i] < '0' || buf[i] > '9')
    {
        Ftp.ok = PROTO_FALSE; // the guard above guarantees at least one port digit follows
        return;
    }
    unsigned v = 0;
    while (i < len && buf[i] >= '0' && buf[i] <= '9')
    {
        v = v * 10 + (unsigned)(buf[i] - '0');
        if (v > 65535)
        {
            Ftp.ok = PROTO_FALSE;
            return;
        }
        i++;
    }
    *port = (uint16_t)v;
    Ftp.ok = PROTO_TRUE;
}

FtpNs Ftp = {
    .build_command = ftp_build_command,
    .build_port = ftp_build_port,
    .build_eprt = ftp_build_eprt,
    .parse_reply = ftp_parse_reply,
    .parse_pasv = ftp_parse_pasv,
    .parse_epsv = ftp_parse_epsv,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_FTP
