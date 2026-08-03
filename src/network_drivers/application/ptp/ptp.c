// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ptp.c
 * @brief PTP / IEEE 1588-2008 (PTPv2) message codec + slave math (pure, host-tested).
 */

#include "network_drivers/application/ptp/ptp.h"

#if PC_ENABLE_PTP

#include <string.h>

// -- big-endian field helpers --

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void put_u64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
    {
        p[i] = (uint8_t)(v >> (8 * (7 - i)));
    }
}

static uint64_t get_u64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
    {
        v = (v << 8) | p[i];
    }
    return v;
}

// -- timestamp --

void pc_ptp_ts_write(uint8_t *p, const pc_ptp_timestamp *ts)
{
    uint64_t s = ts->seconds;
    p[0] = (uint8_t)(s >> 40);
    p[1] = (uint8_t)(s >> 32);
    p[2] = (uint8_t)(s >> 24);
    p[3] = (uint8_t)(s >> 16);
    p[4] = (uint8_t)(s >> 8);
    p[5] = (uint8_t)s;
    put_u32(p + 6, ts->nanoseconds);
}

void pc_ptp_ts_read(const uint8_t *p, pc_ptp_timestamp *ts)
{
    ts->seconds = ((uint64_t)p[0] << 40) | ((uint64_t)p[1] << 32) | ((uint64_t)p[2] << 24) | ((uint64_t)p[3] << 16) |
                  ((uint64_t)p[4] << 8) | (uint64_t)p[5];
    ts->nanoseconds = get_u32(p + 6);
}

int64_t pc_ptp_ts_to_ns(const pc_ptp_timestamp *ts)
{
    return (int64_t)ts->seconds * 1000000000LL + (int64_t)ts->nanoseconds;
}

void pc_ptp_ts_from_ns(int64_t ns, pc_ptp_timestamp *ts)
{
    if (ns < 0)
    {
        ns = 0; // on-wire timestamps are unsigned
    }
    ts->seconds = (uint64_t)(ns / 1000000000LL);
    ts->nanoseconds = (uint32_t)(ns % 1000000000LL);
}

// -- header --

size_t pc_ptp_build_header(uint8_t *buf, size_t cap, const pc_ptp_header *h, uint16_t body_len)
{
    if (!buf || !h || cap < PC_PTP_HEADER_LEN)
    {
        return 0;
    }
    memset(buf, 0, PC_PTP_HEADER_LEN);
    buf[0] = (uint8_t)((h->transport_specific << 4) | (h->message_type & 0x0F));
    buf[1] = (uint8_t)(h->version & 0x0F);
    put_u16(buf + 2, (uint16_t)(PC_PTP_HEADER_LEN + body_len));
    buf[4] = h->domain;
    put_u16(buf + 6, h->flags);
    put_u64(buf + 8, (uint64_t)h->correction);
    memcpy(buf + 20, h->clock_identity, 8);
    put_u16(buf + 28, h->port_number);
    put_u16(buf + 30, h->sequence_id);
    buf[32] = h->control;
    buf[33] = (uint8_t)h->log_interval;
    return PC_PTP_HEADER_LEN;
}

proto_bool pc_ptp_parse_header(const uint8_t *s, size_t len, pc_ptp_header *h)
{
    if (!s || !h || len < PC_PTP_HEADER_LEN)
    {
        return PROTO_FALSE;
    }
    h->message_type = (uint8_t)(s[0] & 0x0F);
    h->transport_specific = (uint8_t)(s[0] >> 4);
    h->version = (uint8_t)(s[1] & 0x0F);
    h->message_length = get_u16(s + 2);
    h->domain = s[4];
    h->flags = get_u16(s + 6);
    h->correction = (int64_t)get_u64(s + 8);
    memcpy(h->clock_identity, s + 20, 8);
    h->port_number = get_u16(s + 28);
    h->sequence_id = get_u16(s + 30);
    h->control = s[32];
    h->log_interval = (int8_t)s[33];
    return PROTO_TRUE;
}

// -- messages --

