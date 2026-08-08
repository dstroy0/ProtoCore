// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ubx.c
 * @brief u-blox UBX binary protocol codec (pure, host-tested).
 */

#include "services/timing_position/ubx/ubx.h"

#if PC_ENABLE_UBX

void pc_ubx_checksum(const uint8_t *body, size_t len, uint8_t *ck_a, uint8_t *ck_b)
{
    uint8_t a = 0, b = 0;
    for (size_t i = 0; i < len; i++)
    {
        a = (uint8_t)(a + body[i]);
        b = (uint8_t)(b + a);
    }
    if (ck_a)
    {
        *ck_a = a;
    }
    if (ck_b)
    {
        *ck_b = b;
    }
}

size_t pc_ubx_build(uint8_t *buf, size_t cap, uint8_t cls, uint8_t id, const uint8_t *payload, uint16_t len)
{
    if (!buf || (len && !payload))
    {
        return 0;
    }
    size_t total = 8u + (size_t)len; // 2 sync + cls + id + 2 len + payload + 2 checksum
    if (cap < total)
    {
        return 0;
    }
    size_t p = 0;
    buf[p++] = PC_UBX_SYNC1;
    buf[p++] = PC_UBX_SYNC2;
    buf[p++] = cls;
    buf[p++] = id;
    buf[p++] = (uint8_t)(len & 0xFFu);
    buf[p++] = (uint8_t)(len >> 8);
    for (uint16_t i = 0; i < len; i++)
    {
        buf[p++] = payload[i];
    }
    uint8_t a = 0, b = 0;
    pc_ubx_checksum(buf + 2, (size_t)len + 4u, &a, &b); // class..payload end
    buf[p++] = a;
    buf[p++] = b;
    return p;
}

size_t pc_ubx_build_poll(uint8_t *buf, size_t cap, uint8_t cls, uint8_t id)
{
    return pc_ubx_build(buf, cap, cls, id, NULL, 0);
}

proto_bool pc_ubx_parse(const uint8_t *s, size_t len, pc_ubx *out)
{
    if (!s || !out || len < 8)
    {
        return PROTO_FALSE;
    }
    if (s[0] != PC_UBX_SYNC1 || s[1] != PC_UBX_SYNC2)
    {
        return PROTO_FALSE;
    }
    uint16_t plen = (uint16_t)(s[4] | ((uint16_t)s[5] << 8));
    if (len < 8u + (size_t)plen)
    {
        return PROTO_FALSE;
    }
    uint8_t a = 0, b = 0;
    pc_ubx_checksum(s + 2, (size_t)plen + 4u, &a, &b);
    if (a != s[6 + plen] || b != s[7 + plen])
    {
        return PROTO_FALSE;
    }
    out->cls = s[2];
    out->id = s[3];
    out->len = plen;
    out->payload = s + 6;
    return PROTO_TRUE;
}

int pc_ubx_ack(const pc_ubx *m, uint8_t *acked_cls, uint8_t *acked_id)
{
    if (!m || m->cls != 0x05u || m->len < 2 || !m->payload)
    {
        return -1;
    }
    if (m->id != 0x00u && m->id != 0x01u)
    {
        return -1;
    }
    if (acked_cls)
    {
        *acked_cls = m->payload[0];
    }
    if (acked_id)
    {
        *acked_id = m->payload[1];
    }
    return m->id == 0x01u ? 1 : 0; // ACK-ACK id 0x01, ACK-NAK id 0x00
}

uint16_t pc_ubx_u16(const uint8_t *p, size_t off)
{
    return (uint16_t)(p[off] | ((uint16_t)p[off + 1] << 8));
}

uint32_t pc_ubx_u32(const uint8_t *p, size_t off)
{
    return (uint32_t)p[off] | ((uint32_t)p[off + 1] << 8) | ((uint32_t)p[off + 2] << 16) | ((uint32_t)p[off + 3] << 24);
}

int16_t pc_ubx_i16(const uint8_t *p, size_t off)
{
    return (int16_t)pc_ubx_u16(p, off);
}

int32_t pc_ubx_i32(const uint8_t *p, size_t off)
{
    return (int32_t)pc_ubx_u32(p, off);
}

