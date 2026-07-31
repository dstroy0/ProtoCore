// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file flow_export.cpp
 * @brief NetFlow v5 / v9 + IPFIX exporter codec (pure, host-tested).
 */

#include "services/net/flow_export/flow_export.h"

#if PC_ENABLE_FLOW_EXPORT

#include <string.h>

#include "shared_primitives/endian.h"

size_t flow_v5_write_header(uint8_t *buf, size_t cap, const FlowV5Header *h)
{
    if (!buf || !h || cap < FLOW_V5_HEADER_SIZE)
    {
        return 0;
    }
    size_t p = 0;
    p += pc_wr16be(buf + p, 5); // version
    p += pc_wr16be(buf + p, h->count);
    p += pc_wr32be(buf + p, h->sys_uptime);
    p += pc_wr32be(buf + p, h->unix_secs);
    p += pc_wr32be(buf + p, h->unix_nsecs);
    p += pc_wr32be(buf + p, h->flow_sequence);
    buf[p++] = h->engine_type;
    buf[p++] = h->engine_id;
    p += pc_wr16be(buf + p, h->sampling_interval);
    return p; // 24
}

size_t flow_v5_write_record(uint8_t *buf, size_t cap, const FlowV5Record *r)
{
    if (!buf || !r || cap < FLOW_V5_RECORD_SIZE)
    {
        return 0;
    }
    size_t p = 0;
    p += pc_wr32be(buf + p, r->src_addr);
    p += pc_wr32be(buf + p, r->dst_addr);
    p += pc_wr32be(buf + p, r->next_hop);
    p += pc_wr16be(buf + p, r->input);
    p += pc_wr16be(buf + p, r->output);
    p += pc_wr32be(buf + p, r->d_pkts);
    p += pc_wr32be(buf + p, r->d_octets);
    p += pc_wr32be(buf + p, r->first);
    p += pc_wr32be(buf + p, r->last);
    p += pc_wr16be(buf + p, r->src_port);
    p += pc_wr16be(buf + p, r->dst_port);
    buf[p++] = 0; // pad1
    buf[p++] = r->tcp_flags;
    buf[p++] = r->prot;
    buf[p++] = r->tos;
    p += pc_wr16be(buf + p, r->src_as);
    p += pc_wr16be(buf + p, r->dst_as);
    buf[p++] = r->src_mask;
    buf[p++] = r->dst_mask;
    buf[p++] = 0; // pad2 (2 octets)
    buf[p++] = 0;
    return p; // 48
}

// ---- v9 / IPFIX cursor ----

static void w_u16(FlowWriter *w, uint16_t v)
{
    if (w->error)
    {
        return;
    }
    if (w->pos + 2 > w->cap)
    {
        w->error = true;
        return;
    }
    w->pos += pc_wr16be(w->buf + w->pos, v);
}

static void w_u32(FlowWriter *w, uint32_t v)
{
    if (w->error)
    {
        return;
    }
    if (w->pos + 4 > w->cap)
    {
        w->error = true;
        return;
    }
    w->pos += pc_wr32be(w->buf + w->pos, v);
}

static void w_bytes(FlowWriter *w, const uint8_t *p, size_t n)
{
    if (w->error)
    {
        return;
    }
    if (w->pos + n > w->cap)
    {
        w->error = true;
        return;
    }
    memcpy(w->buf + w->pos, p, n);
    w->pos += n;
}

// Append @p n zero octets (FlowSet padding).
static void w_zero(FlowWriter *w, size_t n)
{
    if (w->error)
    {
        return;
    }
    if (w->pos + n > w->cap)
    {
        w->error = true;
        return;
    }
    memset(w->buf + w->pos, 0, n);
    w->pos += n;
}

static void patch16(FlowWriter *w, size_t off, uint16_t v)
{
    pc_wr16be(w->buf + off, v);
}

