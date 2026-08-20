// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ptp.c
 * @brief PTP / IEEE 1588-2008 (PTPv2) message codec + slave math (pure, host-tested).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_PTP

#include "mmgr/protomem/protomem.h"
#include "network_drivers/application/ptp/ptp.h"

PROTOCORE_BEGIN_DECLS

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

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_ptp_build_header(uint8_t *restrict work);
void protocore_ptp_parse_header(uint8_t *restrict work);
void protocore_ptp_ts_read(uint8_t *restrict work);
void protocore_ptp_ts_write(uint8_t *restrict work);

void protocore_ptp_ts_write(uint8_t *restrict work)
{
    (void)work;
    uint8_t *p = PtpV.ts_write_args.p;
    const protocore_ptp_timestamp *ts = PtpV.ts_write_args.ts;

    uint64_t s = ts->seconds;
    p[0] = (uint8_t)(s >> 40);
    p[1] = (uint8_t)(s >> 32);
    p[2] = (uint8_t)(s >> 24);
    p[3] = (uint8_t)(s >> 16);
    p[4] = (uint8_t)(s >> 8);
    p[5] = (uint8_t)s;
    put_u32(p + 6, ts->nanoseconds);
}

void protocore_ptp_ts_read(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *p = PtpV.ts_read_args.p;
    protocore_ptp_timestamp *ts = PtpV.ts_read_args.ts;

    ts->seconds = ((uint64_t)p[0] << 40) | ((uint64_t)p[1] << 32) | ((uint64_t)p[2] << 24) | ((uint64_t)p[3] << 16) |
                  ((uint64_t)p[4] << 8) | (uint64_t)p[5];
    ts->nanoseconds = get_u32(p + 6);
}

void protocore_ptp_ts_to_ns(uint8_t *restrict work)
{
    (void)work;
    const protocore_ptp_timestamp *ts = PtpV.ts_to_ns_args.ts;

    PtpV.value = (int64_t)ts->seconds * 1000000000LL + (int64_t)ts->nanoseconds;
}

void protocore_ptp_ts_from_ns(uint8_t *restrict work)
{
    (void)work;
    int64_t ns = PtpV.ts_from_ns_args.ns;
    protocore_ptp_timestamp *ts = PtpV.ts_from_ns_args.ts;

    if (ns < 0)
    {
        ns = 0; // on-wire timestamps are unsigned
    }
    ts->seconds = (uint64_t)(ns / 1000000000LL);
    ts->nanoseconds = (uint32_t)(ns % 1000000000LL);
}

// -- header --

void protocore_ptp_build_header(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = PtpV.build_header_args.buf;
    size_t cap = PtpV.build_header_args.cap;
    const protocore_ptp_header *h = PtpV.build_header_args.h;
    uint16_t body_len = PtpV.build_header_args.body_len;

    if (!buf || !h || cap < PROTOCORE_PTP_HEADER_LEN)
    {
        PtpV.n = 0;
        return;
    }
    mem.set(buf, 0, PROTOCORE_PTP_HEADER_LEN);
    buf[0] = (uint8_t)((h->transport_specific << 4) | (h->message_type & 0x0F));
    buf[1] = (uint8_t)(h->version & 0x0F);
    put_u16(buf + 2, (uint16_t)(PROTOCORE_PTP_HEADER_LEN + body_len));
    buf[4] = h->domain;
    put_u16(buf + 6, h->flags);
    put_u64(buf + 8, (uint64_t)h->correction);
    mem.cpy(buf + 20, h->clock_identity, 8);
    put_u16(buf + 28, h->port_number);
    put_u16(buf + 30, h->sequence_id);
    buf[32] = h->control;
    buf[33] = (uint8_t)h->log_interval;
    PtpV.n = PROTOCORE_PTP_HEADER_LEN;
}

void protocore_ptp_parse_header(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *s = PtpV.parse_header_args.s;
    size_t len = PtpV.parse_header_args.len;
    protocore_ptp_header *h = PtpV.parse_header_args.h;

    if (!s || !h || len < PROTOCORE_PTP_HEADER_LEN)
    {
        PtpV.ok = PROTO_FALSE;
        return;
    }
    h->message_type = (uint8_t)(s[0] & 0x0F);
    h->transport_specific = (uint8_t)(s[0] >> 4);
    h->version = (uint8_t)(s[1] & 0x0F);
    h->message_length = get_u16(s + 2);
    h->domain = s[4];
    h->flags = get_u16(s + 6);
    h->correction = (int64_t)get_u64(s + 8);
    mem.cpy(h->clock_identity, s + 20, 8);
    h->port_number = get_u16(s + 28);
    h->sequence_id = get_u16(s + 30);
    h->control = s[32];
    h->log_interval = (int8_t)s[33];
    PtpV.ok = PROTO_TRUE;
}

