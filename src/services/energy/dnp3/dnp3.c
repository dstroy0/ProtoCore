// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dnp3.c
 * @brief DNP3 (IEEE 1815) data-link frame builder + parser (pure, host-tested).
 */

#include "services/energy/dnp3/dnp3.h"
#include "shared_primitives/crc.h"    // PC_CRC16_DNP
#include "shared_primitives/endian.h" // pc_rd16le / pc_rd32le

#if PC_ENABLE_DNP3

#include <string.h>

uint16_t pc_dnp3_crc(const uint8_t *data, size_t len)
{
    // The DNP3 link-layer block check: reflected poly 0xA6BC = reflect(0x3D65), init 0, final XOR 0xFFFF -
    // cataloged as CRC-16/DNP.
    return (uint16_t)pc_crc(&PC_CRC16_DNP, data, len);
}

// Append a CRC over [data, data+n) low octet first.
static size_t put_crc(uint8_t *p, const uint8_t *data, size_t n)
{
    uint16_t crc = pc_dnp3_crc(data, n);
    p[0] = (uint8_t)(crc & 0xFF);
    p[1] = (uint8_t)(crc >> 8);
    return DNP3_CRC_LEN;
}

size_t pc_dnp3_build_frame(uint8_t *buf, size_t cap, uint8_t control, uint16_t dest, uint16_t src,
                           const uint8_t *user_data, size_t user_data_len)
{
    if (!buf || (user_data_len && !user_data) || user_data_len > DNP3_MAX_USER_DATA)
    {
        return 0;
    }
    size_t nblocks = (user_data_len + DNP3_BLOCK_LEN - 1) / DNP3_BLOCK_LEN;
    size_t total = DNP3_HEADER_BLOCK_LEN + user_data_len + nblocks * DNP3_CRC_LEN;
    if (total > cap)
    {
        return 0;
    }

    size_t p = 0;
    buf[p++] = DNP3_START0;
    buf[p++] = DNP3_START1;
    buf[p++] = (uint8_t)(DNP3_LEN_OVERHEAD + user_data_len); // LEN counts CTRL + DEST + SRC + data
    buf[p++] = control;
    buf[p++] = (uint8_t)(dest & 0xFF); // LE
    buf[p++] = (uint8_t)(dest >> 8);
    buf[p++] = (uint8_t)(src & 0xFF);
    buf[p++] = (uint8_t)(src >> 8);
    p += put_crc(buf + p, buf, DNP3_HEADER_LEN); // header CRC over the header octets

    size_t off = 0;
    while (off < user_data_len)
    {
        size_t blk = user_data_len - off;
        if (blk > DNP3_BLOCK_LEN)
        {
            blk = DNP3_BLOCK_LEN;
        }
        memcpy(buf + p, user_data + off, blk);
        put_crc(buf + p + blk, buf + p, blk); // CRC over this block's data
        p += blk + DNP3_CRC_LEN;
        off += blk;
    }
    return total;
}

proto_bool pc_dnp3_parse_frame(const uint8_t *buf, size_t len, Dnp3Frame *out, uint8_t *out_user, size_t out_cap,
                               size_t *out_user_len)
{
    if (!buf || !out || len < DNP3_HEADER_BLOCK_LEN)
    {
        return PROTO_FALSE;
    }
    if (buf[0] != DNP3_START0 || buf[1] != DNP3_START1)
    {
        return PROTO_FALSE;
    }
    uint8_t length = buf[2];
    if (length < DNP3_LEN_OVERHEAD) // LEN must at least cover CTRL + DEST + SRC
    {
        return PROTO_FALSE;
    }
    size_t user_len = (size_t)length - DNP3_LEN_OVERHEAD;
    if (user_len > DNP3_MAX_USER_DATA)
    // forced length into [DNP3_LEN_OVERHEAD, 255] (length is a uint8_t, so
    // 255 is a hard ceiling, not just an untested one), so this unsigned
    // subtraction cannot underflow and user_len = length-DNP3_LEN_OVERHEAD is
    // confined to [0, 250] = [0, DNP3_MAX_USER_DATA]; the true arm has no
    // reachable value of length to take it with
    {
        return PROTO_FALSE;
        // DNP3_MAX_USER_DATA given length's uint8_t domain plus the line-76 lower-bound guard
    }

    uint16_t hcrc = pc_dnp3_crc(buf, DNP3_HEADER_LEN);
    if ((uint16_t)(buf[DNP3_HEADER_LEN] | (buf[DNP3_HEADER_LEN + 1] << 8)) != hcrc)
    {
        return PROTO_FALSE; // header CRC mismatch
    }

    size_t nblocks = (user_len + DNP3_BLOCK_LEN - 1) / DNP3_BLOCK_LEN;
    size_t total = DNP3_HEADER_BLOCK_LEN + user_len + nblocks * DNP3_CRC_LEN;
    if (total > len)
    {
        return PROTO_FALSE; // frame not fully buffered
    }

    if (user_len && (!out_user || user_len > out_cap))
    {
        return PROTO_FALSE;
    }

    size_t p = DNP3_HEADER_BLOCK_LEN;
    size_t off = 0;
    while (off < user_len)
    {
        size_t blk = user_len - off;
        if (blk > DNP3_BLOCK_LEN)
        {
            blk = DNP3_BLOCK_LEN;
        }
        uint16_t bcrc = pc_dnp3_crc(buf + p, blk);
        if ((uint16_t)(buf[p + blk] | (buf[p + blk + 1] << 8)) != bcrc)
        {
            return PROTO_FALSE; // block CRC mismatch
        }
        memcpy(out_user + off, buf + p, blk);
        p += blk + DNP3_CRC_LEN;
        off += blk;
    }

    out->length = length;
    out->control = buf[3];
    out->dest = (uint16_t)(buf[4] | (buf[5] << 8));
    out->src = (uint16_t)(buf[6] | (buf[7] << 8));
    if (out_user_len)
    {
        *out_user_len = user_len;
    }
    return PROTO_TRUE;
}

