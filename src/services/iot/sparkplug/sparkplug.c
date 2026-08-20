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
#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from

#if PROTOCORE_ENABLE_SPARKPLUG

#include "mmgr/protomem/protomem.h"         // mem.cpy / mem.set: the spans a topic and a decode move
#include "mmgr/protostr/protostr.h"         // str.len: the bounded length of each topic element
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

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define SPARKPLUG_OFF_CTX 0u
static_assert(SPARKPLUG_OFF_CTX + sizeof(struct SparkplugStorage) <= PROTOCORE_SPARKPLUG_BORROW,
              "PROTOCORE_SPARKPLUG_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define SPARKPLUG_CTX(w) ((struct SparkplugStorage *)(void *)((w) + SPARKPLUG_OFF_CTX))

// Append a VARINT record carrying v under field, on the encoder row slot names.
static void pb_varint(uint8_t slot, uint32_t field, uint64_t v)
{
    ProtobufV.slot = slot;
    ProtobufV.tag.field_number = field;
    ProtobufV.value.u64 = v;
    Protobuf.write_uint64(protocore_protobuf_span());
}

// Append a LEN record carrying the NUL-terminated s under field.
static void pb_text(uint8_t slot, uint32_t field, const char *s)
{
    ProtobufV.slot = slot;
    ProtobufV.tag.field_number = field;
    ProtobufV.value.text = s;
    Protobuf.write_string(protocore_protobuf_span());
}

// Append a LEN record carrying len octets of data under field.
static void pb_span(uint8_t slot, uint32_t field, const uint8_t *data, size_t len)
{
    ProtobufV.slot = slot;
    ProtobufV.tag.field_number = field;
    ProtobufV.value.data = data;
    ProtobufV.value.len = len;
    Protobuf.write_bytes(protocore_protobuf_span());
}

// Serialize one Metric (Sparkplug 3.0.0 sec 6.4.6) into buf over the encoder row slot names,
// returning its length or 0 on overflow. Holds no codec state, so it takes the row, span and Metric.
static size_t metric_encode(uint8_t slot, uint8_t *buf, size_t cap, const SpbMetric *m)
{
    ProtobufV.slot = slot;
    ProtobufV.writer.buf = buf;
    ProtobufV.writer.cap = cap;
    Protobuf.writer_open(protocore_protobuf_span());
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
        ProtobufV.slot = slot;
        ProtobufV.tag.field_number = SPB_METRIC_FLOAT_VALUE;
        ProtobufV.value.f32 = m->float_value;
        Protobuf.write_float(protocore_protobuf_span());
        break;
    case SPB_M_DOUBLE:
        ProtobufV.slot = slot;
        ProtobufV.tag.field_number = SPB_METRIC_DOUBLE_VALUE;
        ProtobufV.value.f64 = m->double_value;
        Protobuf.write_double(protocore_protobuf_span());
        break;
    case SPB_M_BOOL:
        ProtobufV.slot = slot;
        ProtobufV.tag.field_number = SPB_METRIC_BOOLEAN_VALUE;
        ProtobufV.value.flag = m->bool_value;
        Protobuf.write_bool(protocore_protobuf_span());
        break;
    case SPB_M_STRING:
        if (m->string_value)
        {
            pb_text(slot, SPB_METRIC_STRING_VALUE, m->string_value);
        }
        break;
    }
    ProtobufV.slot = slot;
    Protobuf.writer_finish(protocore_protobuf_span());
    return ProtobufV.n;
}

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_SPARKPLUG_BORROW persistent bytes
} SparkplugOwnCtx;
static SparkplugOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_sparkplug_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_SPARKPLUG_BORROW).buf;
    }
    return s_own.span;
}

