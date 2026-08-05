// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the SenML (RFC 8428) pack builders (services/iot/senml): SenML-JSON (exact
// string assertions, anchored on the RFC example) and SenML-CBOR (read back with the CBOR
// reader). Pure host tests.

#include "network_drivers/presentation/codec/cbor/cbor.h"
#include "services/iot/senml/senml.h"
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// The RFC 8428 single-record example.
void test_json_canonical()
{
    SenmlRecord r = {0};
    r.base_name = "urn:dev:ow:10e2073a01080063";
    r.unit = "Cel";
    r.value_kind = SENML_V_FLOAT;
    r.value = 23.1;
    char buf[128];
    size_t n = pc_senml_json_build(buf, sizeof(buf), &r, 1);
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_EQUAL_STRING("[{\"bn\":\"urn:dev:ow:10e2073a01080063\",\"u\":\"Cel\",\"v\":23.1}]", buf);
}

// A multi-record pack: a base name on the first record, plain names after.
void test_json_multi_record()
{
    SenmlRecord r[2] = {0};
    r[0].base_name = "urn:dev:ow:1;";
    r[0].name = "voltage";
    r[0].unit = "V";
    r[0].value_kind = SENML_V_FLOAT;
    r[0].value = 120.1;
    r[1].name = "current";
    r[1].unit = "A";
    r[1].value_kind = SENML_V_FLOAT;
    r[1].value = 1.25;
    char buf[160];
    size_t n = pc_senml_json_build(buf, sizeof(buf), r, 2);
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_EQUAL_STRING(
        "[{\"bn\":\"urn:dev:ow:1;\",\"n\":\"voltage\",\"u\":\"V\",\"v\":120.1},{\"n\":\"current\",\"u\":\"A\",\"v\":1."
        "25}]",
        buf);
}

// String / boolean values, and an integral time emitted as an integer (full precision).
void test_json_string_bool_time()
{
    SenmlRecord rs = {0};
    rs.name = "status";
    rs.value_kind = SENML_V_STRING;
    rs.value_str = "ok";
    rs.has_time = PROTO_TRUE;
    rs.time = 1600000000; // integral -> "1600000000", not "1.6e+09"
    char buf[128];
    TEST_ASSERT_GREATER_THAN(0, (int)pc_senml_json_build(buf, sizeof(buf), &rs, 1));
    TEST_ASSERT_EQUAL_STRING("[{\"n\":\"status\",\"vs\":\"ok\",\"t\":1600000000}]", buf);

    SenmlRecord rb = {0};
    rb.name = "open";
    rb.value_kind = SENML_V_BOOL;
    rb.value_bool = PROTO_TRUE;
    TEST_ASSERT_GREATER_THAN(0, (int)pc_senml_json_build(buf, sizeof(buf), &rb, 1));
    TEST_ASSERT_EQUAL_STRING("[{\"n\":\"open\",\"vb\":true}]", buf);
}

