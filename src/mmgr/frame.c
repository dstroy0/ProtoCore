// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file frame.c
 * @brief The one frame engine. Walks a pc_field spec, pulls one argument per valued field.
 *
 * Every conversion is a pc_sb appender, so what lives here is the dispatch loop and the
 * fail-closed contract, never formatting logic.
 */

#include "mmgr/frame.h"
#include "mmgr/protostr.h" // str: the bounded-run walks
#include "shared_primitives/speed_opt.h"

#ifndef PC_FRAME_SCAN_LITERALS
#define PC_FRAME_SCAN_LITERALS 0 // 1 = find each literal length at runtime instead of reading it from the spec
#endif

// A dispatch loop over a table, handling no secrets, so the size level the TU would otherwise
// inherit buys nothing here and costs the appenders their inlining.
PC_OPTIMIZE_O2

// A null string field renders empty, so no caller has to coalesce one before passing it.
static const char *str_or_empty(const char *s)
{
    if (s == NULL)
    {
        return "";
    }
    return s;
}

size_t pc_frame_build(char *out, size_t cap, const pc_field *spec, const pc_fval *v, size_t nv)
{
    if (!out || cap == 0 || !spec)
    {
        return 0;
    }
    pc_sb b = {out, cap, 0, PROTO_TRUE};
    size_t k = 0; // next value; a valued field consumes one
    for (const pc_field *f = spec; f->kind != PC_FK_END; f++)
    {
        if (f->kind == PC_FK_LIT)
        {
#if PC_FRAME_SCAN_LITERALS
            pc_sb_put(&b, f->lit);
#else
            // the spec carries the length; scanning for the NUL would rediscover it every call
            pc_sb_put_n(&b, f->lit, f->len);
#endif
            continue;
        }
        // A valued field needs a value, and it needs the one the spec declared. Either failure is
        // the caller and the spec disagreeing, which cannot produce the frame that was asked for.
        if (k >= nv || !v || v[k].kind != f->kind)
        {
            out[0] = '\0';
            return 0;
        }
        const pc_fval *a = &v[k];
        k++;
        switch (f->kind)
        {
        case PC_FK_STR:
            pc_sb_put(&b, str_or_empty(a->as.s));
            break;
        case PC_FK_U32:
            pc_sb_u32(&b, a->as.u32);
            break;
        case PC_FK_U64:
            pc_sb_u64(&b, a->as.u64);
            break;
        case PC_FK_I64:
            pc_sb_i64(&b, a->as.i64);
            break;
        case PC_FK_DEC:
            pc_sb_u32w(&b, a->as.u32, f->width);
            break;
        case PC_FK_HEX:
            pc_sb_hex(&b, a->as.u64, f->width ? f->width : 1);
            break;
        case PC_FK_OCT:
            pc_sb_uint(&b, a->as.u64, 8, f->width ? f->width : 1);
            break;
        case PC_FK_G:
            pc_sb_g(&b, a->as.d, f->width ? f->width : 6);
            break;
        case PC_FK_FIX:
            pc_sb_fixed(&b, a->as.d, f->width);
            break;
        case PC_FK_CH:
            pc_sb_ch(&b, a->as.c);
            break;
        case PC_FK_JSON:
            pc_sb_json(&b, str_or_empty(a->as.s));
            break;
        case PC_FK_XML:
            pc_sb_xml(&b, str_or_empty(a->as.s));
            break;
        default:
            // An unknown opcode means the spec and this engine disagree; refuse rather than
            // emit a frame that is missing a field and looks well-formed.
            out[0] = '\0';
            return 0;
        }
    }
    if (k != nv)
    {
        out[0] = '\0'; // more values than the spec declares fields
        return 0;
    }
    size_t n = pc_sb_finish(&b);
    if (n == 0)
    {
        // Did not fit (or was empty). Leave a valid, empty C string either way: callers that
        // ignore the return must never read stale bytes or an unterminated buffer.
        out[0] = '\0';
    }
    return n;
}

size_t pc_frame_append(char *out, size_t cap, const pc_field *spec, const pc_fval *v, size_t nv)
{
    if (!out || cap == 0 || !spec)
    {
        return 0;
    }
    size_t used = str.len(out, cap);
    if (used >= cap)
    {
        return 0;
    }
    size_t n = pc_frame_build(out + used, cap - used, spec, v, nv);
    if (n == 0)
    {
        out[used] = '\0'; // rewind: the frame is added whole or not at all
        return 0;
    }
    return used + n;
}
