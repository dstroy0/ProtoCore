// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/mtconnect: the MTConnectStreams + MTConnectError document builders.

#include "services/machine_tool/mtconnect/mtconnect.h"
#include <stdio.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static proto_bool contains(const char *hay, const char *needle)
{
    return strstr(hay, needle) != NULL;
}

void test_streams_document(void)
{
    char buf[1024];
    pc_mtc_streams s;
    pc_mtc_streams_begin(&s, buf, sizeof(buf), 1500, 42, "cnc1");
    pc_mtc_streams_add(&s, PC_MTC_EVENT, "Availability", "avail", 40, "2026-07-06T00:00:00Z", "AVAILABLE");
    pc_mtc_streams_add(&s, PC_MTC_SAMPLE, "Position", "xpos", 41, "2026-07-06T00:00:01Z", "12.5");
    pc_mtc_streams_add(&s, PC_MTC_CONDITION, "SystemCondition", "sys", 42, "2026-07-06T00:00:02Z", "Fault");
    size_t n = pc_mtc_streams_end(&s);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);

    TEST_ASSERT_TRUE(contains(buf, "<MTConnectStreams"));
    TEST_ASSERT_TRUE(contains(buf, "instanceId=\"1500\""));
    TEST_ASSERT_TRUE(contains(buf, "nextSequence=\"42\""));
    TEST_ASSERT_TRUE(contains(buf, "<DeviceStream name=\"cnc1\">"));
    TEST_ASSERT_TRUE(contains(buf, "<Events><Availability dataItemId=\"avail\" sequence=\"40\""));
    TEST_ASSERT_TRUE(contains(buf, ">AVAILABLE</Availability></Events>"));
    TEST_ASSERT_TRUE(contains(buf, "<Samples><Position dataItemId=\"xpos\" sequence=\"41\""));
    TEST_ASSERT_TRUE(contains(buf, ">12.5</Position></Samples>"));
    TEST_ASSERT_TRUE(contains(buf, "<Condition><Fault type=\"SystemCondition\" dataItemId=\"sys\""));
    // Properly closed.
    TEST_ASSERT_TRUE(contains(buf, "</ComponentStream></DeviceStream></Streams></MTConnectStreams>"));
}

void test_streams_escapes_value(void)
{
    char buf[512];
    pc_mtc_streams s;
    pc_mtc_streams_begin(&s, buf, sizeof(buf), 1, 1, "d");
    pc_mtc_streams_add(&s, PC_MTC_EVENT, "Message", "msg", 1, "T", "a<b&c");
    pc_mtc_streams_end(&s);
    TEST_ASSERT_TRUE(contains(buf, ">a&lt;b&amp;c</Message>"));
}

