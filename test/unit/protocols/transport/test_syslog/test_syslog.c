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
    SyslogV.line.out = buf;
    SyslogV.line.cap = sizeof(buf);
    SyslogV.header.facility = SYSLOG_FAC_LOCAL0;
    SyslogV.record.severity = SYSLOG_INFO;
    SyslogV.header.hostname = "esp32";
    SyslogV.header.app_name = "app";
    SyslogV.record.msg = "hello";
    Syslog.format(protocore_syslog_span());
    size_t n = SyslogV.n;
    TEST_ASSERT_GREATER_THAN_UINT(0, n);

    TEST_ASSERT_EQUAL_STRING("<134>1 - esp32 app - - - hello", buf);
}

void test_pri_computation_varies()
{
    char buf[256];

    SyslogV.line.out = buf;
    SyslogV.line.cap = sizeof(buf);
    SyslogV.header.facility = SYSLOG_FAC_DAEMON;
    SyslogV.record.severity = SYSLOG_ERR;
    SyslogV.header.hostname = "h";
    SyslogV.header.app_name = "a";
    SyslogV.record.msg = "m";
    Syslog.format(protocore_syslog_span());
    TEST_ASSERT_EQUAL_STRING("<27>1 - h a - - - m", buf);

    SyslogV.line.out = buf;
    SyslogV.line.cap = sizeof(buf);
    SyslogV.header.facility = SYSLOG_FAC_LOCAL7;
    SyslogV.record.severity = SYSLOG_EMERG;
    SyslogV.header.hostname = "h";
    SyslogV.header.app_name = "a";
    SyslogV.record.msg = "m";
    Syslog.format(protocore_syslog_span());
    TEST_ASSERT_EQUAL_STRING("<184>1 - h a - - - m", buf);
}

void test_nilvalue_for_empty_fields()
{
    char buf[256];
    SyslogV.line.out = buf;
    SyslogV.line.cap = sizeof(buf);
    SyslogV.header.facility = SYSLOG_FAC_USER;
    SyslogV.record.severity = SYSLOG_DEBUG;
    SyslogV.header.hostname = NULL;
    SyslogV.header.app_name = "";
    SyslogV.record.msg = "msg";
    Syslog.format(protocore_syslog_span());

    TEST_ASSERT_EQUAL_STRING("<15>1 - - - - - - msg", buf);
}

void test_empty_message_ok()
{
    char buf[256];
    SyslogV.line.out = buf;
    SyslogV.line.cap = sizeof(buf);
    SyslogV.header.facility = SYSLOG_FAC_LOCAL0;
    SyslogV.record.severity = SYSLOG_NOTICE;
    SyslogV.header.hostname = "h";
    SyslogV.header.app_name = "a";
    SyslogV.record.msg = NULL;
    Syslog.format(protocore_syslog_span());
    size_t n = SyslogV.n;
    TEST_ASSERT_GREATER_THAN_UINT(0, n);

    TEST_ASSERT_EQUAL_STRING("<133>1 - h a - - - ", buf);
}

void test_overflow_returns_zero()
{
    char buf[16];
    SyslogV.line.out = buf;
    SyslogV.line.cap = sizeof(buf);
    SyslogV.header.facility = SYSLOG_FAC_LOCAL0;
    SyslogV.record.severity = SYSLOG_INFO;
    SyslogV.header.hostname = "esp32";
    SyslogV.header.app_name = "app";
    SyslogV.record.msg = "a long message";
    Syslog.format(protocore_syslog_span());
    size_t n = SyslogV.n;
    TEST_ASSERT_EQUAL_UINT(0, n);
}

void test_length_matches_strlen()
{
    char buf[256];
    SyslogV.line.out = buf;
    SyslogV.line.cap = sizeof(buf);
    SyslogV.header.facility = SYSLOG_FAC_LOCAL0;
    SyslogV.record.severity = SYSLOG_INFO;
    SyslogV.header.hostname = "host";
    SyslogV.header.app_name = "app";
    SyslogV.record.msg = "payload";
    Syslog.format(protocore_syslog_span());
    size_t n = SyslogV.n;
    TEST_ASSERT_EQUAL_UINT(strlen(buf), n);
}

void test_init_and_log_captured()
{
    protocore_net_host_udp_reset();
    SyslogV.collector.addr = "192.168.1.1";
    SyslogV.collector.port = 514;
    SyslogV.header.hostname = "host1";
    SyslogV.header.app_name = "myapp";
    SyslogV.header.facility = SYSLOG_FAC_LOCAL0;
    Syslog.init(protocore_syslog_span());
    SyslogV.record.severity = SYSLOG_INFO;
    SyslogV.record.msg = "hello";
    Syslog.log(protocore_syslog_span());
    TEST_ASSERT_TRUE(SyslogV.ok);
    const char *expect = "<134>1 - host1 myapp - - - hello";
    TEST_ASSERT_EQUAL_UINT(strlen(expect), udp_cap_len());
    TEST_ASSERT_EQUAL_MEMORY(expect, udp_cap(), udp_cap_len());
}

void test_log_not_ready_when_no_server()
{
    protocore_net_host_udp_reset();
    SyslogV.collector.addr = NULL;
    SyslogV.collector.port = 514;
    SyslogV.header.hostname = "h";
    SyslogV.header.app_name = "a";
    SyslogV.header.facility = SYSLOG_FAC_USER;
    Syslog.init(protocore_syslog_span());
    SyslogV.record.severity = SYSLOG_INFO;
    SyslogV.record.msg = "x";
    Syslog.log(protocore_syslog_span());
    TEST_ASSERT_FALSE(SyslogV.ok);
    TEST_ASSERT_EQUAL_UINT(0, udp_cap_len());
}

