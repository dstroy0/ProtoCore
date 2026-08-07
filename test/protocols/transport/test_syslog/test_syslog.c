// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the RFC 5424 syslog client (pc_syslog_format formatter + pc_syslog_init /
// pc_syslog_log over the udp_transport host capture seam).

#include "network_drivers/transport/udp.h"
#include "services/net/syslog/syslog.h"
#include <string.h>

#include <unity.h>

// The client queues; poll() moves the frame to the wire, which is where the host pcb driver
// records it. The last datagram it recorded is what this side sent.
static const uint8_t *udp_cap(void)
{
    Udp.client->poll();
    size_t n = pc_net_host_udp_count();
    return n ? pc_net_host_udp_at(n - 1)->data : NULL;
}
static size_t udp_cap_len(void)
{
    Udp.client->poll();
    size_t n = pc_net_host_udp_count();
    return n ? pc_net_host_udp_at(n - 1)->len : 0;
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
    size_t n = pc_syslog_format(buf, sizeof(buf), SYSLOG_FAC_LOCAL0, SYSLOG_INFO, "esp32", "app", "hello");
    TEST_ASSERT_GREATER_THAN_UINT(0, n);
    // PRI = 16*8 + 6 = 134
    TEST_ASSERT_EQUAL_STRING("<134>1 - esp32 app - - - hello", buf);
}

void test_pri_computation_varies()
{
    char buf[256];
    // daemon(3)*8 + err(3) = 27
    pc_syslog_format(buf, sizeof(buf), SYSLOG_FAC_DAEMON, SYSLOG_ERR, "h", "a", "m");
    TEST_ASSERT_EQUAL_STRING("<27>1 - h a - - - m", buf);
    // local7(23)*8 + emerg(0) = 184
    pc_syslog_format(buf, sizeof(buf), SYSLOG_FAC_LOCAL7, SYSLOG_EMERG, "h", "a", "m");
    TEST_ASSERT_EQUAL_STRING("<184>1 - h a - - - m", buf);
}

void test_nilvalue_for_empty_fields()
{
    char buf[256];
    pc_syslog_format(buf, sizeof(buf), SYSLOG_FAC_USER, SYSLOG_DEBUG, NULL, "", "msg");
    // user(1)*8 + debug(7) = 15; empty hostname + appname -> "-"
    TEST_ASSERT_EQUAL_STRING("<15>1 - - - - - - msg", buf);
}

void test_empty_message_ok()
{
    char buf[256];
    size_t n = pc_syslog_format(buf, sizeof(buf), SYSLOG_FAC_LOCAL0, SYSLOG_NOTICE, "h", "a", NULL);
    TEST_ASSERT_GREATER_THAN_UINT(0, n);
    // local0(16)*8 + notice(5) = 133; trailing MSG empty
    TEST_ASSERT_EQUAL_STRING("<133>1 - h a - - - ", buf);
}

void test_overflow_returns_zero()
{
    char buf[16]; // far too small for the header + message
    size_t n = pc_syslog_format(buf, sizeof(buf), SYSLOG_FAC_LOCAL0, SYSLOG_INFO, "esp32", "app", "a long message");
    TEST_ASSERT_EQUAL_UINT(0, n);
}

void test_length_matches_strlen()
{
    char buf[256];
    size_t n = pc_syslog_format(buf, sizeof(buf), SYSLOG_FAC_LOCAL0, SYSLOG_INFO, "host", "app", "payload");
    TEST_ASSERT_EQUAL_UINT(strlen(buf), n);
}

// pc_syslog_init + pc_syslog_log format the record and hand it to Udp.client->sendto.
void test_init_and_log_captured()
{
    pc_net_host_udp_reset();
    pc_syslog_init("192.168.1.1", 514, "host1", "myapp", SYSLOG_FAC_LOCAL0);
    TEST_ASSERT_TRUE(pc_syslog_log(SYSLOG_INFO, "hello"));
    const char *expect = "<134>1 - host1 myapp - - - hello";
    TEST_ASSERT_EQUAL_UINT(strlen(expect), udp_cap_len());
    TEST_ASSERT_EQUAL_MEMORY(expect, udp_cap(), udp_cap_len());
}

// With no server configured the client is not ready and sends nothing.
void test_log_not_ready_when_no_server()
{
    pc_net_host_udp_reset();
    pc_syslog_init(NULL, 514, "h", "a", SYSLOG_FAC_USER); // null server_ip -> copy_field empties it
    TEST_ASSERT_FALSE(pc_syslog_log(SYSLOG_INFO, "x"));
    TEST_ASSERT_EQUAL_UINT(0, udp_cap_len());
}

// A null output, a zero cap, and out-of-range PRI values are handled defensively.
void test_format_null_and_pri_clamp()
{
    char buf[64];
    // Guard clauses return 0.
    TEST_ASSERT_EQUAL_UINT(0, pc_syslog_format(NULL, sizeof(buf), (SyslogFacility)0, (SyslogSeverity)0, "h", "a", "m"));
    TEST_ASSERT_EQUAL_UINT(0, pc_syslog_format(buf, 0, (SyslogFacility)0, (SyslogSeverity)0, "h", "a", "m"));
    // facility/severity are unsigned enums, so PRI is never negative; an over-range value cast in at the
    // boundary clamps to 191.
    pc_syslog_format(buf, sizeof(buf), (SyslogFacility)25, (SyslogSeverity)0, "h", "a", "m"); // 200 clamps to 191
    TEST_ASSERT_EQUAL_STRING("<191>1 - h a - - - m", buf);
}

