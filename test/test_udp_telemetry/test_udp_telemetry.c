// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the UDP telemetry line builder (services/iot/udp_telemetry): the
// InfluxDB line-protocol formatting and fail-closed overflow. The UDP cast is
// ESP32-only (a no-op on host).

#include "services/iot/udp_telemetry/udp_telemetry.h"
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_int_and_uint_fields()
{
    char buf[128];
    pc_line l;
    pc_line_init(&l, buf, sizeof(buf), "env");
    pc_line_add_int(&l, "rssi", -42);
    pc_line_add_uint(&l, "uptime", 1234u);
    TEST_ASSERT_TRUE(pc_line_ok(&l));
    TEST_ASSERT_EQUAL_STRING("env rssi=-42i,uptime=1234u", buf); // signed 'i' vs unsigned 'u'
    TEST_ASSERT_EQUAL_size_t(strlen("env rssi=-42i,uptime=1234u"), pc_line_len(&l));
}

void test_float_field()
{
    char buf[64];
    pc_line l;
    pc_line_init(&l, buf, sizeof(buf), "weather");
    pc_line_add_float(&l, "temp", 21.5f, 1);
    pc_line_add_float(&l, "hum", 60.25f, 2);
    TEST_ASSERT_TRUE(pc_line_ok(&l));
    TEST_ASSERT_EQUAL_STRING("weather temp=21.5,hum=60.25", buf);
}

void test_no_fields_not_ok()
{
    char buf[32];
    pc_line l;
    pc_line_init(&l, buf, sizeof(buf), "m");
    TEST_ASSERT_FALSE(pc_line_ok(&l)); // measurement only, no fields
    TEST_ASSERT_EQUAL_STRING("m", buf);
}

void test_overflow_fails_closed()
{
    char buf[16];
    pc_line l;
    pc_line_init(&l, buf, sizeof(buf), "measurement_with_a_long_name");
    pc_line_add_int(&l, "x", 1);
    TEST_ASSERT_FALSE(pc_line_ok(&l)); // did not fit
}

// Tags sit between the measurement and the fields (series key); a trailing
// timestamp is space-separated after the fields.
void test_tags_and_timestamp()
{
    char buf[128];
    pc_line l;
    pc_line_init(&l, buf, sizeof(buf), "env");
    pc_line_add_tag(&l, "host", "esp32-1");
    pc_line_add_tag(&l, "room", "lab");
    pc_line_add_int(&l, "rssi", -42);
    pc_line_set_timestamp(&l, 1465839830100400200LL);
    TEST_ASSERT_TRUE(pc_line_ok(&l));
    TEST_ASSERT_EQUAL_STRING("env,host=esp32-1,room=lab rssi=-42i 1465839830100400200", buf);
}

// Tag keys/values escape comma, equals and space (line protocol special chars).
void test_tag_escaping()
{
    char buf[128];
    pc_line l;
    pc_line_init(&l, buf, sizeof(buf), "m");
    pc_line_add_tag(&l, "a b", "x,y=z");
    pc_line_add_int(&l, "f", 1);
    TEST_ASSERT_TRUE(pc_line_ok(&l));
    TEST_ASSERT_EQUAL_STRING("m,a\\ b=x\\,y\\=z f=1i", buf);
}

// A tag added after a field is a misuse -> the line fails closed.
void test_tag_after_field_fails_closed()
{
    char buf[64];
    pc_line l;
    pc_line_init(&l, buf, sizeof(buf), "m");
    pc_line_add_int(&l, "f", 1);
    pc_line_add_tag(&l, "late", "nope"); // invalid ordering
    TEST_ASSERT_FALSE(pc_line_ok(&l));
}

void test_host_stubs_and_line_overflow()
{
    pc_udp_telemetry_begin("host", 8125); // host no-op stub
    char buf[8];
    pc_line l;
    pc_line_init(&l, buf, sizeof(buf), "measurementNameFarTooLongForBuf");
    TEST_ASSERT_TRUE(l.overflow); // measurement did not fit
    pc_udp_telemetry_cast(&l);    // host no-op stub
    TEST_PASS();
}

// A zero-capacity buffer skips the initial null-terminate (cap==0) and the
// measurement append fails closed immediately (no room, not even for '\0').
void test_zero_capacity_line_overflows()
{
    char buf[8];
    pc_line l;
    pc_line_init(&l, buf, 0, "m");
    TEST_ASSERT_TRUE(l.overflow);
    TEST_ASSERT_FALSE(pc_line_ok(&l));
}

// A NULL measurement is tolerated (treated as empty), not a crash.
void test_null_measurement_is_empty()
{
    char buf[64];
    pc_line l;
    pc_line_init(&l, buf, sizeof(buf), NULL);
    pc_line_add_int(&l, "f", 1);
    TEST_ASSERT_TRUE(pc_line_ok(&l));
    TEST_ASSERT_EQUAL_STRING(" f=1i", buf); // no measurement text, field still appended
}

// A NULL tag value (or key) is tolerated: the escaped-append is a no-op, so
// the '=' separator is written with nothing after it, not a crash.
void test_null_tag_value_appends_nothing()
{
    char buf[64];
    pc_line l;
    pc_line_init(&l, buf, sizeof(buf), "m");
    pc_line_add_tag(&l, "host", NULL);
    pc_line_add_int(&l, "f", 1);
    TEST_ASSERT_TRUE(pc_line_ok(&l));
    TEST_ASSERT_EQUAL_STRING("m,host= f=1i", buf);
}

// Setting the timestamp before any field is present is a misuse -> fails closed
// (mirrors the tag-after-field ordering rule, but for the trailing timestamp).
void test_timestamp_before_fields_fails_closed()
{
    char buf[64];
    pc_line l;
    pc_line_init(&l, buf, sizeof(buf), "m");
    pc_line_set_timestamp(&l, 42);
    TEST_ASSERT_TRUE(l.overflow);
    TEST_ASSERT_FALSE(pc_line_ok(&l));
}

// A well-formed (ok) line reaches the host pc_udp_telemetry_send() stub via
// pc_udp_telemetry_cast(); on host builds it always returns false (no transport).
void test_cast_valid_line_reaches_host_send_stub()
{
    char buf[64];
    pc_line l;
    pc_line_init(&l, buf, sizeof(buf), "env");
    pc_line_add_int(&l, "rssi", -42);
    TEST_ASSERT_TRUE(pc_line_ok(&l));
    TEST_ASSERT_FALSE(pc_udp_telemetry_cast(&l));               // host stub: never sends
    TEST_ASSERT_FALSE(pc_udp_telemetry_send(buf, sizeof(buf))); // host stub: always false
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_int_and_uint_fields);
    RUN_TEST(test_float_field);
    RUN_TEST(test_no_fields_not_ok);
    RUN_TEST(test_overflow_fails_closed);
    RUN_TEST(test_tags_and_timestamp);
    RUN_TEST(test_tag_escaping);
    RUN_TEST(test_tag_after_field_fails_closed);
    RUN_TEST(test_host_stubs_and_line_overflow);
    RUN_TEST(test_zero_capacity_line_overflows);
    RUN_TEST(test_null_measurement_is_empty);
    RUN_TEST(test_null_tag_value_appends_nothing);
    RUN_TEST(test_timestamp_before_fields_fails_closed);
    RUN_TEST(test_cast_valid_line_reaches_host_send_stub);
    return UNITY_END();
}
