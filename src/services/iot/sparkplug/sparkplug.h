// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sparkplug.h
 * @brief Sparkplug B payload + topic codec (PC_ENABLE_SPARKPLUG) - zero-heap builder for
 *        the Eclipse Sparkplug B industrial-IoT MQTT payload (a Protobuf message) and its
 *        topic namespace. Builds on the Protobuf codec (services/iot/protobuf) and is published
 *        with the MQTT client.
 *
 * Topic: `spBv1.0/<group_id>/<message_type>/<edge_node_id>[/<device_id>]`, where message_type
 * is NBIRTH / NDEATH / NDATA / DBIRTH / DDEATH / DDATA / STATE.
 *
 * Payload (Tahu `Payload` protobuf): timestamp(1), metrics(2, repeated), seq(3), uuid(4),
 * body(5). Each Metric: name(1), alias(2), timestamp(3), datatype(4), and a value in the
 * oneof - int_value(10) / long_value(11) / float_value(12) / double_value(13) /
 * boolean_value(14) / string_value(15). Field numbers + datatype codes verified against the
 * Eclipse Tahu sparkplug_b.proto.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SPARKPLUG_H
#define PROTOCORE_SPARKPLUG_H

#include "protocore_config.h"

#if PC_ENABLE_SPARKPLUG

#include <stddef.h>
#include <stdint.h>

// Sparkplug B data type codes (a subset; Tahu DataType enum).
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

/** @brief Which value the metric carries (selects the Tahu Metric value-oneof field). */
enum class SpbMetricKind : uint8_t
{
    SPB_M_INT,    ///< int_value (field 10, uint32)
    SPB_M_LONG,   ///< long_value (field 11, uint64)
    SPB_M_FLOAT,  ///< float_value (field 12)
    SPB_M_DOUBLE, ///< double_value (field 13)
    SPB_M_BOOL,   ///< boolean_value (field 14)
    SPB_M_STRING, ///< string_value (field 15)
};

/** @brief One Sparkplug B metric. nullptr name / has_* false fields are omitted. */
struct SpbMetric
{
    const char *name; ///< metric name (omit on DATA when using an alias)
    bool has_alias;
    uint64_t alias;
    bool has_timestamp;
    uint64_t timestamp;
    uint32_t datatype; ///< SPB_DT_*
    SpbMetricKind kind;
    uint32_t int_value;
    uint64_t long_value;
    float float_value;
    double double_value;
    bool bool_value;
    const char *string_value;
};

/** @brief Build the `spBv1.0/...` topic. @p device may be null for a node-level topic. */
size_t pc_spb_build_topic(char *buf, size_t cap, const char *group, const char *message_type, const char *edge_node,
                          const char *device);

/** @brief Serialize one Metric (a Tahu Metric protobuf message). Returns length, or 0. */
size_t pc_spb_build_metric(uint8_t *buf, size_t cap, const SpbMetric *m);

/** @brief Serialize a Payload: timestamp + the @p n metrics + seq. Returns length, or 0. */
size_t pc_spb_build_payload(uint8_t *buf, size_t cap, uint64_t timestamp, uint64_t seq, const SpbMetric *metrics,
                            size_t n);

// ---- decoding (the subscriber side, built on the protobuf reader) ----

/** @brief The top-level fields of a decoded Sparkplug B Payload; metrics are iterated separately. */
struct SpbPayloadHeader
{
    bool has_timestamp;
    uint64_t timestamp;
    bool has_seq;
    uint64_t seq; ///< Sparkplug sequence number
};

/**
 * @brief Parse a Sparkplug B Payload's top-level fields: the timestamp (field 1) and sequence number
 *        (field 3). The repeated metrics (field 2) are read with pc_spb_payload_next_metric.
 * @return true iff the protobuf parses without truncation; false otherwise.
 */
bool pc_spb_parse_payload(const uint8_t *buf, size_t len, SpbPayloadHeader *out);

/**
 * @brief Iterate the metric sub-messages (field 2) of a Payload. Start @p pos at 0; each call points
 *        @p metric / @p metric_len at the next metric's protobuf bytes and advances @p pos.
 * @return true while a metric remains; false at the end of the payload / on a malformed field.
 */
bool pc_spb_payload_next_metric(const uint8_t *buf, size_t len, size_t *pos, const uint8_t **metric,
                                size_t *metric_len);

/** @brief A decoded Sparkplug B Metric. The name / string_value point INTO the buffer (NOT NUL-terminated). */
struct SpbMetricDecoded
{
    const char *name; ///< metric name, or nullptr if omitted (a DATA metric addressed by alias)
    size_t name_len;
    bool has_alias;
    uint64_t alias;
    bool has_timestamp;
    uint64_t timestamp;
    uint32_t datatype;  ///< SPB_DT_*
    bool has_value;     ///< false if no value oneof field was present
    SpbMetricKind kind; ///< which value member is set (valid when @ref has_value)
    uint32_t int_value;
    uint64_t long_value;
    float float_value;
    double double_value;
    bool bool_value;
    const char *string_value; ///< string_value bytes (NOT NUL-terminated), or nullptr
    size_t string_value_len;
};

/**
 * @brief Decode a Metric message (a slice from pc_spb_payload_next_metric) into @p out: the name, alias,
 *        timestamp, datatype, and the value oneof (int / long / float / double / boolean / string).
 * @return true iff the protobuf parses without truncation; false otherwise.
 */
bool pc_spb_parse_metric(const uint8_t *buf, size_t len, SpbMetricDecoded *out);

#endif // PC_ENABLE_SPARKPLUG

#endif // PROTOCORE_SPARKPLUG_H
