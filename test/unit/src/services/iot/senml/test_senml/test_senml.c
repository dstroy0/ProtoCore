// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the SenML Pack builders and the Record resolver (services/iot/senml/senml.h).
//
// RFC 8428 governs SenML and prints the expectations used here: sec 5.1.1 and sec 5.1.2 give whole
// JSON Packs verbatim, sec 4.3 Table 1 and sec 6 Table 4 give the labels and their CBOR integer map
// keys, and sec 4.6 defines what resolving a Record means.
//
// test_rfc8428_section_5_1_1_example is load-bearing: sec 5.1.1 prints
// [{"n":"urn:dev:ow:10e2073a01080063","u":"Cel","v":23.1}] as a complete Pack, so reproducing it
// character for character pins the array, the object, the label spelling, the member order and the
// Number rendering all at once.

#include "network_drivers/presentation/codec/cbor/cbor.h"
#include "services/iot/senml/senml.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Unity's double assertions are compiled out in this build, so a Number is compared by hand.
static void assert_near(double want, double got, double eps, const char *what)
{
    const double d = (want > got) ? (want - got) : (got - want);
    TEST_ASSERT_TRUE_MESSAGE(d <= eps, what);
}

static char g_json[512];
static uint8_t g_bin[512];
static SenmlResolved g_resolved[8];

static size_t json_build(const SenmlRecord *records, size_t count, size_t cap)
{
    memset(g_json, 0, sizeof(g_json));
    Senml.pack.records = records;
    Senml.pack.count = count;
    Senml.json.buf = g_json;
    Senml.json.cap = cap;
    Senml.json_build(Senml.internal);
    return Senml.n;
}

static size_t binary_build(const SenmlRecord *records, size_t count, size_t cap)
{
    memset(g_bin, 0, sizeof(g_bin));
    Senml.pack.records = records;
    Senml.pack.count = count;
    Senml.binary.codec = &Cbor;
    Senml.binary.buf = g_bin;
    Senml.binary.cap = cap;
    Senml.binary_build(Senml.internal);
    return Senml.n;
}

static size_t resolve(const SenmlRecord *records, size_t count)
{
    memset(g_resolved, 0, sizeof(g_resolved));
    Senml.pack.records = records;
    Senml.pack.count = count;
    Senml.resolved.out = g_resolved;
    Senml.resolved.max = sizeof(g_resolved) / sizeof(g_resolved[0]);
    Senml.resolve(Senml.internal);
    return Senml.n;
}

// RFC 8428 sec 5.1.1 "Single Data Point", printed in full:
//   [
//     {"n":"urn:dev:ow:10e2073a01080063","u":"Cel","v":23.1}
//   ]
void test_rfc8428_section_5_1_1_example(void)
{
    static const SenmlRecord PACK[1] = {{
        .name = "urn:dev:ow:10e2073a01080063",
        .unit = "Cel",
        .value_kind = SENML_VALUE_NUMBER,
        .value = 23.1,
    }};
    static const char WANT[] = "[{\"n\":\"urn:dev:ow:10e2073a01080063\",\"u\":\"Cel\",\"v\":23.1}]";
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT) - 1, json_build(PACK, 1, sizeof(g_json)));
    TEST_ASSERT_TRUE(Senml.ok);
    TEST_ASSERT_EQUAL_STRING(WANT, g_json);
}

// RFC 8428 sec 5.1.2 "Multiple Data Points", printed in full:
//   [
//     {"bn":"urn:dev:ow:10e2073a01080063:","n":"voltage","u":"V","v":120.1},
//     {"n":"current","u":"A","v":1.2}
//   ]
// The member order is the sec 4.1 Base Fields ahead of the sec 4.2 Regular Fields, as printed.
void test_rfc8428_section_5_1_2_example(void)
{
    static const SenmlRecord PACK[2] = {
        {
            .base_name = "urn:dev:ow:10e2073a01080063:",
            .name = "voltage",
            .unit = "V",
            .value_kind = SENML_VALUE_NUMBER,
            .value = 120.1,
        },
        {
            .name = "current",
            .unit = "A",
            .value_kind = SENML_VALUE_NUMBER,
            .value = 1.2,
        },
    };
    static const char WANT[] = "[{\"bn\":\"urn:dev:ow:10e2073a01080063:\",\"n\":\"voltage\",\"u\":\"V\",\"v\":120.1},"
                               "{\"n\":\"current\",\"u\":\"A\",\"v\":1.2}]";
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT) - 1, json_build(PACK, 2, sizeof(g_json)));
    TEST_ASSERT_TRUE(Senml.ok);
    TEST_ASSERT_EQUAL_STRING(WANT, g_json);
}