// SenML-CBOR: a CBOR array of integer-keyed maps; read it back with the CBOR reader.
void test_cbor_round_trip()
{
    SenmlRecord r = {0};
    r.name = "temp";
    r.unit = "Cel";
    r.value_kind = SENML_V_FLOAT;
    r.value = 42; // integral -> emitted as a CBOR integer
    uint8_t buf[64];
    size_t n = pc_senml_build(pc_codec_cbor(), buf, sizeof(buf), &r, 1);
    TEST_ASSERT_GREATER_THAN(0, (int)n);

    pc_cspan rd;
    rd = pc_cspan_from(buf, n);
    size_t arr;
    TEST_ASSERT_TRUE(pc_cbor_read_array(&rd, &arr));
    TEST_ASSERT_EQUAL_size_t(1, arr);
    size_t fields;
    TEST_ASSERT_TRUE(pc_cbor_read_map(&rd, &fields));
    TEST_ASSERT_EQUAL_size_t(3, fields);

    int64_t key;
    const char *s;
    size_t slen;
    TEST_ASSERT_TRUE(pc_cbor_read_int(&rd, &key));
    TEST_ASSERT_EQUAL_INT64(0, key); // n
    TEST_ASSERT_TRUE(pc_cbor_read_str(&rd, &s, &slen));
    TEST_ASSERT_EQUAL_MEMORY("temp", s, slen);

    TEST_ASSERT_TRUE(pc_cbor_read_int(&rd, &key));
    TEST_ASSERT_EQUAL_INT64(1, key); // u
    TEST_ASSERT_TRUE(pc_cbor_read_str(&rd, &s, &slen));
    TEST_ASSERT_EQUAL_MEMORY("Cel", s, slen);

    TEST_ASSERT_TRUE(pc_cbor_read_int(&rd, &key));
    TEST_ASSERT_EQUAL_INT64(2, key); // v
    int64_t v;
    TEST_ASSERT_TRUE(pc_cbor_read_int(&rd, &v));
    TEST_ASSERT_EQUAL_INT64(42, v);
    TEST_ASSERT_TRUE(pc_cspan_ok(rd));
}

// A negative base-label is encoded (bn = -2) and reads back as a negative key.
void test_cbor_base_name_key()
{
    SenmlRecord r = {0};
    r.base_name = "dev1";
    r.value_kind = SENML_V_NONE;
    uint8_t buf[32];
    size_t n = pc_senml_build(pc_codec_cbor(), buf, sizeof(buf), &r, 1);
    pc_cspan rd;
    rd = pc_cspan_from(buf, n);
    size_t arr, fields;
    TEST_ASSERT_TRUE(pc_cbor_read_array(&rd, &arr));
    TEST_ASSERT_TRUE(pc_cbor_read_map(&rd, &fields));
    TEST_ASSERT_EQUAL_size_t(1, fields);
    int64_t key;
    TEST_ASSERT_TRUE(pc_cbor_read_int(&rd, &key));
    TEST_ASSERT_EQUAL_INT64(-2, key); // bn
}

void test_overflow_fails_closed()
{
    SenmlRecord r = {0};
    r.base_name = "urn:dev:ow:10e2073a01080063";
    r.unit = "Cel";
    r.value_kind = SENML_V_FLOAT;
    r.value = 23.1;
    char small[16];
    TEST_ASSERT_EQUAL_size_t(0, pc_senml_json_build(small, sizeof(small), &r, 1));
    uint8_t csmall[4];
    TEST_ASSERT_EQUAL_size_t(0, pc_senml_build(pc_codec_cbor(), csmall, sizeof(csmall), &r, 1));
}

// JSON with a base time and a value-less (PC_NONE) record.
void test_json_base_time_and_none()
{
    SenmlRecord r = {0};
    r.base_name = "dev";
    r.has_base_time = PROTO_TRUE;
    r.base_time = 100; // integral -> "100"
    r.value_kind = SENML_V_NONE;
    char buf[64];
    TEST_ASSERT_GREATER_THAN(0, (int)pc_senml_json_build(buf, sizeof(buf), &r, 1));
    TEST_ASSERT_EQUAL_STRING("[{\"bn\":\"dev\",\"bt\":100}]", buf);
}