// Join `spBv1.0/group_id/message_type/edge_node_id[/device_id]` (Sparkplug 3.0.0 sec 4.1) into
// ns->topic_out, and report its length in ns->n. A topic that does not fit writes nothing.
void protocore_sparkplug_build_topic(uint8_t *restrict work)
{
    (void)work;
    SparkplugV.n = 0;
    SparkplugV.ok = PROTO_FALSE;
    char *out = SparkplugV.topic_out.out;
    const size_t cap = SparkplugV.topic_out.cap;
    const char *group = SparkplugV.topic.group_id;
    const char *type = SparkplugV.topic.message_type;
    const char *node = SparkplugV.topic.edge_node_id;
    const char *device = SparkplugV.topic.device_id;
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
    SparkplugV.n = p;
    SparkplugV.ok = PROTO_TRUE;
}

// Serialize ns->metrics.list[0] as one Metric message into ns->out, reporting its length in ns->n.
void protocore_sparkplug_build_metric(uint8_t *restrict work)
{
    (void)work;
    SparkplugV.n = 0;
    SparkplugV.ok = PROTO_FALSE;
    if (!SparkplugV.out.buf || !SparkplugV.metrics.list)
    {
        return;
    }
    SparkplugV.n = metric_encode(SPB_SLOT_MSG, SparkplugV.out.buf, SparkplugV.out.cap, &SparkplugV.metrics.list[0]);
    SparkplugV.ok = SparkplugV.n != 0;
}

// Serialize a Payload (Sparkplug 3.0.0 sec 6.4.5): timestamp(1), then metrics(2) once per Metric,
// then seq(3). Each Metric goes into the codec's scratch first; one that overflows it fails the
// whole build closed.
void protocore_sparkplug_build_payload(uint8_t *restrict work)
{
    SparkplugV.n = 0;
    SparkplugV.ok = PROTO_FALSE;
    const size_t count = SparkplugV.metrics.count;
    if (!SparkplugV.out.buf || (count && !SparkplugV.metrics.list))
    {
        return;
    }
    ProtobufV.slot = SPB_SLOT_MSG;
    ProtobufV.writer.buf = SparkplugV.out.buf;
    ProtobufV.writer.cap = SparkplugV.out.cap;
    Protobuf.writer_open(protocore_protobuf_span());
    pb_varint(SPB_SLOT_MSG, SPB_PAYLOAD_TIMESTAMP, SparkplugV.payload.timestamp);
    for (size_t i = 0; i < count; i++)
    {
        size_t mlen = metric_encode(SPB_SLOT_METRIC, SPARKPLUG_CTX(work)->metric, sizeof(SPARKPLUG_CTX(work)->metric),
                                    &SparkplugV.metrics.list[i]);
        if (!mlen)
        {
            return;
        }
        pb_span(SPB_SLOT_MSG, SPB_PAYLOAD_METRICS, SPARKPLUG_CTX(work)->metric, mlen);
    }
    pb_varint(SPB_SLOT_MSG, SPB_PAYLOAD_SEQ, SparkplugV.payload.seq);
    ProtobufV.slot = SPB_SLOT_MSG;
    Protobuf.writer_finish(protocore_protobuf_span());
    SparkplugV.n = ProtobufV.n;
    SparkplugV.ok = SparkplugV.n != 0;
}

// Read a Payload's timestamp(1) and seq(3) from ns->source into ns->header. metrics(2), uuid(4) and
// body(5) are stepped over.
void protocore_sparkplug_parse_payload(uint8_t *restrict work)
{
    (void)work;
    SparkplugV.ok = PROTO_FALSE;
    mem.set(&SparkplugV.header, 0, sizeof(SparkplugV.header));
    if (!SparkplugV.source.buf)
    {
        return;
    }
    const size_t len = SparkplugV.source.len;
    ProtobufV.slot = SPB_SLOT_MSG;
    ProtobufV.source.buf = SparkplugV.source.buf;
    ProtobufV.source.len = len;
    ProtobufV.source.pos = 0;
    Protobuf.reader_open(protocore_protobuf_span());
    size_t pos = 0;
    while (pos < len)
    {
        ProtobufV.slot = SPB_SLOT_MSG;
        Protobuf.read_record(protocore_protobuf_span());
        if (!ProtobufV.ok)
        {
            return;
        }
        pos = ProtobufV.n;
        if (ProtobufV.record.field_number == SPB_PAYLOAD_TIMESTAMP &&
            ProtobufV.record.wire_type == PROTOCORE_PROTOBUF_WT_VARINT)
        {
            SparkplugV.header.has_timestamp = PROTO_TRUE;
            SparkplugV.header.timestamp = ProtobufV.record.value;
        }
        else if (ProtobufV.record.field_number == SPB_PAYLOAD_SEQ &&
                 ProtobufV.record.wire_type == PROTOCORE_PROTOBUF_WT_VARINT)
        {
            SparkplugV.header.has_seq = PROTO_TRUE;
            SparkplugV.header.seq = ProtobufV.record.value;
        }
    }
    SparkplugV.ok = PROTO_TRUE;
}

