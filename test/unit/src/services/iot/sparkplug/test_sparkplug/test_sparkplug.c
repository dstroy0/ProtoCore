// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Sparkplug B codec (services/iot/sparkplug/sparkplug.h).
//
// The load-bearing case is test_metric_wire_octets: Sparkplug 3.0.0 sec 6.4.1 publishes the Metric
// schema with its field numbers, and Google's Protocol Buffers "Encoding" document publishes the
// tag formula `(field_number << 3) | wire_type` and the Base 128 varint. Every octet asserted here
// is spelled out from those two published definitions in the comment above it, so a wrong field
// number or a wrong wire type is caught against the schema rather than against this encoder.
//
// The topic cases quote sec 4.1's structure and the sec 5.2 example topic verbatim.

#include "services/iot/sparkplug/sparkplug.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static char g_topic[128];
static uint8_t g_out[256];

// Join the four sec 4.1 elements and return the buffer.
static const char *topic(const char *group, const char *type, const char *node, const char *device)
{
    SparkplugV.topic.group_id = group;
    SparkplugV.topic.message_type = type;
    SparkplugV.topic.edge_node_id = node;
    SparkplugV.topic.device_id = device;
    SparkplugV.topic_out.out = g_topic;
    SparkplugV.topic_out.cap = sizeof(g_topic);
    Sparkplug.build_topic(protocore_sparkplug_span());
    return g_topic;
}

// Serialize one Metric into g_out and return how many octets it took.
static size_t encode_metric(const SpbMetric *m)
{
    SparkplugV.out.buf = g_out;
    SparkplugV.out.cap = sizeof(g_out);
    SparkplugV.metrics.list = m;
    SparkplugV.metrics.count = 1;
    Sparkplug.build_metric(protocore_sparkplug_span());
    return SparkplugV.n;
}

// Sparkplug 3.0.0 sec 4.1: "namespace/group_id/message_type/edge_node_id/[device_id]", and sec
// 4.1.1 fixes the namespace element to the UTF-8 constant "spBv1.0". sec 5.2 prints the topic
// "spBv1.0/Group1/NBIRTH/EdgeNode1" verbatim.
void test_topic_namespace(void)
{
    TEST_ASSERT_EQUAL_STRING("spBv1.0/Group1/NBIRTH/EdgeNode1", topic("Group1", SPB_MSG_NBIRTH, "EdgeNode1", NULL));
    TEST_ASSERT_TRUE(SparkplugV.ok);
    TEST_ASSERT_EQUAL_UINT(strlen("spBv1.0/Group1/NBIRTH/EdgeNode1"), SparkplugV.n);

    // sec 4.1.5: the optional device_id element is the fifth level, so a Device topic carries it.
    TEST_ASSERT_EQUAL_STRING("spBv1.0/Group1/DBIRTH/EdgeNode1/Device1",
                             topic("Group1", SPB_MSG_DBIRTH, "EdgeNode1", "Device1"));
    TEST_ASSERT_TRUE(SparkplugV.ok);
}

// sec 4.1.3 names nine message_type elements; each one joins into its own topic.
void test_topic_every_message_type(void)
{
    static const char *const TYPE[] = {SPB_MSG_NBIRTH, SPB_MSG_NDEATH, SPB_MSG_DBIRTH, SPB_MSG_DDEATH, SPB_MSG_NDATA,
                                       SPB_MSG_DDATA,  SPB_MSG_NCMD,   SPB_MSG_DCMD,   SPB_MSG_STATE};
    for (size_t i = 0; i < sizeof(TYPE) / sizeof(TYPE[0]); i++)
    {
        const char *t = topic("G", TYPE[i], "N", NULL);
        const size_t len = strlen(TYPE[i]);
        TEST_ASSERT_TRUE_MESSAGE(SparkplugV.ok, TYPE[i]);
        // "spBv1.0/G/" is 10 octets, then the type, then "/N".
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(TYPE[i], t + 10, len, TYPE[i]);
        TEST_ASSERT_EQUAL_CHAR_MESSAGE('/', t[10 + len], TYPE[i]);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(10u + len + 2u, SparkplugV.n, TYPE[i]);
    }
}