// CBOR with base time + string value + time, and a separate boolean value.
void test_cbor_all_kinds()
{
    SenmlRecord r = {0};
    r.has_base_time = PROTO_TRUE;
    r.base_time = 5;
    r.name = "s";
    r.value_kind = SENML_V_STRING;
    r.value_str = "hi";
    r.has_time = PROTO_TRUE;
    r.time = 9;
    uint8_t buf[64];
    size_t n = pc_senml_build(pc_codec_cbor(), buf, sizeof(buf), &r, 1);
    TEST_ASSERT_GREATER_THAN(0, (int)n);

    pc_cspan rd;
    rd = pc_cspan_from(buf, n);
    size_t arr, fields;
    TEST_ASSERT_TRUE(pc_cbor_read_array(&rd, &arr));
    TEST_ASSERT_TRUE(pc_cbor_read_map(&rd, &fields));
    TEST_ASSERT_EQUAL_size_t(4, fields); // bt, n, vs, t
    int64_t key, iv;
    const char *s;
    size_t sl;
    TEST_ASSERT_TRUE(pc_cbor_read_int(&rd, &key));
    TEST_ASSERT_EQUAL_INT64(-3, key); // bt
    TEST_ASSERT_TRUE(pc_cbor_read_int(&rd, &iv));
    TEST_ASSERT_EQUAL_INT64(5, iv);
    TEST_ASSERT_TRUE(pc_cbor_read_int(&rd, &key));
    TEST_ASSERT_EQUAL_INT64(0, key); // n
    TEST_ASSERT_TRUE(pc_cbor_read_str(&rd, &s, &sl));
    TEST_ASSERT_TRUE(pc_cbor_read_int(&rd, &key));
    TEST_ASSERT_EQUAL_INT64(3, key); // vs
    TEST_ASSERT_TRUE(pc_cbor_read_str(&rd, &s, &sl));
    TEST_ASSERT_EQUAL_MEMORY("hi", s, sl);
    TEST_ASSERT_TRUE(pc_cbor_read_int(&rd, &key));
    TEST_ASSERT_EQUAL_INT64(6, key); // t
    TEST_ASSERT_TRUE(pc_cbor_read_int(&rd, &iv));
    TEST_ASSERT_EQUAL_INT64(9, iv);

    SenmlRecord rb = {0};
    rb.name = "b";
    rb.value_kind = SENML_V_BOOL;
    rb.value_bool = PROTO_TRUE;
    uint8_t bb[32];
    size_t bn = pc_senml_build(pc_codec_cbor(), bb, sizeof(bb), &rb, 1);
    TEST_ASSERT_GREATER_THAN(0, (int)bn);
    pc_cspan rd2;
    rd2 = pc_cspan_from(bb, bn);
    TEST_ASSERT_TRUE(pc_cbor_read_array(&rd2, &arr));
    TEST_ASSERT_TRUE(pc_cbor_read_map(&rd2, &fields));
    TEST_ASSERT_EQUAL_size_t(2, fields); // n, vb
    TEST_ASSERT_TRUE(pc_cbor_read_int(&rd2, &key));
    TEST_ASSERT_TRUE(pc_cbor_read_str(&rd2, &s, &sl));
    TEST_ASSERT_TRUE(pc_cbor_read_int(&rd2, &key));
    TEST_ASSERT_EQUAL_INT64(4, key); // vb
    proto_bool bv = PROTO_FALSE;
    TEST_ASSERT_TRUE(pc_cbor_read_bool(&rd2, &bv));
    TEST_ASSERT_TRUE(bv);
}

void test_senml_null_args()
{
    SenmlRecord r = {0};
    char jb[32];
    uint8_t cb[32];
    TEST_ASSERT_EQUAL_size_t(0, pc_senml_json_build(NULL, sizeof(jb), &r, 1));
    TEST_ASSERT_EQUAL_size_t(0, pc_senml_json_build(jb, sizeof(jb), NULL, 1)); // count && !records
    TEST_ASSERT_EQUAL_size_t(0, pc_senml_build(pc_codec_cbor(), NULL, sizeof(cb), &r, 1));
    TEST_ASSERT_EQUAL_size_t(0, pc_senml_build(pc_codec_cbor(), cb, sizeof(cb), NULL, 1));
}