void test_error_document(void)
{
    char buf[512];
    size_t n = pc_mtc_error(1500, "UNSUPPORTED", "bad path", buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(contains(buf, "<MTConnectError"));
    TEST_ASSERT_TRUE(contains(buf, "instanceId=\"1500\""));
    TEST_ASSERT_TRUE(contains(buf, "<Error errorCode=\"UNSUPPORTED\">bad path</Error>"));
}

void test_overflow_returns_zero(void)
{
    char buf[40];
    pc_mtc_streams s;
    pc_mtc_streams_begin(&s, buf, sizeof(buf), 1, 1, "device");
    pc_mtc_streams_add(&s, PC_MTC_SAMPLE, "Position", "x", 1, "T", "1.0");
    TEST_ASSERT_EQUAL_size_t(0, pc_mtc_streams_end(&s)); // did not fit
}

void test_escape_gt_quote_and_overflow()
{
    char buf[512];
    pc_mtc_streams s;
    pc_mtc_streams_begin(&s, buf, sizeof(buf), 1, 1, "d");
    pc_mtc_streams_add(&s, PC_MTC_EVENT, "Message", "msg", 1, "T", "a>b\"c");
    pc_mtc_streams_end(&s);
    TEST_ASSERT_NOT_NULL(strstr(buf, "&gt;"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "&quot;"));
    // Stream overflow closes at end(); error document overflow fails closed.
    char tiny[64];
    pc_mtc_streams s2;
    pc_mtc_streams_begin(&s2, tiny, sizeof(tiny), 1, 1, "device-with-a-very-long-name");
    pc_mtc_streams_add(&s2, PC_MTC_SAMPLE, "Position", "xpos", 1, "2026-07-06T00:00:01Z", "12.5");
    TEST_ASSERT_EQUAL_size_t(0, pc_mtc_streams_end(&s2));
    char t2[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_mtc_error(1, "X", "y", t2, sizeof(t2)));
}

void test_devices_probe_document(void)
{
    char buf[1024];
    pc_mtc_streams s;
    pc_mtc_devices_begin(&s, buf, sizeof(buf), 1500, "dev1", "cnc1", "uuid-abc");
    pc_mtc_devices_add_item(&s, PC_MTC_EVENT, "avail", "Availability", NULL, NULL);
    pc_mtc_devices_add_item(&s, PC_MTC_SAMPLE, "xpos", "Position", "Xabs", "MILLIMETER");
    pc_mtc_devices_add_item(&s, PC_MTC_CONDITION, "sys", "SystemCondition", NULL, NULL);
    size_t n = pc_mtc_devices_end(&s);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);

    TEST_ASSERT_TRUE(contains(buf, "<MTConnectDevices"));
    TEST_ASSERT_TRUE(contains(buf, "instanceId=\"1500\""));
    TEST_ASSERT_TRUE(contains(buf, "<Devices><Device id=\"dev1\" name=\"cnc1\" uuid=\"uuid-abc\"><DataItems>"));
    TEST_ASSERT_TRUE(contains(buf, "<DataItem category=\"EVENT\" id=\"avail\" type=\"Availability\"/>"));
    // name + units are emitted only when present.
    TEST_ASSERT_TRUE(contains(
        buf, "<DataItem category=\"SAMPLE\" id=\"xpos\" type=\"Position\" name=\"Xabs\" units=\"MILLIMETER\"/>"));
    TEST_ASSERT_TRUE(contains(buf, "<DataItem category=\"CONDITION\" id=\"sys\" type=\"SystemCondition\"/>"));
    TEST_ASSERT_TRUE(contains(buf, "</DataItems></Device></Devices></MTConnectDevices>"));
}

void test_devices_escape_and_overflow(void)
{
    char buf[1024];
    pc_mtc_streams s;
    pc_mtc_devices_begin(&s, buf, sizeof(buf), 1, "d<1", "n&m", "u");
    pc_mtc_devices_add_item(&s, PC_MTC_EVENT, "i\"d", "T>y", NULL, NULL);
    TEST_ASSERT_TRUE(pc_mtc_devices_end(&s) > 0);
    TEST_ASSERT_TRUE(contains(buf, "id=\"d&lt;1\" name=\"n&amp;m\""));
    TEST_ASSERT_TRUE(contains(buf, "id=\"i&quot;d\" type=\"T&gt;y\""));
    // A device document that does not fit fails closed.
    char tiny[40];
    pc_mtc_streams s2;
    pc_mtc_devices_begin(&s2, tiny, sizeof(tiny), 1, "dev", "device-with-a-very-long-name", "uuid");
    pc_mtc_devices_add_item(&s2, PC_MTC_SAMPLE, "xpos", "Position", "Xabs", "MILLIMETER");
    TEST_ASSERT_EQUAL_size_t(0, pc_mtc_devices_end(&s2));
}

void test_assets_document(void)
{
    char buf[1024];
    pc_mtc_streams s;
    pc_mtc_assets_begin(&s, buf, sizeof(buf), 1500, 2, 1024);
    pc_mtc_assets_cutting_tool_begin(&s, "tool-1", "SN-42", "T17", "uuid-abc", "2026-07-09T00:00:00Z");
    pc_mtc_assets_tool_life(&s, "MINUTES", "DOWN", "100", "42");
    pc_mtc_assets_cutting_tool_end(&s);
    // A second tool with only the required assetId - optional attrs omitted.
    pc_mtc_assets_cutting_tool_begin(&s, "tool-2", NULL, NULL, NULL, NULL);
    pc_mtc_assets_tool_life(&s, "PART_COUNT", "UP", NULL, "7");
    pc_mtc_assets_cutting_tool_end(&s);
    size_t n = pc_mtc_assets_end(&s);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);

    TEST_ASSERT_TRUE(contains(buf, "<MTConnectAssets"));
    TEST_ASSERT_TRUE(contains(buf, "instanceId=\"1500\" version=\"1.4\" assetBufferSize=\"1024\" assetCount=\"2\""));
    TEST_ASSERT_TRUE(contains(
        buf, "<Assets><CuttingTool assetId=\"tool-1\" serialNumber=\"SN-42\" toolId=\"T17\" deviceUuid=\"uuid-abc\" "
             "timestamp=\"2026-07-09T00:00:00Z\"><CuttingToolLifeCycle>"));
    TEST_ASSERT_TRUE(contains(buf, "<ToolLife type=\"MINUTES\" countDirection=\"DOWN\" limit=\"100\">42</ToolLife>"));
    TEST_ASSERT_TRUE(contains(buf, "</CuttingToolLifeCycle></CuttingTool>"));
    // Second tool: no optional attrs, no limit.
    TEST_ASSERT_TRUE(contains(buf, "<CuttingTool assetId=\"tool-2\"><CuttingToolLifeCycle>"));
    TEST_ASSERT_TRUE(contains(buf, "<ToolLife type=\"PART_COUNT\" countDirection=\"UP\">7</ToolLife>"));
    TEST_ASSERT_TRUE(contains(buf, "</Assets></MTConnectAssets>"));
}

