// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file exc_decoder.c
 * @brief Panic / exception decoder (see exc_decoder.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_EXC_DECODER

#include "cellularum_laboro/cellularum_laboro.h" // cellul.find: each field's marker inside the panic dump
#include "server/core/exc_decoder/exc_decoder.h"
#include "shared/hex/hex.h"            // PROTOCORE_HEX: the shared digit tables
#include "verba_scribo/verba_scribo.h" // verba_*: the text and number writers the builders chain

PROTOCORE_BEGIN_DECLS

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

static size_t put_json_str(char *out, size_t cap, size_t at, const char *s)
{
    at = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = at, .text = "\"");
    const char *src = s ? s : "";
    for (const char *p = src; *p; p++)
    {
        if (*p == '"' || *p == '\\')
        {
            char esc[3] = {'\\', *p, '\0'};
            at = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = at, .text = esc);
        }
        else
        {
            at = EMBED_CALL(verba_littera.ch, VerbaLitteraCfg, .out = out, .cap = cap, .at = at, .ch = *p);
        }
    }
    return EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = at, .text = "\"");
}

// Emit a 32-bit value as a JSON string literal "0x........".
static size_t put_hex32(char *out, size_t cap, size_t at, uint32_t v)
{
    char t[13] = "\"0x00000000\"";
    for (int i = 0; i < 8; i++)
    {
        t[3 + i] = PROTOCORE_HEX.lower[(v >> ((7 - i) * 4)) & 0xF];
    }
    return EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = at, .text = t);
}

static size_t put_int(char *out, size_t cap, size_t at, int v)
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
    return EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = at, .text = o);
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
    const char *c = EMBED_CALL(cellul.find, CatenaFinitaCfg, .src = text,
                               .cap = EMBED_CALL(cellul.len, CatenaFinitaCfg, .src = text, .cap = 0xFFFF) + 1u,
                               .other = "panic'ed (", .other_cap = sizeof("panic'ed ("), .ci = PROTO_FALSE);
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
    const char *co = EMBED_CALL(cellul.find, CatenaFinitaCfg, .src = text,
                                .cap = EMBED_CALL(cellul.len, CatenaFinitaCfg, .src = text, .cap = 0xFFFF) + 1u,
                                .other = "Core ", .other_cap = sizeof("Core "), .ci = PROTO_FALSE);
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
    const char *e = EMBED_CALL(cellul.find, CatenaFinitaCfg, .src = text,
                               .cap = EMBED_CALL(cellul.len, CatenaFinitaCfg, .src = text, .cap = 0xFFFF) + 1u,
                               .other = "EXCVADDR", .other_cap = sizeof("EXCVADDR"), .ci = PROTO_FALSE);
    if (!e)
    {
        return;
    }
    const char *colon = EMBED_CALL(cellul.find, CatenaFinitaCfg, .src = e,
                                   .cap = EMBED_CALL(cellul.len, CatenaFinitaCfg, .src = e, .cap = 0xFFFF) + 1u,
                                   .other = ":", .other_cap = sizeof(":"), .ci = PROTO_FALSE);
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
    const char *pcl =
        EMBED_CALL(cellul.starts, CatenaFinitaCfg, .src = text, .other = "PC", .cap = 2, .ci = PROTO_FALSE)
            ? text
            : EMBED_CALL(cellul.find, CatenaFinitaCfg, .src = text,
                         .cap = EMBED_CALL(cellul.len, CatenaFinitaCfg, .src = text, .cap = 0xFFFF) + 1u,
                         .other = "\nPC", .other_cap = sizeof("\nPC"), .ci = PROTO_FALSE);
    if (!pcl)
    {
        return;
    }
    const char *colon = EMBED_CALL(cellul.find, CatenaFinitaCfg, .src = pcl,
                                   .cap = EMBED_CALL(cellul.len, CatenaFinitaCfg, .src = pcl, .cap = 0xFFFF) + 1u,
                                   .other = ":", .other_cap = sizeof(":"), .ci = PROTO_FALSE);
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
    const char *bt = EMBED_CALL(cellul.find, CatenaFinitaCfg, .src = text,
                                .cap = EMBED_CALL(cellul.len, CatenaFinitaCfg, .src = text, .cap = 0xFFFF) + 1u,
                                .other = "Backtrace:", .other_cap = sizeof("Backtrace:"), .ci = PROTO_FALSE);
    if (!bt)
    {
        return;
    }
    const char *p = bt + 10;
    while (out->frame_count < PROTOCORE_EXC_MAX_FRAMES)
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

void protocore_exc_parse(uint8_t *work)
{
    (void)work;
    const char *text = ExcV.parse_args.text;
    ExcInfo *out = ExcV.parse_args.info;

    ExcV.ok = PROTO_FALSE;
    if (!text || !out)
    {
        return;
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

    ExcV.ok = out->cause[0] != '\0' || out->pc != 0 || out->frame_count > 0;
}

void protocore_exc_json(uint8_t *work)
{
    (void)work;
    const ExcInfo *info = ExcV.parse_args.info;
    char *out = ExcV.out_args.out;
    const size_t cap = ExcV.out_args.cap;

    if (!info || !out || cap == 0)
    {
        ExcV.n = 0;
        return;
    }
    size_t b = 0;
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = "{");
    proto_bool first = PROTO_TRUE;
    if (info->core >= 0)
    {
        b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = "\"core\":");
        b = put_int(out, cap, b, info->core);
        first = PROTO_FALSE;
    }
    if (!first)
    {
        b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = ",");
    }
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = "\"cause\":");
    b = put_json_str(out, cap, b, info->cause);
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = ",\"pc\":");
    b = put_hex32(out, cap, b, info->pc);
    if (info->has_excvaddr)
    {
        b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = ",\"excvaddr\":");
        b = put_hex32(out, cap, b, info->excvaddr);
    }
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = ",\"backtrace\":[");
    for (size_t i = 0; i < info->frame_count; i++)
    {
        if (i)
        {
            b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = ",");
        }
        b = put_hex32(out, cap, b, info->frames[i].pc);
    }
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = "]}");
    if (!EMBED_CALL(verba_finis.ok, VerbaFinisCfg, .cap = cap, .at = b))
    {
        out[0] = '\0';
        ExcV.n = 0;
        return;
    }
    out[b] = '\0';
    ExcV.n = b;
}

#if PROTOCORE_HAS_VENDOR_COREDUMP
// The image half lives in exc_coredump.c, the arm that has one to read; it is bound here so the
// whole surface is one initializer rather than a runtime install with an order to get wrong.
void protocore_exc_present(uint8_t *work);
void protocore_exc_summary(uint8_t *work);
void protocore_exc_read(uint8_t *work);
void protocore_exc_save(uint8_t *work);
void protocore_exc_erase(uint8_t *work);
#endif

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
ExcVars ExcV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_EXC_DECODER