// A Number that is integral is written as an integer, so a Time or a whole-numbered reading keeps
// full precision instead of being rounded into a fixed number of significant digits. RFC 8428 sec 5
// requires only that receivers handle the IEEE double-precision range, and sec 5.1.3 itself prints
// `"v":20` for a whole reading.
void test_integral_numbers_are_written_as_integers(void)
{
    static const SenmlRecord PACK[2] = {
        {.name = "a", .value_kind = SENML_VALUE_NUMBER, .value = 20.0},
        {.name = "b", .value_kind = SENML_VALUE_NUMBER, .value = -1, .has_time = PROTO_TRUE, .time = 1276020076.0},
    };
    TEST_ASSERT_TRUE(json_build(PACK, 2, sizeof(g_json)) > 0);
    TEST_ASSERT_EQUAL_STRING("[{\"n\":\"a\",\"v\":20},{\"n\":\"b\",\"v\":-1,\"t\":1276020076}]", g_json);
}

// The three value fields of RFC 8428 sec 4.2 and their sec 5 JSON types: Value (v) a Number, String
// Value (vs) a String, Boolean Value (vb) a Boolean. A Record with none carries no value member.
void test_the_three_value_fields(void)
{
    static const SenmlRecord PACK[4] = {
        {.name = "s", .value_kind = SENML_VALUE_STRING, .string_value = "hi"},
        {.name = "t", .value_kind = SENML_VALUE_BOOLEAN, .boolean_value = PROTO_TRUE},
        {.name = "f", .value_kind = SENML_VALUE_BOOLEAN, .boolean_value = PROTO_FALSE},
        {.base_name = "base:", .value_kind = SENML_VALUE_NONE},
    };
    TEST_ASSERT_TRUE(json_build(PACK, 4, sizeof(g_json)) > 0);
    TEST_ASSERT_EQUAL_STRING("[{\"n\":\"s\",\"vs\":\"hi\"},{\"n\":\"t\",\"vb\":true},{\"n\":\"f\",\"vb\":false},"
                             "{\"bn\":\"base:\"}]",
                             g_json);

    // An empty Pack is the empty array, which is still a well-formed sec 3 Pack.
    TEST_ASSERT_EQUAL_UINT(2u, json_build(PACK, 0, sizeof(g_json)));
    TEST_ASSERT_EQUAL_STRING("[]", g_json);
}

// RFC 8428 sec 6: the CBOR representation "uses integers for the labels, as defined in Table 4",
// which gives Base Name -2, Base Time -3, Name 0, Unit 1, Value 2, String Value 3, Boolean Value 4
// and Time 6. RFC 8949 encodes -2 as 0x21 and -3 as 0x22 (major type 1 carrying -1-n) and the
// non-negative labels as themselves; the RFC 8428 sec 6 hex dump shows exactly those octets.
void test_cbor_table_4_integer_map_keys(void)
{
    static const SenmlRecord PACK[1] = {{
        .base_name = "u:",
        .has_base_time = PROTO_TRUE,
        .base_time = 1.0,
        .name = "n",
        .unit = "V",
        .value_kind = SENML_VALUE_NUMBER,
        .value = 2.0,
        .has_time = PROTO_TRUE,
        .time = 3.0,
    }};
    // array(1), map(6), then the six label / value pairs in the module's field order.
    static const uint8_t WANT[] = {
        0x81,                 // array of one Record
        0xA6,                 // map of six entries
        0x21, 0x62, 'u', ':', // -2 (bn), text(2) "u:"
        0x22, 0x01,           // -3 (bt), unsigned 1
        0x00, 0x61, 'n',      // 0 (n), text(1) "n"
        0x01, 0x61, 'V',      // 1 (u), text(1) "V"
        0x02, 0x02,           // 2 (v), unsigned 2
        0x06, 0x03,           // 6 (t), unsigned 3
    };
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), binary_build(PACK, 1, sizeof(g_bin)));
    TEST_ASSERT_TRUE(Senml.ok);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT, g_bin, sizeof(WANT));

    // String Value 3 and Boolean Value 4, with RFC 8949's true (0xf5) and false (0xf4).
    static const SenmlRecord VALUES[2] = {
        {.name = "s", .value_kind = SENML_VALUE_STRING, .string_value = "hi"},
        {.name = "b", .value_kind = SENML_VALUE_BOOLEAN, .boolean_value = PROTO_TRUE},
    };
    static const uint8_t WANT_VALUES[] = {
        0x82,                  // array of two Records
        0xA2, 0x00, 0x61, 's', // map(2), 0 (n), "s"
        0x03, 0x62, 'h',  'i', // 3 (vs), text(2) "hi"
        0xA2, 0x00, 0x61, 'b', // map(2), 0 (n), "b"
        0x04, 0xF5,            // 4 (vb), true
    };
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT_VALUES), binary_build(VALUES, 2, sizeof(g_bin)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT_VALUES, g_bin, sizeof(WANT_VALUES));
}