void test_assets_escape_and_overflow(void)
{
    char buf[1024];
    pc_mtc_streams s;
    pc_mtc_assets_begin(&s, buf, sizeof(buf), 1, 1, 8);
    pc_mtc_assets_cutting_tool_begin(&s, "a<1", "s&n", NULL, NULL, NULL);
    pc_mtc_assets_tool_life(&s, "WEAR", "UP", NULL, "1>2");
    pc_mtc_assets_cutting_tool_end(&s);
    TEST_ASSERT_TRUE(pc_mtc_assets_end(&s) > 0);
    TEST_ASSERT_TRUE(contains(buf, "assetId=\"a&lt;1\" serialNumber=\"s&amp;n\""));
    TEST_ASSERT_TRUE(contains(buf, ">1&gt;2</ToolLife>"));
    // An asset document that does not fit fails closed.
    char tiny[40];
    pc_mtc_streams s2;
    pc_mtc_assets_begin(&s2, tiny, sizeof(tiny), 1, 1, 8);
    pc_mtc_assets_cutting_tool_begin(&s2, "a-tool-with-a-very-long-asset-id", NULL, NULL, NULL, NULL);
    pc_mtc_assets_cutting_tool_end(&s2);
    TEST_ASSERT_EQUAL_size_t(0, pc_mtc_assets_end(&s2));
}

void test_sample_buffer_and_query(void)
{
    pc_mtc_sample_buffer b;
    pc_mtc_sample_buffer_init(&b, 1);
    TEST_ASSERT_EQUAL_UINT64(1, pc_mtc_sample_buffer_add(&b, PC_MTC_SAMPLE, "Position", "xpos", "T1", "1.0"));
    TEST_ASSERT_EQUAL_UINT64(2, pc_mtc_sample_buffer_add(&b, PC_MTC_SAMPLE, "Position", "xpos", "T2", "2.0"));
    TEST_ASSERT_EQUAL_UINT64(3, pc_mtc_sample_buffer_add(&b, PC_MTC_EVENT, "Execution", "exec", "T3", "ACTIVE"));

    char buf[1024];
    size_t n = pc_mtc_sample_query(&b, buf, sizeof(buf), 1500, "cnc1", 1, 10);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
    // Full sample-cursor header.
    TEST_ASSERT_TRUE(contains(buf, "instanceId=\"1500\" version=\"1.4\" bufferSize=\""));
    TEST_ASSERT_TRUE(contains(buf, "firstSequence=\"1\" lastSequence=\"3\" nextSequence=\"4\""));
    TEST_ASSERT_TRUE(contains(buf, "<Position dataItemId=\"xpos\" sequence=\"1\" timestamp=\"T1\">1.0</Position>"));
    TEST_ASSERT_TRUE(
        contains(buf, "<Execution dataItemId=\"exec\" sequence=\"3\" timestamp=\"T3\">ACTIVE</Execution>"));

    // A windowed request: one observation from sequence 2, so nextSequence advances to 3.
    n = pc_mtc_sample_query(&b, buf, sizeof(buf), 1500, "cnc1", 2, 1);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(contains(buf, "firstSequence=\"1\" lastSequence=\"3\" nextSequence=\"3\""));
    TEST_ASSERT_TRUE(contains(buf, "sequence=\"2\" timestamp=\"T2\">2.0</Position>"));
    TEST_ASSERT_FALSE(contains(buf, "timestamp=\"T3\"")); // count=1 stopped before seq 3
}

