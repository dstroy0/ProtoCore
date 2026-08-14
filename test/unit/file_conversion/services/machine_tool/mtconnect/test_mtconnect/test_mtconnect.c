// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/machine_tool/mtconnect/mtconnect.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static void assert_has(const char *doc, const char *needle)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(doc, needle), needle);
}

static void assert_lacks(const char *doc, const char *needle)
{
    TEST_ASSERT_NULL_MESSAGE(strstr(doc, needle), needle);
}

void test_streams_document_skeleton(void)
{
    char buf[1024];
    protocore_mtc_streams s;
    protocore_mtc_streams_begin(&s, buf, sizeof(buf), 7, 100, "VF2");
    size_t n = protocore_mtc_streams_end(&s);

    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
    assert_has(buf, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    assert_has(buf, "<MTConnectStreams xmlns=\"urn:mtconnect.org:MTConnectStreams:1.4\">");
    assert_has(buf, "<Header instanceId=\"7\" version=\"1.4\" nextSequence=\"100\"/>");
    assert_has(buf, "<Streams><DeviceStream name=\"VF2\">");
    assert_has(buf, "</DeviceStream></Streams></MTConnectStreams>");

    assert_lacks(buf, "<ComponentStream");
}

void test_sample_and_event_wrappers(void)
{
    char buf[1024];
    protocore_mtc_streams s;
    protocore_mtc_streams_begin(&s, buf, sizeof(buf), 1, 3, "VF2");
    protocore_mtc_streams_add(&s, PROTOCORE_MTC_SAMPLE, "Position", "Xabs", 1, "2026-01-01T00:00:00Z", "1.5");
    protocore_mtc_streams_add(&s, PROTOCORE_MTC_EVENT, "Execution", "exec", 2, "2026-01-01T00:00:01Z", "ACTIVE");
    TEST_ASSERT_TRUE(protocore_mtc_streams_end(&s) > 0);

    assert_has(buf, "<ComponentStream component=\"Device\">");
    assert_has(buf, "<Samples><Position dataItemId=\"Xabs\" sequence=\"1\" "
                    "timestamp=\"2026-01-01T00:00:00Z\">1.5</Position></Samples>");
    assert_has(buf, "<Events><Execution dataItemId=\"exec\" sequence=\"2\" "
                    "timestamp=\"2026-01-01T00:00:01Z\">ACTIVE</Execution></Events>");
    assert_has(buf, "</ComponentStream></DeviceStream>");
}

void test_condition_value_becomes_the_sub_element(void)
{
    char buf[1024];
    protocore_mtc_streams s;
    protocore_mtc_streams_begin(&s, buf, sizeof(buf), 1, 2, "VF2");
    protocore_mtc_streams_add(&s, PROTOCORE_MTC_CONDITION, "SystemCondition", "sys", 1, "2026-01-01T00:00:00Z",
                              "Fault");
    TEST_ASSERT_TRUE(protocore_mtc_streams_end(&s) > 0);

    assert_has(buf, "<Condition><Fault type=\"SystemCondition\" dataItemId=\"sys\" sequence=\"1\" "
                    "timestamp=\"2026-01-01T00:00:00Z\"/></Condition>");

    protocore_mtc_streams_begin(&s, buf, sizeof(buf), 1, 2, "VF2");
    protocore_mtc_streams_add(&s, PROTOCORE_MTC_CONDITION, "SystemCondition", "sys", 1, "2026-01-01T00:00:00Z", NULL);
    TEST_ASSERT_TRUE(protocore_mtc_streams_end(&s) > 0);
    assert_has(buf, "<Condition><Normal type=\"SystemCondition\"");
}

void test_xml_special_characters_are_escaped(void)
{
    char buf[1024];
    protocore_mtc_streams s;
    protocore_mtc_streams_begin(&s, buf, sizeof(buf), 1, 1, "A&B");
    protocore_mtc_streams_add(&s, PROTOCORE_MTC_EVENT, "Message", "msg", 1, "2026-01-01T00:00:00Z",
                              "a<b>c&d\"e");
    TEST_ASSERT_TRUE(protocore_mtc_streams_end(&s) > 0);

    assert_has(buf, "name=\"A&amp;B\"");
    assert_has(buf, ">a&lt;b&gt;c&amp;d&quot;e<");

    assert_lacks(buf, "a<b");
    assert_lacks(buf, "c&d");
}

void test_error_document(void)
{
    char buf[512];
    size_t n = protocore_mtc_error(42, "OUT_OF_RANGE", "'from' must be <= 99", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
    assert_has(buf, "<MTConnectError xmlns=\"urn:mtconnect.org:MTConnectError:1.4\">");
    assert_has(buf, "<Header instanceId=\"42\" version=\"1.4\"/>");
    assert_has(buf, "<Errors><Error errorCode=\"OUT_OF_RANGE\">");
    assert_has(buf, "&lt;= 99</Error></Errors></MTConnectError>");
    TEST_ASSERT_EQUAL_size_t(0u, protocore_mtc_error(42, "OUT_OF_RANGE", "x", buf, 8));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_mtc_error(42, "OUT_OF_RANGE", "x", NULL, sizeof(buf)));
}

void test_devices_probe_document(void)
{
    char buf[1024];
    protocore_mtc_streams s;
    protocore_mtc_devices_begin(&s, buf, sizeof(buf), 7, "d1", "VF2", "uuid-1");
    protocore_mtc_devices_add_item(&s, PROTOCORE_MTC_SAMPLE, "Xabs", "Position", "Xpos", "MILLIMETER");
    protocore_mtc_devices_add_item(&s, PROTOCORE_MTC_EVENT, "exec", "Execution", NULL, NULL);
    protocore_mtc_devices_add_item(&s, PROTOCORE_MTC_CONDITION, "sys", "SystemCondition", "", "");
    size_t n = protocore_mtc_devices_end(&s);

    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
    assert_has(buf, "<MTConnectDevices xmlns=\"urn:mtconnect.org:MTConnectDevices:1.4\">");
    assert_has(buf, "<Devices><Device id=\"d1\" name=\"VF2\" uuid=\"uuid-1\"><DataItems>");
    assert_has(buf, "<DataItem category=\"SAMPLE\" id=\"Xabs\" type=\"Position\" name=\"Xpos\" units=\"MILLIMETER\"/>");

    assert_has(buf, "<DataItem category=\"EVENT\" id=\"exec\" type=\"Execution\"/>");
    assert_has(buf, "<DataItem category=\"CONDITION\" id=\"sys\" type=\"SystemCondition\"/>");
    assert_lacks(buf, "name=\"\"");
    assert_lacks(buf, "units=\"\"");
    assert_has(buf, "</DataItems></Device></Devices></MTConnectDevices>");
}

void test_assets_cutting_tool_document(void)
{
    char buf[1024];
    protocore_mtc_streams s;
    protocore_mtc_assets_begin(&s, buf, sizeof(buf), 7, 1, 64);
    protocore_mtc_assets_cutting_tool_begin(&s, "T5", "SN-9", "5", "uuid-1", "2026-01-01T00:00:00Z");
    protocore_mtc_assets_tool_life(&s, "MINUTES", "DOWN", "100", "37");
    protocore_mtc_assets_tool_life(&s, "PART_COUNT", "UP", NULL, "12");
    protocore_mtc_assets_cutting_tool_end(&s);
    size_t n = protocore_mtc_assets_end(&s);

    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
    assert_has(buf, "<MTConnectAssets xmlns=\"urn:mtconnect.org:MTConnectAssets:1.4\">");
    assert_has(buf, "<Header instanceId=\"7\" version=\"1.4\" assetBufferSize=\"64\" assetCount=\"1\"/>");
    assert_has(buf, "<Assets><CuttingTool assetId=\"T5\" serialNumber=\"SN-9\" toolId=\"5\" deviceUuid=\"uuid-1\" "
                    "timestamp=\"2026-01-01T00:00:00Z\"><CuttingToolLifeCycle>");
    assert_has(buf, "<ToolLife type=\"MINUTES\" countDirection=\"DOWN\" limit=\"100\">37</ToolLife>");
    assert_has(buf, "<ToolLife type=\"PART_COUNT\" countDirection=\"UP\">12</ToolLife>");
    assert_has(buf, "</CuttingToolLifeCycle></CuttingTool></Assets></MTConnectAssets>");
}

void test_overflow_reports_zero_length(void)
{
    char buf[64];
    protocore_mtc_streams s;
    protocore_mtc_streams_begin(&s, buf, sizeof(buf), 1, 1, "VF2");
    protocore_mtc_streams_add(&s, PROTOCORE_MTC_SAMPLE, "Position", "Xabs", 1, "2026-01-01T00:00:00Z", "1.5");
    TEST_ASSERT_FALSE(s.ok);
    TEST_ASSERT_EQUAL_size_t(0u, protocore_mtc_streams_end(&s));

    protocore_mtc_streams_begin(&s, NULL, 1024, 1, 1, "VF2");
    TEST_ASSERT_FALSE(s.ok);
    TEST_ASSERT_EQUAL_size_t(0u, protocore_mtc_streams_end(&s));
}

void test_sample_buffer_assigns_monotonic_sequences(void)
{
    static protocore_mtc_sample_buffer b;
    protocore_mtc_sample_buffer_init(&b, 1000);
    TEST_ASSERT_EQUAL_UINT64(1000u, b.next_seq);
    TEST_ASSERT_EQUAL_UINT64(1000u, b.first_seq);
    TEST_ASSERT_EQUAL_UINT32(0u, b.count);

    for (uint32_t i = 0; i < 3; i++)
    {
        uint64_t seq = protocore_mtc_sample_buffer_add(&b, PROTOCORE_MTC_SAMPLE, "Position", "Xabs",
                                                       "2026-01-01T00:00:00Z", "1.5");
        TEST_ASSERT_EQUAL_UINT64((uint64_t)(1000 + i), seq);
    }
    TEST_ASSERT_EQUAL_UINT32(3u, b.count);
    TEST_ASSERT_EQUAL_UINT64(1003u, b.next_seq);
    TEST_ASSERT_EQUAL_UINT64(1000u, b.first_seq);

    protocore_mtc_sample_buffer_init(&b, 0);
    TEST_ASSERT_EQUAL_UINT64(1u, b.next_seq);
    TEST_ASSERT_EQUAL_UINT64(1u, b.first_seq);
}

void test_sample_buffer_eviction_advances_first_sequence(void)
{
    static protocore_mtc_sample_buffer b;
    protocore_mtc_sample_buffer_init(&b, 1000);
    for (uint32_t i = 0; i < PROTOCORE_MTC_SAMPLE_BUFFER + 1; i++)
    {
        protocore_mtc_sample_buffer_add(&b, PROTOCORE_MTC_SAMPLE, "Position", "Xabs", "2026-01-01T00:00:00Z", "1.5");
    }
    TEST_ASSERT_EQUAL_UINT32((uint32_t)PROTOCORE_MTC_SAMPLE_BUFFER, b.count);
    TEST_ASSERT_EQUAL_UINT64(1001u, b.first_seq);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)(1000 + PROTOCORE_MTC_SAMPLE_BUFFER + 1), b.next_seq);

    TEST_ASSERT_EQUAL_UINT64((uint64_t)b.count, b.next_seq - b.first_seq);
}

