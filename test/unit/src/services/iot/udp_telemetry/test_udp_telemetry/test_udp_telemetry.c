// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the line protocol caster (services/iot/udp_telemetry/udp_telemetry.h).
//
// The load-bearing case is test_published_point: InfluxData's line protocol tutorial prints the
// point "weather,location=us-midwest temperature=82 1465839830100400200" and labels each element
// of it, so reproducing that line octet for octet is what makes this builder trustworthy. The
// escaping, integer-suffix and unsigned-suffix cases below likewise rebuild lines the InfluxDB v2
// line protocol reference prints verbatim. The transport is UDP (RFC 768); a host build declares
// no network stack, so the send half is only asserted to refuse.

#include "services/iot/udp_telemetry/udp_telemetry.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static char g_line[256];

static void open_line(const char *m, size_t cap)
{
    UdpTelemetryV.line.buf = g_line;
    UdpTelemetryV.line.cap = cap;
    UdpTelemetryV.line.measurement = m;
    UdpTelemetry.measurement(protocore_udp_telemetry_span());
}

static void tag(const char *k, const char *v)
{
    UdpTelemetryV.tags.key = k;
    UdpTelemetryV.tags.value = v;
    UdpTelemetry.tag(protocore_udp_telemetry_span());
}

static void field_int(const char *k, int64_t v)
{
    UdpTelemetryV.fields.key = k;
    UdpTelemetryV.fields.i64 = v;
    UdpTelemetry.field_int(protocore_udp_telemetry_span());
}

static void field_uint(const char *k, uint64_t v)
{
    UdpTelemetryV.fields.key = k;
    UdpTelemetryV.fields.u64 = v;
    UdpTelemetry.field_uint(protocore_udp_telemetry_span());
}

static void field_float(const char *k, float v, uint8_t decimals)
{
    UdpTelemetryV.fields.key = k;
    UdpTelemetryV.fields.f32 = v;
    UdpTelemetryV.fields.decimals = decimals;
    UdpTelemetry.field_float(protocore_udp_telemetry_span());
}

static void stamp(int64_t unix_ns)
{
    UdpTelemetryV.time.unix_ns = unix_ns;
    UdpTelemetry.timestamp(protocore_udp_telemetry_span());
}

// InfluxData, "InfluxDB line protocol tutorial", prints this point and labels its four elements as
// measurement, tag set, field set and timestamp:
//
//     weather,location=us-midwest temperature=82 1465839830100400200
//
// A float field value carries no type suffix, so 82 renders with zero decimals.
void test_published_point(void)
{
    static const char WANT[] = "weather,location=us-midwest temperature=82 1465839830100400200";
    open_line("weather", sizeof(g_line));
    tag("location", "us-midwest");
    field_float("temperature", 82.0f, 0);
    stamp(1465839830100400200LL);

    TEST_ASSERT_TRUE(UdpTelemetryV.ok);
    TEST_ASSERT_FALSE(UdpTelemetryV.overflow);
    TEST_ASSERT_EQUAL_STRING(WANT, g_line);
    TEST_ASSERT_EQUAL_UINT(strlen(WANT), UdpTelemetryV.n);
}

// The v2 reference's "Special characters" example, rebuilt: a tag key and a tag value each carrying
// a space, which the table says must be escaped with a backslash.
//
//     myMeasurement,tag\ Key1=tag\ Value1,tag\ Key2=tag\ Value2 fieldKey=100
void test_published_tag_escaping(void)
{
    static const char WANT[] = "myMeasurement,tag\\ Key1=tag\\ Value1,tag\\ Key2=tag\\ Value2 fieldKey=100";
    open_line("myMeasurement", sizeof(g_line));
    tag("tag Key1", "tag Value1");
    tag("tag Key2", "tag Value2");
    field_float("fieldKey", 100.0f, 0);

    TEST_ASSERT_TRUE(UdpTelemetryV.ok);
    TEST_ASSERT_EQUAL_STRING(WANT, g_line);
}

// The same table names comma and equals sign alongside space for a tag key and a tag value.
void test_tag_escapes_comma_and_equals(void)
{
    static const char WANT[] = "m,a\\,b\\=c=x\\,y\\=z fieldKey=1i";
    open_line("m", sizeof(g_line));
    tag("a,b=c", "x,y=z");
    field_int("fieldKey", 1);
    TEST_ASSERT_TRUE(UdpTelemetryV.ok);
    TEST_ASSERT_EQUAL_STRING(WANT, g_line);
}