static size_t build_ts_msg(uint8_t *buf, size_t cap, const pc_ptp_header *h, const pc_ptp_timestamp *ts, uint8_t mtype,
                           uint8_t control)
{
    if (!buf || !h || !ts || cap < PC_PTP_HEADER_LEN + PC_PTP_TS_LEN)
    {
        return 0;
    }
    pc_ptp_header hh = *h;
    hh.message_type = mtype;
    hh.control = control;
    if (hh.version == 0)
    {
        hh.version = 2;
    }
    pc_ptp_build_header(buf, cap, &hh, PC_PTP_TS_LEN);
    pc_ptp_ts_write(buf + PC_PTP_HEADER_LEN, ts);
    return PC_PTP_HEADER_LEN + PC_PTP_TS_LEN;
}

size_t pc_ptp_build_sync(uint8_t *buf, size_t cap, const pc_ptp_header *h, const pc_ptp_timestamp *origin)
{
    return build_ts_msg(buf, cap, h, origin, PC_PTP_SYNC, 0x00);
}

size_t pc_ptp_build_delay_req(uint8_t *buf, size_t cap, const pc_ptp_header *h, const pc_ptp_timestamp *origin)
{
    return build_ts_msg(buf, cap, h, origin, PC_PTP_DELAY_REQ, 0x01);
}

size_t pc_ptp_build_follow_up(uint8_t *buf, size_t cap, const pc_ptp_header *h, const pc_ptp_timestamp *precise)
{
    return build_ts_msg(buf, cap, h, precise, PC_PTP_FOLLOW_UP, 0x02);
}

size_t pc_ptp_build_delay_resp(uint8_t *buf, size_t cap, const pc_ptp_header *h, const pc_ptp_timestamp *recv,
                               const uint8_t *req_clock_id, uint16_t req_port)
{
    const size_t body = PC_PTP_TS_LEN + 10; // receiveTimestamp + requestingPortIdentity(10)
    if (!buf || !h || !recv || !req_clock_id || cap < PC_PTP_HEADER_LEN + body)
    {
        return 0;
    }
    pc_ptp_header hh = *h;
    hh.message_type = PC_PTP_DELAY_RESP;
    hh.control = 0x03;
    if (hh.version == 0)
    {
        hh.version = 2;
    }
    pc_ptp_build_header(buf, cap, &hh, (uint16_t)body);
    uint8_t *p = buf + PC_PTP_HEADER_LEN;
    pc_ptp_ts_write(p, recv);
    p += PC_PTP_TS_LEN;
    memcpy(p, req_clock_id, 8);
    p += 8;
    put_u16(p, req_port);
    return PC_PTP_HEADER_LEN + body;
}

size_t pc_ptp_build_pdelay_req(uint8_t *buf, size_t cap, const pc_ptp_header *h, const pc_ptp_timestamp *origin)
{
    const size_t body = PC_PTP_TS_LEN + 10; // originTimestamp + 10-octet reserved (pads to Pdelay_Resp length)
    if (!buf || !h || !origin || cap < PC_PTP_HEADER_LEN + body)
    {
        return 0;
    }
    pc_ptp_header hh = *h;
    hh.message_type = PC_PTP_PDELAY_REQ;
    hh.control = 0x05; // "all others" (IEEE 1588-2008 Table 23)
    if (hh.version == 0)
    {
        hh.version = 2;
    }
    pc_ptp_build_header(buf, cap, &hh, (uint16_t)body);
    uint8_t *p = buf + PC_PTP_HEADER_LEN;
    pc_ptp_ts_write(p, origin);
    memset(p + PC_PTP_TS_LEN, 0, 10); // reserved
    return PC_PTP_HEADER_LEN + body;
}

// Pdelay_Resp and Pdelay_Resp_Follow_Up share a body layout (a timestamp + the requesting port identity);
// only the messageType and which timestamp it carries differ.
static size_t build_pdelay_resp_msg(uint8_t *buf, size_t cap, const pc_ptp_header *h, const pc_ptp_timestamp *ts,
                                    const uint8_t *req_clock_id, uint16_t req_port, uint8_t mtype)
{
    const size_t body = PC_PTP_TS_LEN + 10; // timestamp + requestingPortIdentity(10)
    if (!buf || !h || !ts || !req_clock_id || cap < PC_PTP_HEADER_LEN + body)
    {
        return 0;
    }
    pc_ptp_header hh = *h;
    hh.message_type = mtype;
    hh.control = 0x05;
    if (hh.version == 0)
    {
        hh.version = 2;
    }
    pc_ptp_build_header(buf, cap, &hh, (uint16_t)body);
    uint8_t *p = buf + PC_PTP_HEADER_LEN;
    pc_ptp_ts_write(p, ts);
    p += PC_PTP_TS_LEN;
    memcpy(p, req_clock_id, 8);
    p += 8;
    put_u16(p, req_port);
    return PC_PTP_HEADER_LEN + body;
}