// A required element the caller left unset joins nothing rather than a topic with an empty level.
void test_topic_refuses_a_missing_element(void)
{
    (void)topic(NULL, SPB_MSG_NDATA, "N", NULL);
    TEST_ASSERT_FALSE(SparkplugV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, SparkplugV.n);

    (void)topic("G", NULL, "N", NULL);
    TEST_ASSERT_FALSE(SparkplugV.ok);

    (void)topic("G", SPB_MSG_NDATA, NULL, NULL);
    TEST_ASSERT_FALSE(SparkplugV.ok);
}

// A buffer one octet short of the topic and its NUL writes nothing, not a truncated topic: a
// truncated topic names a different Edge Node.
void test_topic_refuses_a_short_buffer(void)
{
    char small[sizeof("spBv1.0/Group1/NBIRTH/EdgeNode1") - 1];
    SparkplugV.topic.group_id = "Group1";
    SparkplugV.topic.message_type = SPB_MSG_NBIRTH;
    SparkplugV.topic.edge_node_id = "EdgeNode1";
    SparkplugV.topic.device_id = NULL;
    SparkplugV.topic_out.out = small;
    SparkplugV.topic_out.cap = sizeof(small);
    Sparkplug.build_topic(protocore_sparkplug_span());
    TEST_ASSERT_FALSE(SparkplugV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, SparkplugV.n);
}

// Sparkplug 3.0.0 sec 6.4.1 Metric: name = 1, datatype = 4, int_value = 10. sec 6.4.16 DataType:
// Int32 = 3. Protocol Buffers "Encoding": tag = (field_number << 3) | wire_type, LEN = 2,
// VARINT = 0, and a LEN record is "the tag, a length varint, then the payload".
//
//   name(1), LEN:      (1 << 3) | 2 = 0x0A, length 4, "temp" = 74 65 6d 70
//   datatype(4),VARINT:(4 << 3) | 0 = 0x20, varint 3  = 03
//   int_value(10),VAR: (10 << 3) | 0 = 0x50, varint 42 = 2a
void test_metric_wire_octets(void)
{
    static const uint8_t WANT[] = {0x0A, 0x04, 0x74, 0x65, 0x6D, 0x70, 0x20, 0x03, 0x50, 0x2A};
    SpbMetric m = {0};
    m.name = "temp";
    m.datatype = SPB_DT_INT32;
    m.kind = SPB_M_INT;
    m.int_value = 42;

    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), encode_metric(&m));
    TEST_ASSERT_TRUE(SparkplugV.ok);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, g_out, sizeof(WANT));
}

// The "Encoding" document's own worked varint: "150 ... 96 01" - two octets, seven payload bits
// each, little-endian, continuation bit set on all but the last. alias is field 2, VARINT, so its
// tag is (2 << 3) | 0 = 0x10.
void test_metric_alias_varint(void)
{
    static const uint8_t WANT[] = {0x10, 0x96, 0x01, 0x20, 0x03, 0x50, 0x00};
    SpbMetric m = {0};
    m.has_alias = PROTO_TRUE;
    m.alias = 150;
    m.datatype = SPB_DT_INT32;
    m.kind = SPB_M_INT;
    m.int_value = 0;

    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), encode_metric(&m));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, g_out, sizeof(WANT));
}

// string_value is field 15, LEN: (15 << 3) | 2 = 0x7A. The "Encoding" document's own LEN example
// carries "testing" as the length 07 followed by 74 65 73 74 69 6e 67. sec 6.4.16 String = 12.
void test_metric_string_value(void)
{
    static const uint8_t WANT[] = {0x20, 0x0C, 0x7A, 0x07, 0x74, 0x65, 0x73, 0x74, 0x69, 0x6E, 0x67};
    SpbMetric m = {0};
    m.datatype = SPB_DT_STRING;
    m.kind = SPB_M_STRING;
    m.string_value = "testing";

    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), encode_metric(&m));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, g_out, sizeof(WANT));
}