// A magnitude outside the int64 range is emitted as a float rather than being truncated
// through the integral fast path.
void test_json_non_integral_magnitudes()
{
    SenmlRecord r = {0};
    r.name = "big";
    r.value_kind = SENML_V_FLOAT;

    r.value = 1e19; // above the int64 range
    char buf[64];
    TEST_ASSERT_GREATER_THAN(0, (int)pc_senml_json_build(buf, sizeof(buf), &r, 1));
    TEST_ASSERT_EQUAL_STRING("[{\"n\":\"big\",\"v\":1e+19}]", buf);

    r.value = -1e19; // below the int64 range
    TEST_ASSERT_GREATER_THAN(0, (int)pc_senml_json_build(buf, sizeof(buf), &r, 1));
    TEST_ASSERT_EQUAL_STRING("[{\"n\":\"big\",\"v\":-1e+19}]", buf);
}

// A STRING record carrying no string emits no value at all - and the CBOR map's declared
// field count drops to match, so the pack stays well-formed.
void test_string_kind_without_value()
{
    SenmlRecord r = {0};
    r.name = "s";
    r.value_kind = SENML_V_STRING;
    r.value_str = NULL;

    char jb[64];
    TEST_ASSERT_GREATER_THAN(0, (int)pc_senml_json_build(jb, sizeof(jb), &r, 1));
    TEST_ASSERT_EQUAL_STRING("[{\"n\":\"s\"}]", jb);

    uint8_t cb[32];
    size_t n = pc_senml_build(pc_codec_cbor(), cb, sizeof(cb), &r, 1);
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    pc_cspan rd;
    rd = pc_cspan_from(cb, n);
    size_t arr, fields;
    TEST_ASSERT_TRUE(pc_cbor_read_array(&rd, &arr));
    TEST_ASSERT_EQUAL_size_t(1, arr);
    TEST_ASSERT_TRUE(pc_cbor_read_map(&rd, &fields));
    TEST_ASSERT_EQUAL_size_t(1, fields); // only n - the absent vs is not counted
    int64_t key;
    const char *s;
    size_t sl;
    TEST_ASSERT_TRUE(pc_cbor_read_int(&rd, &key));
    TEST_ASSERT_EQUAL_INT64(0, key); // n
    TEST_ASSERT_TRUE(pc_cbor_read_str(&rd, &s, &sl));
    TEST_ASSERT_EQUAL_MEMORY("s", s, sl);
}

// A zero-count pack is valid and short-circuits the records null-check, in both encodings.
void test_empty_pack_allows_null_records()
{
    char jb[16];
    TEST_ASSERT_GREATER_THAN(0, (int)pc_senml_json_build(jb, sizeof(jb), NULL, 0));
    TEST_ASSERT_EQUAL_STRING("[]", jb);

    uint8_t cb[16];
    size_t cn = pc_senml_build(pc_codec_cbor(), cb, sizeof(cb), NULL, 0);
    TEST_ASSERT_GREATER_THAN(0, (int)cn);
    pc_cspan rd;
    size_t arr;
    rd = pc_cspan_from(cb, cn);
    TEST_ASSERT_TRUE(pc_cbor_read_array(&rd, &arr));
    TEST_ASSERT_EQUAL_size_t(0, arr);
}