void test_format_null_and_pri_clamp()
{
    char buf[64];

    SyslogV.line.out = NULL;
    SyslogV.line.cap = sizeof(buf);
    SyslogV.header.facility = (SyslogFacility)0;
    SyslogV.record.severity = (SyslogSeverity)0;
    SyslogV.header.hostname = "h";
    SyslogV.header.app_name = "a";
    SyslogV.record.msg = "m";
    Syslog.format(protocore_syslog_span());
    TEST_ASSERT_EQUAL_UINT(0, SyslogV.n);
    SyslogV.line.out = buf;
    SyslogV.line.cap = 0;
    SyslogV.header.facility = (SyslogFacility)0;
    SyslogV.record.severity = (SyslogSeverity)0;
    SyslogV.header.hostname = "h";
    SyslogV.header.app_name = "a";
    SyslogV.record.msg = "m";
    Syslog.format(protocore_syslog_span());
    TEST_ASSERT_EQUAL_UINT(0, SyslogV.n);

    SyslogV.line.out = buf;
    SyslogV.line.cap = sizeof(buf);
    SyslogV.header.facility = (SyslogFacility)25;
    SyslogV.record.severity = (SyslogSeverity)0;
    SyslogV.header.hostname = "h";
    SyslogV.header.app_name = "a";
    SyslogV.record.msg = "m";
    Syslog.format(protocore_syslog_span());
    TEST_ASSERT_EQUAL_STRING("<191>1 - h a - - - m", buf);
}

void test_init_truncates_long_fields()
{
    protocore_net_host_udp_reset();
    char longname[PROTOCORE_SYSLOG_FIELD_MAX + 16];
    memset(longname, 'H', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    SyslogV.collector.addr = "10.0.0.1";
    SyslogV.collector.port = 514;
    SyslogV.header.hostname = longname;
    SyslogV.header.app_name = "a";
    SyslogV.header.facility = SYSLOG_FAC_LOCAL0;
    Syslog.init(protocore_syslog_span());
    SyslogV.record.severity = SYSLOG_INFO;
    SyslogV.record.msg = "m";
    Syslog.log(protocore_syslog_span());
    TEST_ASSERT_TRUE(SyslogV.ok);
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
    SyslogV.collector.addr = "";
    SyslogV.collector.port = 514;
    SyslogV.header.hostname = "host";
    SyslogV.header.app_name = "app";
    SyslogV.header.facility = SYSLOG_FAC_LOCAL0;
    Syslog.init(protocore_syslog_span());
    SyslogV.record.severity = SYSLOG_INFO;
    SyslogV.record.msg = "x";
    Syslog.log(protocore_syslog_span());
    TEST_ASSERT_FALSE(SyslogV.ok);
    TEST_ASSERT_EQUAL_UINT(0, udp_cap_len());
}

void test_format_hostname_empty_appname_null()
{
    char buf[64];
    SyslogV.line.out = buf;
    SyslogV.line.cap = sizeof(buf);
    SyslogV.header.facility = SYSLOG_FAC_USER;
    SyslogV.record.severity = SYSLOG_WARNING;
    SyslogV.header.hostname = "";
    SyslogV.header.app_name = NULL;
    SyslogV.record.msg = "msg2";
    Syslog.format(protocore_syslog_span());
    size_t n = SyslogV.n;
    TEST_ASSERT_GREATER_THAN_UINT(0, n);

    TEST_ASSERT_EQUAL_STRING("<12>1 - - - - - - msg2", buf);
}

void test_format_append_boundaries()
{
    char buf[32];
    static const size_t fail_caps[] = {1, 2, 7, 8, 9, 10, 17, 18};
    for (size_t i = 0; i < sizeof(fail_caps) / sizeof(fail_caps[0]); i++)
    {
        SyslogV.line.out = buf;
        SyslogV.line.cap = fail_caps[i];
        SyslogV.header.facility = SYSLOG_FAC_USER;
        SyslogV.record.severity = SYSLOG_EMERG;
        SyslogV.header.hostname = "h";
        SyslogV.header.app_name = "a";
        SyslogV.record.msg = "m";
        Syslog.format(protocore_syslog_span());
        size_t n = SyslogV.n;
        TEST_ASSERT_EQUAL_UINT(0, n);
    }

    SyslogV.line.out = buf;
    SyslogV.line.cap = 19;
    SyslogV.header.facility = SYSLOG_FAC_USER;
    SyslogV.record.severity = SYSLOG_EMERG;
    SyslogV.header.hostname = "h";
    SyslogV.header.app_name = "a";
    SyslogV.record.msg = "m";
    Syslog.format(protocore_syslog_span());
    size_t n = SyslogV.n;
    TEST_ASSERT_GREATER_THAN_UINT(0, n);
    TEST_ASSERT_EQUAL_STRING("<8>1 - h a - - - m", buf);
}

void test_log_overflow_when_ready()
{
    protocore_net_host_udp_reset();
    char longname[PROTOCORE_SYSLOG_FIELD_MAX + 16];
    memset(longname, 'H', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    SyslogV.collector.addr = "10.0.0.1";
    SyslogV.collector.port = 514;
    SyslogV.header.hostname = longname;
    SyslogV.header.app_name = longname;
    SyslogV.header.facility = SYSLOG_FAC_LOCAL0;
    Syslog.init(protocore_syslog_span());
    char longmsg[240];
    memset(longmsg, 'M', sizeof(longmsg) - 1);
    longmsg[sizeof(longmsg) - 1] = '\0';

    SyslogV.record.severity = SYSLOG_INFO;
    SyslogV.record.msg = longmsg;
    Syslog.log(protocore_syslog_span());
    TEST_ASSERT_FALSE(SyslogV.ok);
    TEST_ASSERT_EQUAL_UINT(0, udp_cap_len());
}
