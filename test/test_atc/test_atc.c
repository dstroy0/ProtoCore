// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/atc: the ATC field-I/O interop snapshot.

#include "mmgr/membuild.h"
#include "services/machine_tool/atc/atc.h"
#include <string.h>
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_snapshot_json(void)
{
    AtcPoint pts[3] = {
        {"det.1", false, 1},        // input
        {"phase.2.green", true, 0}, // output
        {"det.2", false, 0},        // input
    };
    AtcFieldIo io = {pts, 3};
    char buf[256];
    size_t n = pc_atc_snapshot_json(&io, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"inputs\":[{\"name\":\"det.1\",\"value\":1}"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "{\"name\":\"det.2\",\"value\":0}"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"outputs\":[{\"name\":\"phase.2.green\",\"value\":0}]"));
}

void test_set_output(void)
{
    AtcPoint pts[2] = {{"det.1", false, 0}, {"phase.1.green", true, 0}};
    AtcFieldIo io = {pts, 2};
    // Set an output.
    TEST_ASSERT_TRUE(pc_atc_set_output(&io, "phase.1.green", 1));
    TEST_ASSERT_EQUAL_UINT8(1, pts[1].value);
    // Cannot set an input.
    TEST_ASSERT_FALSE(pc_atc_set_output(&io, "det.1", 1));
    // Unknown name.
    TEST_ASSERT_FALSE(pc_atc_set_output(&io, "nope", 1));
}

void test_get(void)
{
    AtcPoint pts[2] = {{"det.1", false, 42}, {"out.1", true, 7}};
    AtcFieldIo io = {pts, 2};
    bool found = false;
    TEST_ASSERT_EQUAL_UINT8(42, pc_atc_get(&io, "det.1", &found));
    TEST_ASSERT_TRUE(found);
    pc_atc_get(&io, "missing", &found);
    TEST_ASSERT_FALSE(found);
}

void test_empty_and_overflow(void)
{
    AtcFieldIo io = {NULL, 0};
    char buf[64];
    size_t n = pc_atc_snapshot_json(&io, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("{\"inputs\":[],\"outputs\":[]}", buf);
    TEST_ASSERT_EQUAL_size_t(strlen("{\"inputs\":[],\"outputs\":[]}"), n);

    AtcPoint pts[1] = {{"a-long-input-name", false, 1}};
    AtcFieldIo io2 = {pts, 1};
    char small[16];
    TEST_ASSERT_EQUAL_size_t(0, pc_atc_snapshot_json(&io2, small, sizeof(small)));
}

void test_json_escapes_and_overflow()
{
    AtcPoint pts[1] = {{"a\"b\\c", false, 1}}; // name with a quote + backslash gets escaped
    AtcFieldIo io = {pts, 1};
    char buf[128];
    size_t n = pc_atc_snapshot_json(&io, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\\\""));                      // escaped quote present
    TEST_ASSERT_EQUAL_size_t(0, pc_atc_snapshot_json(&io, buf, 8)); // tiny cap fails closed
}