// An over-long hostname is truncated to PC_SYSLOG_FIELD_MAX - 1 characters.
void test_init_truncates_long_fields()
{
    pc_net_host_udp_reset();
    char longname[PC_SYSLOG_FIELD_MAX + 16];
    memset(longname, 'H', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    pc_syslog_init("10.0.0.1", 514, longname, "a", SYSLOG_FAC_LOCAL0);
    TEST_ASSERT_TRUE(pc_syslog_log(SYSLOG_INFO, "m"));
    const char *sent = (const char *)udp_cap();
    const char *host = strstr(sent, "1 - ") + 4; // start of the HOSTNAME field
    size_t hcount = 0;
    while (host[hcount] == 'H')
    {
        hcount++;
    }
    TEST_ASSERT_EQUAL_UINT((size_t)(PC_SYSLOG_FIELD_MAX - 1), hcount);
}

// An empty (non-null) server_ip hits copy_field()'s "src non-null but empty" branch (distinct from
// the null-src branch already covered by test_log_not_ready_when_no_server) and leaves the client
// not-ready, since server_ip[0] is '\0'.
void test_init_empty_server_ip_not_ready()
{
    pc_net_host_udp_reset();
    pc_syslog_init("", 514, "host", "app", SYSLOG_FAC_LOCAL0);
    TEST_ASSERT_FALSE(pc_syslog_log(SYSLOG_INFO, "x"));
    TEST_ASSERT_EQUAL_UINT(0, udp_cap_len());
}

// An empty (non-null) hostname and a null appname exercise the remaining branch of each field's
// "(field && field[0]) ? field : \"-\"" ternary: test_nilvalue_for_empty_fields already covered
// hostname==NULL and appname=="" ; this covers hostname=="" and appname==NULL.
void test_format_hostname_empty_appname_null()
{
    char buf[64];
    size_t n = pc_syslog_format(buf, sizeof(buf), SYSLOG_FAC_USER, SYSLOG_WARNING, "", NULL, "msg2");
    TEST_ASSERT_GREATER_THAN_UINT(0, n);
    // user(1)*8 + warning(4) = 12; empty hostname + null appname -> both NILVALUE "-"
    TEST_ASSERT_EQUAL_STRING("<12>1 - - - - - - msg2", buf);
}

// Every memcpy span in the sl_append() chain (PRI digits, ">1 - ", HOSTNAME, SP, APP-NAME,
// " - - - ", MSG) is starved by exactly one byte in turn, so each append's own failure branch is
// hit individually rather than only ever failing on whichever span the earlier overflow test
// happens to reach first. facility USER(1)/severity EMERG(0) gives PRI=8, a single decimal digit,
// so every byte offset below is exact. This also exercises pri<10 (the `if (pri >= 10)` false arm).
void test_format_append_boundaries()
{
    char buf[32];
    static const size_t fail_caps[] = {1, 2, 7, 8, 9, 10, 17, 18};
    for (size_t i = 0; i < sizeof(fail_caps) / sizeof(fail_caps[0]); i++)
    {
        size_t n = pc_syslog_format(buf, fail_caps[i], SYSLOG_FAC_USER, SYSLOG_EMERG, "h", "a", "m");
        TEST_ASSERT_EQUAL_UINT(0, n);
    }
    // One byte past the last boundary above: every span now fits exactly.
    size_t n = pc_syslog_format(buf, 19, SYSLOG_FAC_USER, SYSLOG_EMERG, "h", "a", "m");
    TEST_ASSERT_GREATER_THAN_UINT(0, n);
    TEST_ASSERT_EQUAL_STRING("<8>1 - h a - - - m", buf);
}

// With the client ready, a message long enough to overflow the PC_SYSLOG_MSG_MAX scratch buffer
// makes the internal pc_syslog_format() call return 0, so pc_syslog_log() must report false
// (the `n == 0` true arm, never reached by test_init_and_log_captured's fitting message).
void test_log_overflow_when_ready()
{
    pc_net_host_udp_reset();
    char longname[PC_SYSLOG_FIELD_MAX + 16];
    memset(longname, 'H', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    pc_syslog_init("10.0.0.1", 514, longname, longname, SYSLOG_FAC_LOCAL0);
    char longmsg[240];
    memset(longmsg, 'M', sizeof(longmsg) - 1);
    longmsg[sizeof(longmsg) - 1] = '\0';
    // header (~1 + 3 + 5 + 31 + 1 + 31 + 7 = 79 bytes) + a 239-byte message overflows the 256-byte
    // scratch buffer, so the format call inside pc_syslog_log() returns 0.
    TEST_ASSERT_FALSE(pc_syslog_log(SYSLOG_INFO, longmsg));
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
