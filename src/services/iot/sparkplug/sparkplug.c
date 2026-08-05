// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sparkplug.c
 * @brief Sparkplug B payload + topic builder over the Protobuf codec (pure, host-tested).
 */

#include "services/iot/sparkplug/sparkplug.h"

#if PC_ENABLE_SPARKPLUG

#include "services/iot/protobuf/protobuf.h"

// Tahu Payload field numbers.
#define SPB_PL_TIMESTAMP 1
#define SPB_PL_METRICS 2
#define SPB_PL_SEQ 3

// Tahu Metric field numbers.
#define SPB_MET_NAME 1
#define SPB_MET_ALIAS 2
#define SPB_MET_TIMESTAMP 3
#define SPB_MET_DATATYPE 4
#define SPB_MET_INT 10
#define SPB_MET_LONG 11
#define SPB_MET_FLOAT 12
#define SPB_MET_DOUBLE 13
#define SPB_MET_BOOL 14
#define SPB_MET_STRING 15

size_t pc_spb_build_topic(char *buf, size_t cap, const char *group, const char *message_type, const char *edge_node,
                          const char *device)
{
    if (!buf || !group || !message_type || !edge_node)
    {
        return 0;
    }
    // spBv1.0/<group>/<message_type>/<edge_node>[/<device>]
    size_t need = 8 /*"spBv1.0/"*/ + strnlen(group, cap) + 1 + strnlen(message_type, cap) + 1 + strnlen(edge_node, cap);
    if (device)
    {
        need += 1 + strnlen(device, cap);
    }
    if (need + 1 > cap) // + NUL
    {
        return 0;
    }
    size_t p = 0;
    const char *prefix = "spBv1.0/";
    memcpy(buf + p, prefix, 8);
    p += 8;
    size_t n = strnlen(group, cap);
    memcpy(buf + p, group, n);
    p += n;
    buf[p++] = '/';
    n = strnlen(message_type, cap);
    memcpy(buf + p, message_type, n);
    p += n;
    buf[p++] = '/';
    n = strnlen(edge_node, cap);
    memcpy(buf + p, edge_node, n);
    p += n;
    if (device)
    {
        buf[p++] = '/';
        n = strnlen(device, cap);
        memcpy(buf + p, device, n);
        p += n;
    }
    buf[p] = '\0';
    return p;
}

size_t pc_spb_build_metric(uint8_t *buf, size_t cap, const SpbMetric *m)
{
    if (!buf || !m)
    {
        return 0;
    }
    PbWriter w;
    pc_pb_writer_init(&w, buf, cap);
    if (m->name)
    {
        pc_pb_string(&w, SPB_MET_NAME, m->name);
    }
    if (m->has_alias)
    {
        pc_pb_uint64(&w, SPB_MET_ALIAS, m->alias);
    }
    if (m->has_timestamp)
    {
        pc_pb_uint64(&w, SPB_MET_TIMESTAMP, m->timestamp);
    }
    pc_pb_uint64(&w, SPB_MET_DATATYPE, m->datatype); // datatype is a uint32; the varint covers it
    switch (m->kind)
    {
    case SPB_M_INT:
        pc_pb_uint64(&w, SPB_MET_INT, m->int_value);
        break;
    case SPB_M_LONG:
        pc_pb_uint64(&w, SPB_MET_LONG, m->long_value);
        break;
    case SPB_M_FLOAT:
        pc_pb_float(&w, SPB_MET_FLOAT, m->float_value);
        break;
    case SPB_M_DOUBLE:
        pc_pb_double(&w, SPB_MET_DOUBLE, m->double_value);
        break;
    case SPB_M_BOOL:
        pc_pb_bool(&w, SPB_MET_BOOL, m->bool_value);
        break;
    case SPB_M_STRING:
        if (m->string_value)
        {
            pc_pb_string(&w, SPB_MET_STRING, m->string_value);
        }
        break;
    }
    return pc_pb_writer_finish(&w);
}

size_t pc_spb_build_payload(uint8_t *buf, size_t cap, uint64_t timestamp, uint64_t seq, const SpbMetric *metrics,
                            size_t n)
{
    if (!buf || (n && !metrics))
    {
        return 0;
    }
    PbWriter w;
    pc_pb_writer_init(&w, buf, cap);
    pc_pb_uint64(&w, SPB_PL_TIMESTAMP, timestamp);
    for (size_t i = 0; i < n; i++)
    {
        // Serialize each Metric submessage into a bounded temp, then add it as a
        // length-delimited field (Payload.metrics). A metric stays well under this bound
        // unless it carries a large string, in which case the build fails closed.
        uint8_t metric[PC_SPB_METRIC_MAX];
        size_t mlen = pc_spb_build_metric(metric, sizeof(metric), &metrics[i]);
        if (!mlen)
        {
            return 0;
        }
        pc_pb_bytes(&w, SPB_PL_METRICS, metric, mlen);
    }
    pc_pb_uint64(&w, SPB_PL_SEQ, seq);
    return pc_pb_writer_finish(&w);
}

