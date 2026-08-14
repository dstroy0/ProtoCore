// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/transport/proxy_protocol/proxy_protocol.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static const uint32_t SRC = 0xC0A80001u;
static const uint32_t DST = 0xC0A8000Bu;

static const uint8_t SIG[12] = {0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, 0x55, 0x49, 0x54, 0x0A};

static proto_bool parsed_without_addr(const char *line)
{
    ProxyInfo info;
    size_t consumed = 0;
    proto_bool ok = proxy_parse((const uint8_t *)line, strlen(line), &info, &consumed);
    return ok && !info.has_addr;
}

void test_v1_spec_example_line(void)
{
    char buf[64];
    size_t n = proxy_v1_build(buf, sizeof(buf), SRC, DST, 56324, 443);
    TEST_ASSERT_EQUAL_STRING("PROXY TCP4 192.168.0.1 192.168.0.11 56324 443\r\n", buf);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);

    ProxyInfo info;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(proxy_parse((const uint8_t *)buf, n, &info, &consumed));
    TEST_ASSERT_EQUAL_UINT8(1, info.version);
    TEST_ASSERT_TRUE(info.has_addr);
    TEST_ASSERT_EQUAL_HEX32(SRC, info.src_addr);
    TEST_ASSERT_EQUAL_HEX32(DST, info.dst_addr);
    TEST_ASSERT_EQUAL_UINT16(56324, info.src_port);
    TEST_ASSERT_EQUAL_UINT16(443, info.dst_port);
    TEST_ASSERT_EQUAL_size_t(n, consumed);
}

void test_v1_widest_tcp4_line_is_56_octets(void)
{
    char buf[64];
    size_t n = proxy_v1_build(buf, sizeof(buf), 0xFFFFFFFFu, 0xFFFFFFFFu, 65535, 65535);
    TEST_ASSERT_EQUAL_STRING("PROXY TCP4 255.255.255.255 255.255.255.255 65535 65535\r\n", buf);
    TEST_ASSERT_EQUAL_size_t(56u, n);
}

void test_v1_unknown_short_form(void)
{
    const char *raw = "PROXY UNKNOWN\r\n";
    ProxyInfo info;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(proxy_parse((const uint8_t *)raw, strlen(raw), &info, &consumed));
    TEST_ASSERT_EQUAL_UINT8(1, info.version);
    TEST_ASSERT_FALSE(info.has_addr);
    TEST_ASSERT_EQUAL_size_t(15u, consumed);
}

void test_v1_unknown_long_form_is_ignored_up_to_the_crlf(void)
{
    const char *raw = "PROXY UNKNOWN ffff::ffff ffff::ffff 65535 65535\r\n";
    ProxyInfo info;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(proxy_parse((const uint8_t *)raw, strlen(raw), &info, &consumed));
    TEST_ASSERT_FALSE(info.has_addr);
    TEST_ASSERT_EQUAL_size_t(strlen(raw), consumed);
}

void test_v2_header_layout(void)
{
    uint8_t buf[32];
    size_t n = proxy_v2_build(buf, sizeof(buf), SRC, DST, 56324, 443);

    const uint8_t want[28] = {0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, 0x55, 0x49, 0x54, 0x0A,
                              0x21,
                              0x11,
                              0x00, 0x0C,
                              0xC0, 0xA8, 0x00, 0x01,
                              0xC0, 0xA8, 0x00, 0x0B,
                              0xDC, 0x04,
                              0x01, 0xBB};
    TEST_ASSERT_EQUAL_size_t(28u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, buf, sizeof(want));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SIG, buf, sizeof(SIG));
}

void test_v2_round_trip(void)
{
    uint8_t buf[32];
    size_t n = proxy_v2_build(buf, sizeof(buf), SRC, DST, 56324, 443);
    ProxyInfo info;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(proxy_parse(buf, n, &info, &consumed));
    TEST_ASSERT_EQUAL_UINT8(2, info.version);
    TEST_ASSERT_TRUE(info.has_addr);
    TEST_ASSERT_EQUAL_HEX32(SRC, info.src_addr);
    TEST_ASSERT_EQUAL_HEX32(DST, info.dst_addr);
    TEST_ASSERT_EQUAL_UINT16(56324, info.src_port);
    TEST_ASSERT_EQUAL_UINT16(443, info.dst_port);
    TEST_ASSERT_EQUAL_size_t(28u, consumed);
}