// float_value is field 12 with wire type I32 = 5, so its tag is (12 << 3) | 5 = 0x65; double_value
// is field 13 with I64 = 1, tag (13 << 3) | 1 = 0x69. The payloads are the IEEE 754 patterns
// least significant octet first: 1.0f is 0x3F800000 and 1.0 is 0x3FF0000000000000.
void test_metric_float_and_double_payloads(void)
{
    static const uint8_t WANT_F[] = {0x20, 0x09, 0x65, 0x00, 0x00, 0x80, 0x3F};
    static const uint8_t WANT_D[] = {0x20, 0x0A, 0x69, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F};

    SpbMetric f = {0};
    f.datatype = SPB_DT_FLOAT;
    f.kind = SPB_M_FLOAT;
    f.float_value = 1.0f;
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT_F), encode_metric(&f));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT_F, g_out, sizeof(WANT_F));

    SpbMetric d = {0};
    d.datatype = SPB_DT_DOUBLE;
    d.kind = SPB_M_DOUBLE;
    d.double_value = 1.0;
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT_D), encode_metric(&d));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT_D, g_out, sizeof(WANT_D));
}

// boolean_value is field 14, VARINT: tag (14 << 3) | 0 = 0x70, payload 1 or 0. sec 6.4.16
// Boolean = 11.
void test_metric_boolean_payload(void)
{
    static const uint8_t WANT_T[] = {0x20, 0x0B, 0x70, 0x01};
    static const uint8_t WANT_F[] = {0x20, 0x0B, 0x70, 0x00};
    SpbMetric m = {0};
    m.datatype = SPB_DT_BOOLEAN;
    m.kind = SPB_M_BOOL;

    m.bool_value = PROTO_TRUE;
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT_T), encode_metric(&m));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT_T, g_out, sizeof(WANT_T));

    m.bool_value = PROTO_FALSE;
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT_F), encode_metric(&m));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT_F, g_out, sizeof(WANT_F));
}

// sec 6.4.1 Payload: timestamp = 1 (VARINT, tag 0x08), metrics = 2 (repeated, LEN, tag 0x12),
// seq = 3 (VARINT, tag 0x18). The metrics record wraps the ten octets test_metric_wire_octets
// derives, so the whole Payload is 2 + 2 + 10 + 2 = 16 octets.
void test_payload_wire_octets(void)
{
    static const uint8_t WANT[] = {0x08, 0x01, 0x12, 0x0A, 0x0A, 0x04, 0x74, 0x65,
                                   0x6D, 0x70, 0x20, 0x03, 0x50, 0x2A, 0x18, 0x00};
    SpbMetric m = {0};
    m.name = "temp";
    m.datatype = SPB_DT_INT32;
    m.kind = SPB_M_INT;
    m.int_value = 42;

    SparkplugV.out.buf = g_out;
    SparkplugV.out.cap = sizeof(g_out);
    SparkplugV.payload.timestamp = 1;
    SparkplugV.payload.seq = 0;
    SparkplugV.metrics.list = &m;
    SparkplugV.metrics.count = 1;
    Sparkplug.build_payload(protocore_sparkplug_span());

    TEST_ASSERT_TRUE(SparkplugV.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), SparkplugV.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, g_out, sizeof(WANT));
}

