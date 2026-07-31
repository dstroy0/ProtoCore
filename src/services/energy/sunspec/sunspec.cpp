// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sunspec.cpp
 * @brief SunSpec Modbus model-chain walker + point readers + map writer (pure, host-tested).
 */

#include "services/energy/sunspec/sunspec.h"

#if PC_ENABLE_SUNSPEC

#include <string.h>

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

bool pc_sunspec_check_marker(const uint8_t *regs, size_t len)
{
    return regs && len >= 4 && be32(regs) == SUNSPEC_MARKER;
}

bool pc_sunspec_begin(const uint8_t *regs, size_t len, size_t *offset)
{
    if (!offset || !pc_sunspec_check_marker(regs, len))
    {
        return false;
    }
    *offset = 4; // past the 2-register marker
    return true;
}

bool pc_sunspec_next_model(const uint8_t *regs, size_t len, size_t *offset, SunSpecModel *out)
{
    if (!regs || !offset || !out)
    {
        return false;
    }
    size_t o = *offset;
    if (o + 4 > len) // need the [id][length] header
    {
        return false;
    }
    uint16_t id = be16(regs + o);
    uint16_t length = be16(regs + o + 2);
    if (id == SUNSPEC_END_MODEL)
    {
        return false; // map terminator
    }
    size_t body_len = (size_t)length * 2;
    if (o + 4 + body_len > len) // body must be fully buffered
    {
        return false;
    }
    out->id = id;
    out->length = length;
    out->body = regs + o + 4;
    out->body_len = body_len;
    *offset = o + 4 + body_len;
    return true;
}

uint16_t pc_sunspec_u16(const uint8_t *body, size_t reg)
{
    return be16(body + reg * 2);
}

int16_t pc_sunspec_i16(const uint8_t *body, size_t reg)
{
    return (int16_t)be16(body + reg * 2);
}

uint32_t pc_sunspec_u32(const uint8_t *body, size_t reg)
{
    return be32(body + reg * 2);
}

int32_t pc_sunspec_i32(const uint8_t *body, size_t reg)
{
    return (int32_t)be32(body + reg * 2);
}

bool pc_sunspec_string(const uint8_t *body, size_t reg, size_t nregs, char *out, size_t out_cap)
{
    if (!body || !out || out_cap == 0)
    {
        return false;
    }
    size_t avail = nregs * 2;
    size_t i = 0;
    const uint8_t *p = body + reg * 2;
    for (; i < avail && i < out_cap - 1; i++)
    {
        if (p[i] == 0) // NUL padding ends the content
        {
            break;
        }
        out[i] = (char)p[i];
    }
    out[i] = '\0';
    return true;
}

// ---- writer ----

void pc_sunspec_writer_init(SunSpecWriter *w, uint8_t *buf, size_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->pos = 0;
    w->error = false;
}

static bool ss_put(SunSpecWriter *w, const uint8_t *p, size_t n)
{
    if (w->error)
    {
        return false;
    }
    if (w->pos + n > w->cap)
    {
        w->error = true;
        return false;
    }
    memcpy(w->buf + w->pos, p, n);
    w->pos += n;
    return true;
}

bool pc_sunspec_write_u16(SunSpecWriter *w, uint16_t v)
{
    uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v};
    return ss_put(w, b, 2);
}

bool pc_sunspec_write_i16(SunSpecWriter *w, int16_t v)
{
    return pc_sunspec_write_u16(w, (uint16_t)v);
}

bool pc_sunspec_write_u32(SunSpecWriter *w, uint32_t v)
{
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    return ss_put(w, b, 4);
}

bool pc_sunspec_write_i32(SunSpecWriter *w, int32_t v)
{
    return pc_sunspec_write_u32(w, (uint32_t)v);
}

bool pc_sunspec_write_marker(SunSpecWriter *w)
{
    return pc_sunspec_write_u32(w, SUNSPEC_MARKER);
}

bool pc_sunspec_write_model_header(SunSpecWriter *w, uint16_t id, uint16_t length)
{
    return pc_sunspec_write_u16(w, id) && pc_sunspec_write_u16(w, length);
}

bool pc_sunspec_write_string(SunSpecWriter *w, const char *s, size_t nregs)
{
    if (!s)
    {
        return false;
    }
    size_t field = nregs * 2;
    if (w->error)
    {
        return false;
    }
    if (w->pos + field > w->cap)
    {
        w->error = true;
        return false;
    }
    size_t slen = strnlen(s, field);
    for (size_t i = 0; i < field; i++)
    {
        w->buf[w->pos + i] = (i < slen) ? (uint8_t)s[i] : 0; // NUL-pad the remainder
    }
    w->pos += field;
    return true;
}

bool pc_sunspec_write_end_model(SunSpecWriter *w)
{
    return pc_sunspec_write_u16(w, SUNSPEC_END_MODEL) && pc_sunspec_write_u16(w, 0);
}

size_t pc_sunspec_writer_finish(SunSpecWriter *w)
{
    return w->error ? 0 : w->pos;
}

#endif // PC_ENABLE_SUNSPEC
