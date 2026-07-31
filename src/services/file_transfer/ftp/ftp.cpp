// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ftp.cpp
 * @brief FTP client wire codec implementation (see ftp.h).
 */

#include "ftp.h"

#if PC_ENABLE_FTP

#include <string.h>

static const size_t FTP_SENT = (size_t)-1; // "overflowed" sentinel threaded through the emitters

// Append raw bytes; propagates the overflow sentinel.
static size_t pc_ftp_emit(char *buf, size_t cap, size_t n, const char *s, size_t slen)
{
    // Overflow-safe bound: n <= cap is invariant (every non-sentinel return is <= cap), but guard
    // n > cap explicitly so cap - n provably cannot underflow; written as subtraction so a huge
    // slen cannot wrap n + slen.
    if (n == FTP_SENT || n > cap || slen > cap - n) // GCOVR_EXCL_BR_LINE  n > cap can't fire: every
    {
        return FTP_SENT; // caller passes n from 0 or a prior emit's return,
                         // which the invariant above keeps <= cap
    }
    // The guard above proves n + slen <= cap, so this write stays inside buf[0, cap). S3519 can't link
    // buf's size to the separate cap parameter and follows an infeasible path (same FP as mms.cpp).
    memcpy(buf + n, s, slen); // NOSONAR - bound proven above; analyzer follows an infeasible path
    return n + slen;
}

// Append an unsigned decimal; propagates the overflow sentinel.
static size_t pc_ftp_emit_uint(char *buf, size_t cap, size_t n, unsigned v)
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
static size_t pc_ftp_finish(char *buf, size_t cap, size_t n)
{
    if (n == FTP_SENT || n >= cap) // no room for the NUL (n == cap); n <= cap invariant avoids n + 1 overflow
    {
        return 0;
    }
    buf[n] = 0;
    return n;
}

size_t pc_ftp_build_command(char *buf, size_t cap, const char *verb, const char *arg)
{
    if (!buf || !verb || !verb[0])
    {
        return 0;
    }
    size_t n = 0;
    n = pc_ftp_emit(buf, cap, n, verb, strnlen(verb, cap));
    if (arg && arg[0])
    {
        n = pc_ftp_emit(buf, cap, n, " ", 1);
        n = pc_ftp_emit(buf, cap, n, arg, strnlen(arg, cap));
    }
    n = pc_ftp_emit(buf, cap, n, "\r\n", 2);
    return pc_ftp_finish(buf, cap, n);
}

size_t pc_ftp_build_port(char *buf, size_t cap, const uint8_t ip[4], uint16_t port)
{
    if (!buf || !ip)
    {
        return 0;
    }
    size_t n = 0;
    n = pc_ftp_emit(buf, cap, n, "PORT ", 5);
    for (int i = 0; i < 4; i++)
    {
        n = pc_ftp_emit_uint(buf, cap, n, ip[i]);
        n = pc_ftp_emit(buf, cap, n, ",", 1);
    }
    n = pc_ftp_emit_uint(buf, cap, n, (unsigned)(port >> 8));
    n = pc_ftp_emit(buf, cap, n, ",", 1);
    n = pc_ftp_emit_uint(buf, cap, n, (unsigned)(port & 0xFF));
    n = pc_ftp_emit(buf, cap, n, "\r\n", 2);
    return pc_ftp_finish(buf, cap, n);
}

size_t pc_ftp_build_eprt(char *buf, size_t cap, const char *ip_str, bool ipv6, uint16_t port)
{
    if (!buf || !ip_str || !ip_str[0])
    {
        return 0;
    }
    size_t n = 0;
    n = pc_ftp_emit(buf, cap, n, "EPRT |", 6);
    n = pc_ftp_emit(buf, cap, n, ipv6 ? "2" : "1", 1);
    n = pc_ftp_emit(buf, cap, n, "|", 1);
    n = pc_ftp_emit(buf, cap, n, ip_str, strnlen(ip_str, cap));
    n = pc_ftp_emit(buf, cap, n, "|", 1);
    n = pc_ftp_emit_uint(buf, cap, n, port);
    n = pc_ftp_emit(buf, cap, n, "|\r\n", 3);
    return pc_ftp_finish(buf, cap, n);
}

