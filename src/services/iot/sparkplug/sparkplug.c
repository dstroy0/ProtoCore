// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sparkplug.c
 * @brief The Sparkplug B codec: the sec 4.1 topic joiner and the sec 6.4.1 Protocol Buffers
 *        encoder and decoder.
 *
 * A build writes into the caller's buffer and touches no socket. A Payload build serializes each
 * Metric into the codec's own scratch first, then adds that as the length-delimited metrics(2)
 * field.
 */

#include "services/iot/sparkplug/sparkplug.h"

#if PROTOCORE_ENABLE_SPARKPLUG

#include "mmgr/protomem.h"                  // mem.cpy / mem.set: the spans a topic and a decode move
#include "mmgr/protostr.h"                  // str.len: the bounded length of each topic element
#include "services/iot/protobuf/protobuf.h" // the wire codec a Payload and a Metric are written with

// Sparkplug 3.0.0 sec 4.1: the namespace element and the MQTT topic level separator that follows it.
#define SPB_TOPIC_PREFIX SPB_NAMESPACE "/"
#define SPB_TOPIC_PREFIX_LEN (sizeof(SPB_TOPIC_PREFIX) - 1)

// Sparkplug 3.0.0 sec 6.4.1 Payload field numbers. uuid(4) and body(5) are not written.
#define SPB_PAYLOAD_TIMESTAMP 1
#define SPB_PAYLOAD_METRICS 2
#define SPB_PAYLOAD_SEQ 3

// Sparkplug 3.0.0 sec 6.4.1 Metric field numbers. is_historical(5) through properties(9), and
// bytes_value(16) through extension_value(19), are not written.
#define SPB_METRIC_NAME 1
#define SPB_METRIC_ALIAS 2
#define SPB_METRIC_TIMESTAMP 3
#define SPB_METRIC_DATATYPE 4
#define SPB_METRIC_INT_VALUE 10
#define SPB_METRIC_LONG_VALUE 11
#define SPB_METRIC_FLOAT_VALUE 12
#define SPB_METRIC_DOUBLE_VALUE 13
#define SPB_METRIC_BOOLEAN_VALUE 14
#define SPB_METRIC_STRING_VALUE 15

// The Protobuf rows this codec drives. A Payload build holds its own row open while each Metric is
// serialized in the second one.
#define SPB_SLOT_MSG 0    ///< the row a Payload or a Metric build appends into, and a decode walks
#define SPB_SLOT_METRIC 1 ///< the row each Metric of a Payload is serialized in

/**
 * @brief The codec's compile-time storage: the scratch one Metric is serialized into.
 *
 * BSS, so a Payload build puts no Metric on a task stack.
 */
struct SparkplugStorage
{
    uint8_t metric[PROTOCORE_SPB_METRIC_MAX]; ///< one encoded Metric, before it becomes a metrics(2) field
};

/**
 * @brief The codec's state and the calls that reach it - what SparkplugNs points at.
 *
 * @var SparkplugInternal::store  the scratch a Payload build serializes each Metric into
 * @var SparkplugInternal::ns     the handle a caller sets a call's members on
 */
struct SparkplugInternal
{
    struct SparkplugStorage *store;
    SparkplugNs *ns;
};

static struct SparkplugStorage s_store;

static struct SparkplugInternal s_sparkplug = {.store = &s_store, .ns = &Sparkplug};

// Append a VARINT record carrying v under field, on the encoder row slot names.
static void pb_varint(uint8_t slot, uint32_t field, uint64_t v)
{
    Protobuf.slot = slot;
    Protobuf.tag.field_number = field;
    Protobuf.value.u64 = v;
    Protobuf.write_uint64(Protobuf.internal);
}

// Append a LEN record carrying the NUL-terminated s under field.
static void pb_text(uint8_t slot, uint32_t field, const char *s)
{
    Protobuf.slot = slot;
    Protobuf.tag.field_number = field;
    Protobuf.value.text = s;
    Protobuf.write_string(Protobuf.internal);
}

// Append a LEN record carrying len octets of data under field.
static void pb_span(uint8_t slot, uint32_t field, const uint8_t *data, size_t len)
{
    Protobuf.slot = slot;
    Protobuf.tag.field_number = field;
    Protobuf.value.data = data;
    Protobuf.value.len = len;
    Protobuf.write_bytes(Protobuf.internal);
}