void test_sample_buffer_eviction(void)
{
    pc_mtc_sample_buffer b;
    pc_mtc_sample_buffer_init(&b, 1);
    // Overfill the ring: PC_MTC_SAMPLE_BUFFER + 8 observations, so the oldest 8 are evicted.
    const uint32_t total = PC_MTC_SAMPLE_BUFFER + 8;
    for (uint32_t i = 1; i <= total; i++)
    {
        char ts[16];
        int p = 0;
        ts[p++] = 'T';
        // tiny uint->decimal
        char d[12];
        int q = 0;
        uint32_t v = i;
        do
        {
            d[q++] = (char)('0' + v % 10);
            v /= 10;
        } while (v);
        while (q > 0)
        {
            ts[p++] = d[--q];
        }
        ts[p] = '\0';
        pc_mtc_sample_buffer_add(&b, PC_MTC_SAMPLE, "Position", "xpos", ts, "9.9");
    }
    // Retained window is [total-BUFFER+1, total]; first advanced by 8.
    char buf[8192];
    size_t n = pc_mtc_sample_query(&b, buf, sizeof(buf), 1, "d", 1 /* stale -> clamps up */, 1000);
    TEST_ASSERT_TRUE(n > 0);
    char expect[96];
    snprintf(expect, sizeof(expect), "firstSequence=\"%u\" lastSequence=\"%u\" nextSequence=\"%u\"",
             (unsigned)(total - PC_MTC_SAMPLE_BUFFER + 1), (unsigned)total, (unsigned)(total + 1));
    TEST_ASSERT_TRUE(contains(buf, expect));
    // The oldest kept observation carries the sliding first sequence, not sequence 1.
    char oldest[48];
    snprintf(oldest, sizeof(oldest), "sequence=\"%u\"", (unsigned)(total - PC_MTC_SAMPLE_BUFFER + 1));
    TEST_ASSERT_TRUE(contains(buf, oldest));
    TEST_ASSERT_FALSE(contains(buf, "sequence=\"1\"")); // evicted
}

void test_sample_query_future_and_empty(void)
{
    pc_mtc_sample_buffer b;
    pc_mtc_sample_buffer_init(&b, 5);
    pc_mtc_sample_buffer_add(&b, PC_MTC_SAMPLE, "Position", "xpos", "T", "1.0"); // seq 5

    char buf[512];
    // A `from` past the newest yields no observations and nextSequence = the buffer's next (6).
    size_t n = pc_mtc_sample_query(&b, buf, sizeof(buf), 1, "d", 100, 10);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(contains(buf, "firstSequence=\"5\" lastSequence=\"5\" nextSequence=\"6\""));
    TEST_ASSERT_FALSE(contains(buf, "<Position"));

    // An empty buffer answers with lastSequence = first-1 and no observations.
    pc_mtc_sample_buffer e;
    pc_mtc_sample_buffer_init(&e, 1);
    n = pc_mtc_sample_query(&e, buf, sizeof(buf), 1, "d", 1, 10);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(contains(buf, "firstSequence=\"1\" lastSequence=\"0\" nextSequence=\"1\""));

    // Fail-closed on a buffer too small for even the header.
    char tiny[32];
    TEST_ASSERT_EQUAL_size_t(0, pc_mtc_sample_query(&b, tiny, sizeof(tiny), 1, "device-name", 5, 10));
}

// Every string a streams observation takes is optional: a null becomes an empty attribute rather
// than a dereference, and a null CONDITION value defaults the sub-element to Normal.
void test_streams_null_strings(void)
{
    char buf[1024];
    pc_mtc_streams s;
    pc_mtc_streams_begin(&s, buf, sizeof(buf), 1, 1, NULL);
    pc_mtc_streams_add(&s, PC_MTC_SAMPLE, NULL, NULL, 7, NULL, NULL);
    pc_mtc_streams_add(&s, PC_MTC_CONDITION, NULL, NULL, 8, NULL, NULL);
    size_t n = pc_mtc_streams_end(&s);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
    TEST_ASSERT_TRUE(contains(buf, "<DeviceStream name=\"\">"));
    TEST_ASSERT_TRUE(contains(buf, "<Samples>< dataItemId=\"\" sequence=\"7\" timestamp=\"\"></></Samples>"));
    TEST_ASSERT_TRUE(
        contains(buf, "<Condition><Normal type=\"\" dataItemId=\"\" sequence=\"8\" timestamp=\"\"/></Condition>"));
}

