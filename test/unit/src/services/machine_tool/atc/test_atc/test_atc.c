// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the ATC field-I/O snapshot (services/machine_tool/atc/atc.h).
//
// No standard publishes this snapshot: the ATC specification standardizes the controller's host
// platform and its field-I/O API, not a JSON document, and atc.h says the shape here is this
// library's own. The expectations below are therefore PROPERTIES plus one published rule - RFC 8259
// sec 7, which requires a quotation mark and a reverse solidus inside a JSON string to be escaped.
// The load-bearing case is test_snapshot_partitions_the_map: the whole point of the snapshot is
// that an engine reads sensors from "inputs" and drivers from "outputs", so a point landing in the
// wrong array, or in both, is the failure that matters.

#include "services/machine_tool/atc/atc.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static char g_out[512];

// Two detector inputs and two signal outputs, interleaved so the order in the table is not the
// order in either array.
static AtcPoint g_points[4];
static AtcFieldIo g_io;

static void build_map(void)
{
    g_points[0].name = "det.1";
    g_points[0].is_output = PROTO_FALSE;
    g_points[0].value = 1;
    g_points[1].name = "phase.2.green";
    g_points[1].is_output = PROTO_TRUE;
    g_points[1].value = 0;
    g_points[2].name = "det.2";
    g_points[2].is_output = PROTO_FALSE;
    g_points[2].value = 0;
    g_points[3].name = "phase.4.red";
    g_points[3].is_output = PROTO_TRUE;
    g_points[3].value = 255;
    g_io.points = g_points;
    g_io.count = 4;
}

// Each point appears once, in the array its direction names, in table order. An input is a sensor
// the engine reads; an output is a driver it sets.
void test_snapshot_partitions_the_map(void)
{
    build_map();
    const size_t n = protocore_atc_snapshot_json(&g_io, g_out, sizeof(g_out));
    TEST_ASSERT_EQUAL_STRING("{\"inputs\":["
                             "{\"name\":\"det.1\",\"value\":1},"
                             "{\"name\":\"det.2\",\"value\":0}"
                             "],\"outputs\":["
                             "{\"name\":\"phase.2.green\",\"value\":0},"
                             "{\"name\":\"phase.4.red\",\"value\":255}"
                             "]}",
                             g_out);
    TEST_ASSERT_EQUAL_UINT(strlen(g_out), n);
}

// A map with no points is still a well-formed snapshot: two empty arrays, not an error.
void test_snapshot_of_an_empty_map(void)
{
    AtcFieldIo empty = {NULL, 0};
    const size_t n = protocore_atc_snapshot_json(&empty, g_out, sizeof(g_out));
    TEST_ASSERT_EQUAL_STRING("{\"inputs\":[],\"outputs\":[]}", g_out);
    TEST_ASSERT_EQUAL_UINT(strlen(g_out), n);
}

// A map that is all one direction leaves the other array empty rather than dropping the braces.
void test_snapshot_of_one_direction(void)
{
    AtcPoint only[1];
    only[0].name = "det.1";
    only[0].is_output = PROTO_FALSE;
    only[0].value = 7;
    AtcFieldIo io = {only, 1};
    (void)protocore_atc_snapshot_json(&io, g_out, sizeof(g_out));
    TEST_ASSERT_EQUAL_STRING("{\"inputs\":[{\"name\":\"det.1\",\"value\":7}],\"outputs\":[]}", g_out);
}

// RFC 8259 sec 7: inside a JSON string "the characters that MUST be escaped" include the quotation
// mark and the reverse solidus, and sec 7 gives "\\" as the compact form of a lone reverse solidus.
// A point name carrying either must not break the document a consumer parses.
void test_point_names_are_json_escaped(void)
{
    AtcPoint p[1];
    p[0].name = "a\"b\\c";
    p[0].is_output = PROTO_FALSE;
    p[0].value = 0;
    AtcFieldIo io = {p, 1};
    (void)protocore_atc_snapshot_json(&io, g_out, sizeof(g_out));
    TEST_ASSERT_EQUAL_STRING("{\"inputs\":[{\"name\":\"a\\\"b\\\\c\",\"value\":0}],\"outputs\":[]}", g_out);
}

// The value is an 8-bit point: a bit reads 0 or 1, a byte reads 0..255, and each renders as plain
// decimal digits with no leading zero.
void test_value_range(void)
{
    static const uint8_t V[] = {0, 1, 9, 10, 99, 100, 254, 255};
    for (size_t i = 0; i < sizeof(V) / sizeof(V[0]); i++)
    {
        AtcPoint p[1];
        p[0].name = "v";
        p[0].is_output = PROTO_FALSE;
        p[0].value = V[i];
        AtcFieldIo io = {p, 1};
        (void)protocore_atc_snapshot_json(&io, g_out, sizeof(g_out));

        char want[64];
        char digits[4];
        size_t d = 0;
        uint8_t v = V[i];
        do
        {
            digits[d++] = (char)('0' + (v % 10u));
            v = (uint8_t)(v / 10u);
        } while (v);
        size_t w = 0;
        const char *head = "{\"inputs\":[{\"name\":\"v\",\"value\":";
        memcpy(want, head, strlen(head));
        w = strlen(head);
        while (d)
        {
            want[w++] = digits[--d];
        }
        const char *tail = "}],\"outputs\":[]}";
        memcpy(want + w, tail, strlen(tail) + 1);
        TEST_ASSERT_EQUAL_STRING(want, g_out);
    }
}