// A Number that is not integral is encoded as a floating-point item. RFC 8949 writes a single
// precision float as 0xfa and four big-endian octets; 23.5 is exact in binary32 as sign 0,
// exponent 4 + 127 = 131 = 0x83 and mantissa 0.46875 * 2^23 = 0x3C0000, so the bits are
// (131 << 23) + 0x3C0000 = 0x41800000 + 0x003C0000 = 0x41BC0000.
void test_cbor_non_integral_number_is_a_float(void)
{
    static const SenmlRecord PACK[1] = {{
        .name = "t",
        .value_kind = SENML_VALUE_NUMBER,
        .value = 23.5,
    }};
    static const uint8_t WANT[] = {
        0x81, 0xA2, 0x00, 0x61, 't', 0x02, 0xFA, 0x41, 0xBC, 0x00, 0x00,
    };
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), binary_build(PACK, 1, sizeof(g_bin)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT, g_bin, sizeof(WANT));
}

// RFC 8428 sec 4.6: a resolved Record has no Base Fields and no relative Time, so its Name is the
// Base Name concatenated with the Name and its Time is the Base Time added to the Time. The Pack
// below is the sec 5.1.2 third example, less the Base Unit and Base Version this module does not
// carry:
//   {"bn":"urn:dev:ow:10e2073a0108006:","bt":1.276020076001e+09,"n":"voltage","v":120.1},
//   {"n":"current","t":-5,"v":1.2},
//   {"n":"current","t":-4,"v":1.3}
void test_resolve_folds_base_name_and_base_time(void)
{
    static const SenmlRecord PACK[3] = {
        {
            .base_name = "urn:dev:ow:10e2073a0108006:",
            .has_base_time = PROTO_TRUE,
            .base_time = 1276020076.001,
            .name = "voltage",
            .unit = "V",
            .value_kind = SENML_VALUE_NUMBER,
            .value = 120.1,
        },
        {.name = "current", .value_kind = SENML_VALUE_NUMBER, .value = 1.2, .has_time = PROTO_TRUE, .time = -5},
        {.name = "current", .value_kind = SENML_VALUE_NUMBER, .value = 1.3, .has_time = PROTO_TRUE, .time = -4},
    };
    TEST_ASSERT_EQUAL_UINT(3u, resolve(PACK, 3));
    TEST_ASSERT_TRUE(Senml.ok);

    TEST_ASSERT_EQUAL_STRING("urn:dev:ow:10e2073a0108006:voltage", g_resolved[0].name);
    TEST_ASSERT_TRUE(g_resolved[0].has_time);
    assert_near(1276020076.001, g_resolved[0].time, 1e-6, "g_resolved[0].time");
    TEST_ASSERT_EQUAL_STRING("V", g_resolved[0].unit);
    TEST_ASSERT_EQUAL_INT(SENML_VALUE_NUMBER, g_resolved[0].value_kind);
    assert_near(120.1, g_resolved[0].value, 1e-9, "g_resolved[0].value");

    // The Base Name and Base Time carry forward to the Records after the one that stated them.
    TEST_ASSERT_EQUAL_STRING("urn:dev:ow:10e2073a0108006:current", g_resolved[1].name);
    assert_near(1276020076.001 - 5.0, g_resolved[1].time, 1e-6, "g_resolved[1].time");
    TEST_ASSERT_EQUAL_STRING("urn:dev:ow:10e2073a0108006:current", g_resolved[2].name);
    assert_near(1276020076.001 - 4.0, g_resolved[2].time, 1e-6, "g_resolved[2].time");
}

// A later Base Name or Base Time overrides the active one from the Record that states it onward, and
// a Record with no Time at all before any Base Time carries none.
void test_resolve_overrides_and_absent_time(void)
{
    static const SenmlRecord PACK[4] = {
        {.name = "a", .value_kind = SENML_VALUE_NUMBER, .value = 1},
        {.base_name = "x/", .name = "b", .value_kind = SENML_VALUE_NUMBER, .value = 2},
        {.base_name = "y/",
         .has_base_time = PROTO_TRUE,
         .base_time = 100.0,
         .name = "c",
         .value_kind = SENML_VALUE_NUMBER,
         .value = 3},
        {.name = "d", .value_kind = SENML_VALUE_NUMBER, .value = 4, .has_time = PROTO_TRUE, .time = 7},
    };
    TEST_ASSERT_EQUAL_UINT(4u, resolve(PACK, 4));
    TEST_ASSERT_EQUAL_STRING("a", g_resolved[0].name);
    TEST_ASSERT_FALSE(g_resolved[0].has_time);
    TEST_ASSERT_EQUAL_STRING("x/b", g_resolved[1].name);
    TEST_ASSERT_FALSE(g_resolved[1].has_time);
    TEST_ASSERT_EQUAL_STRING("y/c", g_resolved[2].name);
    TEST_ASSERT_TRUE(g_resolved[2].has_time);
    assert_near(100.0, g_resolved[2].time, 1e-9, "g_resolved[2].time");
    TEST_ASSERT_EQUAL_STRING("y/d", g_resolved[3].name);
    assert_near(107.0, g_resolved[3].time, 1e-9, "g_resolved[3].time");

    // A resolve stops at the room the caller lent, and reports how many Records it filled.
    Senml.pack.records = PACK;
    Senml.pack.count = 4;
    Senml.resolved.out = g_resolved;
    Senml.resolved.max = 2;
    Senml.resolve(Senml.internal);
    TEST_ASSERT_TRUE(Senml.ok);
    TEST_ASSERT_EQUAL_UINT(2u, Senml.n);
}