static bool pc_ftp_is_3digit(const char *p)
{
    return p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9' && p[2] >= '0' && p[2] <= '9';
}

static int pc_ftp_code3(const char *p)
{
    return (p[0] - '0') * 100 + (p[1] - '0') * 10 + (p[2] - '0');
}

// Index just past the LF of the line starting at @p start, or 0 if the line is not yet complete.
static size_t pc_ftp_line_end(const char *buf, size_t len, size_t start)
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

bool pc_ftp_parse_reply(const char *buf, size_t len, int *code, size_t *consumed)
{
    if (!buf || len < 4 || !pc_ftp_is_3digit(buf))
    {
        return false;
    }
    int first = pc_ftp_code3(buf);
    char sep = buf[3];

    if (sep == ' ')
    {
        size_t eol = pc_ftp_line_end(buf, len, 0);
        if (!eol)
        {
            return false; // line not fully received
        }
        *code = first;
        *consumed = eol;
        return true;
    }
    if (sep != '-')
    {
        return false; // malformed: the separator must be SP or '-'
    }

    // Multiline: end at the first line that begins with the same code followed by a space.
    size_t pos = pc_ftp_line_end(buf, len, 0);
    if (!pos)
    {
        return false;
    }
    while (pos < len)
    {
        if (len - pos >= 4 && pc_ftp_is_3digit(buf + pos) && pc_ftp_code3(buf + pos) == first && buf[pos + 3] == ' ')
        {
            size_t eol = pc_ftp_line_end(buf, len, pos);
            if (!eol)
            {
                return false; // terminator line not fully received
            }
            *code = first;
            *consumed = eol;
            return true;
        }
        size_t eol = pc_ftp_line_end(buf, len, pos);
        if (!eol)
        {
            return false; // partial continuation line; need more
        }
        pos = eol;
    }
    return false; // no terminator yet
}

bool pc_ftp_parse_pasv(const char *buf, size_t len, uint8_t ip[4], uint16_t *port)
{
    if (!buf || !ip || !port)
    {
        return false;
    }
    size_t i = 0;
    while (i < len && buf[i] != '(')
    {
        i++;
    }
    if (i >= len)
    {
        return false;
    }
    i++; // past '('

    unsigned nums[6];
    for (int ni = 0; ni < 6; ni++)
    {
        if (i >= len || buf[i] < '0' || buf[i] > '9')
        {
            return false; // the guard above guarantees at least one digit in this field
        }
        unsigned v = 0;
        while (i < len && buf[i] >= '0' && buf[i] <= '9')
        {
            v = v * 10 + (unsigned)(buf[i] - '0');
            if (v > 255)
            {
                return false;
            }
            i++;
        }
        nums[ni] = v;
        if (ni < 5)
        {
            if (i >= len || buf[i] != ',')
            {
                return false;
            }
            i++;
        }
    }
    ip[0] = (uint8_t)nums[0];
    ip[1] = (uint8_t)nums[1];
    ip[2] = (uint8_t)nums[2];
    ip[3] = (uint8_t)nums[3];
    *port = (uint16_t)(nums[4] * 256 + nums[5]);
    return true;
}

bool pc_ftp_parse_epsv(const char *buf, size_t len, uint16_t *port)
{
    if (!buf || !port)
    {
        return false;
    }
    size_t i = 0;
    while (i < len && buf[i] != '(')
    {
        i++;
    }
    if (i >= len)
    {
        return false;
    }
    i++; // past '('
    if (i >= len)
    {
        return false;
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
        return false;
    }

    if (i >= len || buf[i] < '0' || buf[i] > '9')
    {
        return false; // the guard above guarantees at least one port digit follows
    }
    unsigned v = 0;
    while (i < len && buf[i] >= '0' && buf[i] <= '9')
    {
        v = v * 10 + (unsigned)(buf[i] - '0');
        if (v > 65535)
        {
            return false;
        }
        i++;
    }
    *port = (uint16_t)v;
    return true;
}

#endif // PC_ENABLE_FTP