// -- messages --

static size_t build_ts_msg(uint8_t *restrict work, uint8_t *buf, size_t cap, const protocore_ptp_header *h,
                           const protocore_ptp_timestamp *ts, uint8_t mtype, uint8_t control)
{
    if (!buf || !h || !ts || cap < PROTOCORE_PTP_HEADER_LEN + PROTOCORE_PTP_TS_LEN)
    {
        return 0;
    }
    protocore_ptp_header hh = *h;
    hh.message_type = mtype;
    hh.control = control;
    if (hh.version == 0)
    {
        hh.version = 2;
    }
    PtpV.build_header_args.buf = buf;
    PtpV.build_header_args.cap = cap;
    PtpV.build_header_args.h = &hh;
    PtpV.build_header_args.body_len = PROTOCORE_PTP_TS_LEN;
    Ptp.build_header(work);
    PtpV.ts_write_args.p = buf + PROTOCORE_PTP_HEADER_LEN;
    PtpV.ts_write_args.ts = ts;
    Ptp.ts_write(work);
    return PROTOCORE_PTP_HEADER_LEN + PROTOCORE_PTP_TS_LEN;
}

void protocore_ptp_build_sync(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = PtpV.build_sync_args.buf;
    size_t cap = PtpV.build_sync_args.cap;
    const protocore_ptp_header *h = PtpV.build_sync_args.h;
    const protocore_ptp_timestamp *origin = PtpV.build_sync_args.origin;

    PtpV.n = build_ts_msg(work, buf, cap, h, origin, PROTOCORE_PTP_SYNC, 0x00);
}

void protocore_ptp_build_delay_req(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = PtpV.build_delay_req_args.buf;
    size_t cap = PtpV.build_delay_req_args.cap;
    const protocore_ptp_header *h = PtpV.build_delay_req_args.h;
    const protocore_ptp_timestamp *origin = PtpV.build_delay_req_args.origin;

    PtpV.n = build_ts_msg(work, buf, cap, h, origin, PROTOCORE_PTP_DELAY_REQ, 0x01);
}

void protocore_ptp_build_follow_up(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = PtpV.build_follow_up_args.buf;
    size_t cap = PtpV.build_follow_up_args.cap;
    const protocore_ptp_header *h = PtpV.build_follow_up_args.h;
    const protocore_ptp_timestamp *precise = PtpV.build_follow_up_args.precise;

    PtpV.n = build_ts_msg(work, buf, cap, h, precise, PROTOCORE_PTP_FOLLOW_UP, 0x02);
}

void protocore_ptp_build_delay_resp(uint8_t *restrict work)
{
    uint8_t *buf = PtpV.build_delay_resp_args.buf;
    size_t cap = PtpV.build_delay_resp_args.cap;
    const protocore_ptp_header *h = PtpV.build_delay_resp_args.h;
    const protocore_ptp_timestamp *recv = PtpV.build_delay_resp_args.recv;
    const uint8_t *req_clock_id = PtpV.build_delay_resp_args.req_clock_id;
    uint16_t req_port = PtpV.build_delay_resp_args.req_port;

    const size_t body = PROTOCORE_PTP_TS_LEN + 10; // receiveTimestamp + requestingPortIdentity(10)
    if (!buf || !h || !recv || !req_clock_id || cap < PROTOCORE_PTP_HEADER_LEN + body)
    {
        PtpV.n = 0;
        return;
    }
    protocore_ptp_header hh = *h;
    hh.message_type = PROTOCORE_PTP_DELAY_RESP;
    hh.control = 0x03;
    if (hh.version == 0)
    {
        hh.version = 2;
    }
    PtpV.build_header_args.buf = buf;
    PtpV.build_header_args.cap = cap;
    PtpV.build_header_args.h = &hh;
    PtpV.build_header_args.body_len = (uint16_t)body;
    protocore_ptp_build_header(work);
    uint8_t *p = buf + PROTOCORE_PTP_HEADER_LEN;
    PtpV.ts_write_args.p = p;
    PtpV.ts_write_args.ts = recv;
    protocore_ptp_ts_write(work);
    p += PROTOCORE_PTP_TS_LEN;
    mem.cpy(p, req_clock_id, 8);
    p += 8;
    put_u16(p, req_port);
    PtpV.n = PROTOCORE_PTP_HEADER_LEN + body;
}