void test_v2_local_command_yields_no_address(void)
{
    uint8_t hdr[16] = {0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51,
                       0x55, 0x49, 0x54, 0x0A, 0x20, 0x11, 0x00, 0x00};
    ProxyInfo info;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(proxy_parse(hdr, sizeof(hdr), &info, &consumed));
    TEST_ASSERT_EQUAL_UINT8(2, info.version);
    TEST_ASSERT_FALSE(info.has_addr);
    TEST_ASSERT_EQUAL_size_t(16u, consumed);
}

void test_v2_unimplemented_family_is_skipped_by_its_length(void)
{
    uint8_t hdr[52];
    memset(hdr, 0xAA, sizeof(hdr));
    memcpy(hdr, SIG, sizeof(SIG));
    hdr[12] = 0x21;
    hdr[13] = 0x21;
    hdr[14] = 0x00;
    hdr[15] = 0x24;
    ProxyInfo info;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(proxy_parse(hdr, sizeof(hdr), &info, &consumed));
    TEST_ASSERT_EQUAL_UINT8(2, info.version);
    TEST_ASSERT_FALSE(info.has_addr);
    TEST_ASSERT_EQUAL_size_t(52u, consumed);
}

void test_v2_rejects_a_foreign_version(void)
{
    uint8_t hdr[16] = {0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51,
                       0x55, 0x49, 0x54, 0x0A, 0x31, 0x11, 0x00, 0x00};
    ProxyInfo info;
    size_t consumed = 0;
    TEST_ASSERT_FALSE(proxy_parse(hdr, sizeof(hdr), &info, &consumed));
}

void test_partial_headers_are_refused(void)
{
    ProxyInfo info;
    size_t consumed = 0;

    const char *v1 = "PROXY TCP4 192.168.0.1 192.168.0.11 56324 443";
    TEST_ASSERT_FALSE(proxy_parse((const uint8_t *)v1, strlen(v1), &info, &consumed));

    uint8_t sig_only[14];
    memcpy(sig_only, SIG, sizeof(SIG));
    sig_only[12] = 0x21;
    sig_only[13] = 0x11;
    TEST_ASSERT_FALSE(proxy_parse(sig_only, sizeof(sig_only), &info, &consumed));

    uint8_t short_block[16];
    memcpy(short_block, SIG, sizeof(SIG));
    short_block[12] = 0x21;
    short_block[13] = 0x11;
    short_block[14] = 0x00;
    short_block[15] = 0x0C;
    TEST_ASSERT_FALSE(proxy_parse(short_block, sizeof(short_block), &info, &consumed));
}

void test_a_lone_cr_or_lf_does_not_terminate_the_line(void)
{
    ProxyInfo info;
    size_t consumed = 0;

    const char *lf = "PROXY TCP4 192.168.0.1 192.168.0.11 56324 443\n";
    TEST_ASSERT_FALSE(proxy_parse((const uint8_t *)lf, strlen(lf), &info, &consumed));

    const char *cr = "PROXY UNKNOWN\rstill the same line\r\n";
    TEST_ASSERT_TRUE(proxy_parse((const uint8_t *)cr, strlen(cr), &info, &consumed));
    TEST_ASSERT_EQUAL_size_t(strlen(cr), consumed);
}