proto_bool pc_spb_parse_payload(const uint8_t *buf, size_t len, SpbPayloadHeader *out)
{
    if (!buf || !out)
    {
        return PROTO_FALSE;
    }
    memset(out, 0, sizeof(*out));
    size_t pos = 0;
    PbField f;
    while (pos < len)
    {
        if (!pc_pb_read_field(buf, len, &pos, &f))
        {
            return PROTO_FALSE;
        }
        if (f.field_number == SPB_PL_TIMESTAMP && f.wire_type == PB_WT_VARINT)
        {
            out->has_timestamp = PROTO_TRUE;
            out->timestamp = f.value;
        }
        else if (f.field_number == SPB_PL_SEQ && f.wire_type == PB_WT_VARINT)
        {
            out->has_seq = PROTO_TRUE;
            out->seq = f.value;
        }
        // metrics (field 2), uuid, body are skipped here
    }
    return PROTO_TRUE;
}

proto_bool pc_spb_payload_next_metric(const uint8_t *buf, size_t len, size_t *pos, const uint8_t **metric,
                                      size_t *metric_len)
{
    if (!buf || !pos || !metric || !metric_len)
    {
        return PROTO_FALSE;
    }
    PbField f;
    while (*pos < len)
    {
        if (!pc_pb_read_field(buf, len, pos, &f))
        {
            return PROTO_FALSE;
        }
        if (f.field_number == SPB_PL_METRICS && f.wire_type == PB_WT_LEN)
        {
            *metric = f.data;
            *metric_len = f.len;
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

// Apply a Metric metadata field (name / alias / timestamp / datatype) from @p f to @p out.
static void spb_apply_meta_field(SpbMetricDecoded *out, const PbField *f)
{
    switch (f->field_number)
    {
    case SPB_MET_NAME:
        if (f->wire_type == PB_WT_LEN)
        {
            out->name = (const char *)f->data;
            out->name_len = f->len;
        }
        break;
    case SPB_MET_ALIAS:
        if (f->wire_type == PB_WT_VARINT)
        {
            out->has_alias = PROTO_TRUE;
            out->alias = f->value;
        }
        break;
    case SPB_MET_TIMESTAMP:
        if (f->wire_type == PB_WT_VARINT)
        {
            out->has_timestamp = PROTO_TRUE;
            out->timestamp = f->value;
        }
        break;
    case SPB_MET_DATATYPE:
        if (f->wire_type == PB_WT_VARINT)
        {
            out->datatype = (uint32_t)f->value;
        }
        break;
    default:
        break;
    }
}

// Apply a Metric typed-value field (one of int / long / float / double / bool / string) from @p f.
static void spb_apply_value_field(SpbMetricDecoded *out, const PbField *f)
{
    switch (f->field_number)
    {
    case SPB_MET_INT:
        if (f->wire_type == PB_WT_VARINT)
        {
            out->has_value = PROTO_TRUE;
            out->kind = SPB_M_INT;
            out->int_value = (uint32_t)f->value;
        }
        break;
    case SPB_MET_LONG:
        if (f->wire_type == PB_WT_VARINT)
        {
            out->has_value = PROTO_TRUE;
            out->kind = SPB_M_LONG;
            out->long_value = f->value;
        }
        break;
    case SPB_MET_FLOAT:
        if (f->wire_type == PB_WT_I32)
        {
            out->has_value = PROTO_TRUE;
            out->kind = SPB_M_FLOAT;
            out->float_value = pc_pb_float_bits((uint32_t)f->value);
        }
        break;
    case SPB_MET_DOUBLE:
        if (f->wire_type == PB_WT_I64)
        {
            out->has_value = PROTO_TRUE;
            out->kind = SPB_M_DOUBLE;
            out->double_value = pc_pb_double_bits(f->value);
        }
        break;
    case SPB_MET_BOOL:
        if (f->wire_type == PB_WT_VARINT)
        {
            out->has_value = PROTO_TRUE;
            out->kind = SPB_M_BOOL;
            out->bool_value = f->value != 0;
        }
        break;
    case SPB_MET_STRING:
        if (f->wire_type == PB_WT_LEN)
        {
            out->has_value = PROTO_TRUE;
            out->kind = SPB_M_STRING;
            out->string_value = (const char *)f->data;
            out->string_value_len = f->len;
        }
        break;
    default:
        break;
    }
}

proto_bool pc_spb_parse_metric(const uint8_t *buf, size_t len, SpbMetricDecoded *out)
{
    if (!buf || !out)
    {
        return PROTO_FALSE;
    }
    memset(out, 0, sizeof(*out));
    size_t pos = 0;
    PbField f;
    while (pos < len)
    {
        if (!pc_pb_read_field(buf, len, &pos, &f))
        {
            return PROTO_FALSE;
        }
        spb_apply_meta_field(out, &f);  // name / alias / timestamp / datatype
        spb_apply_value_field(out, &f); // the typed value (int / long / float / double / bool / string)
    }
    return PROTO_TRUE;
}

#endif // PC_ENABLE_SPARKPLUG