void protocore_ptp_build_pdelay_req(uint8_t *restrict work)
{
    uint8_t *buf = PtpV.build_pdelay_req_args.buf;
    size_t cap = PtpV.build_pdelay_req_args.cap;
    const protocore_ptp_header *h = PtpV.build_pdelay_req_args.h;
    const protocore_ptp_timestamp *origin = PtpV.build_pdelay_req_args.origin;

    const size_t body = PROTOCORE_PTP_TS_LEN + 10; // originTimestamp + 10-octet reserved (pads to Pdelay_Resp length)
    if (!buf || !h || !origin || cap < PROTOCORE_PTP_HEADER_LEN + body)
    {
        PtpV.n = 0;
        return;
    }
    protocore_ptp_header hh = *h;
    hh.message_type = PROTOCORE_PTP_PDELAY_REQ;
    hh.control = 0x05; // "all others" (IEEE 1588-2008 Table 23)
    if (hh.version == 0)
    {
        hh.version = 2;
    }
    PtpV.build_header_args.buf = buf;
    PtpV.build_header_args.cap = cap;
    PtpV.build_header_args.h = &hh;
    PtpV.build_header_args.body_len = (uint16_t)body;
    protocore_ptp_build_header(work);
    uint8_t *p = buf + PROTOCORE_PTP_HEADER_LEN;
    PtpV.ts_write_args.p = p;
    PtpV.ts_write_args.ts = origin;
    protocore_ptp_ts_write(work);
    mem.set(p + PROTOCORE_PTP_TS_LEN, 0, 10); // reserved
    PtpV.n = PROTOCORE_PTP_HEADER_LEN + body;
}

// Pdelay_Resp and Pdelay_Resp_Follow_Up share a body layout (a timestamp + the requesting port identity);
// only the messageType and which timestamp it carries differ.
static size_t build_pdelay_resp_msg(uint8_t *restrict work, uint8_t *buf, size_t cap, const protocore_ptp_header *h,
                                    const protocore_ptp_timestamp *ts, const uint8_t *req_clock_id, uint16_t req_port,
                                    uint8_t mtype)
{
    const size_t body = PROTOCORE_PTP_TS_LEN + 10; // timestamp + requestingPortIdentity(10)
    if (!buf || !h || !ts || !req_clock_id || cap < PROTOCORE_PTP_HEADER_LEN + body)
    {
        return 0;
    }
    protocore_ptp_header hh = *h;
    hh.message_type = mtype;
    hh.control = 0x05;
    if (hh.version == 0)
    {
        hh.version = 2;
    }
    PtpV.build_header_args.buf = buf;
    PtpV.build_header_args.cap = cap;
    PtpV.build_header_args.h = &hh;
    PtpV.build_header_args.body_len = (uint16_t)body;
    Ptp.build_header(work);
    uint8_t *p = buf + PROTOCORE_PTP_HEADER_LEN;
    PtpV.ts_write_args.p = p;
    PtpV.ts_write_args.ts = ts;
    Ptp.ts_write(work);
    p += PROTOCORE_PTP_TS_LEN;
    mem.cpy(p, req_clock_id, 8);
    p += 8;
    put_u16(p, req_port);
    return PROTOCORE_PTP_HEADER_LEN + body;
}

void protocore_ptp_build_pdelay_resp(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = PtpV.build_pdelay_resp_args.buf;
    size_t cap = PtpV.build_pdelay_resp_args.cap;
    const protocore_ptp_header *h = PtpV.build_pdelay_resp_args.h;
    const protocore_ptp_timestamp *recv = PtpV.build_pdelay_resp_args.recv;
    const uint8_t *req_clock_id = PtpV.build_pdelay_resp_args.req_clock_id;
    uint16_t req_port = PtpV.build_pdelay_resp_args.req_port;

    PtpV.n = build_pdelay_resp_msg(work, buf, cap, h, recv, req_clock_id, req_port, PROTOCORE_PTP_PDELAY_RESP);
}