// v2 reference, Integer: "Trailing i on the number specifies an integer", with the printed examples
// myMeasurement fieldKey=1i, fieldKey=12485903i and fieldKey=-12485903i, and the stated bounds
// -9223372036854775808i and 9223372036854775807i.
void test_published_integer_fields(void)
{
    open_line("myMeasurement", sizeof(g_line));
    field_int("fieldKey", 1);
    TEST_ASSERT_EQUAL_STRING("myMeasurement fieldKey=1i", g_line);

    open_line("myMeasurement", sizeof(g_line));
    field_int("fieldKey", 12485903);
    TEST_ASSERT_EQUAL_STRING("myMeasurement fieldKey=12485903i", g_line);

    open_line("myMeasurement", sizeof(g_line));
    field_int("fieldKey", -12485903);
    TEST_ASSERT_EQUAL_STRING("myMeasurement fieldKey=-12485903i", g_line);

    open_line("m", sizeof(g_line));
    field_int("v", (int64_t)-9223372036854775807LL - 1);
    TEST_ASSERT_EQUAL_STRING("m v=-9223372036854775808i", g_line);

    open_line("m", sizeof(g_line));
    field_int("v", 9223372036854775807LL);
    TEST_ASSERT_EQUAL_STRING("m v=9223372036854775807i", g_line);
}

// v2 reference, UInteger: "Trailing u on the number specifies an unsigned integer", printed as
// myMeasurement fieldKey=1u and fieldKey=12485903u, bounded by 0u and 18446744073709551615u.
void test_published_unsigned_fields(void)
{
    open_line("myMeasurement", sizeof(g_line));
    field_uint("fieldKey", 1u);
    TEST_ASSERT_EQUAL_STRING("myMeasurement fieldKey=1u", g_line);

    open_line("myMeasurement", sizeof(g_line));
    field_uint("fieldKey", 12485903u);
    TEST_ASSERT_EQUAL_STRING("myMeasurement fieldKey=12485903u", g_line);

    open_line("m", sizeof(g_line));
    field_uint("v", 0u);
    TEST_ASSERT_EQUAL_STRING("m v=0u", g_line);

    open_line("m", sizeof(g_line));
    field_uint("v", 18446744073709551615ull);
    TEST_ASSERT_EQUAL_STRING("m v=18446744073709551615u", g_line);
}

// v2 reference, Float: the default numerical type, written unsuffixed. Its printed examples include
// myMeasurement fieldKey=1.0. The decimals argument fixes how many digits follow the point.
void test_published_float_field(void)
{
    open_line("myMeasurement", sizeof(g_line));
    field_float("fieldKey", 1.0f, 1);
    TEST_ASSERT_EQUAL_STRING("myMeasurement fieldKey=1.0", g_line);

    open_line("m", sizeof(g_line));
    field_float("v", -1.5f, 3);
    TEST_ASSERT_EQUAL_STRING("m v=-1.500", g_line);
}

// v2 reference, Whitespace: "The first unescaped space delimits the measurement and the tag set
// from the field set. The second unescaped space delimits the field set from the timestamp." So
// the first field opens with a space and every later one with a comma.
void test_field_set_separators(void)
{
    static const char WANT[] = "m,t=v a=1i,b=2u,c=3.5 7";
    open_line("m", sizeof(g_line));
    tag("t", "v");
    field_int("a", 1);
    field_uint("b", 2u);
    field_float("c", 3.5f, 1);
    stamp(7);
    TEST_ASSERT_TRUE(UdpTelemetryV.ok);
    TEST_ASSERT_EQUAL_STRING(WANT, g_line);

    // Exactly two unescaped spaces in the finished line.
    int spaces = 0;
    for (const char *p = g_line; *p; p++)
    {
        if (*p == ' ')
        {
            spaces++;
        }
    }
    TEST_ASSERT_EQUAL_INT(2, spaces);
}

// v2 reference, Field set: "Points must have at least one field." A measurement alone, or a
// measurement with tags, is not a point yet.
void test_a_point_needs_a_field(void)
{
    open_line("m", sizeof(g_line));
    TEST_ASSERT_FALSE(UdpTelemetryV.ok);
    TEST_ASSERT_FALSE(UdpTelemetryV.overflow);

    tag("t", "v");
    TEST_ASSERT_FALSE(UdpTelemetryV.ok);

    field_int("a", 1);
    TEST_ASSERT_TRUE(UdpTelemetryV.ok);
}

// The tag set sits between the measurement and the space that opens the field set, so a tag
// appended after a field would read as part of the field set. The line is failed instead.
void test_tag_after_a_field_is_refused(void)
{
    open_line("m", sizeof(g_line));
    field_int("a", 1);
    TEST_ASSERT_TRUE(UdpTelemetryV.ok);

    tag("t", "v");
    TEST_ASSERT_FALSE(UdpTelemetryV.ok);
    TEST_ASSERT_TRUE(UdpTelemetryV.overflow);
    TEST_ASSERT_EQUAL_STRING("m a=1i", g_line); // nothing was appended
}