// A buffer too small for the whole document reports 0 rather than a truncated snapshot: half a JSON
// object does not parse, and a consumer would take it for a transport error.
void test_snapshot_refuses_a_short_buffer(void)
{
    build_map();
    const size_t full = protocore_atc_snapshot_json(&g_io, g_out, sizeof(g_out));
    TEST_ASSERT_TRUE(full > 0);

    char small[16];
    TEST_ASSERT_EQUAL_UINT(0u, protocore_atc_snapshot_json(&g_io, small, sizeof(small)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_atc_snapshot_json(&g_io, g_out, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_atc_snapshot_json(&g_io, NULL, sizeof(g_out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_atc_snapshot_json(NULL, g_out, sizeof(g_out)));
}

// A buffer of exactly the document's length plus its NUL succeeds, and one octet less does not.
void test_snapshot_buffer_boundary(void)
{
    build_map();
    const size_t full = protocore_atc_snapshot_json(&g_io, g_out, sizeof(g_out));

    char exact[256];
    TEST_ASSERT_TRUE(full + 1 <= sizeof(exact));
    TEST_ASSERT_EQUAL_UINT(full, protocore_atc_snapshot_json(&g_io, exact, full + 1));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_atc_snapshot_json(&g_io, exact, full));
}

// A command drives an output by name, and the getter reads back exactly what was set.
void test_set_output_then_get(void)
{
    build_map();
    proto_bool found = PROTO_FALSE;

    TEST_ASSERT_EQUAL_UINT8(0u, protocore_atc_get(&g_io, "phase.2.green", &found));
    TEST_ASSERT_TRUE(found);

    TEST_ASSERT_TRUE(protocore_atc_set_output(&g_io, "phase.2.green", 1));
    TEST_ASSERT_EQUAL_UINT8(1u, protocore_atc_get(&g_io, "phase.2.green", &found));
    TEST_ASSERT_TRUE(found);

    TEST_ASSERT_TRUE(protocore_atc_set_output(&g_io, "phase.2.green", 200));
    TEST_ASSERT_EQUAL_UINT8(200u, protocore_atc_get(&g_io, "phase.2.green", &found));

    // The set is visible in the snapshot the engine reads back.
    (void)protocore_atc_snapshot_json(&g_io, g_out, sizeof(g_out));
    TEST_ASSERT_NOT_NULL(strstr(g_out, "{\"name\":\"phase.2.green\",\"value\":200}"));
}

// An input is a sensor, not a driver: a command naming one changes nothing.
void test_set_output_refuses_an_input(void)
{
    build_map();
    TEST_ASSERT_FALSE(protocore_atc_set_output(&g_io, "det.1", 0));

    proto_bool found = PROTO_FALSE;
    TEST_ASSERT_EQUAL_UINT8(1u, protocore_atc_get(&g_io, "det.1", &found)); // unchanged
    TEST_ASSERT_TRUE(found);
}

// A name that is not in the map is reported as absent rather than as the value zero, and the
// getter's found flag is what separates the two.
void test_unknown_point(void)
{
    build_map();
    proto_bool found = PROTO_TRUE;
    TEST_ASSERT_EQUAL_UINT8(0u, protocore_atc_get(&g_io, "nope", &found));
    TEST_ASSERT_FALSE(found);

    TEST_ASSERT_FALSE(protocore_atc_set_output(&g_io, "nope", 1));

    // A point whose value really is zero is found.
    found = PROTO_FALSE;
    TEST_ASSERT_EQUAL_UINT8(0u, protocore_atc_get(&g_io, "det.2", &found));
    TEST_ASSERT_TRUE(found);
}

// Names are matched whole and case-sensitively: a prefix of a point name is a different point.
void test_names_match_whole(void)
{
    build_map();
    proto_bool found = PROTO_TRUE;
    (void)protocore_atc_get(&g_io, "det", &found);
    TEST_ASSERT_FALSE(found);

    found = PROTO_TRUE;
    (void)protocore_atc_get(&g_io, "det.1.extra", &found);
    TEST_ASSERT_FALSE(found);

    found = PROTO_TRUE;
    (void)protocore_atc_get(&g_io, "DET.1", &found);
    TEST_ASSERT_FALSE(found);
}

// The found flag is optional; a caller that does not want it passes NULL and still gets the value.
void test_get_without_the_found_flag(void)
{
    build_map();
    TEST_ASSERT_EQUAL_UINT8(1u, protocore_atc_get(&g_io, "det.1", NULL));
    TEST_ASSERT_EQUAL_UINT8(0u, protocore_atc_get(&g_io, "nope", NULL));
}

// A missing map or a missing name is reported, not dereferenced.
void test_accessors_refuse_missing_arguments(void)
{
    build_map();
    proto_bool found = PROTO_TRUE;
    TEST_ASSERT_EQUAL_UINT8(0u, protocore_atc_get(NULL, "det.1", &found));
    TEST_ASSERT_FALSE(found);

    found = PROTO_TRUE;
    TEST_ASSERT_EQUAL_UINT8(0u, protocore_atc_get(&g_io, NULL, &found));
    TEST_ASSERT_FALSE(found);

    TEST_ASSERT_FALSE(protocore_atc_set_output(NULL, "phase.2.green", 1));
    TEST_ASSERT_FALSE(protocore_atc_set_output(&g_io, NULL, 1));
}
