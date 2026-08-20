// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
    Syslog.line.out = buf;
    Syslog.line.cap = sizeof(buf);
    Syslog.header.facility = SYSLOG_FAC_LOCAL0;
    Syslog.record.severity = SYSLOG_INFO;
    Syslog.header.hostname = "esp32";
    Syslog.header.app_name = "app";
    Syslog.record.msg = "hello";
    Syslog.format(protocore_syslog_span());
    size_t n = Syslog.n;
    TEST_ASSERT_GREATER_THAN_UINT(0, n);

    TEST_ASSERT_EQUAL_STRING("<134>1 - esp32 app - - - hello", buf);
}

void test_pri_computation_varies()
{
    char buf[256];

    Syslog.line.out = buf;
    Syslog.line.cap = sizeof(buf);
    Syslog.header.facility = SYSLOG_FAC_DAEMON;
    Syslog.record.severity = SYSLOG_ERR;
    Syslog.header.hostname = "h";
    Syslog.header.app_name = "a";
    Syslog.record.msg = "m";
    Syslog.format(protocore_syslog_span());
    TEST_ASSERT_EQUAL_STRING("<27>1 - h a - - - m", buf);

    Syslog.line.out = buf;
    Syslog.line.cap = sizeof(buf);
    Syslog.header.facility = SYSLOG_FAC_LOCAL7;
    Syslog.record.severity = SYSLOG_EMERG;
    Syslog.header.hostname = "h";
    Syslog.header.app_name = "a";
    Syslog.record.msg = "m";
    Syslog.format(protocore_syslog_span());
    TEST_ASSERT_EQUAL_STRING("<184>1 - h a - - - m", buf);
}

void test_nilvalue_for_empty_fields()
{
    char buf[256];
    Syslog.line.out = buf;
    Syslog.line.cap = sizeof(buf);
    Syslog.header.facility = SYSLOG_FAC_USER;
    Syslog.record.severity = SYSLOG_DEBUG;
    Syslog.header.hostname = NULL;
    Syslog.header.app_name = "";
    Syslog.record.msg = "msg";
    Syslog.format(protocore_syslog_span());

    TEST_ASSERT_EQUAL_STRING("<15>1 - - - - - - msg", buf);
}

void test_empty_message_ok()
{
    char buf[256];
    Syslog.line.out = buf;
    Syslog.line.cap = sizeof(buf);
    Syslog.header.facility = SYSLOG_FAC_LOCAL0;
    Syslog.record.severity = SYSLOG_NOTICE;
    Syslog.header.hostname = "h";
    Syslog.header.app_name = "a";
    Syslog.record.msg = NULL;
    Syslog.format(protocore_syslog_span());
    size_t n = Syslog.n;
    TEST_ASSERT_GREATER_THAN_UINT(0, n);

    TEST_ASSERT_EQUAL_STRING("<133>1 - h a - - - ", buf);
}

void test_overflow_returns_zero()
{
    char buf[16];
    Syslog.line.out = buf;
    Syslog.line.cap = sizeof(buf);
    Syslog.header.facility = SYSLOG_FAC_LOCAL0;
    Syslog.record.severity = SYSLOG_INFO;
    Syslog.header.hostname = "esp32";
    Syslog.header.app_name = "app";
    Syslog.record.msg = "a long message";
    Syslog.format(protocore_syslog_span());
    size_t n =
        Syslog.n;
    TEST_ASSERT_EQUAL_UINT(0, n);
}

void test_length_matches_strlen()
{
    char buf[256];
    Syslog.line.out = buf;
    Syslog.line.cap = sizeof(buf);
    Syslog.header.facility = SYSLOG_FAC_LOCAL0;
    Syslog.record.severity = SYSLOG_INFO;
    Syslog.header.hostname = "host";
    Syslog.header.app_name = "app";
    Syslog.record.msg = "payload";
    Syslog.format(protocore_syslog_span());
    size_t n = Syslog.n;
    TEST_ASSERT_EQUAL_UINT(strlen(buf), n);
}

void test_init_and_log_captured()
{
    protocore_net_host_udp_reset();
    Syslog.collector.addr = "192.168.1.1";
    Syslog.collector.port = 514;
    Syslog.header.hostname = "host1";
    Syslog.header.app_name = "myapp";
    Syslog.header.facility = SYSLOG_FAC_LOCAL0;
    Syslog.init(protocore_syslog_span());
    Syslog.record.severity = SYSLOG_INFO;
    Syslog.record.msg = "hello";
    Syslog.log(protocore_syslog_span());
    TEST_ASSERT_TRUE(Syslog.ok);
    const char *expect = "<134>1 - host1 myapp - - - hello";
    TEST_ASSERT_EQUAL_UINT(strlen(expect), udp_cap_len());
    TEST_ASSERT_EQUAL_MEMORY(expect, udp_cap(), udp_cap_len());
}

void test_log_not_ready_when_no_server()
{
    protocore_net_host_udp_reset();
    Syslog.collector.addr = NULL;
    Syslog.collector.port = 514;
    Syslog.header.hostname = "h";
    Syslog.header.app_name = "a";
    Syslog.header.facility = SYSLOG_FAC_USER;
    Syslog.init(protocore_syslog_span());
    Syslog.record.severity = SYSLOG_INFO;
    Syslog.record.msg = "x";
    Syslog.log(protocore_syslog_span());
    TEST_ASSERT_FALSE(Syslog.ok);
    TEST_ASSERT_EQUAL_UINT(0, udp_cap_len());
}