// Serialize one Metric (Sparkplug 3.0.0 sec 6.4.6) into buf over the encoder row slot names,
// returning its length or 0 on overflow. Holds no codec state, so it takes the row, span and Metric.
static size_t metric_encode(uint8_t slot, uint8_t *buf, size_t cap, const SpbMetric *m)
{
    Protobuf.slot = slot;
    Protobuf.writer.buf = buf;
    Protobuf.writer.cap = cap;
    Protobuf.writer_open(Protobuf.internal);
    if (m->name)
    {
        pb_text(slot, SPB_METRIC_NAME, m->name);
    }
    if (m->has_alias)
    {
        pb_varint(slot, SPB_METRIC_ALIAS, m->alias);
    }
    if (m->has_timestamp)
    {
        pb_varint(slot, SPB_METRIC_TIMESTAMP, m->timestamp);
    }
    pb_varint(slot, SPB_METRIC_DATATYPE, m->datatype); // datatype is a uint32; the varint covers it
    switch (m->kind)
    {
    case SPB_M_INT:
        pb_varint(slot, SPB_METRIC_INT_VALUE, m->int_value);
        break;
    case SPB_M_LONG:
        pb_varint(slot, SPB_METRIC_LONG_VALUE, m->long_value);
        break;
    case SPB_M_FLOAT:
        Protobuf.slot = slot;
        Protobuf.tag.field_number = SPB_METRIC_FLOAT_VALUE;
        Protobuf.value.f32 = m->float_value;
        Protobuf.write_float(Protobuf.internal);
        break;
    case SPB_M_DOUBLE:
        Protobuf.slot = slot;
        Protobuf.tag.field_number = SPB_METRIC_DOUBLE_VALUE;
        Protobuf.value.f64 = m->double_value;
        Protobuf.write_double(Protobuf.internal);
        break;
    case SPB_M_BOOL:
        Protobuf.slot = slot;
        Protobuf.tag.field_number = SPB_METRIC_BOOLEAN_VALUE;
        Protobuf.value.flag = m->bool_value;
        Protobuf.write_bool(Protobuf.internal);
        break;
    case SPB_M_STRING:
        if (m->string_value)
        {
            pb_text(slot, SPB_METRIC_STRING_VALUE, m->string_value);
        }
        break;
    }
    Protobuf.slot = slot;
    Protobuf.writer_finish(Protobuf.internal);
    return Protobuf.n;
}

// Join `spBv1.0/group_id/message_type/edge_node_id[/device_id]` (Sparkplug 3.0.0 sec 4.1) into
// ns->topic_out, and report its length in ns->n. A topic that does not fit writes nothing.
static void spb_build_topic(struct SparkplugInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    char *out = ctx->ns->topic_out.out;
    const size_t cap = ctx->ns->topic_out.cap;
    const char *group = ctx->ns->topic.group_id;
    const char *type = ctx->ns->topic.message_type;
    const char *node = ctx->ns->topic.edge_node_id;
    const char *device = ctx->ns->topic.device_id;
    if (!out || !group || !type || !node)
    {
        return;
    }
    size_t need = SPB_TOPIC_PREFIX_LEN + str.len(group, cap) + 1 + str.len(type, cap) + 1 + str.len(node, cap);
    if (device)
    {
        need += 1 + str.len(device, cap);
    }
    if (need + 1 > cap) // + NUL
    {
        return;
    }
    size_t p = 0;
    mem.cpy(out + p, SPB_TOPIC_PREFIX, SPB_TOPIC_PREFIX_LEN);
    p += SPB_TOPIC_PREFIX_LEN;
    size_t n = str.len(group, cap);
    mem.cpy(out + p, group, n);
    p += n;
    out[p++] = '/';
    n = str.len(type, cap);
    mem.cpy(out + p, type, n);
    p += n;
    out[p++] = '/';
    n = str.len(node, cap);
    mem.cpy(out + p, node, n);
    p += n;
    if (device)
    {
        out[p++] = '/';
        n = str.len(device, cap);
        mem.cpy(out + p, device, n);
        p += n;
    }
    out[p] = '\0';
    ctx->ns->n = p;
    ctx->ns->ok = PROTO_TRUE;
}

// Serialize ns->metrics.list[0] as one Metric message into ns->out, reporting its length in ns->n.
static void spb_build_metric(struct SparkplugInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->out.buf || !ctx->ns->metrics.list)
    {
        return;
    }
    ctx->ns->n = metric_encode(SPB_SLOT_MSG, ctx->ns->out.buf, ctx->ns->out.cap, &ctx->ns->metrics.list[0]);
    ctx->ns->ok = ctx->ns->n != 0;
}

