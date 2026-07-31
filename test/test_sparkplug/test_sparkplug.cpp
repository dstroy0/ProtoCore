// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the Sparkplug B codec (services/iot/sparkplug): the topic builder, the Metric
// serializer (exact protobuf bytes), and a Payload round-trip read with the protobuf cursor.
// Field numbers per the Eclipse Tahu sparkplug_b.proto. Pure host tests.

#include "services/iot/protobuf/protobuf.h"
#include "services/iot/sparkplug/sparkplug.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_topic()
{
    char buf[64];
    size_t n = pc_spb_build_topic(buf, sizeof(buf), "group1", "NDATA", "edge1", nullptr);
    TEST_ASSERT_EQUAL_STRING("spBv1.0/group1/NDATA/edge1", buf);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);

    n = pc_spb_build_topic(buf, sizeof(buf), "group1", "DDATA", "edge1", "dev1");
    TEST_ASSERT_EQUAL_STRING("spBv1.0/group1/DDATA/edge1/dev1", buf);
    TEST_ASSERT_GREATER_THAN(0, (int)n);
}

// A double metric "temperature" = 23.5 -> exact Tahu Metric protobuf bytes.
void test_metric_bytes()
{
    SpbMetric m = {};
    m.name = "temperature";
    m.datatype = SPB_DT_DOUBLE;
    m.kind = SpbMetricKind::SPB_M_DOUBLE;
    m.double_value = 23.5;
    uint8_t buf[64];
    size_t n = pc_spb_build_metric(buf, sizeof(buf), &m);
    const uint8_t expect[] = {
        0x0A, 0x0B, 't',  'e',  'm',  'p',  'e',  'r',  'a', 't', 'u', 'r', 'e', // name (field 1)
        0x20, 0x0A,                                                              // datatype (field 4) = 10 (Double)
        0x69, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x37, 0x40                     // double_value (field 13) = 23.5 LE
    };
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

// Build a Payload and walk it back with the protobuf cursor.
void test_payload_round_trip()
{
    SpbMetric m = {};
    m.name = "temperature";
    m.datatype = SPB_DT_DOUBLE;
    m.kind = SpbMetricKind::SPB_M_DOUBLE;
    m.double_value = 23.5;
    uint8_t buf[128];
    size_t n = pc_spb_build_payload(buf, sizeof(buf), 1000, 5, &m, 1);
    TEST_ASSERT_GREATER_THAN(0, (int)n);

    size_t pos = 0;
    PbField f;
    // field 1: timestamp.
    TEST_ASSERT_TRUE(pc_pb_read_field(buf, n, &pos, &f));
    TEST_ASSERT_EQUAL_UINT32(1, f.field_number);
    TEST_ASSERT_EQUAL_UINT64(1000, f.value);
    // field 2: the metric submessage.
    TEST_ASSERT_TRUE(pc_pb_read_field(buf, n, &pos, &f));
    TEST_ASSERT_EQUAL_UINT32(2, f.field_number);
    TEST_ASSERT_EQUAL_UINT8(PB_WT_LEN, f.wire_type);
    const uint8_t *metric = f.data;
    size_t metric_len = f.len;
    // field 3: seq.
    TEST_ASSERT_TRUE(pc_pb_read_field(buf, n, &pos, &f));
    TEST_ASSERT_EQUAL_UINT32(3, f.field_number);
    TEST_ASSERT_EQUAL_UINT64(5, f.value);

    // Walk the metric: name (1), datatype (4), double_value (13).
    size_t mp = 0;
    TEST_ASSERT_TRUE(pc_pb_read_field(metric, metric_len, &mp, &f));
    TEST_ASSERT_EQUAL_UINT32(1, f.field_number);
    TEST_ASSERT_EQUAL_MEMORY("temperature", f.data, f.len);
    TEST_ASSERT_TRUE(pc_pb_read_field(metric, metric_len, &mp, &f));
    TEST_ASSERT_EQUAL_UINT32(4, f.field_number);
    TEST_ASSERT_EQUAL_UINT64(SPB_DT_DOUBLE, f.value);
    TEST_ASSERT_TRUE(pc_pb_read_field(metric, metric_len, &mp, &f));
    TEST_ASSERT_EQUAL_UINT32(13, f.field_number);
    TEST_ASSERT_EQUAL_HEX64(0x4037800000000000ULL, f.value); // bits of 23.5
    TEST_ASSERT_TRUE(pc_pb_double_bits(f.value) == 23.5);
}

void test_metric_int_and_string()
{
    SpbMetric mi = {};
    mi.name = "count";
    mi.datatype = SPB_DT_INT32;
    mi.kind = SpbMetricKind::SPB_M_INT;
    mi.int_value = 42;
    uint8_t buf[64];
    size_t n = pc_spb_build_metric(buf, sizeof(buf), &mi);
    size_t pos = 0;
    PbField f;
    // skip name + datatype, read the int value (field 10).
    pc_pb_read_field(buf, n, &pos, &f); // name
    pc_pb_read_field(buf, n, &pos, &f); // datatype
    TEST_ASSERT_TRUE(pc_pb_read_field(buf, n, &pos, &f));
    TEST_ASSERT_EQUAL_UINT32(10, f.field_number);
    TEST_ASSERT_EQUAL_UINT64(42, f.value);

    SpbMetric ms = {};
    ms.name = "status";
    ms.datatype = SPB_DT_STRING;
    ms.kind = SpbMetricKind::SPB_M_STRING;
    ms.string_value = "ok";
    n = pc_spb_build_metric(buf, sizeof(buf), &ms);
    pos = 0;
    pc_pb_read_field(buf, n, &pos, &f); // name
    pc_pb_read_field(buf, n, &pos, &f); // datatype
    TEST_ASSERT_TRUE(pc_pb_read_field(buf, n, &pos, &f));
    TEST_ASSERT_EQUAL_UINT32(15, f.field_number); // string_value
    TEST_ASSERT_EQUAL_MEMORY("ok", f.data, f.len);
}

// A metric carrying an alias instead of a name (DATA messages after birth).
void test_metric_alias()
{
    SpbMetric m = {};
    m.has_alias = true;
    m.alias = 7;
    m.datatype = SPB_DT_BOOLEAN;
    m.kind = SpbMetricKind::SPB_M_BOOL;
    m.bool_value = true;
    uint8_t buf[32];
    size_t n = pc_spb_build_metric(buf, sizeof(buf), &m);
    size_t pos = 0;
    PbField f;
    TEST_ASSERT_TRUE(pc_pb_read_field(buf, n, &pos, &f));
    TEST_ASSERT_EQUAL_UINT32(2, f.field_number); // alias (no name field)
    TEST_ASSERT_EQUAL_UINT64(7, f.value);
}

void test_overflow_fails_closed()
{
    SpbMetric m = {};
    m.name = "temperature";
    m.datatype = SPB_DT_DOUBLE;
    m.kind = SpbMetricKind::SPB_M_DOUBLE;
    m.double_value = 23.5;
    uint8_t small[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_spb_build_metric(small, sizeof(small), &m));
    char tsmall[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_spb_build_topic(tsmall, sizeof(tsmall), "group1", "NDATA", "edge1", nullptr));
}

// Null-argument guards, the Long/Float metric kinds + timestamp, and the payload
// fail-closed paths (null metrics, and a metric that overflows the per-metric buffer).
void test_spb_error_and_kind_paths()
{
    char tbuf[64];
    TEST_ASSERT_EQUAL_UINT(0, pc_spb_build_topic(nullptr, sizeof(tbuf), "g", "NDATA", "e", nullptr));
    TEST_ASSERT_EQUAL_UINT(0, pc_spb_build_topic(tbuf, sizeof(tbuf), nullptr, "NDATA", "e", nullptr));

    uint8_t buf[256];
    TEST_ASSERT_EQUAL_UINT(0, pc_spb_build_metric(nullptr, sizeof(buf), nullptr));
    TEST_ASSERT_EQUAL_UINT(0, pc_spb_build_metric(buf, sizeof(buf), nullptr));

    SpbMetric ml = {};
    ml.name = "lng";
    ml.has_timestamp = true;
    ml.timestamp = 123;
    ml.datatype = SPB_DT_INT32;
    ml.kind = SpbMetricKind::SPB_M_LONG;
    ml.long_value = 0x1122334455ull;
    TEST_ASSERT_TRUE(pc_spb_build_metric(buf, sizeof(buf), &ml) > 0); // timestamp + Long

    SpbMetric mf = {};
    mf.name = "flt";
    mf.datatype = SPB_DT_DOUBLE;
    mf.kind = SpbMetricKind::SPB_M_FLOAT;
    mf.float_value = 2.5f;
    TEST_ASSERT_TRUE(pc_spb_build_metric(buf, sizeof(buf), &mf) > 0); // Float

    TEST_ASSERT_EQUAL_UINT(0, pc_spb_build_payload(buf, sizeof(buf), 1, 0, nullptr, 2)); // n>0, null metrics

    static char big[PC_SPB_METRIC_MAX + 64];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    SpbMetric ms = {};
    ms.name = "s";
    ms.datatype = SPB_DT_STRING;
    ms.kind = SpbMetricKind::SPB_M_STRING;
    ms.string_value = big;
    TEST_ASSERT_EQUAL_UINT(0, pc_spb_build_payload(buf, sizeof(buf), 1, 0, &ms, 1)); // per-metric overflow
}

// Remaining branch gaps: pc_spb_build_topic's null message_type / null edge_node arms,
// a STRING metric with a null string_value (the value field is omitted, not just skipped
// silently on garbage), the metric-kind switch's no-match fallthrough (an out-of-range
// SpbMetricKind - never produced by this codebase, but the generated branch exists and a
// caller could still hand one in via a raw cast, so it's exercised rather than excluded),
// and pc_spb_build_payload's null-buf and n==0 arms.
void test_spb_more_branch_coverage()
{
    char tbuf[64];
    TEST_ASSERT_EQUAL_UINT(0, pc_spb_build_topic(tbuf, sizeof(tbuf), "g", nullptr, "e", nullptr));
    TEST_ASSERT_EQUAL_UINT(0, pc_spb_build_topic(tbuf, sizeof(tbuf), "g", "NDATA", nullptr, nullptr));

    // STRING kind with no string_value: name + datatype only, no value field.
    SpbMetric ms2 = {};
    ms2.name = "s2";
    ms2.datatype = SPB_DT_STRING;
    ms2.kind = SpbMetricKind::SPB_M_STRING;
    uint8_t buf2[64];
    size_t n2 = pc_spb_build_metric(buf2, sizeof(buf2), &ms2);
    TEST_ASSERT_TRUE(n2 > 0);
    size_t pos2 = 0;
    PbField f2;
    TEST_ASSERT_TRUE(pc_pb_read_field(buf2, n2, &pos2, &f2));
    TEST_ASSERT_EQUAL_UINT32(1, f2.field_number); // name
    TEST_ASSERT_TRUE(pc_pb_read_field(buf2, n2, &pos2, &f2));
    TEST_ASSERT_EQUAL_UINT32(4, f2.field_number);              // datatype
    TEST_ASSERT_FALSE(pc_pb_read_field(buf2, n2, &pos2, &f2)); // no string_value field

    // Out-of-range kind: none of the switch cases match, so again just name + datatype.
    SpbMetric mu = {};
    mu.name = "unk";
    mu.datatype = SPB_DT_INT32;
    mu.kind = (SpbMetricKind)99;
    uint8_t buf3[64];
    size_t n3 = pc_spb_build_metric(buf3, sizeof(buf3), &mu);
    TEST_ASSERT_TRUE(n3 > 0);
    size_t pos3 = 0;
    PbField f3;
    TEST_ASSERT_TRUE(pc_pb_read_field(buf3, n3, &pos3, &f3));
    TEST_ASSERT_EQUAL_UINT32(1, f3.field_number); // name
    TEST_ASSERT_TRUE(pc_pb_read_field(buf3, n3, &pos3, &f3));
    TEST_ASSERT_EQUAL_UINT32(4, f3.field_number);              // datatype
    TEST_ASSERT_FALSE(pc_pb_read_field(buf3, n3, &pos3, &f3)); // no value field

    TEST_ASSERT_EQUAL_UINT(0, pc_spb_build_payload(nullptr, 128, 1, 0, nullptr, 0)); // null buf

    // n == 0: timestamp + seq only, no metrics field.
    uint8_t pbuf[32];
    size_t pn = pc_spb_build_payload(pbuf, sizeof(pbuf), 42, 7, nullptr, 0);
    TEST_ASSERT_TRUE(pn > 0);
    size_t pp = 0;
    PbField pf;
    TEST_ASSERT_TRUE(pc_pb_read_field(pbuf, pn, &pp, &pf));
    TEST_ASSERT_EQUAL_UINT32(1, pf.field_number); // timestamp
    TEST_ASSERT_TRUE(pc_pb_read_field(pbuf, pn, &pp, &pf));
    TEST_ASSERT_EQUAL_UINT32(3, pf.field_number);            // seq
    TEST_ASSERT_FALSE(pc_pb_read_field(pbuf, pn, &pp, &pf)); // no metrics field
}

// Build a two-metric payload, then decode it back with the subscriber-side parser.
void test_decode_payload_and_metrics()
{
    SpbMetric m[2] = {};
    m[0].name = "temperature";
    m[0].has_timestamp = true;
    m[0].timestamp = 12345;
    m[0].datatype = SPB_DT_DOUBLE;
    m[0].kind = SpbMetricKind::SPB_M_DOUBLE;
    m[0].double_value = 23.5;
    m[1].name = "status";
    m[1].has_alias = true;
    m[1].alias = 7;
    m[1].datatype = SPB_DT_STRING;
    m[1].kind = SpbMetricKind::SPB_M_STRING;
    m[1].string_value = "OK";
    uint8_t buf[256];
    size_t n = pc_spb_build_payload(buf, sizeof(buf), 1000, 5, m, 2);
    TEST_ASSERT_GREATER_THAN(0, (int)n);

    SpbPayloadHeader hdr;
    TEST_ASSERT_TRUE(pc_spb_parse_payload(buf, n, &hdr));
    TEST_ASSERT_TRUE(hdr.has_timestamp);
    TEST_ASSERT_EQUAL_UINT64(1000, hdr.timestamp);
    TEST_ASSERT_TRUE(hdr.has_seq);
    TEST_ASSERT_EQUAL_UINT64(5, hdr.seq);

    size_t pos = 0;
    const uint8_t *mb;
    size_t mlen;
    SpbMetricDecoded d;

    // First metric: double "temperature".
    TEST_ASSERT_TRUE(pc_spb_payload_next_metric(buf, n, &pos, &mb, &mlen));
    TEST_ASSERT_TRUE(pc_spb_parse_metric(mb, mlen, &d));
    TEST_ASSERT_EQUAL_size_t(11, d.name_len);
    TEST_ASSERT_EQUAL_MEMORY("temperature", d.name, 11);
    TEST_ASSERT_TRUE(d.has_timestamp);
    TEST_ASSERT_EQUAL_UINT64(12345, d.timestamp);
    TEST_ASSERT_EQUAL_UINT32(SPB_DT_DOUBLE, d.datatype);
    TEST_ASSERT_TRUE(d.has_value);
    TEST_ASSERT_EQUAL_INT((int)SpbMetricKind::SPB_M_DOUBLE, (int)d.kind);
    TEST_ASSERT_TRUE(d.double_value == 23.5);

    // Second metric: string "status" addressed by alias 7.
    TEST_ASSERT_TRUE(pc_spb_payload_next_metric(buf, n, &pos, &mb, &mlen));
    TEST_ASSERT_TRUE(pc_spb_parse_metric(mb, mlen, &d));
    TEST_ASSERT_EQUAL_MEMORY("status", d.name, 6);
    TEST_ASSERT_TRUE(d.has_alias);
    TEST_ASSERT_EQUAL_UINT64(7, d.alias);
    TEST_ASSERT_EQUAL_INT((int)SpbMetricKind::SPB_M_STRING, (int)d.kind);
    TEST_ASSERT_EQUAL_size_t(2, d.string_value_len);
    TEST_ASSERT_EQUAL_MEMORY("OK", d.string_value, 2);

    // No third metric.
    TEST_ASSERT_FALSE(pc_spb_payload_next_metric(buf, n, &pos, &mb, &mlen));

    // Null-argument guards.
    TEST_ASSERT_FALSE(pc_spb_parse_payload(nullptr, n, &hdr));
    TEST_ASSERT_FALSE(pc_spb_parse_metric(nullptr, mlen, &d));
    TEST_ASSERT_FALSE(pc_spb_payload_next_metric(nullptr, n, &pos, &mb, &mlen));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_spb_error_and_kind_paths);
    RUN_TEST(test_decode_payload_and_metrics);
    RUN_TEST(test_topic);
    RUN_TEST(test_metric_bytes);
    RUN_TEST(test_payload_round_trip);
    RUN_TEST(test_metric_int_and_string);
    RUN_TEST(test_metric_alias);
    RUN_TEST(test_overflow_fails_closed);
    RUN_TEST(test_spb_more_branch_coverage);
    return UNITY_END();
}