// --- transport function (IEEE 1815 §8.2) ---

uint8_t pc_dnp3_transport_header(proto_bool fir, proto_bool fin, uint8_t seq)
{
    return (uint8_t)((fin ? DNP3_TR_FIN : 0u) | (fir ? DNP3_TR_FIR : 0u) | (seq & DNP3_TR_SEQ_MASK));
}

size_t pc_dnp3_build_transport_segment(uint8_t *out, size_t cap, proto_bool fir, proto_bool fin, uint8_t seq,
                                       const uint8_t *app_data, size_t app_len)
{
    if (!out || (app_len && !app_data) || app_len > DNP3_TR_MAX_APP)
    {
        return 0;
    }
    if (cap < 1 + app_len)
    {
        return 0;
    }
    out[0] = pc_dnp3_transport_header(fir, fin, seq);
    if (app_len)
    {
        memcpy(out + 1, app_data, app_len);
    }
    return 1 + app_len;
}

void pc_dnp3_transport_rx_init(Dnp3TransportRx *r, uint8_t *buf, size_t cap)
{
    if (!r)
    {
        return;
    }
    r->buf = buf;
    r->cap = cap;
    r->len = 0;
    r->expect_seq = 0;
    r->active = PROTO_FALSE;
    r->done = PROTO_FALSE;
}

int pc_dnp3_transport_feed(Dnp3TransportRx *r, const uint8_t *user, size_t user_len)
{
    if (!r || !r->buf || !user || user_len < 1)
    {
        return DNP3_TR_IGNORED;
    }
    uint8_t hdr = user[0];
    proto_bool fir = (hdr & DNP3_TR_FIR) != 0;
    proto_bool fin = (hdr & DNP3_TR_FIN) != 0;
    uint8_t seq = (uint8_t)(hdr & DNP3_TR_SEQ_MASK);
    const uint8_t *app = user + 1;
    size_t app_len = user_len - 1;

    if (fir) // a first segment starts (or restarts) the fragment
    {
        r->len = 0;
        r->active = PROTO_TRUE;
        r->done = PROTO_FALSE;
        r->expect_seq = seq;
    }
    else
    {
        if (!r->active) // a continuation with no fragment in progress
        {
            return DNP3_TR_IGNORED;
        }
        if (seq != r->expect_seq) // out of sequence: abandon and discard
        {
            r->active = PROTO_FALSE;
            return DNP3_TR_IGNORED;
        }
    }

    if (app_len > r->cap - r->len) // would overflow the fragment buffer
    {
        r->active = PROTO_FALSE;
        return DNP3_TR_ERROR;
    }
    if (app_len)
    {
        memcpy(r->buf + r->len, app, app_len);
    }
    r->len += app_len;
    r->expect_seq = (uint8_t)((r->expect_seq + 1) & DNP3_TR_SEQ_MASK);
    if (fin)
    {
        r->active = PROTO_FALSE;
        r->done = PROTO_TRUE;
        return DNP3_TR_COMPLETE;
    }
    return DNP3_TR_PROGRESS;
}