// Each document builder rejects a null buffer and a zero capacity independently, and stays safe to
// drive to completion afterwards (every writer is a no-op once ok is cleared).
void test_builders_reject_null_buffer_and_zero_cap(void)
{
    char b[64];
    pc_mtc_streams s;

    pc_mtc_streams_begin(&s, NULL, sizeof(b), 1, 1, "d");
    TEST_ASSERT_FALSE(s.ok);
    pc_mtc_streams_add(&s, PC_MTC_EVENT, "T", "i", 1, "ts", "v");
    TEST_ASSERT_EQUAL_size_t(0, pc_mtc_streams_end(&s));
    pc_mtc_streams_begin(&s, b, 0, 1, 1, "d");
    TEST_ASSERT_FALSE(s.ok);
    TEST_ASSERT_EQUAL_size_t(0, pc_mtc_streams_end(&s));

    pc_mtc_devices_begin(&s, NULL, sizeof(b), 1, "d", "n", "u");
    TEST_ASSERT_FALSE(s.ok);
    pc_mtc_devices_add_item(&s, PC_MTC_EVENT, "i", "T", "n", "mm");
    TEST_ASSERT_EQUAL_size_t(0, pc_mtc_devices_end(&s));
    pc_mtc_devices_begin(&s, b, 0, 1, "d", "n", "u");
    TEST_ASSERT_FALSE(s.ok);
    TEST_ASSERT_EQUAL_size_t(0, pc_mtc_devices_end(&s));

    pc_mtc_assets_begin(&s, NULL, sizeof(b), 1, 1, 1);
    TEST_ASSERT_FALSE(s.ok);
    pc_mtc_assets_cutting_tool_begin(&s, "a", NULL, NULL, NULL, NULL);
    pc_mtc_assets_cutting_tool_end(&s);
    TEST_ASSERT_EQUAL_size_t(0, pc_mtc_assets_end(&s));
    pc_mtc_assets_begin(&s, b, 0, 1, 1, 1);
    TEST_ASSERT_FALSE(s.ok);
    TEST_ASSERT_EQUAL_size_t(0, pc_mtc_assets_end(&s));

    TEST_ASSERT_EQUAL_size_t(0, pc_mtc_error(1, "E", "m", NULL, sizeof(b)));
    TEST_ASSERT_EQUAL_size_t(0, pc_mtc_error(1, "E", "m", b, 0));
}