// Report the next metrics(2) sub-message of a Payload in ns->metric_bytes / ns->metric_len and
// advance ns->source.cursor past it. False at the end of the Payload or on a malformed field.
void protocore_sparkplug_next_metric(uint8_t *restrict work)
{
    (void)work;
    SparkplugV.ok = PROTO_FALSE;
    SparkplugV.metric_bytes = NULL;
    SparkplugV.metric_len = 0;
    if (!SparkplugV.source.buf)
    {
        return;
    }
    const size_t len = SparkplugV.source.len;
    ProtobufV.slot = SPB_SLOT_MSG;
    ProtobufV.source.buf = SparkplugV.source.buf;
    ProtobufV.source.len = len;
    ProtobufV.source.pos = SparkplugV.source.cursor;
    Protobuf.reader_open(protocore_protobuf_span());
    while (SparkplugV.source.cursor < len)
    {
        ProtobufV.slot = SPB_SLOT_MSG;
        Protobuf.read_record(protocore_protobuf_span());
        if (!ProtobufV.ok)
        {
            return;
        }
        SparkplugV.source.cursor = ProtobufV.n;
        if (ProtobufV.record.field_number == SPB_PAYLOAD_METRICS &&
            ProtobufV.record.wire_type == PROTOCORE_PROTOBUF_WT_LEN)
        {
            SparkplugV.metric_bytes = ProtobufV.record.data;
            SparkplugV.metric_len = ProtobufV.record.len;
            SparkplugV.ok = PROTO_TRUE;
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
            ProtobufV.value.u32 = (uint32_t)f->value;
            Protobuf.float_bits(protocore_protobuf_span());
            out->float_value = ProtobufV.f32;
        }
        break;
    case SPB_METRIC_DOUBLE_VALUE:
        if (f->wire_type == PROTOCORE_PROTOBUF_WT_I64)
        {
            out->has_value = PROTO_TRUE;
            out->kind = SPB_M_DOUBLE;
            ProtobufV.value.u64 = f->value;
            Protobuf.double_bits(protocore_protobuf_span());
            out->double_value = ProtobufV.f64;
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
void protocore_sparkplug_parse_metric(uint8_t *restrict work)
{
    (void)work;
    SparkplugV.ok = PROTO_FALSE;
    mem.set(&SparkplugV.metric, 0, sizeof(SparkplugV.metric));
    if (!SparkplugV.source.buf)
    {
        return;
    }
    const size_t len = SparkplugV.source.len;
    ProtobufV.slot = SPB_SLOT_MSG;
    ProtobufV.source.buf = SparkplugV.source.buf;
    ProtobufV.source.len = len;
    ProtobufV.source.pos = 0;
    Protobuf.reader_open(protocore_protobuf_span());
    size_t pos = 0;
    while (pos < len)
    {
        ProtobufV.slot = SPB_SLOT_MSG;
        Protobuf.read_record(protocore_protobuf_span());
        if (!ProtobufV.ok)
        {
            return;
        }
        pos = ProtobufV.n;
        const ProtobufRecord rec = ProtobufV.record;
        spb_apply_meta_field(&SparkplugV.metric, &rec);  // name / alias / timestamp / datatype
        spb_apply_value_field(&SparkplugV.metric, &rec); // the value oneof member
    }
    SparkplugV.ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
SparkplugVars SparkplugV;

#endif // PROTOCORE_ENABLE_SPARKPLUG
