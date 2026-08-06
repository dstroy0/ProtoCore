// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the HAProxy PROXY protocol codec (services/net/proxy_protocol): the v1 (text)
// and v2 (binary) builders + the unified parser. Per the HAProxy spec. Pure host tests.

#include "services/net/proxy_protocol/proxy_protocol.h"
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// 203.0.113.50 / 203.0.113.10 in host-order uint32.
static const uint32_t SRC = 0xCB007132u;
static const uint32_t DST = 0xCB00710Au;

void test_v1_build()
{
    char buf[64];
    size_t n = proxy_v1_build(buf, sizeof(buf), SRC, DST, 12345, 80);
    TEST_ASSERT_EQUAL_STRING("PROXY TCP4 203.0.113.50 203.0.113.10 12345 80\r\n", buf);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
}

void test_v1_round_trip()
{
    uint8_t buf[64];
    size_t n = proxy_v1_build((char *)buf, sizeof(buf), SRC, DST, 12345, 80);
    ProxyInfo info;
    size_t consumed;
    TEST_ASSERT_TRUE(proxy_parse(buf, n, &info, &consumed));
    TEST_ASSERT_EQUAL_UINT8(1, info.version);
    TEST_ASSERT_TRUE(info.has_addr);
    TEST_ASSERT_EQUAL_HEX32(SRC, info.src_addr);
    TEST_ASSERT_EQUAL_HEX32(DST, info.dst_addr);
    TEST_ASSERT_EQUAL_UINT16(12345, info.src_port);
    TEST_ASSERT_EQUAL_UINT16(80, info.dst_port);
    TEST_ASSERT_EQUAL_size_t(n, consumed);
}

