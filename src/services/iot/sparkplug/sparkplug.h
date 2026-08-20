// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sparkplug.h
 * @brief Sparkplug B topic and payload codec (PROTOCORE_ENABLE_SPARKPLUG) - a zero-heap builder and
 *        reader for the Sparkplug B topic namespace and its Google Protocol Buffers payload.
 *
 * **The governing standard is not IETF.** Sparkplug is an Eclipse Foundation specification:
 * Sparkplug 3.0.0, Eclipse Sparkplug Contributors. It rides MQTT, which is an OASIS standard -
 * Sparkplug 3.0.0 sec 1.5 (Normative References) names MQTT Version 3.1.1 Plus Errata 01, OASIS
 * Standard Incorporating Approved Errata 01, and MQTT Version 5.0, OASIS Standard. No RFC governs
 * either one, and none is cited here.
 *
 * Sparkplug 3.0.0 sec 4.1 gives the topic:
 *
 *     namespace/group_id/message_type/edge_node_id/[device_id]
 *
 * The namespace element is the UTF-8 constant `spBv1.0` (sec 4.1.1). group_id (sec 4.1.2),
 * edge_node_id (sec 4.1.4) and device_id (sec 4.1.5) are UTF-8 excluding the reserved `+`, `/` and
 * `#`. sec 4.1.3 defines nine message_type values. sec 4.1.5 states device_id MUST be included with
 * DBIRTH, DDEATH, DDATA and DCMD, and MUST NOT be included with NBIRTH, NDEATH, NDATA, NCMD and
 * STATE. The separator between elements is the MQTT topic level separator (MQTT Version 5.0
 * sec 4.7.1.1).
 *
 * Sparkplug 3.0.0 sec 6.4.1 (Google Protocol Buffer Schema) gives the payload. Payload carries
 * timestamp(1), metrics(2, repeated), seq(3), uuid(4) and body(5). Each Metric carries name(1),
 * alias(2), timestamp(3), datatype(4), is_historical(5), is_transient(6), is_null(7), metadata(8),
 * properties(9), and one member of the value oneof - int_value(10), long_value(11), float_value(12),
 * double_value(13), boolean_value(14), string_value(15), bytes_value(16), dataset_value(17),
 * template_value(18), extension_value(19).
 *
 * This codec covers name, alias, timestamp, datatype and the first six value members. is_historical,
 * is_transient, is_null, metadata, properties, bytes_value, dataset_value, template_value and
 * extension_value are neither written nor reported.
 *
 * Built on the Protocol Buffers codec (services/iot/protobuf) and published with the MQTT client.
 *
 * The module exports one symbol, @ref Sparkplug. Everything in sparkplug.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SPARKPLUG_H
#define PROTOCORE_SPARKPLUG_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_SPARKPLUG

PROTOCORE_BEGIN_DECLS

/** @brief Sparkplug 3.0.0 sec 4.1.1: the namespace element for the Sparkplug B payload definition. */
#define SPB_NAMESPACE "spBv1.0"

// Sparkplug 3.0.0 sec 4.1.3 message_type elements.
#define SPB_MSG_NBIRTH "NBIRTH" ///< birth certificate for Sparkplug Edge Nodes
#define SPB_MSG_NDEATH "NDEATH" ///< death certificate for Sparkplug Edge Nodes
#define SPB_MSG_DBIRTH "DBIRTH" ///< birth certificate for Devices
#define SPB_MSG_DDEATH "DDEATH" ///< death certificate for Devices
#define SPB_MSG_NDATA "NDATA"   ///< Edge Node data message
#define SPB_MSG_DDATA "DDATA"   ///< Device data message
#define SPB_MSG_NCMD "NCMD"     ///< Edge Node command message
#define SPB_MSG_DCMD "DCMD"     ///< Device command message
#define SPB_MSG_STATE "STATE"   ///< Sparkplug Host Application state message

// Sparkplug 3.0.0 sec 6.4.16 DataType, the codes Metric.datatype takes.
#define SPB_DT_UNKNOWN 0
#define SPB_DT_INT8 1
#define SPB_DT_INT16 2
#define SPB_DT_INT32 3
#define SPB_DT_INT64 4
#define SPB_DT_UINT8 5
#define SPB_DT_UINT16 6
#define SPB_DT_UINT32 7
#define SPB_DT_UINT64 8
#define SPB_DT_FLOAT 9
#define SPB_DT_DOUBLE 10
#define SPB_DT_BOOLEAN 11
#define SPB_DT_STRING 12
#define SPB_DT_DATETIME 13
#define SPB_DT_TEXT 14
#define SPB_DT_UUID 15
#define SPB_DT_DATASET 16
#define SPB_DT_BYTES 17
#define SPB_DT_FILE 18
#define SPB_DT_TEMPLATE 19
#define SPB_DT_PROPERTYSET 20
#define SPB_DT_PROPERTYSETLIST 21
#define SPB_DT_INT8_ARRAY 22
#define SPB_DT_INT16_ARRAY 23
#define SPB_DT_INT32_ARRAY 24
#define SPB_DT_INT64_ARRAY 25
#define SPB_DT_UINT8_ARRAY 26
#define SPB_DT_UINT16_ARRAY 27
#define SPB_DT_UINT32_ARRAY 28
#define SPB_DT_UINT64_ARRAY 29
#define SPB_DT_FLOAT_ARRAY 30
#define SPB_DT_DOUBLE_ARRAY 31
#define SPB_DT_BOOLEAN_ARRAY 32
#define SPB_DT_STRING_ARRAY 33
#define SPB_DT_DATETIME_ARRAY 34