size_t pc_ptp_build_pdelay_resp(uint8_t *buf, size_t cap, const pc_ptp_header *h, const pc_ptp_timestamp *recv,
                                const uint8_t *req_clock_id, uint16_t req_port)
{
    return build_pdelay_resp_msg(buf, cap, h, recv, req_clock_id, req_port, PC_PTP_PDELAY_RESP);
}

size_t pc_ptp_build_pdelay_resp_follow_up(uint8_t *buf, size_t cap, const pc_ptp_header *h,
                                          const pc_ptp_timestamp *origin, const uint8_t *req_clock_id,
                                          uint16_t req_port)
{
    return build_pdelay_resp_msg(buf, cap, h, origin, req_clock_id, req_port, PC_PTP_PDELAY_RESP_FOLLOW_UP);
}

size_t pc_ptp_build_announce(uint8_t *buf, size_t cap, const pc_ptp_header *h, const pc_ptp_announce *a)
{
    const size_t body = 30; // originTimestamp(10)+utc(2)+rsv(1)+p1(1)+quality(4)+p2(1)+id(8)+steps(2)+src(1)
    if (!buf || !h || !a || cap < PC_PTP_HEADER_LEN + body)
    {
        return 0;
    }
    pc_ptp_header hh = *h;
    hh.message_type = PC_PTP_ANNOUNCE;
    hh.control = 0x05;
    if (hh.version == 0)
    {
        hh.version = 2;
    }
    pc_ptp_build_header(buf, cap, &hh, (uint16_t)body);
    uint8_t *p = buf + PC_PTP_HEADER_LEN;
    pc_ptp_ts_write(p, &a->origin);
    p += PC_PTP_TS_LEN;
    put_u16(p, (uint16_t)a->utc_offset);
    p += 2;
    *p++ = 0; // reserved
    *p++ = a->gm_priority1;
    *p++ = a->gm_clock_class;
    *p++ = a->gm_clock_accuracy;
    put_u16(p, a->gm_variance);
    p += 2;
    *p++ = a->gm_priority2;
    memcpy(p, a->gm_identity, 8);
    p += 8;
    put_u16(p, a->steps_removed);
    p += 2;
    *p = a->time_source;
    return PC_PTP_HEADER_LEN + body;
}

proto_bool pc_ptp_parse_timestamp_msg(const uint8_t *s, size_t len, pc_ptp_header *h, pc_ptp_timestamp *ts)
{
    if (!ts || !pc_ptp_parse_header(s, len, h))
    {
        return PROTO_FALSE;
    }
    if (len < PC_PTP_HEADER_LEN + PC_PTP_TS_LEN)
    {
        return PROTO_FALSE;
    }
    if (h->message_type != PC_PTP_SYNC && h->message_type != PC_PTP_DELAY_REQ && h->message_type != PC_PTP_FOLLOW_UP)
    {
        return PROTO_FALSE;
    }
    pc_ptp_ts_read(s + PC_PTP_HEADER_LEN, ts);
    return PROTO_TRUE;
}

proto_bool pc_ptp_parse_delay_resp(const uint8_t *s, size_t len, pc_ptp_header *h, pc_ptp_delay_resp *out)
{
    if (!out || !pc_ptp_parse_header(s, len, h))
    {
        return PROTO_FALSE;
    }
    if (h->message_type != PC_PTP_DELAY_RESP)
    {
        return PROTO_FALSE;
    }
    if (len < PC_PTP_HEADER_LEN + PC_PTP_TS_LEN + 10)
    {
        return PROTO_FALSE;
    }
    const uint8_t *p = s + PC_PTP_HEADER_LEN;
    pc_ptp_ts_read(p, &out->receive);
    p += PC_PTP_TS_LEN;
    memcpy(out->req_clock_id, p, 8);
    p += 8;
    out->req_port = get_u16(p);
    return PROTO_TRUE;
}

