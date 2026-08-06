// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/utmc: the UTMC common-database request/response codec.

#include "services/transportation/utmc/utmc.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static proto_bool has(const char *hay, const char *needle)
{
    return strstr(hay, needle) != NULL;
}

void test_request(void)
{
    char buf[128];
    size_t n = pc_utmc_request("PC_042", buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("<?xml version=\"1.0\"?><UTMCRequest><object id=\"PC_042\"/></UTMCRequest>", buf);
}

void test_response(void)
{
    char buf[256];
    pc_utmc_response("SIGN_7", "SLOW", UTMC_QUALITY_GOOD, "2026-07-06T12:00:00Z", buf, sizeof(buf));
    TEST_ASSERT_TRUE(has(buf, "<UTMCResponse><object id=\"SIGN_7\""));
    TEST_ASSERT_TRUE(has(buf, "value=\"SLOW\""));
    TEST_ASSERT_TRUE(has(buf, "quality=\"0\""));
    TEST_ASSERT_TRUE(has(buf, "timestamp=\"2026-07-06T12:00:00Z\""));
}

void test_response_escapes(void)
{
    char buf[256];
    pc_utmc_response("A&B", "x<y", UTMC_QUALITY_SUSPECT, "t", buf, sizeof(buf));
    TEST_ASSERT_TRUE(has(buf, "id=\"A&amp;B\""));
    TEST_ASSERT_TRUE(has(buf, "value=\"x&lt;y\""));
    TEST_ASSERT_TRUE(has(buf, "quality=\"1\""));
}

void test_parse_request(void)
{
    // Derive the expected length from the id itself. A hardcoded literal here was
    // coupled to the old id spelling and silently went stale when it was renamed.
    const char *want = "PC_042";
    const char *req = "<?xml version=\"1.0\"?><UTMCRequest><object id=\"PC_042\"/></UTMCRequest>";
    char id[32];
    size_t n = pc_utmc_parse_request(req, strlen(req), id, sizeof(id));
    TEST_ASSERT_EQUAL_size_t(strlen(want), n);
    TEST_ASSERT_EQUAL_STRING(want, id);
    // No id -> 0.
    const char *noid = "<UTMCRequest><object/></UTMCRequest>";
    TEST_ASSERT_EQUAL_size_t(0, pc_utmc_parse_request(noid, strlen(noid), id, sizeof(id)));
}

void test_overflow(void)
{
    char buf[16];
    TEST_ASSERT_EQUAL_size_t(0, pc_utmc_request("a-very-long-object-id-here", buf, sizeof(buf)));
}

void test_parse_request_guards()
{
    char out[64];
    TEST_ASSERT_EQUAL_size_t(0, pc_utmc_parse_request(NULL, 10, out, sizeof(out))); // null xml
    const char *xml = "<x id=\"ABCDEFGHIJ\"/>";
    TEST_ASSERT_EQUAL_size_t(0, pc_utmc_parse_request(xml, strlen(xml), out, 4)); // id overflows out
    const char *unterm = "<x id=\"ABC";
    TEST_ASSERT_EQUAL_size_t(0, pc_utmc_parse_request(unterm, strlen(unterm), out, sizeof(out))); // unterminated
}

void test_quality_multidigit(void)
{
    // A quality value >= 10 forces put_u()'s do/while to loop more than once.
    char buf[256];
    size_t n = pc_utmc_response("X", "v", 255, "t", buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(has(buf, "quality=\"255\""));
}

void test_null_out_and_zero_cap_guards(void)
{
    char buf[64];
    // pc_utmc_request: null out buffer, then zero-capacity buffer.
    TEST_ASSERT_EQUAL_size_t(0, pc_utmc_request("id", NULL, 64));
    TEST_ASSERT_EQUAL_size_t(0, pc_utmc_request("id", buf, 0));
    // pc_utmc_response: same two guard paths.
    TEST_ASSERT_EQUAL_size_t(0, pc_utmc_response("id", "v", UTMC_QUALITY_GOOD, "t", NULL, 64));
    TEST_ASSERT_EQUAL_size_t(0, pc_utmc_response("id", "v", UTMC_QUALITY_GOOD, "t", buf, 0));
    // pc_utmc_parse_request: null out buffer, then zero-capacity buffer (valid xml both times).
    const char *xml = "<x id=\"ABC\"/>";
    TEST_ASSERT_EQUAL_size_t(0, pc_utmc_parse_request(xml, strlen(xml), NULL, 64));
    TEST_ASSERT_EQUAL_size_t(0, pc_utmc_parse_request(xml, strlen(xml), buf, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_request);
    RUN_TEST(test_response);
    RUN_TEST(test_response_escapes);
    RUN_TEST(test_parse_request);
    RUN_TEST(test_overflow);
    RUN_TEST(test_parse_request_guards);
    RUN_TEST(test_quality_multidigit);
    RUN_TEST(test_null_out_and_zero_cap_guards);
    return UNITY_END();
}