// Serialize a Payload (Sparkplug 3.0.0 sec 6.4.5): timestamp(1), then metrics(2) once per Metric,
// then seq(3). Each Metric goes into the codec's scratch first; one that overflows it fails the
// whole build closed.
static void spb_build_payload(struct SparkplugInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    const size_t count = ctx->ns->metrics.count;
    if (!ctx->ns->out.buf || (count && !ctx->ns->metrics.list))
    {
        return;
    }
    Protobuf.slot = SPB_SLOT_MSG;
    Protobuf.writer.buf = ctx->ns->out.buf;
    Protobuf.writer.cap = ctx->ns->out.cap;
    Protobuf.writer_open(Protobuf.internal);
    pb_varint(SPB_SLOT_MSG, SPB_PAYLOAD_TIMESTAMP, ctx->ns->payload.timestamp);
    for (size_t i = 0; i < count; i++)
    {
        size_t mlen =
            metric_encode(SPB_SLOT_METRIC, ctx->store->metric, sizeof(ctx->store->metric), &ctx->ns->metrics.list[i]);
        if (!mlen)
        {
            return;
        }
        pb_span(SPB_SLOT_MSG, SPB_PAYLOAD_METRICS, ctx->store->metric, mlen);
    }
    pb_varint(SPB_SLOT_MSG, SPB_PAYLOAD_SEQ, ctx->ns->payload.seq);
    Protobuf.slot = SPB_SLOT_MSG;
    Protobuf.writer_finish(Protobuf.internal);
    ctx->ns->n = Protobuf.n;
    ctx->ns->ok = ctx->ns->n != 0;
}

// Read a Payload's timestamp(1) and seq(3) from ns->source into ns->header. metrics(2), uuid(4) and
// body(5) are stepped over.
static void spb_parse_payload(struct SparkplugInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    mem.set(&ctx->ns->header, 0, sizeof(ctx->ns->header));
    if (!ctx->ns->source.buf)
    {
        return;
    }
    const size_t len = ctx->ns->source.len;
    Protobuf.slot = SPB_SLOT_MSG;
    Protobuf.source.buf = ctx->ns->source.buf;
    Protobuf.source.len = len;
    Protobuf.source.pos = 0;
    Protobuf.reader_open(Protobuf.internal);
    size_t pos = 0;
    while (pos < len)
    {
        Protobuf.slot = SPB_SLOT_MSG;
        Protobuf.read_record(Protobuf.internal);
        if (!Protobuf.ok)
        {
            return;
        }
        pos = Protobuf.n;
        if (Protobuf.record.field_number == SPB_PAYLOAD_TIMESTAMP &&
            Protobuf.record.wire_type == PROTOCORE_PROTOBUF_WT_VARINT)
        {
            ctx->ns->header.has_timestamp = PROTO_TRUE;
            ctx->ns->header.timestamp = Protobuf.record.value;
        }
        else if (Protobuf.record.field_number == SPB_PAYLOAD_SEQ &&
                 Protobuf.record.wire_type == PROTOCORE_PROTOBUF_WT_VARINT)
        {
            ctx->ns->header.has_seq = PROTO_TRUE;
            ctx->ns->header.seq = Protobuf.record.value;
        }
    }
    ctx->ns->ok = PROTO_TRUE;
}

// Report the next metrics(2) sub-message of a Payload in ns->metric_bytes / ns->metric_len and
// advance ns->source.cursor past it. False at the end of the Payload or on a malformed field.
static void spb_next_metric(struct SparkplugInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->metric_bytes = NULL;
    ctx->ns->metric_len = 0;
    if (!ctx->ns->source.buf)
    {
        return;
    }
    const size_t len = ctx->ns->source.len;
    Protobuf.slot = SPB_SLOT_MSG;
    Protobuf.source.buf = ctx->ns->source.buf;
    Protobuf.source.len = len;
    Protobuf.source.pos = ctx->ns->source.cursor;
    Protobuf.reader_open(Protobuf.internal);
    while (ctx->ns->source.cursor < len)
    {
        Protobuf.slot = SPB_SLOT_MSG;
        Protobuf.read_record(Protobuf.internal);
        if (!Protobuf.ok)
        {
            return;
        }
        ctx->ns->source.cursor = Protobuf.n;
        if (Protobuf.record.field_number == SPB_PAYLOAD_METRICS &&
            Protobuf.record.wire_type == PROTOCORE_PROTOBUF_WT_LEN)
        {
            ctx->ns->metric_bytes = Protobuf.record.data;
            ctx->ns->metric_len = Protobuf.record.len;
            ctx->ns->ok = PROTO_TRUE;
            return;
        }
    }
}