uint8_t pc_dnp3_app_control(proto_bool fir, proto_bool fin, proto_bool con, proto_bool uns, uint8_t seq)
{
    return (uint8_t)((fir ? DNP3_AC_FIR : 0u) | (fin ? DNP3_AC_FIN : 0u) | (con ? DNP3_AC_CON : 0u) |
                     (uns ? DNP3_AC_UNS : 0u) | (seq & DNP3_AC_SEQ_MASK));
}

size_t pc_dnp3_build_app_request(uint8_t *out, size_t cap, uint8_t app_control, uint8_t fc, const uint8_t *objects,
                                 size_t obj_len)
{
    if (!out || (obj_len && !objects) || cap < 2 + obj_len)
    {
        return 0;
    }
    out[0] = app_control;
    out[1] = fc;
    if (obj_len)
    {
        memcpy(out + 2, objects, obj_len);
    }
    return 2 + obj_len;
}

size_t pc_dnp3_build_app_response(uint8_t *out, size_t cap, uint8_t app_control, uint8_t fc, uint16_t iin,
                                  const uint8_t *objects, size_t obj_len)
{
    if (!out || (obj_len && !objects) || cap < 4 + obj_len)
    {
        return 0;
    }
    out[0] = app_control;
    out[1] = fc;
    out[2] = (uint8_t)iin;        // IIN1, little-endian
    out[3] = (uint8_t)(iin >> 8); // IIN2
    if (obj_len)
    {
        memcpy(out + 4, objects, obj_len);
    }
    return 4 + obj_len;
}

proto_bool pc_dnp3_parse_app_header(const uint8_t *frag, size_t len, Dnp3AppHeader *out)
{
    if (!frag || !out || len < 2)
    {
        return PROTO_FALSE;
    }
    uint8_t ac = frag[0];
    uint8_t fc = frag[1];
    proto_bool is_response = (fc == DNP3_FC_RESPONSE || fc == DNP3_FC_UNSOLICITED_RESPONSE);
    size_t hdr_len = is_response ? 4u : 2u;
    if (len < hdr_len) // a response needs the two IIN octets
    {
        return PROTO_FALSE;
    }
    out->app_control = ac;
    out->fir = (ac & DNP3_AC_FIR) != 0;
    out->fin = (ac & DNP3_AC_FIN) != 0;
    out->con = (ac & DNP3_AC_CON) != 0;
    out->uns = (ac & DNP3_AC_UNS) != 0;
    out->seq = (uint8_t)(ac & DNP3_AC_SEQ_MASK);
    out->fc = fc;
    out->is_response = is_response;
    out->iin = is_response ? (uint16_t)(frag[2] | (frag[3] << 8)) : 0u;
    out->obj_len = len - hdr_len;
    out->objects = out->obj_len ? frag + hdr_len : NULL;
    return PROTO_TRUE;
}

size_t pc_dnp3_build_object_header_range(uint8_t *buf, size_t cap, uint8_t group, uint8_t variation, uint32_t start,
                                         uint32_t stop)
{
    if (!buf || stop < start)
    {
        return 0;
    }
    uint8_t range_code = 0;
    size_t range_len = 0;
    if (stop <= 0xFFu) // stop is the larger index, so it decides the width
    {
        range_code = DNP3_RANGE_START_STOP_1;
        range_len = 2;
    }
    else if (stop <= 0xFFFFu)
    {
        range_code = DNP3_RANGE_START_STOP_2;
        range_len = 4;
    }
    else
    {
        range_code = DNP3_RANGE_START_STOP_4;
        range_len = 8;
    }
    size_t total = 3 + range_len;
    if (cap < total)
    {
        return 0;
    }
    buf[0] = group;
    buf[1] = variation;
    buf[2] = range_code; // prefix code 0 (no per-object index prefix) | range specifier
    if (range_code == DNP3_RANGE_START_STOP_1)
    {
        buf[3] = (uint8_t)start;
        buf[4] = (uint8_t)stop;
    }
    else if (range_code == DNP3_RANGE_START_STOP_2)
    {
        pc_wr16le(buf + 3, (uint16_t)start);
        pc_wr16le(buf + 5, (uint16_t)stop);
    }
    else
    {
        pc_wr32le(buf + 3, start);
        pc_wr32le(buf + 7, stop);
    }
    return total;
}

size_t pc_dnp3_build_object_header_all(uint8_t *buf, size_t cap, uint8_t group, uint8_t variation)
{
    if (!buf || cap < 3)
    {
        return 0;
    }
    buf[0] = group;
    buf[1] = variation;
    buf[2] = DNP3_RANGE_NO_RANGE; // qualifier 0x06: all objects, no range field
    return 3;
}