void test_atc_null_and_missing_args(void)
{
    AtcPoint pts[2] = {{"det.1", false, 1}, {"phase.1.green", true, 0}};
    AtcFieldIo io = {pts, 2};
    AtcFieldIo io_null_points = {NULL, 1}; // count > 0 but points is null: must fail closed too
    char buf[64];

    // pc_atc_snapshot_json: null io / null out / (count>0 && !points) all fail closed.
    TEST_ASSERT_EQUAL_size_t(0, pc_atc_snapshot_json(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0, pc_atc_snapshot_json(&io, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0, pc_atc_snapshot_json(&io_null_points, buf, sizeof(buf)));

    // pc_atc_set_output: null io / null name / null points all fail closed.
    TEST_ASSERT_FALSE(pc_atc_set_output(NULL, "phase.1.green", 1));
    TEST_ASSERT_FALSE(pc_atc_set_output(&io, NULL, 1));
    TEST_ASSERT_FALSE(pc_atc_set_output(&io_null_points, "phase.1.green", 1));

    // pc_atc_get: null io / null name / null points all fail closed and report found=false.
    bool found = true;
    TEST_ASSERT_EQUAL_UINT8(0, pc_atc_get(NULL, "det.1", &found));
    TEST_ASSERT_FALSE(found);
    found = true;
    TEST_ASSERT_EQUAL_UINT8(0, pc_atc_get(&io, NULL, &found));
    TEST_ASSERT_FALSE(found);
    found = true;
    TEST_ASSERT_EQUAL_UINT8(0, pc_atc_get(&io_null_points, "det.1", &found));
    TEST_ASSERT_FALSE(found);

    // pc_atc_get with a null `found` out-param must tolerate it on both the miss and hit paths.
    TEST_ASSERT_EQUAL_UINT8(0, pc_atc_get(&io, "missing", NULL));
    TEST_ASSERT_EQUAL_UINT8(1, pc_atc_get(&io, "det.1", NULL));
}

void test_atc_null_name_point_and_multidigit_value(void)
{
    // A point with a null name renders as an empty JSON string and is safely skipped (never
    // matched) by name-based lookups; a value >=10 exercises put_u8's multi-digit path.
    AtcPoint pts[2] = {
        {NULL, true, 5},
        {"phase.9.red", true, 250},
    };
    AtcFieldIo io = {pts, 2};
    char buf[128];
    size_t n = pc_atc_snapshot_json(&io, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(
        strstr(buf, "\"outputs\":[{\"name\":\"\",\"value\":5},{\"name\":\"phase.9.red\",\"value\":250}]"));

    // set_output must skip the null-named point and still find the real one.
    TEST_ASSERT_TRUE(pc_atc_set_output(&io, "phase.9.red", 99));
    TEST_ASSERT_EQUAL_UINT8(99, pts[1].value);

    // get must skip the null-named point and still find the real one.
    bool found = false;
    TEST_ASSERT_EQUAL_UINT8(99, pc_atc_get(&io, "phase.9.red", &found));
    TEST_ASSERT_TRUE(found);
}

void test_strbuf_xml_and_json_direct(void)
{
    // pc_sb_xml: all four escapes (&,<,>,") plus literal passthrough chars, in one pass.
    char buf[64];
    pc_sb b = {buf, sizeof(buf), 0, true};
    pc_sb_xml(&b, "<a>&\"b\"</a>");
    TEST_ASSERT_TRUE(b.ok);
    buf[b.len] = '\0';
    TEST_ASSERT_EQUAL_STRING("&lt;a&gt;&amp;&quot;b&quot;&lt;/a&gt;", buf);

    // A NULL s appends nothing.
    size_t before = b.len;
    pc_sb_xml(&b, NULL);
    TEST_ASSERT_TRUE(b.ok);
    TEST_ASSERT_EQUAL_size_t(before, b.len);

    // Overflow on the raw-copy path latches ok=false and stops writing (no further chars land).
    char tiny[2];
    pc_sb b2 = {tiny, sizeof(tiny), 0, true};
    pc_sb_xml(&b2, "ab");
    TEST_ASSERT_FALSE(b2.ok);
    TEST_ASSERT_EQUAL_size_t(1, b2.len); // only 'a' fit before latching closed

    // Once latched closed, a further call is a no-op (the `!b->ok` guard short-circuits).
    pc_sb_xml(&b2, "more");
    TEST_ASSERT_FALSE(b2.ok);
    TEST_ASSERT_EQUAL_size_t(1, b2.len);

    // pc_sb_json: a NULL s emits an empty string literal.
    char jbuf[8];
    pc_sb b3 = {jbuf, sizeof(jbuf), 0, true};
    pc_sb_json(&b3, NULL);
    TEST_ASSERT_TRUE(b3.ok);
    jbuf[b3.len] = '\0';
    TEST_ASSERT_EQUAL_STRING("\"\"", jbuf);

    // pc_sb_json: quote/backslash escapes plus literal passthrough chars, ample buffer.
    char jbuf2[32];
    pc_sb b4 = {jbuf2, sizeof(jbuf2), 0, true};
    pc_sb_json(&b4, "a\"\\b");
    TEST_ASSERT_TRUE(b4.ok);
    jbuf2[b4.len] = '\0';
    TEST_ASSERT_EQUAL_CHAR('"', jbuf2[0]);
    TEST_ASSERT_EQUAL_CHAR('"', jbuf2[b4.len - 1]);
    TEST_ASSERT_NOT_NULL(strstr(jbuf2, "\\\"")); // escaped quote
    TEST_ASSERT_NOT_NULL(strstr(jbuf2, "\\\\")); // escaped backslash
    TEST_ASSERT_NOT_NULL(strstr(jbuf2, "a"));
    TEST_ASSERT_NOT_NULL(strstr(jbuf2, "b"));

    // pc_sb_json: overflow on the raw-copy path latches ok=false without writing past cap.
    char jtiny[3];
    pc_sb b5 = {jtiny, sizeof(jtiny), 0, true};
    pc_sb_json(&b5, "xy");
    TEST_ASSERT_FALSE(b5.ok);
    TEST_ASSERT_EQUAL_size_t(2, b5.len); // opening quote + 'x'; 'y' overflowed
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_snapshot_json);
    RUN_TEST(test_set_output);
    RUN_TEST(test_get);
    RUN_TEST(test_empty_and_overflow);
    RUN_TEST(test_json_escapes_and_overflow);
    RUN_TEST(test_atc_null_and_missing_args);
    RUN_TEST(test_atc_null_name_point_and_multidigit_value);
    RUN_TEST(test_strbuf_xml_and_json_direct);
    return UNITY_END();
}