// Build a Payload, then read it back: the header fields, then each metrics(2) sub-message, then
// the Metric inside it. Whatever the encoder emits, the decoder must name the same values.
void test_payload_round_trip(void)
{
    SpbMetric list[2] = {{0}, {0}};
    list[0].name = "a";
    list[0].datatype = SPB_DT_INT32;
    list[0].kind = SPB_M_INT;
    list[0].int_value = 7;
    list[1].name = "b";
    list[1].has_alias = PROTO_TRUE;
    list[1].alias = 9;
    list[1].datatype = SPB_DT_STRING;
    list[1].kind = SPB_M_STRING;
    list[1].string_value = "hi";

    SparkplugV.out.buf = g_out;
    SparkplugV.out.cap = sizeof(g_out);
    SparkplugV.payload.timestamp = 1700000000000ull; // milliseconds since epoch, UTC (sec 6.4.5)
    SparkplugV.payload.seq = 255;                    // sec 6.4.5: 0..255, wrapping to zero
    SparkplugV.metrics.list = list;
    SparkplugV.metrics.count = 2;
    Sparkplug.build_payload(protocore_sparkplug_span());
    TEST_ASSERT_TRUE(SparkplugV.ok);
    const size_t len = SparkplugV.n;

    SparkplugV.source.buf = g_out;
    SparkplugV.source.len = len;
    SparkplugV.source.cursor = 0;
    Sparkplug.parse_payload(protocore_sparkplug_span());
    TEST_ASSERT_TRUE(SparkplugV.ok);
    TEST_ASSERT_TRUE(SparkplugV.header.has_timestamp);
    TEST_ASSERT_EQUAL_UINT64(1700000000000ull, SparkplugV.header.timestamp);
    TEST_ASSERT_TRUE(SparkplugV.header.has_seq);
    TEST_ASSERT_EQUAL_UINT64(255u, SparkplugV.header.seq);

    // metric 0
    SparkplugV.source.cursor = 0;
    Sparkplug.next_metric(protocore_sparkplug_span());
    TEST_ASSERT_TRUE(SparkplugV.ok);
    const uint8_t *m0 = SparkplugV.metric_bytes;
    const size_t m0len = SparkplugV.metric_len;
    const size_t after0 = SparkplugV.source.cursor;

    SparkplugV.source.buf = m0;
    SparkplugV.source.len = m0len;
    Sparkplug.parse_metric(protocore_sparkplug_span());
    TEST_ASSERT_TRUE(SparkplugV.ok);
    TEST_ASSERT_EQUAL_UINT(1u, SparkplugV.metric.name_len);
    TEST_ASSERT_EQUAL_CHAR('a', SparkplugV.metric.name[0]);
    TEST_ASSERT_EQUAL_UINT32(SPB_DT_INT32, SparkplugV.metric.datatype);
    TEST_ASSERT_TRUE(SparkplugV.metric.has_value);
    TEST_ASSERT_EQUAL_INT(SPB_M_INT, SparkplugV.metric.kind);
    TEST_ASSERT_EQUAL_UINT32(7u, SparkplugV.metric.int_value);

    // metric 1, resuming the walk where the first one left the cursor
    SparkplugV.source.buf = g_out;
    SparkplugV.source.len = len;
    SparkplugV.source.cursor = after0;
    Sparkplug.next_metric(protocore_sparkplug_span());
    TEST_ASSERT_TRUE(SparkplugV.ok);
    const uint8_t *m1 = SparkplugV.metric_bytes;
    const size_t m1len = SparkplugV.metric_len;
    const size_t after1 = SparkplugV.source.cursor;

    SparkplugV.source.buf = m1;
    SparkplugV.source.len = m1len;
    Sparkplug.parse_metric(protocore_sparkplug_span());
    TEST_ASSERT_TRUE(SparkplugV.ok);
    TEST_ASSERT_TRUE(SparkplugV.metric.has_alias);
    TEST_ASSERT_EQUAL_UINT64(9u, SparkplugV.metric.alias);
    TEST_ASSERT_EQUAL_INT(SPB_M_STRING, SparkplugV.metric.kind);
    TEST_ASSERT_EQUAL_UINT(2u, SparkplugV.metric.string_value_len);
    TEST_ASSERT_EQUAL_MEMORY("hi", SparkplugV.metric.string_value, 2);

    // Two metrics were written, so the third walk finds none.
    SparkplugV.source.buf = g_out;
    SparkplugV.source.len = len;
    SparkplugV.source.cursor = after1;
    Sparkplug.next_metric(protocore_sparkplug_span());
    TEST_ASSERT_FALSE(SparkplugV.ok);
    TEST_ASSERT_NULL(SparkplugV.metric_bytes);
}

// The decoded name and string_value slice the source rather than copying it: sparkplug.h states
// they point INTO the octets handed in.
void test_decoded_strings_point_into_the_source(void)
{
    SpbMetric m = {0};
    m.name = "temp";
    m.datatype = SPB_DT_INT32;
    m.kind = SPB_M_INT;
    m.int_value = 42;
    const size_t len = encode_metric(&m);

    SparkplugV.source.buf = g_out;
    SparkplugV.source.len = len;
    Sparkplug.parse_metric(protocore_sparkplug_span());
    TEST_ASSERT_TRUE(SparkplugV.ok);
    TEST_ASSERT_TRUE(SparkplugV.metric.name >= (const char *)g_out);
    TEST_ASSERT_TRUE(SparkplugV.metric.name + SparkplugV.metric.name_len <= (const char *)g_out + len);
}