void protocore_ptp_build_pdelay_resp_follow_up(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = PtpV.build_pdelay_resp_follow_up_args.buf;
    size_t cap = PtpV.build_pdelay_resp_follow_up_args.cap;
    const protocore_ptp_header *h = PtpV.build_pdelay_resp_follow_up_args.h;
    const protocore_ptp_timestamp *origin = PtpV.build_pdelay_resp_follow_up_args.origin;
    const uint8_t *req_clock_id = PtpV.build_pdelay_resp_follow_up_args.req_clock_id;
    uint16_t req_port = PtpV.build_pdelay_resp_follow_up_args.req_port;

    PtpV.n =
        build_pdelay_resp_msg(work, buf, cap, h, origin, req_clock_id, req_port, PROTOCORE_PTP_PDELAY_RESP_FOLLOW_UP);
}

void protocore_ptp_build_announce(uint8_t *restrict work)
{
    uint8_t *buf = PtpV.build_announce_args.buf;
    size_t cap = PtpV.build_announce_args.cap;
    const protocore_ptp_header *h = PtpV.build_announce_args.h;
    const protocore_ptp_announce *a = PtpV.build_announce_args.a;

    const size_t body = 30; // originTimestamp(10)+utc(2)+rsv(1)+p1(1)+quality(4)+p2(1)+id(8)+steps(2)+src(1)
    if (!buf || !h || !a || cap < PROTOCORE_PTP_HEADER_LEN + body)
    {
        PtpV.n = 0;
        return;
    }
    protocore_ptp_header hh = *h;
    hh.message_type = PROTOCORE_PTP_ANNOUNCE;
    hh.control = 0x05;
    if (hh.version == 0)
    {
        hh.version = 2;
    }
    PtpV.build_header_args.buf = buf;
    PtpV.build_header_args.cap = cap;
    PtpV.build_header_args.h = &hh;
    PtpV.build_header_args.body_len = (uint16_t)body;
    protocore_ptp_build_header(work);
    uint8_t *p = buf + PROTOCORE_PTP_HEADER_LEN;
    PtpV.ts_write_args.p = p;
    PtpV.ts_write_args.ts = &a->origin;
    protocore_ptp_ts_write(work);
    p += PROTOCORE_PTP_TS_LEN;
    put_u16(p, (uint16_t)a->utc_offset);
    p += 2;
    *p++ = 0; // reserved
    *p++ = a->gm_priority1;
    *p++ = a->gm_clock_class;
    *p++ = a->gm_clock_accuracy;
    put_u16(p, a->gm_variance);
    p += 2;
    *p++ = a->gm_priority2;
    mem.cpy(p, a->gm_identity, 8);
    p += 8;
    put_u16(p, a->steps_removed);
    p += 2;
    *p = a->time_source;
    PtpV.n = PROTOCORE_PTP_HEADER_LEN + body;
}

void protocore_ptp_parse_timestamp_msg(uint8_t *restrict work)
{
    const uint8_t *s = PtpV.parse_timestamp_msg_args.s;
    size_t len = PtpV.parse_timestamp_msg_args.len;
    protocore_ptp_header *h = PtpV.parse_timestamp_msg_args.h;
    protocore_ptp_timestamp *ts = PtpV.parse_timestamp_msg_args.ts;

    if (!ts)
    {
        PtpV.ok = PROTO_FALSE;
        return;
    }
    PtpV.parse_header_args.s = s;
    PtpV.parse_header_args.len = len;
    PtpV.parse_header_args.h = h;
    protocore_ptp_parse_header(work);
    if (!PtpV.ok)
    {
        PtpV.ok = PROTO_FALSE;
        return;
    }
    if (len < PROTOCORE_PTP_HEADER_LEN + PROTOCORE_PTP_TS_LEN)
    {
        PtpV.ok = PROTO_FALSE;
        return;
    }
    if (h->message_type != PROTOCORE_PTP_SYNC && h->message_type != PROTOCORE_PTP_DELAY_REQ &&
        h->message_type != PROTOCORE_PTP_FOLLOW_UP)
    {
        PtpV.ok = PROTO_FALSE;
        return;
    }
    PtpV.ts_read_args.p = s + PROTOCORE_PTP_HEADER_LEN;
    PtpV.ts_read_args.ts = ts;
    protocore_ptp_ts_read(work);
    PtpV.ok = PROTO_TRUE;
}