void test_format_null_and_pri_clamp()
{
    char buf[64];

    Syslog.line.out = NULL;
    Syslog.line.cap = sizeof(buf);
    Syslog.header.facility = (SyslogFacility)0;
    Syslog.record.severity = (SyslogSeverity)0;
    Syslog.header.hostname = "h";
    Syslog.header.app_name = "a";
    Syslog.record.msg = "m";
    Syslog.format(protocore_syslog_span());
    TEST_ASSERT_EQUAL_UINT(
        0, Syslog.n);
    Syslog.line.out = buf;
    Syslog.line.cap = 0;
    Syslog.header.facility = (SyslogFacility)0;
    Syslog.record.severity = (SyslogSeverity)0;
    Syslog.header.hostname = "h";
    Syslog.header.app_name = "a";
    Syslog.record.msg = "m";
    Syslog.format(protocore_syslog_span());
    TEST_ASSERT_EQUAL_UINT(0, Syslog.n);

    Syslog.line.out = buf;
    Syslog.line.cap = sizeof(buf);
    Syslog.header.facility = (SyslogFacility)25;
    Syslog.record.severity = (SyslogSeverity)0;
    Syslog.header.hostname = "h";
    Syslog.header.app_name = "a";
    Syslog.record.msg = "m";
    Syslog.format(protocore_syslog_span());
    TEST_ASSERT_EQUAL_STRING("<191>1 - h a - - - m", buf);
}

void test_init_truncates_long_fields()
{
    protocore_net_host_udp_reset();
    char longname[PROTOCORE_SYSLOG_FIELD_MAX + 16];
    memset(longname, 'H', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    Syslog.collector.addr = "10.0.0.1";
    Syslog.collector.port = 514;
    Syslog.header.hostname = longname;
    Syslog.header.app_name = "a";
    Syslog.header.facility = SYSLOG_FAC_LOCAL0;
    Syslog.init(protocore_syslog_span());
    Syslog.record.severity = SYSLOG_INFO;
    Syslog.record.msg = "m";
    Syslog.log(protocore_syslog_span());
    TEST_ASSERT_TRUE(Syslog.ok);
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
    Syslog.collector.addr = "";
    Syslog.collector.port = 514;
    Syslog.header.hostname = "host";
    Syslog.header.app_name = "app";
    Syslog.header.facility = SYSLOG_FAC_LOCAL0;
    Syslog.init(protocore_syslog_span());
    Syslog.record.severity = SYSLOG_INFO;
    Syslog.record.msg = "x";
    Syslog.log(protocore_syslog_span());
    TEST_ASSERT_FALSE(Syslog.ok);
    TEST_ASSERT_EQUAL_UINT(0, udp_cap_len());
}

void test_format_hostname_empty_appname_null()
{
    char buf[64];
    Syslog.line.out = buf;
    Syslog.line.cap = sizeof(buf);
    Syslog.header.facility = SYSLOG_FAC_USER;
    Syslog.record.severity = SYSLOG_WARNING;
    Syslog.header.hostname = "";
    Syslog.header.app_name = NULL;
    Syslog.record.msg = "msg2";
    Syslog.format(protocore_syslog_span());
    size_t n = Syslog.n;
    TEST_ASSERT_GREATER_THAN_UINT(0, n);

    TEST_ASSERT_EQUAL_STRING("<12>1 - - - - - - msg2", buf);
}

void test_format_append_boundaries()
{
    char buf[32];
    static const size_t fail_caps[] = {1, 2, 7, 8, 9, 10, 17, 18};
    for (size_t i = 0; i < sizeof(fail_caps) / sizeof(fail_caps[0]); i++)
    {
        Syslog.line.out = buf;
        Syslog.line.cap = fail_caps[i];
        Syslog.header.facility = SYSLOG_FAC_USER;
        Syslog.record.severity = SYSLOG_EMERG;
        Syslog.header.hostname = "h";
        Syslog.header.app_name = "a";
        Syslog.record.msg = "m";
        Syslog.format(protocore_syslog_span());
        size_t n = Syslog.n;
        TEST_ASSERT_EQUAL_UINT(0, n);
    }

    Syslog.line.out = buf;
    Syslog.line.cap = 19;
    Syslog.header.facility = SYSLOG_FAC_USER;
    Syslog.record.severity = SYSLOG_EMERG;
    Syslog.header.hostname = "h";
    Syslog.header.app_name = "a";
    Syslog.record.msg = "m";
    Syslog.format(protocore_syslog_span());
    size_t n = Syslog.n;
    TEST_ASSERT_GREATER_THAN_UINT(0, n);
    TEST_ASSERT_EQUAL_STRING("<8>1 - h a - - - m", buf);
}

void test_log_overflow_when_ready()
{
    protocore_net_host_udp_reset();
    char longname[PROTOCORE_SYSLOG_FIELD_MAX + 16];
    memset(longname, 'H', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    Syslog.collector.addr = "10.0.0.1";
    Syslog.collector.port = 514;
    Syslog.header.hostname = longname;
    Syslog.header.app_name = longname;
    Syslog.header.facility = SYSLOG_FAC_LOCAL0;
    Syslog.init(protocore_syslog_span());
    char longmsg[240];
    memset(longmsg, 'M', sizeof(longmsg) - 1);
    longmsg[sizeof(longmsg) - 1] = '\0';

    Syslog.record.severity = SYSLOG_INFO;
    Syslog.record.msg = longmsg;
    Syslog.log(protocore_syslog_span());
    TEST_ASSERT_FALSE(Syslog.ok);
    TEST_ASSERT_EQUAL_UINT(0, udp_cap_len());
}