void test_sample_query_window_and_next_sequence(void)
{
    static protocore_mtc_sample_buffer b;
    char buf[4096];
    protocore_mtc_sample_buffer_init(&b, 1000);
    protocore_mtc_sample_buffer_add(&b, PROTOCORE_MTC_SAMPLE, "Position", "Xabs", "2026-01-01T00:00:00Z", "1.0");
    protocore_mtc_sample_buffer_add(&b, PROTOCORE_MTC_SAMPLE, "Position", "Xabs", "2026-01-01T00:00:01Z", "2.0");
    protocore_mtc_sample_buffer_add(&b, PROTOCORE_MTC_SAMPLE, "Position", "Xabs", "2026-01-01T00:00:02Z", "3.0");

    size_t n = protocore_mtc_sample_query(&b, buf, sizeof(buf), 7, "VF2", 1000, 10);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
    assert_has(buf, "firstSequence=\"1000\"");
    assert_has(buf, "lastSequence=\"1002\"");
    assert_has(buf, "nextSequence=\"1003\"");
    assert_has(buf, "sequence=\"1000\"");
    assert_has(buf, "sequence=\"1002\"");
    assert_has(buf, ">1.0<");
    assert_has(buf, ">3.0<");

    TEST_ASSERT_TRUE(protocore_mtc_sample_query(&b, buf, sizeof(buf), 7, "VF2", 1001, 1) > 0);
    assert_has(buf, "nextSequence=\"1002\"");
    assert_has(buf, "sequence=\"1001\"");
    assert_lacks(buf, "sequence=\"1002\"");
    assert_lacks(buf, "sequence=\"1000\"");
}