// A Payload with no fields at all decodes: the header reports both optional fields absent rather
// than a stale value.
void test_parse_reports_absent_header_fields(void)
{
    static const uint8_t EMPTY[1] = {0};
    SparkplugV.source.buf = EMPTY;
    SparkplugV.source.len = 0;
    Sparkplug.parse_payload(protocore_sparkplug_span());
    TEST_ASSERT_TRUE(SparkplugV.ok);
    TEST_ASSERT_FALSE(SparkplugV.header.has_timestamp);
    TEST_ASSERT_FALSE(SparkplugV.header.has_seq);
    TEST_ASSERT_EQUAL_UINT64(0u, SparkplugV.header.timestamp);
}

// A varint whose last octet still carries the continuation bit runs off the end of the buffer, so
// the record cannot be read and the parse fails rather than reporting half a value.
void test_parse_rejects_a_truncated_varint(void)
{
    static const uint8_t BAD[] = {0x08, 0x80}; // timestamp(1) VARINT, continuation set, nothing after
    SparkplugV.source.buf = BAD;
    SparkplugV.source.len = sizeof(BAD);
    Sparkplug.parse_payload(protocore_sparkplug_span());
    TEST_ASSERT_FALSE(SparkplugV.ok);
}

// A LEN record whose length prefix runs past the buffer is refused, so a metrics(2) sub-message can
// never be reported outside the octets the caller handed in.
void test_next_metric_rejects_an_overlong_length(void)
{
    static const uint8_t BAD[] = {0x12, 0x7F, 0x00}; // metrics(2) LEN, length 127, one octet present
    SparkplugV.source.buf = BAD;
    SparkplugV.source.len = sizeof(BAD);
    SparkplugV.source.cursor = 0;
    Sparkplug.next_metric(protocore_sparkplug_span());
    TEST_ASSERT_FALSE(SparkplugV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, SparkplugV.metric_len);
}

// A buffer too small for the encoded Payload reports nothing written, so a partial protobuf message
// never reaches a broker.
void test_build_refuses_a_short_buffer(void)
{
    SpbMetric m = {0};
    m.name = "temp";
    m.datatype = SPB_DT_INT32;
    m.kind = SPB_M_INT;
    m.int_value = 42;

    uint8_t small[8];
    SparkplugV.out.buf = small;
    SparkplugV.out.cap = sizeof(small);
    SparkplugV.payload.timestamp = 1;
    SparkplugV.payload.seq = 0;
    SparkplugV.metrics.list = &m;
    SparkplugV.metrics.count = 1;
    Sparkplug.build_payload(protocore_sparkplug_span());
    TEST_ASSERT_FALSE(SparkplugV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, SparkplugV.n);
}

// A null destination is reported, not written through.
void test_build_refuses_a_null_buffer(void)
{
    SpbMetric m = {0};
    m.datatype = SPB_DT_INT32;
    m.kind = SPB_M_INT;

    SparkplugV.out.buf = NULL;
    SparkplugV.out.cap = 0;
    SparkplugV.metrics.list = &m;
    SparkplugV.metrics.count = 1;
    Sparkplug.build_metric(protocore_sparkplug_span());
    TEST_ASSERT_FALSE(SparkplugV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, SparkplugV.n);

    Sparkplug.build_payload(protocore_sparkplug_span());
    TEST_ASSERT_FALSE(SparkplugV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, SparkplugV.n);
}

// A Payload with no Metrics still carries its header: timestamp(1) then seq(3), 4 octets.
void test_payload_with_no_metrics(void)
{
    static const uint8_t WANT[] = {0x08, 0x02, 0x18, 0x03};
    SparkplugV.out.buf = g_out;
    SparkplugV.out.cap = sizeof(g_out);
    SparkplugV.payload.timestamp = 2;
    SparkplugV.payload.seq = 3;
    SparkplugV.metrics.list = NULL;
    SparkplugV.metrics.count = 0;
    Sparkplug.build_payload(protocore_sparkplug_span());
    TEST_ASSERT_TRUE(SparkplugV.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), SparkplugV.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, g_out, sizeof(WANT));
}