proto_bool pc_ubx_parse_nav_pvt(const pc_ubx *m, pc_ubx_nav_pvt *out)
{
    if (!m || !out || !m->payload)
    {
        return PROTO_FALSE;
    }
    if (m->cls != PC_UBX_CLASS_NAV || m->id != PC_UBX_NAV_PVT || m->len < PC_UBX_NAV_PVT_LEN)
    {
        return PROTO_FALSE;
    }
    const uint8_t *p = m->payload;
    out->itow_ms = pc_ubx_u32(p, 0);
    out->year = pc_ubx_u16(p, 4);
    out->month = p[6];
    out->day = p[7];
    out->hour = p[8];
    out->minute = p[9];
    out->second = p[10];
    out->valid = p[11];
    out->time_acc_ns = pc_ubx_u32(p, 12);
    out->nano = pc_ubx_i32(p, 16);
    out->fix_type = p[20];
    out->flags = p[21];
    out->num_sv = p[23];
    out->lon_1e7 = pc_ubx_i32(p, 24);
    out->lat_1e7 = pc_ubx_i32(p, 28);
    out->height_mm = pc_ubx_i32(p, 32);
    out->hmsl_mm = pc_ubx_i32(p, 36);
    out->h_acc_mm = pc_ubx_u32(p, 40);
    out->v_acc_mm = pc_ubx_u32(p, 44);
    out->vel_n_mm_s = pc_ubx_i32(p, 48);
    out->vel_e_mm_s = pc_ubx_i32(p, 52);
    out->vel_d_mm_s = pc_ubx_i32(p, 56);
    out->gspeed_mm_s = pc_ubx_i32(p, 60);
    out->head_mot_1e5 = pc_ubx_i32(p, 64);
    out->s_acc_mm_s = pc_ubx_u32(p, 68);
    out->head_acc_1e5 = pc_ubx_u32(p, 72);
    out->pdop_1e2 = pc_ubx_u16(p, 76);
    return PROTO_TRUE;
}

// True iff m is a NAV-SAT frame whose declared length holds the fixed header + numSvs blocks.
static proto_bool ubx_nav_sat_ok(const pc_ubx *m, uint8_t *num_svs_out)
{
    if (!m || !m->payload || m->cls != PC_UBX_CLASS_NAV || m->id != PC_UBX_NAV_SAT)
    {
        return PROTO_FALSE;
    }
    if (m->len < PC_UBX_NAV_SAT_HDR_LEN)
    {
        return PROTO_FALSE;
    }
    uint8_t num = m->payload[5]; // numSvs
    if ((size_t)m->len < (size_t)PC_UBX_NAV_SAT_HDR_LEN + (size_t)num * PC_UBX_NAV_SAT_ENTRY_LEN)
    {
        return PROTO_FALSE;
    }
    if (num_svs_out)
    {
        *num_svs_out = num;
    }
    return PROTO_TRUE;
}

proto_bool pc_ubx_parse_nav_sat(const pc_ubx *m, pc_ubx_nav_sat_hdr *out)
{
    uint8_t num = 0;
    if (!out || !ubx_nav_sat_ok(m, &num))
    {
        return PROTO_FALSE;
    }
    out->itow_ms = pc_ubx_u32(m->payload, 0);
    out->version = m->payload[4];
    out->num_svs = num;
    return PROTO_TRUE;
}

proto_bool pc_ubx_nav_sat_get(const pc_ubx *m, uint8_t index, pc_ubx_sat *out)
{
    uint8_t num = 0;
    if (!out || !ubx_nav_sat_ok(m, &num) || index >= num)
    {
        return PROTO_FALSE;
    }
    const uint8_t *p = m->payload + PC_UBX_NAV_SAT_HDR_LEN + (size_t)index * PC_UBX_NAV_SAT_ENTRY_LEN;
    out->gnss_id = p[0];
    out->sv_id = p[1];
    out->cno_dbhz = p[2];
    out->elev_deg = (int8_t)p[3];
    out->azim_deg = pc_ubx_i16(p, 4);
    out->pr_res_01m = pc_ubx_i16(p, 6);
    out->flags = pc_ubx_u32(p, 8);
    return PROTO_TRUE;
}

proto_bool pc_ubx_parse_nav_timeutc(const pc_ubx *m, pc_ubx_nav_time_utc *out)
{
    if (!m || !out || !m->payload)
    {
        return PROTO_FALSE;
    }
    if (m->cls != PC_UBX_CLASS_NAV || m->id != PC_UBX_NAV_TIMEUTC || m->len < PC_UBX_NAV_TIMEUTC_LEN)
    {
        return PROTO_FALSE;
    }
    const uint8_t *p = m->payload;
    out->itow_ms = pc_ubx_u32(p, 0);
    out->time_acc_ns = pc_ubx_u32(p, 4);
    out->nano = pc_ubx_i32(p, 8);
    out->year = pc_ubx_u16(p, 12);
    out->month = p[14];
    out->day = p[15];
    out->hour = p[16];
    out->minute = p[17];
    out->second = p[18];
    out->valid = p[19];
    out->utc_valid = (p[19] & PC_UBX_TIMEUTC_VALID_UTC) != 0;
    return PROTO_TRUE;
}

size_t pc_ubx_build_cfg_msg(uint8_t *buf, size_t cap, uint8_t cls, uint8_t id, uint8_t rate)
{
    const uint8_t pl[3] = {cls, id, rate}; // msgClass, msgID, rate (on the current port)
    return pc_ubx_build(buf, cap, PC_UBX_CLASS_CFG, PC_UBX_CFG_MSG, pl, sizeof(pl));
}

