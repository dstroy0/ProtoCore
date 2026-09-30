// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file gpib.c
 * @brief GPIB-over-LAN (Prologix-style) `++` command codec (pure, host-tested).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_GPIB

#include "memoria_operor/memoria_operor.h"
#include "services/instrumentation/gpib/gpib.h"
#include "verba_scribo/verba_scribo.h" // verba_*: the text and number writers the builders chain

PROTOCORE_BEGIN_DECLS

size_t protocore_gpib_command(char *buf, size_t cap, const char *cmd)
{
    if (!buf || cap == 0 || !cmd)
    {
        return 0;
    }
    size_t sb_cmd = 0;
    sb_cmd = EMBED_CALL(verba_textus.put_n, VerbaTextusCfg, .out = buf, .cap = cap, .at = sb_cmd, .text = "++",
                        .text_len = sizeof("++") - 1);
    sb_cmd = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = buf, .cap = cap, .at = sb_cmd, .text = cmd);
    sb_cmd = EMBED_CALL(verba_textus.put_n, VerbaTextusCfg, .out = buf, .cap = cap, .at = sb_cmd, .text = "\n",
                        .text_len = sizeof("\n") - 1);
    return EMBED_CALL(verba_finis.finish, VerbaFinisCfg, .out = buf, .cap = cap, .at = sb_cmd);
}

size_t protocore_gpib_addr(char *buf, size_t cap, uint8_t pad, int sad)
{
    if (!buf || cap == 0 || pad > 30)
    {
        return 0;
    }
    size_t sb_addr = 0;
    sb_addr = EMBED_CALL(verba_textus.put_n, VerbaTextusCfg, .out = buf, .cap = cap, .at = sb_addr, .text = "++addr ",
                         .text_len = sizeof("++addr ") - 1);
    sb_addr = EMBED_CALL(verba_numerus.u32, VerbaNumerusCfg, .out = buf, .cap = cap, .at = sb_addr, .val = pad);
    if (sad >= 0)
    {
        sb_addr = EMBED_CALL(verba_littera.ch, VerbaLitteraCfg, .out = buf, .cap = cap, .at = sb_addr, .ch = ' ');
        sb_addr = EMBED_CALL(verba_numerus.i64, VerbaNumerusCfg, .out = buf, .cap = cap, .at = sb_addr, .sval = sad);
    }
    sb_addr = EMBED_CALL(verba_textus.put_n, VerbaTextusCfg, .out = buf, .cap = cap, .at = sb_addr, .text = "\n",
                         .text_len = sizeof("\n") - 1);
    return EMBED_CALL(verba_finis.finish, VerbaFinisCfg, .out = buf, .cap = cap, .at = sb_addr);
}

size_t protocore_gpib_read(char *buf, size_t cap, GpibRead mode, uint8_t ch)
{
    if (!buf || cap == 0)
    {
        return 0;
    }
    size_t sb_read = 0;
    switch (mode)
    {
    case UNTIL_EOI:
        sb_read = EMBED_CALL(verba_textus.put_n, VerbaTextusCfg, .out = buf, .cap = cap, .at = sb_read,
                             .text = "++read eoi\n", .text_len = sizeof("++read eoi\n") - 1);
        break;
    case UNTIL_CHAR:
        sb_read = EMBED_CALL(verba_textus.put_n, VerbaTextusCfg, .out = buf, .cap = cap, .at = sb_read,
                             .text = "++read ", .text_len = sizeof("++read ") - 1);
        sb_read = EMBED_CALL(verba_numerus.u32, VerbaNumerusCfg, .out = buf, .cap = cap, .at = sb_read, .val = ch);
        sb_read = EMBED_CALL(verba_textus.put_n, VerbaTextusCfg, .out = buf, .cap = cap, .at = sb_read, .text = "\n",
                             .text_len = sizeof("\n") - 1);
        break;
    case UNTIL_TIMEOUT:
    default:
        sb_read = EMBED_CALL(verba_textus.put_n, VerbaTextusCfg, .out = buf, .cap = cap, .at = sb_read,
                             .text = "++read\n", .text_len = sizeof("++read\n") - 1);
        break;
    }
    return EMBED_CALL(verba_finis.finish, VerbaFinisCfg, .out = buf, .cap = cap, .at = sb_read);
}

size_t protocore_gpib_spoll(char *buf, size_t cap, int pad, int sad)
{
    if (!buf || cap == 0)
    {
        return 0;
    }
    size_t sb_spoll = 0;
    sb_spoll = EMBED_CALL(verba_textus.put_n, VerbaTextusCfg, .out = buf, .cap = cap, .at = sb_spoll, .text = "++spoll",
                          .text_len = sizeof("++spoll") - 1);
    if (pad >= 0)
    {
        sb_spoll = EMBED_CALL(verba_littera.ch, VerbaLitteraCfg, .out = buf, .cap = cap, .at = sb_spoll, .ch = ' ');
        sb_spoll = EMBED_CALL(verba_numerus.i64, VerbaNumerusCfg, .out = buf, .cap = cap, .at = sb_spoll, .sval = pad);
        if (sad >= 0)
        {
            sb_spoll = EMBED_CALL(verba_littera.ch, VerbaLitteraCfg, .out = buf, .cap = cap, .at = sb_spoll, .ch = ' ');
            sb_spoll =
                EMBED_CALL(verba_numerus.i64, VerbaNumerusCfg, .out = buf, .cap = cap, .at = sb_spoll, .sval = sad);
        }
    }
    sb_spoll = EMBED_CALL(verba_textus.put_n, VerbaTextusCfg, .out = buf, .cap = cap, .at = sb_spoll, .text = "\n",
                          .text_len = sizeof("\n") - 1);
    return EMBED_CALL(verba_finis.finish, VerbaFinisCfg, .out = buf, .cap = cap, .at = sb_spoll);
}

size_t protocore_gpib_eos(char *buf, size_t cap, GpibEos eos)
{
    if (!buf || cap == 0)
    {
        return 0;
    }
    size_t sb_eos = 0;
    sb_eos = EMBED_CALL(verba_textus.put_n, VerbaTextusCfg, .out = buf, .cap = cap, .at = sb_eos, .text = "++eos ",
                        .text_len = sizeof("++eos ") - 1);
    // the wire field IS the enumerator's decimal value
    sb_eos = EMBED_CALL(verba_numerus.i64, VerbaNumerusCfg, .out = buf, .cap = cap, .at = sb_eos, .sval = (int64_t)eos);
    sb_eos = EMBED_CALL(verba_textus.put_n, VerbaTextusCfg, .out = buf, .cap = cap, .at = sb_eos, .text = "\n",
                        .text_len = sizeof("\n") - 1);
    return EMBED_CALL(verba_finis.finish, VerbaFinisCfg, .out = buf, .cap = cap, .at = sb_eos);
}

size_t protocore_gpib_build_data(uint8_t *buf, size_t cap, const uint8_t *src, size_t len)
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

proto_bool protocore_gpib_is_command(const char *line, size_t len)
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

proto_bool protocore_gpib_parse_decimal(const char *s, size_t len, uint32_t *out)
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

proto_bool protocore_gpib_parse_addr(const char *s, size_t len, uint8_t *pad, int *sad)
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
    if (i != len)
    {
        return PROTO_FALSE;
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

proto_bool protocore_gpib_parse_version(const char *s, size_t len, const char **ver, size_t *ver_len)
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
        if (EMBED_CALL(memor.cmp, MemoriaCfg, .src = s + i, .other = key, .bytes = klen) == 0)
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

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_GPIB