/** @brief Which member of the Metric value oneof carries the value (Sparkplug 3.0.0 sec 6.4.1). */
typedef enum PROTO_ENUM_PACKED
{
    SPB_M_INT,    ///< int_value, field 10, uint32
    SPB_M_LONG,   ///< long_value, field 11, uint64
    SPB_M_FLOAT,  ///< float_value, field 12
    SPB_M_DOUBLE, ///< double_value, field 13
    SPB_M_BOOL,   ///< boolean_value, field 14
    SPB_M_STRING, ///< string_value, field 15
} SpbMetricKind;

/** @brief One Metric to encode (Sparkplug 3.0.0 sec 6.4.6). A null name and a false has_* omit the field. */
typedef struct
{
    const char *name; ///< name, field 1; omit on a DATA metric addressed by alias
    proto_bool has_alias;
    uint64_t alias; ///< alias, field 2
    proto_bool has_timestamp;
    uint64_t timestamp;       ///< timestamp, field 3, milliseconds since epoch in UTC
    uint32_t datatype;        ///< datatype, field 4, an SPB_DT_* code
    SpbMetricKind kind;       ///< which value oneof member below is written
    uint32_t int_value;       ///< int_value, field 10
    uint64_t long_value;      ///< long_value, field 11
    float float_value;        ///< float_value, field 12
    double double_value;      ///< double_value, field 13
    proto_bool bool_value;    ///< boolean_value, field 14
    const char *string_value; ///< string_value, field 15
} SpbMetric;

/** @brief A decoded Payload's top-level fields (Sparkplug 3.0.0 sec 6.4.5); metrics are iterated separately. */
typedef struct
{
    proto_bool has_timestamp;
    uint64_t timestamp; ///< timestamp, field 1, milliseconds since epoch in UTC
    proto_bool has_seq;
    uint64_t seq; ///< seq, field 3
} SpbPayloadHeader;

/** @brief A decoded Metric (Sparkplug 3.0.0 sec 6.4.6). name and string_value point INTO the source, un-terminated. */
typedef struct
{
    const char *name; ///< name, field 1, or nullptr when the metric is addressed by alias
    size_t name_len;
    proto_bool has_alias;
    uint64_t alias; ///< alias, field 2
    proto_bool has_timestamp;
    uint64_t timestamp;       ///< timestamp, field 3
    uint32_t datatype;        ///< datatype, field 4, an SPB_DT_* code
    proto_bool has_value;     ///< false when no value oneof member was present
    SpbMetricKind kind;       ///< which value member is set, valid when @ref has_value
    uint32_t int_value;       ///< int_value, field 10
    uint64_t long_value;      ///< long_value, field 11
    float float_value;        ///< float_value, field 12
    double double_value;      ///< double_value, field 13
    proto_bool bool_value;    ///< boolean_value, field 14
    const char *string_value; ///< string_value bytes, field 15, un-terminated, or nullptr
    size_t string_value_len;
} SpbMetricDecoded;

/** @brief Sparkplug 3.0.0 sec 4.1 topic namespace elements, less the fixed namespace element. */
typedef struct
{
    const char *group_id;     ///< group_id (sec 4.1.2)
    const char *message_type; ///< message_type (sec 4.1.3), one of the SPB_MSG_* constants
    const char *edge_node_id; ///< edge_node_id (sec 4.1.4)
    const char *device_id;    ///< device_id (sec 4.1.5); NULL for an Edge Node topic
} SpbTopicArgs;

/** @brief Where a built topic string lands. */
typedef struct
{
    char *out;  ///< the buffer a topic build writes into
    size_t cap; ///< how much room it has, the NUL included
} SpbTopicOutArgs;

/** @brief Where encoded Protocol Buffers octets land. */
typedef struct
{
    uint8_t *buf; ///< the buffer a Payload or Metric build writes into
    size_t cap;   ///< how many octets it holds
} SpbOutArgs;

/** @brief The Payload header fields a build stamps (Sparkplug 3.0.0 sec 6.4.5). */
typedef struct
{
    uint64_t timestamp; ///< timestamp, field 1, milliseconds since epoch; MUST be UTC
    uint64_t seq;       ///< seq, field 3; 0..255, incrementing by one and wrapping to zero
} SpbPayloadArgs;

