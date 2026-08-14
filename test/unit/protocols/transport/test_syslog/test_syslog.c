// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/transport/udp/udp.h"
#include "services/net/syslog/syslog.h"
#include <string.h>

#include <unity.h>

static const uint8_t *udp_cap(void)
{
    size_t n = protocore_net_host_udp_count();
    return n ? protocore_net_host_udp_at(n - 1)->data : NULL;
}
static size_t udp_cap_len(void)
{
    size_t n = protocore_net_host_udp_count();
    return n ? protocore_net_host_udp_at(n - 1)->len : 0;
}

void setUp()
{
}
void tearDown()
{
}

void test_pri_local0_info()
{
    char buf[256];
    size_t n = protocore_syslog_format(buf, sizeof(buf), SYSLOG_FAC_LOCAL0, SYSLOG_INFO, "esp32", "app", "hello");
    TEST_ASSERT_GREATER_THAN_UINT(0, n);

    TEST_ASSERT_EQUAL_STRING("<134>1 - esp32 app - - - hello", buf);
}

void test_pri_computation_varies()
{
    char buf[256];

    protocore_syslog_format(buf, sizeof(buf), SYSLOG_FAC_DAEMON, SYSLOG_ERR, "h", "a", "m");
    TEST_ASSERT_EQUAL_STRING("<27>1 - h a - - - m", buf);

    protocore_syslog_format(buf, sizeof(buf), SYSLOG_FAC_LOCAL7, SYSLOG_EMERG, "h", "a", "m");
    TEST_ASSERT_EQUAL_STRING("<184>1 - h a - - - m", buf);
}

void test_nilvalue_for_empty_fields()
{
    char buf[256];
    protocore_syslog_format(buf, sizeof(buf), SYSLOG_FAC_USER, SYSLOG_DEBUG, NULL, "", "msg");

    TEST_ASSERT_EQUAL_STRING("<15>1 - - - - - - msg", buf);
}

void test_empty_message_ok()
{
    char buf[256];
    size_t n = protocore_syslog_format(buf, sizeof(buf), SYSLOG_FAC_LOCAL0, SYSLOG_NOTICE, "h", "a", NULL);
    TEST_ASSERT_GREATER_THAN_UINT(0, n);

    TEST_ASSERT_EQUAL_STRING("<133>1 - h a - - - ", buf);
}

void test_overflow_returns_zero()
{
    char buf[16];
    size_t n = protocore_syslog_format(buf, sizeof(buf), SYSLOG_FAC_LOCAL0, SYSLOG_INFO, "esp32", "app", "a long message");
    TEST_ASSERT_EQUAL_UINT(0, n);
}

void test_length_matches_strlen()
{
    char buf[256];
    size_t n = protocore_syslog_format(buf, sizeof(buf), SYSLOG_FAC_LOCAL0, SYSLOG_INFO, "host", "app", "payload");
    TEST_ASSERT_EQUAL_UINT(strlen(buf), n);
}

void test_init_and_log_captured()
{
    protocore_net_host_udp_reset();
    protocore_syslog_init("192.168.1.1", 514, "host1", "myapp", SYSLOG_FAC_LOCAL0);
    TEST_ASSERT_TRUE(protocore_syslog_log(SYSLOG_INFO, "hello"));
    const char *expect = "<134>1 - host1 myapp - - - hello";
    TEST_ASSERT_EQUAL_UINT(strlen(expect), udp_cap_len());
    TEST_ASSERT_EQUAL_MEMORY(expect, udp_cap(), udp_cap_len());
}

void test_log_not_ready_when_no_server()
{
    protocore_net_host_udp_reset();
    protocore_syslog_init(NULL, 514, "h", "a", SYSLOG_FAC_USER);
    TEST_ASSERT_FALSE(protocore_syslog_log(SYSLOG_INFO, "x"));
    TEST_ASSERT_EQUAL_UINT(0, udp_cap_len());
}