// The error document takes null strings, and at EVERY capacity below the one it needs it fails
// closed without writing past the buffer it was handed. The sweep walks the boundary through both
// writers - the whole-string one and the per-character escape one.
void test_error_null_strings_and_capacity_sweep(void)
{
    char buf[512];
    size_t n = pc_mtc_error(7, NULL, NULL, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(contains(buf, "<Errors><Error errorCode=\"\"></Error></Errors>"));

    char full[512];
    const size_t complete = pc_mtc_error(7, "AB", "CD", full, sizeof(full));
    TEST_ASSERT_TRUE(complete > 0);
    for (size_t cap = 1; cap <= complete + 1; cap++)
    {
        char small[512];
        memset(small, 0x7E, sizeof(small));
        size_t got = pc_mtc_error(7, "AB", "CD", small, cap);
        // One byte beyond the document is needed for the NUL, so only cap > complete succeeds.
        TEST_ASSERT_EQUAL_size_t(cap > complete ? complete : 0, got);
        for (size_t i = cap; i < sizeof(small); i++)
        {
            TEST_ASSERT_EQUAL_HEX8(0x7E, (uint8_t)small[i]); // nothing written past the declared capacity
        }
    }
}

// A probe DataItem: null id/type become empty attributes, and an EMPTY optional name/units is
// omitted exactly as a null one is.
void test_devices_null_ids_and_empty_optionals(void)
{
    char buf[1024];
    pc_mtc_streams s;
    pc_mtc_devices_begin(&s, buf, sizeof(buf), 1, NULL, NULL, NULL);
    pc_mtc_devices_add_item(&s, PC_MTC_SAMPLE, NULL, NULL, "", "");
    size_t n = pc_mtc_devices_end(&s);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(contains(buf, "<Devices><Device id=\"\" name=\"\" uuid=\"\"><DataItems>"));
    // Self-closing straight after type: the empty name/units were dropped, not emitted.
    TEST_ASSERT_TRUE(contains(buf, "<DataItem category=\"SAMPLE\" id=\"\" type=\"\"/>"));
    TEST_ASSERT_FALSE(contains(buf, "units="));
}

// A cutting tool whose optional attributes are all empty strings, and a ToolLife with null
// type/countDirection/value plus an empty limit.
void test_assets_empty_optionals_and_null_strings(void)
{
    char buf[1024];
    pc_mtc_streams s;
    pc_mtc_assets_begin(&s, buf, sizeof(buf), 1, 0, 0);
    pc_mtc_assets_cutting_tool_begin(&s, NULL, "", "", "", "");
    pc_mtc_assets_tool_life(&s, NULL, NULL, "", NULL);
    pc_mtc_assets_cutting_tool_end(&s);
    size_t n = pc_mtc_assets_end(&s);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
    TEST_ASSERT_TRUE(contains(buf, "assetBufferSize=\"0\" assetCount=\"0\""));
    TEST_ASSERT_TRUE(contains(buf, "<Assets><CuttingTool assetId=\"\"><CuttingToolLifeCycle>"));
    TEST_ASSERT_TRUE(contains(buf, "<ToolLife type=\"\" countDirection=\"\"></ToolLife>"));
    TEST_ASSERT_FALSE(contains(buf, "serialNumber")); // empty optionals omitted
    TEST_ASSERT_FALSE(contains(buf, "limit"));
}

// The sample ring stores bounded copies: a null field becomes an empty string and an over-long one
// is truncated to its field cap rather than overrunning it. A start sequence of 0 means 1.
void test_sample_buffer_null_and_truncated_fields(void)
{
    pc_mtc_sample_buffer b;
    pc_mtc_sample_buffer_init(&b, 0);
    TEST_ASSERT_EQUAL_UINT64(1, b.next_seq);
    TEST_ASSERT_EQUAL_UINT64(1, pc_mtc_sample_buffer_add(&b, PC_MTC_EVENT, NULL, NULL, NULL, NULL));

    const char *long_type = "AVeryLongDataItemTypeNameThatExceedsTheCap";
    TEST_ASSERT_TRUE(strlen(long_type) > PC_MTC_STR_MAX);
    TEST_ASSERT_EQUAL_UINT64(2, pc_mtc_sample_buffer_add(&b, PC_MTC_SAMPLE, long_type, "x", "T2", "1.0"));

    char buf[4096];
    size_t n = pc_mtc_sample_query(&b, buf, sizeof(buf), 1, NULL, 1, 10);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
    TEST_ASSERT_TRUE(contains(buf, "<DeviceStream name=\"\">")); // null device name
    TEST_ASSERT_TRUE(contains(buf, "< dataItemId=\"\" sequence=\"1\" timestamp=\"\"></>"));

    char truncated[PC_MTC_STR_MAX + 1];
    memcpy(truncated, long_type, PC_MTC_STR_MAX);
    truncated[PC_MTC_STR_MAX] = '\0';
    TEST_ASSERT_TRUE(contains(buf, truncated));
    TEST_ASSERT_FALSE(contains(buf, long_type));
}

// The sample query is a document builder like the rest: a null buffer or zero capacity fails closed.
void test_sample_query_rejects_null_buffer_and_zero_cap(void)
{
    pc_mtc_sample_buffer b;
    pc_mtc_sample_buffer_init(&b, 1);
    pc_mtc_sample_buffer_add(&b, PC_MTC_SAMPLE, "Position", "x", "T", "1.0");
    TEST_ASSERT_EQUAL_size_t(0, pc_mtc_sample_query(&b, NULL, 512, 1, "d", 1, 10));
    char buf[512];
    TEST_ASSERT_EQUAL_size_t(0, pc_mtc_sample_query(&b, buf, 0, 1, "d", 1, 10));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_streams_document);
    RUN_TEST(test_streams_escapes_value);
    RUN_TEST(test_error_document);
    RUN_TEST(test_overflow_returns_zero);
    RUN_TEST(test_escape_gt_quote_and_overflow);
    RUN_TEST(test_devices_probe_document);
    RUN_TEST(test_devices_escape_and_overflow);
    RUN_TEST(test_assets_document);
    RUN_TEST(test_assets_escape_and_overflow);
    RUN_TEST(test_sample_buffer_and_query);
    RUN_TEST(test_sample_buffer_eviction);
    RUN_TEST(test_sample_query_future_and_empty);
    RUN_TEST(test_streams_null_strings);
    RUN_TEST(test_builders_reject_null_buffer_and_zero_cap);
    RUN_TEST(test_error_null_strings_and_capacity_sweep);
    RUN_TEST(test_devices_null_ids_and_empty_optionals);
    RUN_TEST(test_assets_empty_optionals_and_null_strings);
    RUN_TEST(test_sample_buffer_null_and_truncated_fields);
    RUN_TEST(test_sample_query_rejects_null_buffer_and_zero_cap);
    return UNITY_END();
}