size_t pc_dnp3_build_crob(uint8_t *buf, size_t cap, uint8_t op_type, uint8_t tcc, proto_bool clear, uint8_t count,
                          uint32_t on_time_ms, uint32_t off_time_ms)
{
    if (!buf || op_type > 0x0Fu || tcc > 0x03u || cap < DNP3_CROB_LEN)
    {
        return 0;
    }
    // Control code: op-type (bits 0-3) | clear (bit 5) | trip-close (bits 6-7). The queue bit (0x10) is obsolete.
    buf[0] = (uint8_t)((op_type & 0x0Fu) | (clear ? 0x20u : 0x00u) | (uint8_t)((tcc & 0x03u) << 6));
    buf[1] = count;
    pc_wr32le(buf + 2, on_time_ms);
    pc_wr32le(buf + 6, off_time_ms);
    buf[10] = 0x00; // status: 0 in a request (the outstation reports the result in its response)
    return DNP3_CROB_LEN;
}

size_t pc_dnp3_build_aob32(uint8_t *buf, size_t cap, int32_t value)
{
    if (!buf || cap < DNP3_AOB_LEN)
    {
        return 0;
    }
    pc_wr32le(buf, (uint32_t)value); // 32-bit signed setpoint, little-endian (two's complement)
    buf[4] = 0x00;                   // control status: 0 in a request (the outstation reports the result)
    return DNP3_AOB_LEN;
}

size_t pc_dnp3_build_aob_float(uint8_t *buf, size_t cap, float value)
{
    if (!buf || cap < DNP3_AOB_LEN)
    {
        return 0;
    }
    uint32_t bits;
    memcpy(&bits, &value, 4); // the IEEE-754 bit pattern, written little-endian (endian-safe)
    pc_wr32le(buf, bits);
    buf[4] = 0x00; // control status: 0 in a request
    return DNP3_AOB_LEN;
}

proto_bool pc_dnp3_parse_object_header(const uint8_t *buf, size_t len, Dnp3ObjectHeader *out)
{
    if (!buf || !out || len < 3) // group + variation + qualifier
    {
        return PROTO_FALSE;
    }
    uint8_t range_code = (uint8_t)(buf[2] & DNP3_QUAL_RANGE_MASK);
    size_t p = 3;
    uint32_t start = 0;
    uint32_t stop = 0;
    uint32_t count = 0;
    proto_bool is_count = PROTO_FALSE;
    switch (range_code)
    {
    case DNP3_RANGE_START_STOP_1:
        if (len < p + 2)
        {
            return PROTO_FALSE;
        }
        start = buf[p];
        stop = buf[p + 1];
        p += 2;
        count = stop - start + 1;
        break;
    case DNP3_RANGE_START_STOP_2:
        if (len < p + 4)
        {
            return PROTO_FALSE;
        }
        start = pc_rd16le(buf + p);
        stop = pc_rd16le(buf + p + 2);
        p += 4;
        count = stop - start + 1;
        break;
    case DNP3_RANGE_START_STOP_4:
        if (len < p + 8)
        {
            return PROTO_FALSE;
        }
        start = pc_rd32le(buf + p);
        stop = pc_rd32le(buf + p + 4);
        p += 8;
        count = stop - start + 1;
        break;
    case DNP3_RANGE_NO_RANGE:
        break; // all objects; no range field follows
    case DNP3_RANGE_COUNT_1:
        if (len < p + 1)
        {
            return PROTO_FALSE;
        }
        count = buf[p];
        p += 1;
        is_count = PROTO_TRUE;
        break;
    case DNP3_RANGE_COUNT_2:
        if (len < p + 2)
        {
            return PROTO_FALSE;
        }
        count = pc_rd16le(buf + p);
        p += 2;
        is_count = PROTO_TRUE;
        break;
    case DNP3_RANGE_COUNT_4:
        if (len < p + 4)
        {
            return PROTO_FALSE;
        }
        count = pc_rd32le(buf + p);
        p += 4;
        is_count = PROTO_TRUE;
        break;
    default:
        return PROTO_FALSE; // an unsupported qualifier range form
    }
    out->group = buf[0];
    out->variation = buf[1];
    out->qualifier = buf[2];
    out->prefix_code = (uint8_t)((buf[2] & DNP3_QUAL_PREFIX_MASK) >> DNP3_QUAL_PREFIX_SHIFT);
    out->range_code = range_code;
    out->is_count = is_count;
    out->start = start;
    out->stop = stop;
    out->count = count;
    out->objects = (p < len) ? buf + p : NULL;
    out->objects_len = len - p;
    return PROTO_TRUE;
}

#endif // PC_ENABLE_DNP3