// --- resolution (RFC 8428 §4.6) ---
void test_resolve()
{
    SenmlRecord rec[3];
    memset(rec, 0, sizeof(rec));
    rec[0].base_name = "urn:dev:ow:10e2073a;";
    rec[0].has_base_time = PROTO_TRUE;
    rec[0].base_time = 1276020076;
    rec[0].name = "temp";
    rec[0].unit = "Cel";
    rec[0].value_kind = SENML_V_FLOAT;
    rec[0].value = 23.5;
    rec[1].name = "humidity"; // no base fields: the base name / time from rec[0] carry forward
    rec[1].value_kind = SENML_V_FLOAT;
    rec[1].value = 40;
    rec[1].has_time = PROTO_TRUE;
    rec[1].time = 10;
    rec[2].base_name = "urn:dev:ow:other;"; // overrides the base name; base time still carries
    rec[2].name = "status";
    rec[2].value_kind = SENML_V_STRING;
    rec[2].value_str = "ok";

    SenmlResolved res[3];
    size_t n = pc_senml_resolve(rec, 3, res, 3);
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_STRING("urn:dev:ow:10e2073a;temp", res[0].name);
    TEST_ASSERT_TRUE(res[0].has_time);
    TEST_ASSERT_EQUAL_INT64(1276020076, (int64_t)res[0].time);
    TEST_ASSERT_EQUAL_STRING("Cel", res[0].unit);

    TEST_ASSERT_EQUAL_STRING("urn:dev:ow:10e2073a;humidity", res[1].name); // base name carried forward
    TEST_ASSERT_TRUE(res[1].has_time);
    TEST_ASSERT_EQUAL_INT64(1276020086, (int64_t)res[1].time); // base time + record time (10)

    TEST_ASSERT_EQUAL_STRING("urn:dev:ow:other;status", res[2].name); // base name overridden
    TEST_ASSERT_EQUAL_INT64(1276020076, (int64_t)res[2].time);        // still on the (unchanged) base time
    TEST_ASSERT_EQUAL(SENML_V_STRING, res[2].value_kind);
    TEST_ASSERT_EQUAL_STRING("ok", res[2].value_str);
}

void test_resolve_edges()
{
    // A pack with no base time at all: a record with neither base time nor time has no resolved time.
    SenmlRecord rec[2];
    memset(rec, 0, sizeof(rec));
    rec[0].name = "a";
    rec[0].value_kind = SENML_V_FLOAT;
    rec[0].value = 1;
    rec[1].name = "b";
    rec[1].value_kind = SENML_V_BOOL;
    rec[1].value_bool = PROTO_TRUE;
    SenmlResolved res[2];
    TEST_ASSERT_EQUAL_size_t(2, pc_senml_resolve(rec, 2, res, 2));
    TEST_ASSERT_FALSE(res[0].has_time);
    TEST_ASSERT_EQUAL_STRING("a", res[0].name); // no base name -> just the record name
    TEST_ASSERT_TRUE(res[1].value_bool);

    // max caps the output; null arguments resolve nothing.
    TEST_ASSERT_EQUAL_size_t(1, pc_senml_resolve(rec, 2, res, 1));
    TEST_ASSERT_EQUAL_size_t(0, pc_senml_resolve(NULL, 2, res, 2));
    TEST_ASSERT_EQUAL_size_t(0, pc_senml_resolve(rec, 2, NULL, 2));

    // A base name + name longer than the buffer truncates safely (no overflow).
    SenmlRecord big[1];
    memset(big, 0, sizeof(big));
    static char longbn[128];
    memset(longbn, 'x', sizeof(longbn) - 1);
    longbn[sizeof(longbn) - 1] = '\0';
    big[0].base_name = longbn;
    big[0].name = "y";
    SenmlResolved r1;
    TEST_ASSERT_EQUAL_size_t(1, pc_senml_resolve(big, 1, &r1, 1));
    TEST_ASSERT_TRUE(strlen(r1.name) < SENML_RESOLVED_NAME_MAX);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_json_non_integral_magnitudes);
    RUN_TEST(test_string_kind_without_value);
    RUN_TEST(test_empty_pack_allows_null_records);
    RUN_TEST(test_json_canonical);
    RUN_TEST(test_json_base_time_and_none);
    RUN_TEST(test_cbor_all_kinds);
    RUN_TEST(test_senml_null_args);
    RUN_TEST(test_json_multi_record);
    RUN_TEST(test_json_string_bool_time);
    RUN_TEST(test_cbor_round_trip);
    RUN_TEST(test_cbor_base_name_key);
    RUN_TEST(test_overflow_fails_closed);
    RUN_TEST(test_resolve);
    RUN_TEST(test_resolve_edges);
    return UNITY_END();
}
