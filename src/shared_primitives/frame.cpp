// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file frame.cpp
 * @brief The one frame engine. Walks a pc_field spec, pulls one argument per valued field.
 *
 * Deliberately thin: every conversion is an already-tested pc_sb appender, so this file adds a
 * dispatch loop and the fail-closed contract, not new formatting logic. That is the point of the
 * spec - the number of places that can get a conversion wrong stays at one no matter how many
 * frames the library declares.
 */

#include "shared_primitives/frame.h"
#include "shared_primitives/speed_opt.h"

#ifndef PC_FRAME_SCAN_LITERALS
#define PC_FRAME_SCAN_LITERALS 0 // 1 = find each literal length at runtime instead of reading it from the spec
#endif

// A dispatch loop over a table with no secrets, and the level it would otherwise inherit is what
// stops the appenders inlining.
PC_OPTIMIZE_O2

size_t pc_frame_vbuild(char *out, size_t cap, const pc_field *spec, va_list ap)
{
    if (!out || cap == 0 || !spec)
    {
        return 0;
    }
    pc_sb b = {out, cap, 0, true};
    for (const pc_field *f = spec; f->kind != PC_FK_END; f++)
    {
        switch (f->kind)
        {
        case PC_FK_LIT:
#if PC_FRAME_SCAN_LITERALS
            pc_sb_put(&b, f->lit);
#else
            // the spec carries the length; scanning for the NUL would rediscover it every call
            pc_sb_put_n(&b, f->lit, f->len);
#endif
            break;
        case PC_FK_STR: {
            const char *s = va_arg(ap, const char *);
            pc_sb_put(&b, s ? s : "");
            break;
        }
        case PC_FK_U32:
            pc_sb_u32(&b, va_arg(ap, uint32_t));
            break;
        case PC_FK_U64:
            pc_sb_u64(&b, va_arg(ap, uint64_t));
            break;
        case PC_FK_I64:
            pc_sb_i64(&b, va_arg(ap, int64_t));
            break;
        case PC_FK_DEC:
            pc_sb_u32w(&b, va_arg(ap, uint32_t), f->width);
            break;
        case PC_FK_HEX:
            pc_sb_hex(&b, va_arg(ap, uint64_t), f->width ? f->width : 1);
            break;
        case PC_FK_OCT:
            pc_sb_uint(&b, va_arg(ap, uint64_t), 8, f->width ? f->width : 1);
            break;
        case PC_FK_G:
            pc_sb_g(&b, va_arg(ap, double), f->width ? f->width : 6);
            break;
        case PC_FK_FIX:
            pc_sb_fixed(&b, va_arg(ap, double), f->width);
            break;
        case PC_FK_CH:
            // char promotes to int through the ellipsis, so it must be read back as int.
            pc_sb_ch(&b, (char)va_arg(ap, int));
            break;
        case PC_FK_JSON:
            pc_sb_json(&b, va_arg(ap, const char *));
            break;
        case PC_FK_XML:
            pc_sb_xml(&b, va_arg(ap, const char *));
            break;
        default:
            // An unknown opcode means the spec and this engine disagree; refuse rather than
            // emit a frame that is missing a field and looks well-formed.
            out[0] = '\0';
            return 0;
        }
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

size_t pc_frame_build(char *out, size_t cap, const pc_field *spec, ...)
{
    va_list ap;
    va_start(ap, spec);
    size_t n = pc_frame_vbuild(out, cap, spec, ap);
    va_end(ap);
    return n;
}

size_t pc_frame_append(char *out, size_t cap, const pc_field *spec, ...)
{
    if (!out || cap == 0 || !spec)
    {
        return 0;
    }
    size_t used = strnlen(out, cap);
    if (used >= cap)
    {
        return 0;
    }
    va_list ap;
    va_start(ap, spec);
    size_t n = pc_frame_vbuild(out + used, cap - used, spec, ap);
    va_end(ap);
    if (n == 0)
    {
        out[used] = '\0'; // rewind: the frame is added whole or not at all
        return 0;
    }
    return used + n;
}