// Apply a Metric metadata field - name(1), alias(2), timestamp(3), datatype(4) - from f to out.
static void spb_apply_meta_field(SpbMetricDecoded *out, const ProtobufRecord *f)
{
    switch (f->field_number)
    {
    case SPB_METRIC_NAME:
        if (f->wire_type == PROTOCORE_PROTOBUF_WT_LEN)
        {
            out->name = (const char *)f->data;
            out->name_len = f->len;
        }
        break;
    case SPB_METRIC_ALIAS:
        if (f->wire_type == PROTOCORE_PROTOBUF_WT_VARINT)
        {
            out->has_alias = PROTO_TRUE;
            out->alias = f->value;
        }
        break;
    case SPB_METRIC_TIMESTAMP:
        if (f->wire_type == PROTOCORE_PROTOBUF_WT_VARINT)
        {
            out->has_timestamp = PROTO_TRUE;
            out->timestamp = f->value;
        }
        break;
    case SPB_METRIC_DATATYPE:
        if (f->wire_type == PROTOCORE_PROTOBUF_WT_VARINT)
        {
            out->datatype = (uint32_t)f->value;
        }
        break;
    default:
        break;
    }
}

// Apply one member of the Metric value oneof - int_value(10) through string_value(15) - from f to out.
static void spb_apply_value_field(SpbMetricDecoded *out, const ProtobufRecord *f)
{
    switch (f->field_number)
    {
    case SPB_METRIC_INT_VALUE:
        if (f->wire_type == PROTOCORE_PROTOBUF_WT_VARINT)
        {
            out->has_value = PROTO_TRUE;
            out->kind = SPB_M_INT;
            out->int_value = (uint32_t)f->value;
        }
        break;
    case SPB_METRIC_LONG_VALUE:
        if (f->wire_type == PROTOCORE_PROTOBUF_WT_VARINT)
        {
            out->has_value = PROTO_TRUE;
            out->kind = SPB_M_LONG;
            out->long_value = f->value;
        }
        break;
    case SPB_METRIC_FLOAT_VALUE:
        if (f->wire_type == PROTOCORE_PROTOBUF_WT_I32)
        {
            out->has_value = PROTO_TRUE;
            out->kind = SPB_M_FLOAT;
            Protobuf.value.u32 = (uint32_t)f->value;
            Protobuf.float_bits(Protobuf.internal);
            out->float_value = Protobuf.f32;
        }
        break;
    case SPB_METRIC_DOUBLE_VALUE:
        if (f->wire_type == PROTOCORE_PROTOBUF_WT_I64)
        {
            out->has_value = PROTO_TRUE;
            out->kind = SPB_M_DOUBLE;
            Protobuf.value.u64 = f->value;
            Protobuf.double_bits(Protobuf.internal);
            out->double_value = Protobuf.f64;
        }
        break;
    case SPB_METRIC_BOOLEAN_VALUE:
        if (f->wire_type == PROTOCORE_PROTOBUF_WT_VARINT)
        {
            out->has_value = PROTO_TRUE;
            out->kind = SPB_M_BOOL;
            out->bool_value = f->value != 0;
        }
        break;
    case SPB_METRIC_STRING_VALUE:
        if (f->wire_type == PROTOCORE_PROTOBUF_WT_LEN)
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

// Decode the Metric in ns->source into ns->metric: the metadata fields and the value oneof member.
static void spb_parse_metric(struct SparkplugInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    mem.set(&ctx->ns->metric, 0, sizeof(ctx->ns->metric));
    if (!ctx->ns->source.buf)
    {
        return;
    }
    const size_t len = ctx->ns->source.len;
    Protobuf.slot = SPB_SLOT_MSG;
    Protobuf.source.buf = ctx->ns->source.buf;
    Protobuf.source.len = len;
    Protobuf.source.pos = 0;
    Protobuf.reader_open(Protobuf.internal);
    size_t pos = 0;
    while (pos < len)
    {
        Protobuf.slot = SPB_SLOT_MSG;
        Protobuf.read_record(Protobuf.internal);
        if (!Protobuf.ok)
        {
            return;
        }
        pos = Protobuf.n;
        const ProtobufRecord rec = Protobuf.record;
        spb_apply_meta_field(&ctx->ns->metric, &rec);  // name / alias / timestamp / datatype
        spb_apply_value_field(&ctx->ns->metric, &rec); // the value oneof member
    }
    ctx->ns->ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
SparkplugNs Sparkplug = {.build_topic = spb_build_topic,
                         .build_metric = spb_build_metric,
                         .build_payload = spb_build_payload,
                         .parse_payload = spb_parse_payload,
                         .next_metric = spb_next_metric,
                         .parse_metric = spb_parse_metric,
                         .internal = &s_sparkplug};

#endif // PROTOCORE_ENABLE_SPARKPLUG