void protocore_ptp_parse_delay_resp(uint8_t *restrict work)
{
    const uint8_t *s = PtpV.parse_delay_resp_args.s;
    size_t len = PtpV.parse_delay_resp_args.len;
    protocore_ptp_header *h = PtpV.parse_delay_resp_args.h;
    protocore_ptp_delay_resp *out = PtpV.parse_delay_resp_args.out;

    if (!out)
    {
        PtpV.ok = PROTO_FALSE;
        return;
    }
    PtpV.parse_header_args.s = s;
    PtpV.parse_header_args.len = len;
    PtpV.parse_header_args.h = h;
    protocore_ptp_parse_header(work);
    if (!PtpV.ok)
    {
        PtpV.ok = PROTO_FALSE;
        return;
    }
    if (h->message_type != PROTOCORE_PTP_DELAY_RESP)
    {
        PtpV.ok = PROTO_FALSE;
        return;
    }
    if (len < PROTOCORE_PTP_HEADER_LEN + PROTOCORE_PTP_TS_LEN + 10)
    {
        PtpV.ok = PROTO_FALSE;
        return;
    }
    const uint8_t *p = s + PROTOCORE_PTP_HEADER_LEN;
    PtpV.ts_read_args.p = p;
    PtpV.ts_read_args.ts = &out->receive;
    protocore_ptp_ts_read(work);
    p += PROTOCORE_PTP_TS_LEN;
    mem.cpy(out->req_clock_id, p, 8);
    p += 8;
    out->req_port = get_u16(p);
    PtpV.ok = PROTO_TRUE;
}

void protocore_ptp_parse_pdelay_req(uint8_t *restrict work)
{
    const uint8_t *s = PtpV.parse_pdelay_req_args.s;
    size_t len = PtpV.parse_pdelay_req_args.len;
    protocore_ptp_header *h = PtpV.parse_pdelay_req_args.h;
    protocore_ptp_timestamp *ts = PtpV.parse_pdelay_req_args.ts;

    if (!ts)
    {
        PtpV.ok = PROTO_FALSE;
        return;
    }
    PtpV.parse_header_args.s = s;
    PtpV.parse_header_args.len = len;
    PtpV.parse_header_args.h = h;
    protocore_ptp_parse_header(work);
    if (!PtpV.ok)
    {
        PtpV.ok = PROTO_FALSE;
        return;
    }
    if (h->message_type != PROTOCORE_PTP_PDELAY_REQ)
    {
        PtpV.ok = PROTO_FALSE;
        return;
    }
    // header, originTimestamp, and the reserved field that follows it: the whole fixed message.
    if (len < PROTOCORE_PTP_PDELAY_REQ_LEN)
    {
        PtpV.ok = PROTO_FALSE;
        return;
    }
    PtpV.ts_read_args.p = s + PROTOCORE_PTP_HEADER_LEN;
    PtpV.ts_read_args.ts = ts;
    protocore_ptp_ts_read(work);
    PtpV.ok = PROTO_TRUE;
}

// Pdelay_Resp and its Follow_Up share the body layout; only the messageType differs.
static proto_bool parse_pdelay_resp_msg(uint8_t *restrict work, const uint8_t *s, size_t len, protocore_ptp_header *h,
                                        protocore_ptp_pdelay_resp *out, uint8_t mtype)
{
    if (!out)
    {
        return PROTO_FALSE;
    }
    PtpV.parse_header_args.s = s;
    PtpV.parse_header_args.len = len;
    PtpV.parse_header_args.h = h;
    Ptp.parse_header(work);
    if (!PtpV.ok)
    {
        return PROTO_FALSE;
    }
    if (h->message_type != mtype)
    {
        return PROTO_FALSE;
    }
    if (len < PROTOCORE_PTP_HEADER_LEN + PROTOCORE_PTP_TS_LEN + 10)
    {
        return PROTO_FALSE;
    }
    const uint8_t *p = s + PROTOCORE_PTP_HEADER_LEN;
    PtpV.ts_read_args.p = p;
    PtpV.ts_read_args.ts = &out->timestamp;
    Ptp.ts_read(work);
    p += PROTOCORE_PTP_TS_LEN;
    mem.cpy(out->req_clock_id, p, 8);
    p += 8;
    out->req_port = get_u16(p);
    return PROTO_TRUE;
}