void test_v2_build_bytes()
{
    uint8_t buf[32];
    size_t n = proxy_v2_build(buf, sizeof(buf), SRC, DST, 12345, 80);
    const uint8_t expect[] = {
        0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, 0x55, 0x49, 0x54, 0x0A, // signature
        0x21, 0x11, 0x00, 0x0C,                                                 // ver_cmd, fam, len 12
        0xCB, 0x00, 0x71, 0x32,                                                 // src 203.0.113.50
        0xCB, 0x00, 0x71, 0x0A,                                                 // dst 203.0.113.10
        0x30, 0x39,                                                             // src port 12345
        0x00, 0x50                                                              // dst port 80
    };
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_size_t(28, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

void test_v2_round_trip()
{
    uint8_t buf[32];
    size_t n = proxy_v2_build(buf, sizeof(buf), SRC, DST, 443, 8080);
    ProxyInfo info;
    size_t consumed;
    TEST_ASSERT_TRUE(proxy_parse(buf, n, &info, &consumed));
    TEST_ASSERT_EQUAL_UINT8(2, info.version);
    TEST_ASSERT_TRUE(info.has_addr);
    TEST_ASSERT_EQUAL_HEX32(SRC, info.src_addr);
    TEST_ASSERT_EQUAL_UINT16(443, info.src_port);
    TEST_ASSERT_EQUAL_UINT16(8080, info.dst_port);
    TEST_ASSERT_EQUAL_size_t(n, consumed);
}

void test_v1_unknown()
{
    const char *raw = "PROXY UNKNOWN\r\n";
    ProxyInfo info;
    size_t consumed;
    TEST_ASSERT_TRUE(proxy_parse((const uint8_t *)raw, strlen(raw), &info, &consumed));
    TEST_ASSERT_EQUAL_UINT8(1, info.version);
    TEST_ASSERT_FALSE(info.has_addr);
    TEST_ASSERT_EQUAL_size_t(strlen(raw), consumed);
}

void test_not_a_proxy_header()
{
    const char *http = "GET / HTTP/1.1\r\n";
    ProxyInfo info;
    size_t consumed;
    TEST_ASSERT_FALSE(proxy_parse((const uint8_t *)http, strlen(http), &info, &consumed));
}

void test_incomplete()
{
    ProxyInfo info;
    size_t consumed;
    // v1 prefix but no CRLF yet.
    const char *v1 = "PROXY TCP4 203.0.113.50 203.0.113.10 12345 80";
    TEST_ASSERT_FALSE(proxy_parse((const uint8_t *)v1, strlen(v1), &info, &consumed));
    // v2 signature only (no address block).
    const uint8_t v2sig[] = {0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, 0x55, 0x49, 0x54, 0x0A, 0x21, 0x11};
    TEST_ASSERT_FALSE(proxy_parse(v2sig, sizeof(v2sig), &info, &consumed));
}

void test_build_overflow_fails_closed()
{
    char small[16];
    TEST_ASSERT_EQUAL_size_t(0, proxy_v1_build(small, sizeof(small), SRC, DST, 12345, 80));
    uint8_t v2small[20];
    TEST_ASSERT_EQUAL_size_t(0, proxy_v2_build(v2small, sizeof(v2small), SRC, DST, 12345, 80)); // needs 28
}

// A complete (CRLF-terminated) v1 header whose 5-tuple is malformed: the header parses, but
// the bad field leaves has_addr false (the address is only set when every field is valid).
static proto_bool no_addr(const char *line)
{
    ProxyInfo info;
    size_t consumed = 0;
    proto_bool ok = proxy_parse((const uint8_t *)line, strlen(line), &info, &consumed);
    return ok && !info.has_addr;
}

void test_v1_malformed_addresses_fail_closed()
{
    // Each line is CRLF-terminated so it reaches parse_ipv4 / parse_u16 (a header without a
    // CRLF is rejected earlier as "line not complete", exercising nothing in those parsers).
    TEST_ASSERT_TRUE(no_addr("PROXY TCP4 x.0.0.1 10.0.0.1 1 2\r\n"));       // non-digit octet start
    TEST_ASSERT_TRUE(no_addr("PROXY TCP4 999.0.0.1 10.0.0.1 1 2\r\n"));     // octet > 255
    TEST_ASSERT_TRUE(no_addr("PROXY TCP4 10x0.0.0.1 10.0.0.1 1 2\r\n"));    // missing dot separator
    TEST_ASSERT_TRUE(no_addr("PROXY TCP4 1.2.3.4x 10.0.0.1 1 2\r\n"));      // trailing junk after 4 octets
    TEST_ASSERT_TRUE(no_addr("PROXY TCP4 10.0.0.1 10.0.0.1 123456 2\r\n")); // port > 5 digits
    TEST_ASSERT_TRUE(no_addr("PROXY TCP4 10.0.0.1 10.0.0.1 8x 2\r\n"));     // non-digit in port
    TEST_ASSERT_TRUE(no_addr("PROXY TCP4 10.0.0.1 10.0.0.1 99999 2\r\n"));  // port > 65535
    TEST_ASSERT_TRUE(no_addr("PROXY TCP4 1.2.3. 10.0.0.1 1 2\r\n"));        // src missing its 4th octet
    TEST_ASSERT_TRUE(no_addr("PROXY TCP4 -1.2.3.4 10.0.0.1 1 2\r\n"));      // octet starts below '0'
    TEST_ASSERT_TRUE(no_addr("PROXY TCP4 1234.0.0.1 10.0.0.1 1 2\r\n"));    // more than 3 digits in an octet
    TEST_ASSERT_TRUE(no_addr("PROXY TCP4 1 10.0.0.1 1 2\r\n"));             // src has no dots at all
    TEST_ASSERT_TRUE(no_addr("PROXY TCP4 10.0.0.1 10.0.0.1 -1 2\r\n"));     // port starts below '0'
    TEST_ASSERT_TRUE(no_addr("PROXY TCP4 10.0.0.1 x.0.0.1 1 2\r\n"));       // dst (not src) address malformed
    TEST_ASSERT_TRUE(no_addr("PROXY TCP4 10.0.0.1 10.0.0.1 1 8x\r\n"));     // dst (not src) port malformed
    TEST_ASSERT_TRUE(no_addr("PROXY TCP 1.2.3.4 5.6.7.8 1 2\r\n"));         // protocol token length != 4 ("TCP")
    TEST_ASSERT_TRUE(no_addr("PROXY TCP6 a b c d\r\n"));                    // protocol token length 4 but != "TCP4"
}

void test_v1_extra_tokens_ignored()
{
    // A 7th space-separated field after a complete 6-field header: the tokenizer stops
    // recording at 6 tokens even though input remains before the CRLF (the tokenizing loop
    // exits on the token-count cap, not on reaching the end of the line).
    const char *raw = "PROXY TCP4 203.0.113.50 203.0.113.10 12345 80 EXTRA\r\n";
    ProxyInfo info;
    size_t consumed;
    TEST_ASSERT_TRUE(proxy_parse((const uint8_t *)raw, strlen(raw), &info, &consumed));
    TEST_ASSERT_TRUE(info.has_addr);
    TEST_ASSERT_EQUAL_UINT16(12345, info.src_port);
    TEST_ASSERT_EQUAL_UINT16(80, info.dst_port);
    TEST_ASSERT_EQUAL_size_t(strlen(raw), consumed);
}

void test_v1_stray_cr_before_terminator()
{
    // A lone '\r' (not followed by '\n') before the real terminator: the CRLF scan must keep
    // looking past it rather than mistaking it for the line end.
    const char *raw = "PROXY UNKNOWN\rXTRA\r\n";
    ProxyInfo info;
    size_t consumed;
    TEST_ASSERT_TRUE(proxy_parse((const uint8_t *)raw, strlen(raw), &info, &consumed));
    TEST_ASSERT_EQUAL_UINT8(1, info.version);
    TEST_ASSERT_FALSE(info.has_addr);
    TEST_ASSERT_EQUAL_size_t(strlen(raw), consumed);
}

void test_v2_non_addr_variants()
{
    ProxyInfo info;
    size_t consumed;

    // version 2, LOCAL command (not PROXY): a valid v2 header that intentionally carries no
    // address (e.g. a load balancer health check).
    uint8_t local_cmd[16] = {0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51,
                             0x55, 0x49, 0x54, 0x0A, 0x20, 0x11, 0x00, 0x00}; // ver_cmd 0x20 (LOCAL)
    TEST_ASSERT_TRUE(proxy_parse(local_cmd, sizeof(local_cmd), &info, &consumed));
    TEST_ASSERT_EQUAL_UINT8(2, info.version);
    TEST_ASSERT_FALSE(info.has_addr);
    TEST_ASSERT_EQUAL_size_t(16, consumed);

    // version 2, PROXY command, but a non-TCP4 address family.
    uint8_t bad_fam[16] = {0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51,
                           0x55, 0x49, 0x54, 0x0A, 0x21, 0x12, 0x00, 0x00}; // fam 0x12, not TCP4
    TEST_ASSERT_TRUE(proxy_parse(bad_fam, sizeof(bad_fam), &info, &consumed));
    TEST_ASSERT_EQUAL_UINT8(2, info.version);
    TEST_ASSERT_FALSE(info.has_addr);
    TEST_ASSERT_EQUAL_size_t(16, consumed);

    // version 2, PROXY command, TCP4 family, but an address-block length too short for a
    // TCP4 tuple (12 octets).
    uint8_t short_block[20] = {0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51,
                               0x55, 0x49, 0x54, 0x0A, 0x21, 0x11, 0x00, 0x04, // addr_len 4 (< 12)
                               0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_TRUE(proxy_parse(short_block, sizeof(short_block), &info, &consumed));
    TEST_ASSERT_EQUAL_UINT8(2, info.version);
    TEST_ASSERT_FALSE(info.has_addr);
    TEST_ASSERT_EQUAL_size_t(20, consumed);
}

void test_short_buffer_not_proxy_header()
{
    // Fewer octets than the v2 signature (12) and shorter than the v1 "PROXY " prefix (6):
    // both length guards must reject it without reading past the buffer.
    const uint8_t tiny[2] = {0x00, 0x01};
    ProxyInfo info;
    size_t consumed;
    TEST_ASSERT_FALSE(proxy_parse(tiny, sizeof(tiny), &info, &consumed));
}

void test_parse_and_build_guards()
{
    ProxyInfo info;
    size_t consumed = 0;
    // proxy_parse null-argument guards + proxy_v1_build null buffer.
    TEST_ASSERT_FALSE(proxy_parse(NULL, 16, &info, &consumed));
    uint8_t any[16] = {0};
    TEST_ASSERT_FALSE(proxy_parse(any, 16, NULL, &consumed));
    TEST_ASSERT_FALSE(proxy_parse(any, 16, &info, NULL));
    TEST_ASSERT_EQUAL_size_t(0, proxy_v1_build(NULL, 64, SRC, DST, 1, 2));
    TEST_ASSERT_EQUAL_size_t(0, proxy_v2_build(NULL, 64, SRC, DST, 1, 2));

    // v2: signature + full header, but the declared address block isn't fully buffered.
    uint8_t under[16] = {0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51,
                         0x55, 0x49, 0x54, 0x0A, 0x21, 0x11, 0x00, 0xFF}; // addr_len 255, only 16 octets present
    TEST_ASSERT_FALSE(proxy_parse(under, sizeof(under), &info, &consumed));

    // v2: signature + full header, but the version nibble is not 2.
    uint8_t badver[16] = {0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51,
                          0x55, 0x49, 0x54, 0x0A, 0x31, 0x11, 0x00, 0x00}; // ver_cmd 0x31 -> high nibble 3
    TEST_ASSERT_FALSE(proxy_parse(badver, sizeof(badver), &info, &consumed));

    // v1 header with trailing spaces + too few tokens (tokenizer break-on-trailing-space path).
    const char *sp = "PROXY UNKNOWN \r\n";
    TEST_ASSERT_TRUE(proxy_parse((const uint8_t *)sp, strlen(sp), &info, &consumed));
    TEST_ASSERT_FALSE(info.has_addr);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_v1_build);
    RUN_TEST(test_v1_round_trip);
    RUN_TEST(test_v2_build_bytes);
    RUN_TEST(test_v2_round_trip);
    RUN_TEST(test_v1_unknown);
    RUN_TEST(test_not_a_proxy_header);
    RUN_TEST(test_incomplete);
    RUN_TEST(test_build_overflow_fails_closed);
    RUN_TEST(test_v1_malformed_addresses_fail_closed);
    RUN_TEST(test_v1_extra_tokens_ignored);
    RUN_TEST(test_v1_stray_cr_before_terminator);
    RUN_TEST(test_v2_non_addr_variants);
    RUN_TEST(test_short_buffer_not_proxy_header);
    RUN_TEST(test_parse_and_build_guards);
    return UNITY_END();
}