// The timestamp trails the field set, so a line with no field has no point to stamp.
void test_timestamp_before_any_field_is_refused(void)
{
    open_line("m", sizeof(g_line));
    tag("t", "v");
    stamp(1);
    TEST_ASSERT_FALSE(UdpTelemetryV.ok);
    TEST_ASSERT_TRUE(UdpTelemetryV.overflow);
    TEST_ASSERT_EQUAL_STRING("m,t=v", g_line);
}

// An append that does not fit latches overflow, and every later append is a no-op, so a line can
// never be half a point followed by a later element.
void test_overflow_latches(void)
{
    open_line("abc", 8);
    TEST_ASSERT_FALSE(UdpTelemetryV.overflow);

    field_int("k", 1); // " k=1i" needs 5 more octets; 3 + 5 leaves no room for the NUL
    TEST_ASSERT_TRUE(UdpTelemetryV.overflow);
    TEST_ASSERT_FALSE(UdpTelemetryV.ok);
    const size_t stuck = UdpTelemetryV.n;

    field_int("k2", 2);
    TEST_ASSERT_TRUE(UdpTelemetryV.overflow);
    TEST_ASSERT_EQUAL_UINT(stuck, UdpTelemetryV.n);
}

// A fresh measurement rebinds the buffer and clears the position, the field flag and the overflow
// latch, so a failed line does not poison the next one.
void test_measurement_reopens_the_line(void)
{
    open_line("abc", 8);
    field_int("k", 1);
    TEST_ASSERT_TRUE(UdpTelemetryV.overflow);

    open_line("m", sizeof(g_line));
    TEST_ASSERT_FALSE(UdpTelemetryV.overflow);
    field_int("a", 1);
    TEST_ASSERT_TRUE(UdpTelemetryV.ok);
    TEST_ASSERT_EQUAL_STRING("m a=1i", g_line);
}

// n counts the octets the line holds, the NUL excluded, and the buffer is terminated at that index.
void test_length_excludes_the_terminator(void)
{
    open_line("m", sizeof(g_line));
    field_int("a", 1);
    TEST_ASSERT_EQUAL_UINT(6u, UdpTelemetryV.n); // "m a=1i"
    TEST_ASSERT_EQUAL_CHAR('\0', g_line[UdpTelemetryV.n]);
}

// A null buffer has nowhere to build, so the line opens already overflowed.
void test_null_buffer_is_refused(void)
{
    UdpTelemetryV.line.buf = NULL;
    UdpTelemetryV.line.cap = 0;
    UdpTelemetryV.line.measurement = "m";
    UdpTelemetry.measurement(protocore_udp_telemetry_span());
    TEST_ASSERT_TRUE(UdpTelemetryV.overflow);
    TEST_ASSERT_FALSE(UdpTelemetryV.ok);

    field_int("a", 1);
    TEST_ASSERT_FALSE(UdpTelemetryV.ok);
}

// A NULL measurement opens the line with nothing, so the point starts at its field set.
void test_null_measurement_opens_an_empty_line(void)
{
    open_line(NULL, sizeof(g_line));
    field_int("a", 1);
    TEST_ASSERT_TRUE(UdpTelemetryV.ok);
    TEST_ASSERT_EQUAL_STRING(" a=1i", g_line);
}

// A host build declares no network stack (PROTOCORE_HAS_NET_STACK 0), so the collector never parses
// and every datagram is refused. RFC 768 acknowledges nothing, so ok reports only that the stack
// took the octets - and here there is no stack to take them.
void test_send_refuses_without_a_network_stack(void)
{
    UdpTelemetryV.collector.addr = "10.0.0.1";
    UdpTelemetryV.collector.port = 8089;
    UdpTelemetry.begin(protocore_udp_telemetry_span());
    TEST_ASSERT_FALSE(UdpTelemetryV.ok);

    open_line("m", sizeof(g_line));
    field_int("a", 1);
    TEST_ASSERT_TRUE(UdpTelemetryV.ok); // the line itself is a complete point

    UdpTelemetry.write(protocore_udp_telemetry_span());
    TEST_ASSERT_FALSE(UdpTelemetryV.ok);

    UdpTelemetryV.payload.data = g_line;
    UdpTelemetryV.payload.len = 6;
    UdpTelemetry.send(protocore_udp_telemetry_span());
    TEST_ASSERT_FALSE(UdpTelemetryV.ok);
}

// A line that is not a point sends nothing, whether or not a stack is underneath.
void test_write_refuses_an_incomplete_line(void)
{
    open_line("m", sizeof(g_line));
    UdpTelemetry.write(protocore_udp_telemetry_span()); // no field set
    TEST_ASSERT_FALSE(UdpTelemetryV.ok);
}