void test_format_null_and_pri_clamp()
{
    char buf[64];

    TEST_ASSERT_EQUAL_UINT(0, protocore_syslog_format(NULL, sizeof(buf), (SyslogFacility)0, (SyslogSeverity)0, "h", "a", "m"));
    TEST_ASSERT_EQUAL_UINT(0, protocore_syslog_format(buf, 0, (SyslogFacility)0, (SyslogSeverity)0, "h", "a", "m"));

    protocore_syslog_format(buf, sizeof(buf), (SyslogFacility)25, (SyslogSeverity)0, "h", "a", "m");
    TEST_ASSERT_EQUAL_STRING("<191>1 - h a - - - m", buf);
}

void test_init_truncates_long_fields()
{
    protocore_net_host_udp_reset();
    char longname[PROTOCORE_SYSLOG_FIELD_MAX + 16];
    memset(longname, 'H', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    protocore_syslog_init("10.0.0.1", 514, longname, "a", SYSLOG_FAC_LOCAL0);
    TEST_ASSERT_TRUE(protocore_syslog_log(SYSLOG_INFO, "m"));
    const char *sent = (const char *)udp_cap();
    const char *host = strstr(sent, "1 - ") + 4;
    size_t hcount = 0;
    while (host[hcount] == 'H')
    {
        hcount++;
    }
    TEST_ASSERT_EQUAL_UINT((size_t)(PROTOCORE_SYSLOG_FIELD_MAX - 1), hcount);
}

void test_init_empty_server_ip_not_ready()
{
    protocore_net_host_udp_reset();
    protocore_syslog_init("", 514, "host", "app", SYSLOG_FAC_LOCAL0);
    TEST_ASSERT_FALSE(protocore_syslog_log(SYSLOG_INFO, "x"));
    TEST_ASSERT_EQUAL_UINT(0, udp_cap_len());
}

void test_format_hostname_empty_appname_null()
{
    char buf[64];
    size_t n = protocore_syslog_format(buf, sizeof(buf), SYSLOG_FAC_USER, SYSLOG_WARNING, "", NULL, "msg2");
    TEST_ASSERT_GREATER_THAN_UINT(0, n);

    TEST_ASSERT_EQUAL_STRING("<12>1 - - - - - - msg2", buf);
}

void test_format_append_boundaries()
{
    char buf[32];
    static const size_t fail_caps[] = {1, 2, 7, 8, 9, 10, 17, 18};
    for (size_t i = 0; i < sizeof(fail_caps) / sizeof(fail_caps[0]); i++)
    {
        size_t n = protocore_syslog_format(buf, fail_caps[i], SYSLOG_FAC_USER, SYSLOG_EMERG, "h", "a", "m");
        TEST_ASSERT_EQUAL_UINT(0, n);
    }

    size_t n = protocore_syslog_format(buf, 19, SYSLOG_FAC_USER, SYSLOG_EMERG, "h", "a", "m");
    TEST_ASSERT_GREATER_THAN_UINT(0, n);
    TEST_ASSERT_EQUAL_STRING("<8>1 - h a - - - m", buf);
}

void test_log_overflow_when_ready()
{
    protocore_net_host_udp_reset();
    char longname[PROTOCORE_SYSLOG_FIELD_MAX + 16];
    memset(longname, 'H', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    protocore_syslog_init("10.0.0.1", 514, longname, longname, SYSLOG_FAC_LOCAL0);
    char longmsg[240];
    memset(longmsg, 'M', sizeof(longmsg) - 1);
    longmsg[sizeof(longmsg) - 1] = '\0';

    TEST_ASSERT_FALSE(protocore_syslog_log(SYSLOG_INFO, longmsg));
    TEST_ASSERT_EQUAL_UINT(0, udp_cap_len());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_pri_local0_info);
    RUN_TEST(test_pri_computation_varies);
    RUN_TEST(test_nilvalue_for_empty_fields);
    RUN_TEST(test_empty_message_ok);
    RUN_TEST(test_overflow_returns_zero);
    RUN_TEST(test_length_matches_strlen);
    RUN_TEST(test_init_and_log_captured);
    RUN_TEST(test_log_not_ready_when_no_server);
    RUN_TEST(test_format_null_and_pri_clamp);
    RUN_TEST(test_init_truncates_long_fields);
    RUN_TEST(test_init_empty_server_ip_not_ready);
    RUN_TEST(test_format_hostname_empty_appname_null);
    RUN_TEST(test_format_append_boundaries);
    RUN_TEST(test_log_overflow_when_ready);
    return UNITY_END();
}