size_t pc_ubx_build_cfg_rate(uint8_t *buf, size_t cap, uint16_t meas_rate_ms, uint16_t nav_rate, uint16_t time_ref)
{
    // measRate(U2) | navRate(U2) | timeRef(U2), all little-endian.
    const uint8_t pl[6] = {(uint8_t)meas_rate_ms, (uint8_t)(meas_rate_ms >> 8),
                           (uint8_t)nav_rate,     (uint8_t)(nav_rate >> 8),
                           (uint8_t)time_ref,     (uint8_t)(time_ref >> 8)};
    return pc_ubx_build(buf, cap, PC_UBX_CLASS_CFG, PC_UBX_CFG_RATE, pl, sizeof(pl));
}

// -- streaming demultiplexer --

enum
{
    S_SYNC1 = 0,
    S_SYNC2,
    S_CLASS,
    S_ID,
    S_LEN_LO,
    S_LEN_HI,
    S_PAYLOAD,
    S_CK_A,
    S_CK_B,
    S_SKIP
};

void pc_ubx_stream_init(pc_ubx_stream *st)
{
    if (st)
    {
        memset(st, 0, sizeof(*st)); // state = S_SYNC1
    }
}

static void ck_add(pc_ubx_stream *st, uint8_t b)
{
    st->ck_a = (uint8_t)(st->ck_a + b);
    st->ck_b = (uint8_t)(st->ck_b + st->ck_a);
}

// The sync-hunt phase (S_SYNC1 / S_SYNC2): find the 0xB5 0x62 frame start, passing non-frame bytes
// through untouched. Returns the stream-feed result and advances st->state on a match.
static int ubx_feed_sync(pc_ubx_stream *st, uint8_t b, uint8_t *passthrough)
{
    if (st->state == S_SYNC1)
    {
        if (b == PC_UBX_SYNC1)
        {
            st->state = S_SYNC2;
            return PC_UBX_NONE;
        }
        if (passthrough)
        {
            *passthrough = b;
        }
        return PC_UBX_PASSTHROUGH;
    }
    // S_SYNC2
    if (b == PC_UBX_SYNC2)
    {
        st->state = S_CLASS;
        return PC_UBX_NONE;
    }
    st->state = S_SYNC1;
    if (b == PC_UBX_SYNC1) // a fresh sync1; the previous one was a false start
    {
        st->state = S_SYNC2;
        return PC_UBX_NONE;
    }
    if (passthrough)
    {
        *passthrough = b;
    }
    return PC_UBX_PASSTHROUGH;
}

int pc_ubx_stream_feed(pc_ubx_stream *st, uint8_t b, pc_ubx *out, uint8_t *passthrough)
{
    if (!st)
    {
        return PC_UBX_NONE;
    }
    if (st->state == S_SYNC1 || st->state == S_SYNC2)
    {
        return ubx_feed_sync(st, b, passthrough);
    }
    switch (st->state)
    {
    case S_CLASS:
        st->cls = b;
        st->ck_a = b; // Fletcher seed: first byte of the checksummed span
        st->ck_b = b;
        st->state = S_ID;
        return PC_UBX_NONE;
    case S_ID:
        st->id = b;
        ck_add(st, b);
        st->state = S_LEN_LO;
        return PC_UBX_NONE;
    case S_LEN_LO:
        st->len = b;
        ck_add(st, b);
        st->state = S_LEN_HI;
        return PC_UBX_NONE;
    case S_LEN_HI:
        st->len = (uint16_t)(st->len | ((uint16_t)b << 8));
        ck_add(st, b);
        st->idx = 0;
        if (st->len > PC_UBX_MAX_PAYLOAD)
        {
            st->skip = (uint32_t)st->len + 2u; // payload + 2 checksum octets to discard
            st->state = S_SKIP;
            return PC_UBX_NONE;
        }
        st->state = (st->len == 0) ? S_CK_A : S_PAYLOAD;
        return PC_UBX_NONE;
    case S_PAYLOAD:
        st->buf[st->idx++] = b;
        ck_add(st, b);
        if (st->idx >= st->len)
        {
            st->state = S_CK_A;
        }
        return PC_UBX_NONE;
    case S_CK_A:
        st->rx_ck_a = b;
        st->state = S_CK_B;
        return PC_UBX_NONE;
    case S_CK_B:
        st->state = S_SYNC1;
        if (st->rx_ck_a == st->ck_a && b == st->ck_b)
        {
            if (out)
            {
                out->cls = st->cls;
                out->id = st->id;
                out->len = st->len;
                out->payload = st->buf;
            }
            return PC_UBX_FRAME;
        }
        return PC_UBX_NONE; // bad checksum: discard, resume hunting for sync
    case S_SKIP:
        st->skip--;
        if (st->skip == 0)
        {
            st->state = S_SYNC1;
            return PC_UBX_OVERFLOW;
        }
        return PC_UBX_NONE;
    default:
        st->state = S_SYNC1; //
        return PC_UBX_NONE;  //
    }
}

#endif // PC_ENABLE_UBX