void protocore_ptp_parse_pdelay_resp(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *s = PtpV.parse_pdelay_resp_args.s;
    size_t len = PtpV.parse_pdelay_resp_args.len;
    protocore_ptp_header *h = PtpV.parse_pdelay_resp_args.h;
    protocore_ptp_pdelay_resp *out = PtpV.parse_pdelay_resp_args.out;

    PtpV.ok = parse_pdelay_resp_msg(work, s, len, h, out, PROTOCORE_PTP_PDELAY_RESP);
}

void protocore_ptp_parse_pdelay_resp_follow_up(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *s = PtpV.parse_pdelay_resp_follow_up_args.s;
    size_t len = PtpV.parse_pdelay_resp_follow_up_args.len;
    protocore_ptp_header *h = PtpV.parse_pdelay_resp_follow_up_args.h;
    protocore_ptp_pdelay_resp *out = PtpV.parse_pdelay_resp_follow_up_args.out;

    PtpV.ok = parse_pdelay_resp_msg(work, s, len, h, out, PROTOCORE_PTP_PDELAY_RESP_FOLLOW_UP);
}

void protocore_ptp_parse_announce(uint8_t *restrict work)
{
    const uint8_t *s = PtpV.parse_announce_args.s;
    size_t len = PtpV.parse_announce_args.len;
    protocore_ptp_header *h = PtpV.parse_announce_args.h;
    protocore_ptp_announce *out = PtpV.parse_announce_args.out;

    if (!out)
    {
        PtpV.ok = PROTO_FALSE;
        return;
    }
    PtpV.parse_header_args.s = s;
    PtpV.parse_header_args.len = len;
    PtpV.parse_header_args.h = h;
    protocore_ptp_parse_header(work);
    if (!PtpV.ok)
    {
        PtpV.ok = PROTO_FALSE;
        return;
    }
    if (h->message_type != PROTOCORE_PTP_ANNOUNCE)
    {
        PtpV.ok = PROTO_FALSE;
        return;
    }
    if (len <
        PROTOCORE_PTP_HEADER_LEN + 30) // originTimestamp(10)+utc(2)+rsv(1)+p1(1)+quality(4)+p2(1)+id(8)+steps(2)+src(1)
    {
        PtpV.ok = PROTO_FALSE;
        return;
    }
    const uint8_t *p = s + PROTOCORE_PTP_HEADER_LEN;
    PtpV.ts_read_args.p = p;
    PtpV.ts_read_args.ts = &out->origin;
    protocore_ptp_ts_read(work);
    p += PROTOCORE_PTP_TS_LEN;
    out->utc_offset = (int16_t)get_u16(p);
    p += 2;
    p += 1; // reserved
    out->gm_priority1 = *p++;
    out->gm_clock_class = *p++;
    out->gm_clock_accuracy = *p++;
    out->gm_variance = get_u16(p);
    p += 2;
    out->gm_priority2 = *p++;
    mem.cpy(out->gm_identity, p, 8);
    p += 8;
    out->steps_removed = get_u16(p);
    p += 2;
    out->time_source = *p;
    PtpV.ok = PROTO_TRUE;
}

// -- slave clock math --

void protocore_ptp_compute(uint8_t *restrict work)
{
    (void)work;
    int64_t t1 = PtpV.compute_args.t1;
    int64_t t2 = PtpV.compute_args.t2;
    int64_t t3 = PtpV.compute_args.t3;
    int64_t t4 = PtpV.compute_args.t4;
    protocore_ptp_sync *out = PtpV.compute_args.out;

    if (!out)
    {
        return;
    }
    int64_t ms = t2 - t1; // master -> slave transit (+ offset)
    int64_t sm = t4 - t3; // slave -> master transit (- offset)
    out->offset_ns = (ms - sm) / 2;
    out->delay_ns = (ms + sm) / 2;
}

void protocore_ptp_compute_link_delay(uint8_t *restrict work)
{
    (void)work;
    int64_t t1 = PtpV.compute_link_delay_args.t1;
    int64_t t2 = PtpV.compute_link_delay_args.t2;
    int64_t t3 = PtpV.compute_link_delay_args.t3;
    int64_t t4 = PtpV.compute_link_delay_args.t4;

    // meanLinkDelay = ((t4 - t1) - (t3 - t2)) / 2: the round trip minus the peer's turnaround, halved.
    PtpV.value = ((t4 - t1) - (t3 - t2)) / 2;
}

/** @brief The operands and the outcome. */
PtpVars PtpV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_PTP