void test_sample_query_clamps_a_stale_from(void)
{
    static protocore_mtc_sample_buffer b;
    char buf[8192];
    protocore_mtc_sample_buffer_init(&b, 1000);
    for (uint32_t i = 0; i < PROTOCORE_MTC_SAMPLE_BUFFER + 1; i++)
    {
        protocore_mtc_sample_buffer_add(&b, PROTOCORE_MTC_EVENT, "Execution", "exec", "2026-01-01T00:00:00Z",
                                        "ACTIVE");
    }
    TEST_ASSERT_TRUE(protocore_mtc_sample_query(&b, buf, sizeof(buf), 7, "VF2", 0, 2) > 0);
    assert_has(buf, "firstSequence=\"1001\"");
    assert_has(buf, "sequence=\"1001\"");
    assert_has(buf, "sequence=\"1002\"");
    assert_has(buf, "nextSequence=\"1003\"");
    assert_lacks(buf, "sequence=\"1000\"");
}

void test_sample_query_past_the_newest_is_empty(void)
{
    static protocore_mtc_sample_buffer b;
    char buf[2048];
    protocore_mtc_sample_buffer_init(&b, 1000);
    protocore_mtc_sample_buffer_add(&b, PROTOCORE_MTC_SAMPLE, "Position", "Xabs", "2026-01-01T00:00:00Z", "1.0");

    TEST_ASSERT_TRUE(protocore_mtc_sample_query(&b, buf, sizeof(buf), 7, "VF2", 1001, 10) > 0);
    assert_has(buf, "nextSequence=\"1001\"");
    assert_lacks(buf, "<ComponentStream");
    assert_has(buf, "<Streams><DeviceStream name=\"VF2\"></DeviceStream></Streams>");

    protocore_mtc_sample_buffer_init(&b, 1000);
    TEST_ASSERT_TRUE(protocore_mtc_sample_query(&b, buf, sizeof(buf), 7, "VF2", 1000, 10) > 0);
    assert_has(buf, "firstSequence=\"1000\"");
    assert_has(buf, "lastSequence=\"999\"");
    assert_has(buf, "nextSequence=\"1000\"");
}
