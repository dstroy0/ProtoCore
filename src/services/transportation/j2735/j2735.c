// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file j2735.c
 * @brief SAE J2735 V2X UPER codec + BSMcore (see j2735.h).
 */

#include "services/transportation/j2735/j2735.h"

#if PC_ENABLE_J2735

void pc_uper_writer_init(UperWriter *w, uint8_t *buf, size_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->bit_pos = 0;
    w->ok = (buf != NULL && cap > 0);
    // Zero the buffer so we can OR bits in - only when it is a real buffer (a null buf leaves ok=false).
    if (buf)
    {
        for (size_t i = 0; i < cap; i++)
        {
            buf[i] = 0;
        }
    }
}

size_t pc_uper_writer_finish(UperWriter *w)
{
    if (!w->ok)
    {
        return 0;
    }
    return (w->bit_pos + 7) / 8;
}

void pc_uper_put_bits(UperWriter *w, uint32_t value, unsigned nbits)
{
    if (!w->ok || nbits == 0)
    {
        return;
    }
    if (w->bit_pos + nbits > w->cap * 8)
    {
        w->ok = PROTO_FALSE;
        return;
    }
    for (unsigned i = 0; i < nbits; i++)
    {
        unsigned bit = (value >> (nbits - 1 - i)) & 1u;
        if (bit)
        {
            size_t p = w->bit_pos + i;
            w->buf[p / 8] |= (uint8_t)(0x80u >> (p % 8));
        }
    }
    w->bit_pos += nbits;
}

void pc_uper_put_bool(UperWriter *w, proto_bool v)
{
    pc_uper_put_bits(w, v ? 1 : 0, 1);
}

unsigned pc_uper_cint_bits(int64_t lo, int64_t hi)
{
    if (hi <= lo)
    {
        return 0; // single value: encoded in zero bits
    }
    uint64_t range = (uint64_t)(hi - lo); // number of steps above lo is 0..range
    unsigned bits = 0;
    while (((uint64_t)1 << bits) <= range)
    {
        bits++;
    }
    return bits;
}

void pc_uper_put_cint(UperWriter *w, int64_t value, int64_t lo, int64_t hi)
{
    unsigned bits = pc_uper_cint_bits(lo, hi);
    if (bits == 0)
    {
        return;
    }
    uint64_t off = (uint64_t)(value - lo);
    // Values wider than 32 bits are not used by the J2735 fields here, but support up to 32.
    pc_uper_put_bits(w, (uint32_t)off, bits);
}

void pc_uper_reader_init(UperReader *r, const uint8_t *buf, size_t nbits)
{
    r->buf = buf;
    r->nbits = nbits;
    r->bit_pos = 0;
    r->ok = (buf != NULL);
}

uint32_t pc_uper_get_bits(UperReader *r, unsigned nbits)
{
    if (!r->ok || nbits == 0)
    {
        return 0;
    }
    if (r->bit_pos + nbits > r->nbits)
    {
        r->ok = PROTO_FALSE;
        return 0;
    }
    uint32_t v = 0;
    for (unsigned i = 0; i < nbits; i++)
    {
        size_t p = r->bit_pos + i;
        unsigned bit = (r->buf[p / 8] >> (7 - (p % 8))) & 1u;
        v = (v << 1) | bit;
    }
    r->bit_pos += nbits;
    return v;
}

proto_bool pc_uper_get_bool(UperReader *r)
{
    return pc_uper_get_bits(r, 1) != 0;
}

int64_t pc_uper_get_cint(UperReader *r, int64_t lo, int64_t hi)
{
    unsigned bits = pc_uper_cint_bits(lo, hi);
    if (bits == 0)
    {
        return lo;
    }
    uint32_t off = pc_uper_get_bits(r, bits);
    return lo + (int64_t)off;
}

size_t pc_j2735_bsm_core_encode(const J2735BsmCore *c, uint8_t *out, size_t cap)
{
    if (!c || !out)
    {
        return 0;
    }
    UperWriter w;
    pc_uper_writer_init(&w, out, cap);
    pc_uper_put_cint(&w, c->msg_count, 0, 127);
    pc_uper_put_bits(&w, c->id, 32);
    pc_uper_put_cint(&w, c->sec_mark, 0, 65535);
    pc_uper_put_cint(&w, c->lat, -900000000, 900000001);
    pc_uper_put_cint(&w, c->lon, -1799999999, 1800000001);
    pc_uper_put_cint(&w, c->elev, -4096, 61439);
    pc_uper_put_cint(&w, c->speed, 0, 8191);
    pc_uper_put_cint(&w, c->heading, 0, 28800);
    return pc_uper_writer_finish(&w);
}