proto_bool pc_ptp_parse_pdelay_req(const uint8_t *s, size_t len, pc_ptp_header *h, pc_ptp_timestamp *ts)
{
    if (!ts || !pc_ptp_parse_header(s, len, h))
    {
        return PROTO_FALSE;
    }
    if (h->message_type != PC_PTP_PDELAY_REQ)
    {
        return PROTO_FALSE;
    }
    if (len < PC_PTP_HEADER_LEN + PC_PTP_TS_LEN) // the originTimestamp (the reserved tail is ignored)
    {
        return PROTO_FALSE;
    }
    pc_ptp_ts_read(s + PC_PTP_HEADER_LEN, ts);
    return PROTO_TRUE;
}

// Pdelay_Resp and its Follow_Up share the body layout; only the messageType differs.
static proto_bool parse_pdelay_resp_msg(const uint8_t *s, size_t len, pc_ptp_header *h, pc_ptp_pdelay_resp *out,
                                        uint8_t mtype)
{
    if (!out || !pc_ptp_parse_header(s, len, h))
    {
        return PROTO_FALSE;
    }
    if (h->message_type != mtype)
    {
        return PROTO_FALSE;
    }
    if (len < PC_PTP_HEADER_LEN + PC_PTP_TS_LEN + 10)
    {
        return PROTO_FALSE;
    }
    const uint8_t *p = s + PC_PTP_HEADER_LEN;
    pc_ptp_ts_read(p, &out->timestamp);
    p += PC_PTP_TS_LEN;
    memcpy(out->req_clock_id, p, 8);
    p += 8;
    out->req_port = get_u16(p);
    return PROTO_TRUE;
}

proto_bool pc_ptp_parse_pdelay_resp(const uint8_t *s, size_t len, pc_ptp_header *h, pc_ptp_pdelay_resp *out)
{
    return parse_pdelay_resp_msg(s, len, h, out, PC_PTP_PDELAY_RESP);
}

proto_bool pc_ptp_parse_pdelay_resp_follow_up(const uint8_t *s, size_t len, pc_ptp_header *h, pc_ptp_pdelay_resp *out)
{
    return parse_pdelay_resp_msg(s, len, h, out, PC_PTP_PDELAY_RESP_FOLLOW_UP);
}

proto_bool pc_ptp_parse_announce(const uint8_t *s, size_t len, pc_ptp_header *h, pc_ptp_announce *out)
{
    if (!out || !pc_ptp_parse_header(s, len, h))
    {
        return PROTO_FALSE;
    }
    if (h->message_type != PC_PTP_ANNOUNCE)
    {
        return PROTO_FALSE;
    }
    if (len < PC_PTP_HEADER_LEN + 30) // originTimestamp(10)+utc(2)+rsv(1)+p1(1)+quality(4)+p2(1)+id(8)+steps(2)+src(1)
    {
        return PROTO_FALSE;
    }
    const uint8_t *p = s + PC_PTP_HEADER_LEN;
    pc_ptp_ts_read(p, &out->origin);
    p += PC_PTP_TS_LEN;
    out->utc_offset = (int16_t)get_u16(p);
    p += 2;
    p += 1; // reserved
    out->gm_priority1 = *p++;
    out->gm_clock_class = *p++;
    out->gm_clock_accuracy = *p++;
    out->gm_variance = get_u16(p);
    p += 2;
    out->gm_priority2 = *p++;
    memcpy(out->gm_identity, p, 8);
    p += 8;
    out->steps_removed = get_u16(p);
    p += 2;
    out->time_source = *p;
    return PROTO_TRUE;
}

// -- slave clock math --

void pc_ptp_compute(int64_t t1, int64_t t2, int64_t t3, int64_t t4, pc_ptp_sync *out)
{
    if (!out)
    {
        return;
    }
    int64_t ms = t2 - t1; // master -> slave transit (+ offset)
    int64_t sm = t4 - t3; // slave -> master transit (- offset)
    out->offset_ns = (ms - sm) / 2;
    out->delay_ns = (ms + sm) / 2;
}

int64_t pc_ptp_compute_link_delay(int64_t t1, int64_t t2, int64_t t3, int64_t t4)
{
    // meanLinkDelay = ((t4 - t1) - (t3 - t2)) / 2: the round trip minus the peer's turnaround, halved.
    return ((t4 - t1) - (t3 - t2)) / 2;
}

#endif // PC_ENABLE_PTP
