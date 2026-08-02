// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file gpib.c
 * @brief GPIB-over-LAN (Prologix-style) `++` command codec (pure, host-tested).
 */

#include "services/instrumentation/gpib/gpib.h"
#include "mmgr/membuild.h" // pc_sb frame builder

#if PC_ENABLE_GPIB

#include <string.h>

size_t pc_gpib_command(char *buf, size_t cap, const char *cmd)
{
    if (!buf || cap == 0 || !cmd)
    {
        return 0;
    }
    pc_sb sb_cmd = {buf, cap, 0, PROTO_TRUE};
    pc_sb_lit(&sb_cmd, "++");
    pc_sb_put(&sb_cmd, cmd);
    pc_sb_lit(&sb_cmd, "\n");
    return pc_sb_finish(&sb_cmd);
}

size_t pc_gpib_addr(char *buf, size_t cap, uint8_t pad, int sad)
{
    if (!buf || cap == 0 || pad > 30)
    {
        return 0;
    }
    pc_sb sb_addr = {buf, cap, 0, PROTO_TRUE};
    pc_sb_lit(&sb_addr, "++addr ");
    pc_sb_u32(&sb_addr, pad);
    if (sad >= 0)
    {
        pc_sb_ch(&sb_addr, ' ');
        pc_sb_i64(&sb_addr, sad);
    }
    pc_sb_lit(&sb_addr, "\n");
    return pc_sb_finish(&sb_addr);
}

size_t pc_gpib_read(char *buf, size_t cap, GpibRead mode, uint8_t ch)
{
    if (!buf || cap == 0)
    {
        return 0;
    }
    pc_sb sb_read = {buf, cap, 0, PROTO_TRUE};
    switch (mode)
    {
    case UNTIL_EOI:
        pc_sb_lit(&sb_read, "++read eoi\n");
        break;
    case UNTIL_CHAR:
        pc_sb_lit(&sb_read, "++read ");
        pc_sb_u32(&sb_read, ch);
        pc_sb_lit(&sb_read, "\n");
        break;
    case UNTIL_TIMEOUT:
    default:
        pc_sb_lit(&sb_read, "++read\n");
        break;
    }
    return pc_sb_finish(&sb_read);
}

size_t pc_gpib_spoll(char *buf, size_t cap, int pad, int sad)
{
    if (!buf || cap == 0)
    {
        return 0;
    }
    pc_sb sb_spoll = {buf, cap, 0, PROTO_TRUE};
    pc_sb_lit(&sb_spoll, "++spoll");
    if (pad >= 0)
    {
        pc_sb_ch(&sb_spoll, ' ');
        pc_sb_i64(&sb_spoll, pad);
        if (sad >= 0)
        {
            pc_sb_ch(&sb_spoll, ' ');
            pc_sb_i64(&sb_spoll, sad);
        }
    }
    pc_sb_lit(&sb_spoll, "\n");
    return pc_sb_finish(&sb_spoll);
}

size_t pc_gpib_eos(char *buf, size_t cap, GpibEos eos)
{
    if (!buf || cap == 0)
    {
        return 0;
    }
    pc_sb sb_eos = {buf, cap, 0, PROTO_TRUE};
    pc_sb_lit(&sb_eos, "++eos ");
    pc_sb_i64(&sb_eos, (int64_t)eos); // the wire field IS the enumerator's decimal value
    pc_sb_lit(&sb_eos, "\n");
    return pc_sb_finish(&sb_eos);
}

size_t pc_gpib_build_data(uint8_t *buf, size_t cap, const uint8_t *src, size_t len)
{
    if (!buf || cap == 0 || (len && !src))
    {
        return 0;
    }
    size_t o = 0;
    for (size_t i = 0; i < len; i++)
    {
        uint8_t c = src[i];
        proto_bool esc = (c == 13 || c == 10 || c == 27 || c == 43); // CR / LF / ESC / '+'
        size_t need = esc ? 2 : 1;
        if (o + need + 1 > cap) // + 1 reserves room for the trailing '\n'
        {
            return 0;
        }
        if (esc)
        {
            buf[o++] = 27; // leading ESC
        }
        buf[o++] = c;
    }
    buf[o++] = '\n'; // the unescaped line terminator
    return o;
}