proto_bool pc_j2735_bsm_core_decode(const uint8_t *in, size_t len, J2735BsmCore *c)
{
    if (!in || !c)
    {
        return PROTO_FALSE;
    }
    UperReader r;
    pc_uper_reader_init(&r, in, len * 8);
    c->msg_count = (uint8_t)pc_uper_get_cint(&r, 0, 127);
    c->id = pc_uper_get_bits(&r, 32);
    c->sec_mark = (uint16_t)pc_uper_get_cint(&r, 0, 65535);
    c->lat = (int32_t)pc_uper_get_cint(&r, -900000000, 900000001);
    c->lon = (int32_t)pc_uper_get_cint(&r, -1799999999, 1800000001);
    c->elev = (int32_t)pc_uper_get_cint(&r, -4096, 61439);
    c->speed = (uint16_t)pc_uper_get_cint(&r, 0, 8191);
    c->heading = (uint16_t)pc_uper_get_cint(&r, 0, 28800);
    return r.ok;
}

size_t pc_j2735_spat_encode(const J2735MovementState *states, size_t count, uint8_t *out, size_t cap)
{
    if (!out || (count && !states) || count > 31)
    {
        return 0;
    }
    UperWriter w;
    pc_uper_writer_init(&w, out, cap);
    pc_uper_put_cint(&w, (int64_t)count, 0, 31);
    for (size_t i = 0; i < count; i++)
    {
        pc_uper_put_cint(&w, states[i].signal_group, 0, 255);
        pc_uper_put_cint(&w, states[i].phase, 0, 9);
        pc_uper_put_cint(&w, states[i].min_end_time, 0, 36000);
        pc_uper_put_cint(&w, states[i].max_end_time, 0, 36000);
    }
    return pc_uper_writer_finish(&w);
}

proto_bool pc_j2735_spat_decode(const uint8_t *in, size_t len, J2735MovementState *out_states, size_t max_states,
                                size_t *out_count)
{
    if (!in || !out_states || !out_count)
    {
        return PROTO_FALSE;
    }
    UperReader r;
    pc_uper_reader_init(&r, in, len * 8);
    size_t count = (size_t)pc_uper_get_cint(&r, 0, 31);
    if (!r.ok || count > max_states)
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i < count; i++)
    {
        out_states[i].signal_group = (uint8_t)pc_uper_get_cint(&r, 0, 255);
        out_states[i].phase = (uint8_t)pc_uper_get_cint(&r, 0, 9);
        out_states[i].min_end_time = (uint16_t)pc_uper_get_cint(&r, 0, 36000);
        out_states[i].max_end_time = (uint16_t)pc_uper_get_cint(&r, 0, 36000);
    }
    if (!r.ok)
    {
        return PROTO_FALSE;
    }
    *out_count = count;
    return PROTO_TRUE;
}

size_t pc_j2735_map_encode(const J2735MapIntersection *isect, const J2735Lane *lanes, size_t count, uint8_t *out,
                           size_t cap)
{
    if (!isect || !out || (count && !lanes) || count > 31)
    {
        return 0;
    }
    UperWriter w;
    pc_uper_writer_init(&w, out, cap);
    pc_uper_put_cint(&w, isect->intersection_id, 0, 65535);
    pc_uper_put_cint(&w, isect->ref_lat, 0, 65535);
    pc_uper_put_cint(&w, isect->ref_lon, 0, 65535);
    pc_uper_put_cint(&w, (int64_t)count, 0, 31);
    for (size_t i = 0; i < count; i++)
    {
        pc_uper_put_cint(&w, lanes[i].lane_id, 0, 255);
        pc_uper_put_bool(&w, lanes[i].is_ingress);
        pc_uper_put_cint(&w, lanes[i].node_x, -2048, 2047);
        pc_uper_put_cint(&w, lanes[i].node_y, -2048, 2047);
    }
    return pc_uper_writer_finish(&w);
}

proto_bool pc_j2735_map_decode(const uint8_t *in, size_t len, J2735MapIntersection *isect, J2735Lane *out_lanes,
                               size_t max_lanes, size_t *out_count)
{
    if (!in || !isect || !out_lanes || !out_count)
    {
        return PROTO_FALSE;
    }
    UperReader r;
    pc_uper_reader_init(&r, in, len * 8);
    isect->intersection_id = (uint16_t)pc_uper_get_cint(&r, 0, 65535);
    isect->ref_lat = (uint16_t)pc_uper_get_cint(&r, 0, 65535);
    isect->ref_lon = (uint16_t)pc_uper_get_cint(&r, 0, 65535);
    size_t count = (size_t)pc_uper_get_cint(&r, 0, 31);
    if (!r.ok || count > max_lanes)
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i < count; i++)
    {
        out_lanes[i].lane_id = (uint8_t)pc_uper_get_cint(&r, 0, 255);
        out_lanes[i].is_ingress = pc_uper_get_bool(&r);
        out_lanes[i].node_x = (int16_t)pc_uper_get_cint(&r, -2048, 2047);
        out_lanes[i].node_y = (int16_t)pc_uper_get_cint(&r, -2048, 2047);
    }
    if (!r.ok)
    {
        return PROTO_FALSE;
    }
    *out_count = count;
    return PROTO_TRUE;
}

#endif // PC_ENABLE_J2735
