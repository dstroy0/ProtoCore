// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file exc_decoder.c
 * @brief ESP32 panic / exception decoder (see exc_decoder.h).
 */

#include "server/exc_decoder.h"
#include "mmgr/membuild.h"         // pc_sb frame builder
#include "shared_primitives/hex.h" // PC_HEX_LOWER - the shared digit table

#if PC_ENABLE_EXC_DECODER

static proto_bool hexval(char c, uint8_t *v)
{
    if (c >= '0' && c <= '9')
    {
        *v = (uint8_t)(c - '0');
    }
    else if (c >= 'a' && c <= 'f')
    {
        *v = (uint8_t)(c - 'a' + 10);
    }
    else if (c >= 'A' && c <= 'F')
    {
        *v = (uint8_t)(c - 'A' + 10);
    }
    else
    {
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t')
    {
        p++;
    }
    return p;
}

// Parse a "0x...." hex literal at p; on success write *out and return the char after the last digit.
static const char *parse_hex(const char *p, uint32_t *out)
{
    if (p[0] != '0' || (p[1] != 'x' && p[1] != 'X'))
    {
        return NULL;
    }
    p += 2;
    uint32_t v = 0;
    int n = 0;
    uint8_t d = 0;
    while (hexval(*p, &d) && n < 8)
    {
        v = (v << 4) | d;
        p++;
        n++;
    }
    if (n == 0)
    {
        return NULL;
    }
    *out = v;
    return p;
}

static void put_json_str(pc_sb *b, const char *s)
{
    pc_sb_put(b, "\"");
    const char *src = s ? s : "";
    for (const char *p = src; *p; p++)
    {
        if (*p == '"' || *p == '\\')
        {
            char esc[3] = {'\\', *p, '\0'};
            pc_sb_put(b, esc);
        }
        else if (b->len + 1 < b->cap)
        {
            b->p[b->len++] = *p;
        }
        else
        {
            b->ok = PROTO_FALSE;
        }
    }
    pc_sb_put(b, "\"");
}

// Emit a 32-bit value as a JSON string literal "0x........".
static void put_hex32(pc_sb *b, uint32_t v)
{
    char t[13] = "\"0x00000000\"";
    for (int i = 0; i < 8; i++)
    {
        t[3 + i] = PC_HEX_LOWER[(v >> ((7 - i) * 4)) & 0xF];
    }
    pc_sb_put(b, t);
}

static void put_int(pc_sb *b, int v)
{
    char t[12];
    int n = 0;
    proto_bool neg = v < 0;
    unsigned u = neg ? (unsigned)(-(long)v) : (unsigned)v;
    do
    {
        t[n++] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    char o[13];
    int k = 0;
    if (neg)
    {
        o[k++] = '-';
    }
    for (int i = 0; i < n; i++)
    {
        o[k++] = t[n - 1 - i];
    }
    o[k] = '\0';
    pc_sb_put(b, o);
}

// Parse a run of decimal digits at @p p into a small non-negative int, clamped to avoid signed-overflow
// UB on absurd input (core ids / counts are tiny). Extracted to keep the callers' scan loops flat.
static int parse_small_int(const char *p)
{
    int n = 0;
    while (*p >= '0' && *p <= '9')
    {
        if (n < 100000)
        {
            n = n * 10 + (*p - '0');
        }
        p++;
    }
    return n;
}

// Cause: "...panic'ed (LoadProhibited)."
static void parse_cause(const char *text, ExcInfo *out)
{
    const char *c = strstr(text, "panic'ed (");
    if (!c)
    {
        return;
    }
    c += 10;
    size_t i = 0;
    while (i < sizeof(out->cause) - 1 && c[i] && c[i] != ')') // range check first (short-circuits the read)
    {
        out->cause[i] = c[i];
        i++;
    }
    out->cause[i] = '\0';
}

// Core number: "Core  N ...".
static void parse_core(const char *text, ExcInfo *out)
{
    const char *co = strstr(text, "Core ");
    if (!co)
    {
        return;
    }
    const char *p = skip_ws(co + 5);
    if (*p >= '0' && *p <= '9')
    {
        out->core = parse_small_int(p); // clamped inside; avoids signed-overflow UB on a huge number
    }
}

// EXCVADDR (faulting data address).
static void parse_excvaddr(const char *text, ExcInfo *out)
{
    const char *e = strstr(text, "EXCVADDR");
    if (!e)
    {
        return;
    }
    const char *colon = strchr(e, ':');
    if (!colon)
    {
        return;
    }
    uint32_t v = 0;
    if (parse_hex(skip_ws(colon + 1), &v))
    {
        out->excvaddr = v;
        out->has_excvaddr = PROTO_TRUE;
    }
}

// Register-dump PC: a line that starts with "PC" (not "EPC..."). Anchor to a line break.
static void parse_pc(const char *text, ExcInfo *out)
{
    const char *pcl = (strncmp(text, "PC", 2) == 0) ? text : strstr(text, "\nPC");
    if (!pcl)
    {
        return;
    }
    const char *colon = strchr(pcl, ':');
    if (!colon)
    {
        return;
    }
    uint32_t v = 0;
    if (parse_hex(skip_ws(colon + 1), &v))
    {
        out->pc = v;
    }
}

// Backtrace: "Backtrace: pc:sp pc:sp ...".
static void parse_backtrace(const char *text, ExcInfo *out)
{
    const char *bt = strstr(text, "Backtrace:");
    if (!bt)
    {
        return;
    }
    const char *p = bt + 10;
    while (out->frame_count < PC_EXC_MAX_FRAMES)
    {
        p = skip_ws(p);
        uint32_t pc = 0;
        uint32_t sp = 0;
        const char *q = parse_hex(p, &pc);
        if (!q || *q != ':')
        {
            break;
        }
        const char *r = parse_hex(q + 1, &sp);
        if (!r)
        {
            break;
        }
        out->frames[out->frame_count].pc = pc;
        out->frames[out->frame_count].sp = sp;
        out->frame_count++;
        p = r;
    }
}

proto_bool pc_exc_parse(const char *text, ExcInfo *out)
{
    if (!text || !out)
    {
        return PROTO_FALSE;
    }
    out->core = -1;
    out->cause[0] = '\0';
    out->pc = 0;
    out->excvaddr = 0;
    out->has_excvaddr = PROTO_FALSE;
    out->frame_count = 0;

    parse_cause(text, out);
    parse_core(text, out);
    parse_excvaddr(text, out);
    parse_pc(text, out);
    parse_backtrace(text, out);

    if (out->pc == 0 && out->frame_count > 0)
    {
        out->pc = out->frames[0].pc;
    }

    return out->cause[0] != '\0' || out->pc != 0 || out->frame_count > 0;
}

size_t pc_exc_json(const ExcInfo *info, char *out, size_t cap)
{
    if (!info || !out || cap == 0)
    {
        return 0;
    }
    pc_sb b = {out, cap, 0, PROTO_TRUE};
    pc_sb_put(&b, "{");
    proto_bool first = PROTO_TRUE;
    if (info->core >= 0)
    {
        pc_sb_put(&b, "\"core\":");
        put_int(&b, info->core);
        first = PROTO_FALSE;
    }
    if (!first)
    {
        pc_sb_put(&b, ",");
    }
    pc_sb_put(&b, "\"cause\":");
    put_json_str(&b, info->cause);
    pc_sb_put(&b, ",\"pc\":");
    put_hex32(&b, info->pc);
    if (info->has_excvaddr)
    {
        pc_sb_put(&b, ",\"excvaddr\":");
        put_hex32(&b, info->excvaddr);
    }
    pc_sb_put(&b, ",\"backtrace\":[");
    for (size_t i = 0; i < info->frame_count; i++)
    {
        if (i)
        {
            pc_sb_put(&b, ",");
        }
        put_hex32(&b, info->frames[i].pc);
    }
    pc_sb_put(&b, "]}");
    if (!b.ok)
    {
        return 0;
    }
    out[b.len] = '\0';
    return b.len;
}

#endif // PC_ENABLE_EXC_DECODER