// A resolved Name longer than the array it lands in leaves an empty Name rather than a truncated one,
// which would name a different resource.
void test_resolve_refuses_to_truncate_a_name(void)
{
    static char long_base[PROTOCORE_SENML_RESOLVED_NAME_MAX + 8];
    memset(long_base, 'a', sizeof(long_base) - 1);
    long_base[sizeof(long_base) - 1] = '\0';
    const SenmlRecord PACK[1] = {{.base_name = long_base, .name = "x", .value_kind = SENML_VALUE_NUMBER, .value = 1}};
    TEST_ASSERT_EQUAL_UINT(1u, resolve(PACK, 1));
    TEST_ASSERT_EQUAL_STRING("", g_resolved[0].name);
}

// Both builders report 0 bytes when the caller's buffer will not hold the whole Pack, so a consumer
// never sees a Pack that ends mid-Record.
void test_builders_report_zero_on_a_short_buffer(void)
{
    static const SenmlRecord PACK[1] = {{
        .name = "urn:dev:ow:10e2073a01080063",
        .unit = "Cel",
        .value_kind = SENML_VALUE_NUMBER,
        .value = 23.1,
    }};
    const size_t whole = json_build(PACK, 1, sizeof(g_json));
    TEST_ASSERT_TRUE(whole > 0);
    for (size_t cap = 1; cap <= whole; cap++)
    {
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, json_build(PACK, 1, cap), "a short buffer must report 0");
        TEST_ASSERT_FALSE(Senml.ok);
    }
    TEST_ASSERT_EQUAL_UINT(whole, json_build(PACK, 1, whole + 1));

    const size_t bin_whole = binary_build(PACK, 1, sizeof(g_bin));
    TEST_ASSERT_TRUE(bin_whole > 0);
    for (size_t cap = 0; cap < bin_whole; cap++)
    {
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, binary_build(PACK, 1, cap), "a short buffer must report 0");
        TEST_ASSERT_FALSE(Senml.ok);
    }
    TEST_ASSERT_EQUAL_UINT(bin_whole, binary_build(PACK, 1, bin_whole));
}

// A call with no destination, no Records behind a nonzero count, or no codec is refused rather than
// written through.
void test_missing_arguments_are_refused(void)
{
    static const SenmlRecord PACK[1] = {{.name = "a", .value_kind = SENML_VALUE_NUMBER, .value = 1}};

    Senml.pack.records = PACK;
    Senml.pack.count = 1;
    Senml.json.buf = NULL;
    Senml.json.cap = sizeof(g_json);
    Senml.json_build(Senml.internal);
    TEST_ASSERT_FALSE(Senml.ok);
    TEST_ASSERT_EQUAL_UINT(0u, Senml.n);

    Senml.json.buf = g_json;
    Senml.pack.records = NULL;
    Senml.json_build(Senml.internal);
    TEST_ASSERT_FALSE(Senml.ok);

    Senml.pack.records = PACK;
    Senml.binary.codec = NULL;
    Senml.binary.buf = g_bin;
    Senml.binary.cap = sizeof(g_bin);
    Senml.binary_build(Senml.internal);
    TEST_ASSERT_FALSE(Senml.ok);

    Senml.binary.codec = &Cbor;
    Senml.binary.buf = NULL;
    Senml.binary_build(Senml.internal);
    TEST_ASSERT_FALSE(Senml.ok);

    Senml.pack.records = NULL;
    Senml.resolved.out = g_resolved;
    Senml.resolved.max = 4;
    Senml.resolve(Senml.internal);
    TEST_ASSERT_FALSE(Senml.ok);
    Senml.pack.records = PACK;
    Senml.resolved.out = NULL;
    Senml.resolve(Senml.internal);
    TEST_ASSERT_FALSE(Senml.ok);
}