void test_v1_field_ranges(void)
{
    TEST_ASSERT_TRUE(parsed_without_addr("PROXY TCP4 256.0.0.1 10.0.0.1 1 2\r\n"));
    TEST_ASSERT_TRUE(parsed_without_addr("PROXY TCP4 10.0.0.1 256.0.0.1 1 2\r\n"));
    TEST_ASSERT_TRUE(parsed_without_addr("PROXY TCP4 1.2.3 10.0.0.1 1 2\r\n"));
    TEST_ASSERT_TRUE(parsed_without_addr("PROXY TCP4 1.2.3.4.5 10.0.0.1 1 2\r\n"));
    TEST_ASSERT_TRUE(parsed_without_addr("PROXY TCP4 1.2.3.4x 10.0.0.1 1 2\r\n"));
    TEST_ASSERT_TRUE(parsed_without_addr("PROXY TCP4 10.0.0.1 10.0.0.1 65536 2\r\n"));
    TEST_ASSERT_TRUE(parsed_without_addr("PROXY TCP4 10.0.0.1 10.0.0.1 1 65536\r\n"));
    TEST_ASSERT_TRUE(parsed_without_addr("PROXY TCP4 10.0.0.1 10.0.0.1 8x 2\r\n"));

    ProxyInfo info;
    size_t consumed = 0;
    const char *edge = "PROXY TCP4 0.0.0.0 255.255.255.255 0 65535\r\n";
    TEST_ASSERT_TRUE(proxy_parse((const uint8_t *)edge, strlen(edge), &info, &consumed));
    TEST_ASSERT_TRUE(info.has_addr);
    TEST_ASSERT_EQUAL_HEX32(0u, info.src_addr);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, info.dst_addr);
    TEST_ASSERT_EQUAL_UINT16(0, info.src_port);
    TEST_ASSERT_EQUAL_UINT16(65535, info.dst_port);
}

void test_v1_leading_zeros_are_refused(void)
{
    TEST_ASSERT_TRUE(parsed_without_addr("PROXY TCP4 010.0.0.1 10.0.0.1 1 2\r\n"));
    TEST_ASSERT_TRUE(parsed_without_addr("PROXY TCP4 10.0.0.1 10.00.0.1 1 2\r\n"));
    TEST_ASSERT_TRUE(parsed_without_addr("PROXY TCP4 10.0.0.1 10.0.0.1 0443 2\r\n"));
    TEST_ASSERT_TRUE(parsed_without_addr("PROXY TCP4 10.0.0.1 10.0.0.1 1 080\r\n"));

    ProxyInfo info;
    size_t consumed = 0;
    const char *zero = "PROXY TCP4 0.0.0.0 0.0.0.0 0 0\r\n";
    TEST_ASSERT_TRUE(proxy_parse((const uint8_t *)zero, strlen(zero), &info, &consumed));
    TEST_ASSERT_TRUE(info.has_addr);
}

void test_v1_other_protocol_tokens_yield_no_address(void)
{
    TEST_ASSERT_TRUE(parsed_without_addr("PROXY TCP6 ::1 ::1 1 2\r\n"));
    TEST_ASSERT_TRUE(parsed_without_addr("PROXY TCP 1.2.3.4 5.6.7.8 1 2\r\n"));
    TEST_ASSERT_TRUE(parsed_without_addr("PROXY WXYZ 1.2.3.4 5.6.7.8 1 2\r\n"));
}

void test_a_stream_without_a_header_is_reported_as_such(void)
{
    const char *http = "GET / HTTP/1.1\r\n";
    ProxyInfo info;
    size_t consumed = 0;
    TEST_ASSERT_FALSE(proxy_parse((const uint8_t *)http, strlen(http), &info, &consumed));

    const uint8_t tiny[2] = {0x00, 0x01};
    TEST_ASSERT_FALSE(proxy_parse(tiny, sizeof(tiny), &info, &consumed));
}

void test_builders_fail_closed_on_a_short_buffer(void)
{
    char small[16];
    TEST_ASSERT_EQUAL_size_t(0u, proxy_v1_build(small, sizeof(small), SRC, DST, 56324, 443));
    uint8_t v2small[27];
    TEST_ASSERT_EQUAL_size_t(0u, proxy_v2_build(v2small, sizeof(v2small), SRC, DST, 56324, 443));
}

void test_null_arguments_are_refused(void)
{
    ProxyInfo info;
    size_t consumed = 0;
    uint8_t any[16] = {0};
    TEST_ASSERT_FALSE(proxy_parse(NULL, sizeof(any), &info, &consumed));
    TEST_ASSERT_FALSE(proxy_parse(any, sizeof(any), NULL, &consumed));
    TEST_ASSERT_FALSE(proxy_parse(any, sizeof(any), &info, NULL));
    TEST_ASSERT_EQUAL_size_t(0u, proxy_v1_build(NULL, 64, SRC, DST, 1, 2));
    TEST_ASSERT_EQUAL_size_t(0u, proxy_v2_build(NULL, 64, SRC, DST, 1, 2));
}