proto_bool pc_gpib_is_command(const char *line, size_t len)
{
    return line && len >= 2 && line[0] == '+' && line[1] == '+';
}

// Trim leading and trailing spaces / CR / LF from [s, s+len); updates s and len.
static void trim(const char **s, size_t *len)
{
    const char *p = *s;
    size_t n = *len;
    while (n && (p[n - 1] == '\r' || p[n - 1] == '\n' || p[n - 1] == ' '))
    {
        n--;
    }
    while (n && (*p == ' '))
    {
        p++;
        n--;
    }
    *s = p;
    *len = n;
}

proto_bool pc_gpib_parse_decimal(const char *s, size_t len, uint32_t *out)
{
    if (!s)
    {
        return PROTO_FALSE;
    }
    trim(&s, &len);
    if (len == 0)
    {
        return PROTO_FALSE;
    }
    uint32_t v = 0;
    for (size_t i = 0; i < len; i++)
    {
        if (s[i] < '0' || s[i] > '9')
        {
            return PROTO_FALSE;
        }
        v = v * 10 + (uint32_t)(s[i] - '0');
    }
    if (out)
    {
        *out = v;
    }
    return PROTO_TRUE;
}

proto_bool pc_gpib_parse_addr(const char *s, size_t len, uint8_t *pad, int *sad)
{
    if (!s)
    {
        return PROTO_FALSE;
    }
    trim(&s, &len);
    if (len == 0)
    {
        return PROTO_FALSE;
    }
    // primary
    size_t i = 0;
    uint32_t p = 0;
    proto_bool any = PROTO_FALSE;
    while (i < len && s[i] >= '0' && s[i] <= '9')
    {
        p = p * 10 + (uint32_t)(s[i] - '0');
        i++;
        any = PROTO_TRUE;
    }
    if (!any || p > 30)
    {
        return PROTO_FALSE;
    }
    // optional secondary after spaces
    int sad_val = -1;
    while (i < len && s[i] == ' ')
    {
        i++;
    }
    if (i < len)
    {
        uint32_t sa = 0;
        proto_bool sany = PROTO_FALSE;
        while (i < len && s[i] >= '0' && s[i] <= '9')
        {
            sa = sa * 10 + (uint32_t)(s[i] - '0');
            i++;
            sany = PROTO_TRUE;
        }
        if (!sany || i != len || sa < 96 || sa > 126)
        {
            return PROTO_FALSE;
        }
        sad_val = (int)sa;
    }
    if (i != len) // GCOVR_EXCL_LINE  unreachable: the space skip above stops at i==len or a non-space, and
    {
        return PROTO_FALSE; // GCOVR_EXCL_LINE  the secondary block only falls through when i==len - so i==len here
    }
    if (pad)
    {
        *pad = (uint8_t)p;
    }
    if (sad)
    {
        *sad = sad_val;
    }
    return PROTO_TRUE;
}

proto_bool pc_gpib_parse_version(const char *s, size_t len, const char **ver, size_t *ver_len)
{
    if (!s)
    {
        return PROTO_FALSE;
    }
    static const char key[] = "version ";
    const size_t klen = sizeof(key) - 1;
    if (len < klen)
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i + klen <= len; i++)
    {
        if (memcmp(s + i, key, klen) == 0)
        {
            const char *v = s + i + klen;
            size_t vlen = len - i - klen;
            trim(&v, &vlen);
            if (vlen == 0)
            {
                return PROTO_FALSE;
            }
            if (ver)
            {
                *ver = v;
            }
            if (ver_len)
            {
                *ver_len = vlen;
            }
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

#endif // PC_ENABLE_GPIB