/** @brief The Metrics a build serializes (Sparkplug 3.0.0 sec 6.4.6). */
typedef struct
{
    const SpbMetric *list; ///< the Metric array; a Metric build serializes list[0]
    size_t count;          ///< how many of them a Payload build writes
} SpbMetricsArgs;

/** @brief The octets a decode reads, and the cursor an iteration carries across calls. */
typedef struct
{
    const uint8_t *buf; ///< the encoded Payload or Metric being read
    size_t len;         ///< how many octets it holds
    size_t cursor;      ///< the metrics iteration position; set 0 to start, advanced by each call
} SpbSourceArgs;

/**
 * @brief The Sparkplug B codec: the sec 4.1 topic namespace and the sec 6.4.1 payload schema.
 *
 * A caller sets the members a call takes, invokes it through ::Sparkplug, and reads the outcome off
 * the same handle.
 *
 * No slot member: the codec holds no rows, so no call names one.
 *
 * @var SparkplugNs::topic         the topic namespace elements a topic build joins (sec 4.1)
 * @var SparkplugNs::topic_out     the buffer a topic build writes into
 * @var SparkplugNs::out           the buffer a Payload or Metric build writes into
 * @var SparkplugNs::payload       the Payload header fields a build stamps (sec 6.4.5)
 * @var SparkplugNs::metrics       the Metrics a build serializes (sec 6.4.6)
 * @var SparkplugNs::source        the octets a decode reads, and its metrics cursor
 * @var SparkplugNs::ok            a call's true/false outcome; a build reports whether it wrote anything
 * @var SparkplugNs::n             the length a build wrote, 0 if it did not fit
 * @var SparkplugNs::header        the Payload header a parse decoded (sec 6.4.5)
 * @var SparkplugNs::metric        the Metric a parse decoded (sec 6.4.6)
 * @var SparkplugNs::metric_bytes  the next Metric sub-message an iteration reports, pointing into @c source
 * @var SparkplugNs::metric_len    how many octets that sub-message holds
 * @var SparkplugNs::build_topic   join `spBv1.0/group_id/message_type/edge_node_id[/device_id]` into @c topic_out
 * @var SparkplugNs::build_metric  serialize @c metrics.list[0] as one Metric message into @c out
 * @var SparkplugNs::build_payload serialize @c payload plus @c metrics.count Metrics as one Payload into @c out
 * @var SparkplugNs::parse_payload read a Payload's timestamp and seq from @c source into @c header
 * @var SparkplugNs::next_metric   report the next metrics(2) sub-message of a Payload and advance @c source.cursor
 * @var SparkplugNs::parse_metric  decode the Metric in @c source into @c metric
 */
typedef struct
{
    SpbTopicArgs topic;        ///< what a topic says
    SpbTopicOutArgs topic_out; ///< where that topic lands
    SpbOutArgs out;            ///< where encoded octets land
    SpbPayloadArgs payload;    ///< what a Payload header says
    SpbMetricsArgs metrics;    ///< what a build serializes
    SpbSourceArgs source;      ///< what a decode reads
    proto_bool ok;
    size_t n;
    SpbPayloadHeader header;
    SpbMetricDecoded metric;
    const uint8_t *metric_bytes;
    size_t metric_len;
} SparkplugVars;

/** @brief The operands and the outcome. */
extern SparkplugVars SparkplugV;

/** @brief The entries. */
typedef struct
{
    void (*const build_topic)(uint8_t *restrict work);
    void (*const build_metric)(uint8_t *restrict work);
    void (*const build_payload)(uint8_t *restrict work);
    void (*const parse_payload)(uint8_t *restrict work);
    void (*const next_metric)(uint8_t *restrict work);
    void (*const parse_metric)(uint8_t *restrict work);
} SparkplugNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in SparkplugV or a region of the borrow at a fixed offset.
void protocore_sparkplug_build_topic(uint8_t *restrict work);
void protocore_sparkplug_build_metric(uint8_t *restrict work);
void protocore_sparkplug_build_payload(uint8_t *restrict work);
void protocore_sparkplug_parse_payload(uint8_t *restrict work);
void protocore_sparkplug_next_metric(uint8_t *restrict work);
void protocore_sparkplug_parse_metric(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Sparkplug.build_topic(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const SparkplugNs Sparkplug __attribute__((unused)) = {
    .build_topic = protocore_sparkplug_build_topic,
    .build_metric = protocore_sparkplug_build_metric,
    .build_payload = protocore_sparkplug_build_payload,
    .parse_payload = protocore_sparkplug_parse_payload,
    .next_metric = protocore_sparkplug_next_metric,
    .parse_metric = protocore_sparkplug_parse_metric,
};

/**
 * @brief The PROTOCORE_SPARKPLUG_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_sparkplug_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SPARKPLUG

#endif // PROTOCORE_SPARKPLUG_H