bool flow_ipfix_begin(FlowWriter *w, uint8_t *buf, size_t cap, uint32_t export_time, uint32_t seq, uint32_t domain_id)
{
    if (!w || !buf)
    {
        return false;
    }
    w->buf = buf;
    w->cap = cap;
    w->pos = 0;
    w->set_start = 0;
    w->records = 0;
    w->version = 10;
    w->error = false;
    w_u16(w, 10); // version
    w_u16(w, 0);  // length placeholder (patched on finish)
    w_u32(w, export_time);
    w_u32(w, seq);
    w_u32(w, domain_id);
    return !w->error;
}

bool flow_v9_begin(FlowWriter *w, uint8_t *buf, size_t cap, uint32_t sys_uptime, uint32_t unix_secs, uint32_t seq,
                   uint32_t source_id)
{
    if (!w || !buf)
    {
        return false;
    }
    w->buf = buf;
    w->cap = cap;
    w->pos = 0;
    w->set_start = 0;
    w->records = 0;
    w->version = 9;
    w->error = false;
    w_u16(w, 9); // version
    w_u16(w, 0); // count placeholder (patched on finish)
    w_u32(w, sys_uptime);
    w_u32(w, unix_secs);
    w_u32(w, seq);
    w_u32(w, source_id);
    return !w->error;
}

bool flow_export_template(FlowWriter *w, uint16_t template_id, const FlowField *fields, size_t field_count)
{
    if (!w || !fields || field_count == 0)
    {
        return false;
    }
    if (w->set_start) // a data set was left open
    {
        flow_export_data_end(w);
    }
    size_t set_off = w->pos;
    w_u16(w, w->version == 9 ? 0 : 2); // FlowSet ID 0 (v9) / Set ID 2 (IPFIX)
    w_u16(w, 0);                       // set length placeholder
    w_u16(w, template_id);
    w_u16(w, (uint16_t)field_count);
    for (size_t i = 0; i < field_count; i++)
    {
        w_u16(w, fields[i].type);
        w_u16(w, fields[i].length);
    }
    if (!w->error)
    {
        patch16(w, set_off + 2, (uint16_t)(w->pos - set_off));
    }
    w->records++; // a template record counts toward the v9 record count
    return !w->error;
}

bool flow_export_data_begin(FlowWriter *w, uint16_t template_id)
{
    if (!w || template_id < 256) // data sets reference a template id >= 256
    {
        return false;
    }
    if (w->set_start)
    {
        flow_export_data_end(w);
    }
    w->set_start = w->pos;
    w_u16(w, template_id); // Set/FlowSet ID == template id
    w_u16(w, 0);           // length placeholder
    return !w->error;
}

bool flow_export_data_record(FlowWriter *w, const uint8_t *record, size_t len)
{
    if (!w || !w->set_start || !record || len == 0)
    {
        return false;
    }
    w_bytes(w, record, len);
    if (!w->error)
    {
        w->records++;
    }
    return !w->error;
}

bool flow_export_data_end(FlowWriter *w)
{
    if (!w || !w->set_start)
    {
        return false;
    }
    if (w->version == 9) // v9 pads each FlowSet so the next starts on a 4-octet boundary
    {
        size_t set_len = w->pos - w->set_start;
        w_zero(w, (4 - (set_len & 3)) & 3);
    }
    if (!w->error)
    {
        patch16(w, w->set_start + 2, (uint16_t)(w->pos - w->set_start));
    }
    w->set_start = 0;
    return !w->error;
}

size_t flow_export_finish(FlowWriter *w)
{
    if (!w)
    {
        return 0;
    }
    if (w->set_start)
    {
        flow_export_data_end(w);
    }
    if (w->error)
    {
        return 0;
    }
    if (w->version == 9)
    {
        patch16(w, 2, w->records); // count = total records
    }
    else
    {
        if (w->pos > 0xFFFF) // the IPFIX length field is 16-bit; fail closed rather than truncate
        {
            return 0;
        }
        patch16(w, 2, (uint16_t)w->pos); // IPFIX message length in octets
    }
    return w->pos;
}

#endif // PC_ENABLE_FLOW_EXPORT
