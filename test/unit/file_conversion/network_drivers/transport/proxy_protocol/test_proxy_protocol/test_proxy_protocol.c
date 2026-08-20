// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the HAProxy PROXY protocol codec
// (network_drivers/transport/proxy_protocol/proxy_protocol.h).
//
// Governing document: "The PROXY protocol - Versions 1 & 2", HAProxy Technologies, read at
// https://www.haproxy.org/download/2.8/doc/proxy-protocol.txt. Sections 2 (common), 2.1
// (human-readable v1) and 2.2 (binary v2).
//
// The load-bearing v1 case is test_v1_published_example_line: sec 2.1 prints
// "PROXY TCP4 192.168.0.1 192.168.0.11 56324 443\r\n" verbatim, and
// test_v1_widest_tcp4_line_is_56_octets reproduces the spec's own
// "PROXY TCP4 255.255.255.255 255.255.255.255 65535 65535\r\n" together with the arithmetic the
// spec publishes beside it. The load-bearing v2 case is test_v2_published_layout: sec 2.2 prints
// the 12-octet signature octet by octet, fixes \x2 as the version nibble, \x1 as the PROXY
// command, \x11 as TCP over IPv4 and 2*4 + 2*2 = 12 as its address length; every other octet of
// the expected header is an address or a port written out in network byte order, derived in the
// comment that carries it.
//
// Six cases assert the document against the codec and are expected to FAIL:
// test_v1_fields_outside_the_published_ranges_are_discarded,
// test_v1_heading_zeroes_are_discarded and test_v1_an_unlisted_family_token_is_discarded rest on
// sec 2.1 "Any sequence which does not exactly match the protocol must be discarded and cause the
// receiver to abort the connection"; test_v2_an_unassigned_command_is_dropped and
// test_v2_an_unassigned_address_family_is_rejected rest on sec 2.2 "Receivers must drop
// connections presenting unexpected values here" and "must be rejected as invalid by receivers";
// test_v1_line_is_bounded_at_107_octets rests on sec 2.1's weaker "should declare the line
// invalid" past 107 characters.

#include "network_drivers/transport/proxy_protocol/proxy_protocol.h"

#include <string.h>
#include <unity.h>

static uint8_t proxy_protocol_work[16]; // the borrow an entry takes; ProxyProtocol never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// The endpoints of sec 2.1's example line, one octet per dotted field:
//   192.168.0.1  -> 0xC0 0xA8 0x00 0x01 -> 0xC0A80001
//   192.168.0.11 -> 0xC0 0xA8 0x00 0x0B -> 0xC0A8000B
static const uint32_t SRC = 0xC0A80001u;
static const uint32_t DST = 0xC0A8000Bu;

// sec 2.2: "\x0D \x0A \x0D \x0A \x00 \x0D \x0A \x51 \x55 \x49 \x54 \x0A".
static const uint8_t SIG[12] = {0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, 0x55, 0x49, 0x54, 0x0A};

static proto_bool parse_line(const char *line, ProxyInfo *info, size_t *consumed)
{
    *consumed = 0;
    ProxyProtocol.parse_args.buf = (const uint8_t *)line;
    ProxyProtocol.parse_args.len = strlen(line);
    ProxyProtocol.parse_args.out = info;
    ProxyProtocol.parse_args.consumed = consumed;
    ProxyProtocol.parse(proxy_protocol_work);
    return ProxyProtocol.ok;
}

static proto_bool line_is_refused(const char *line)
{
    ProxyInfo info;
    size_t consumed = 0;
    return !parse_line(line, &info, &consumed);
}

// Lay a v2 header down: signature, ver_cmd, fam, then the address length as two octets "in network
// endian order" (sec 2.2). Returns 16 + addr_len, the total the spec fixes for the header.
static size_t v2_head(uint8_t *buf, uint8_t ver_cmd, uint8_t fam, uint16_t addr_len)
{
    memcpy(buf, SIG, sizeof(SIG));
    buf[12] = ver_cmd;
    buf[13] = fam;
    buf[14] = (uint8_t)(addr_len >> 8);
    buf[15] = (uint8_t)addr_len;
    return (size_t)16 + (size_t)addr_len;
}

// sec 2.1 prints one example of a v1 line:
//     PROXY TCP4 192.168.0.1 192.168.0.11 56324 443\r\n
void test_v1_published_example_line(void)
{
    char buf[64];
    ProxyProtocol.v1_build_args.buf = buf;
    ProxyProtocol.v1_build_args.cap = sizeof(buf);
    ProxyProtocol.v1_build_args.src_addr = SRC;
    ProxyProtocol.v1_build_args.dst_addr = DST;
    ProxyProtocol.v1_build_args.src_port = 56324;
    ProxyProtocol.v1_build_args.dst_port = 443;
    ProxyProtocol.v1_build(proxy_protocol_work);
    size_t n = ProxyProtocol.n;
    TEST_ASSERT_EQUAL_STRING("PROXY TCP4 192.168.0.1 192.168.0.11 56324 443\r\n", buf);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);

    ProxyInfo info;
    size_t consumed = 0;
    ProxyProtocol.parse_args.buf = (const uint8_t *)buf;
    ProxyProtocol.parse_args.len = n;
    ProxyProtocol.parse_args.out = &info;
    ProxyProtocol.parse_args.consumed = &consumed;
    ProxyProtocol.parse(proxy_protocol_work);
    TEST_ASSERT_TRUE(ProxyProtocol.ok);
    TEST_ASSERT_EQUAL_UINT8(1, info.version);
    TEST_ASSERT_TRUE(info.has_addr);
    TEST_ASSERT_EQUAL_HEX32(SRC, info.src_addr);
    TEST_ASSERT_EQUAL_HEX32(DST, info.dst_addr);
    TEST_ASSERT_EQUAL_UINT16(56324, info.src_port);
    TEST_ASSERT_EQUAL_UINT16(443, info.dst_port);
    TEST_ASSERT_EQUAL_size_t(n, consumed);
}

// sec 2.1 publishes the widest TCP/IPv4 line and its length:
//   "PROXY TCP4 255.255.255.255 255.255.255.255 65535 65535\r\n"
//   => 5 + 1 + 4 + 1 + 15 + 1 + 15 + 1 + 5 + 1 + 5 + 2 = 56 chars
void test_v1_widest_tcp4_line_is_56_octets(void)
{
    char buf[64];
    ProxyProtocol.v1_build_args.buf = buf;
    ProxyProtocol.v1_build_args.cap = sizeof(buf);
    ProxyProtocol.v1_build_args.src_addr = 0xFFFFFFFFu;
    ProxyProtocol.v1_build_args.dst_addr = 0xFFFFFFFFu;
    ProxyProtocol.v1_build_args.src_port = 65535;
    ProxyProtocol.v1_build_args.dst_port = 65535;
    ProxyProtocol.v1_build(proxy_protocol_work);
    size_t n = ProxyProtocol.n;
    TEST_ASSERT_EQUAL_STRING("PROXY TCP4 255.255.255.255 255.255.255.255 65535 65535\r\n", buf);
    TEST_ASSERT_EQUAL_size_t(56u, n);
}

// sec 2.1 publishes the short UNKNOWN form and its length:
//   "PROXY UNKNOWN\r\n" => 5 + 1 + 7 + 2 = 15 chars
void test_v1_unknown_short_form_is_15_octets(void)
{
    ProxyInfo info;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(parse_line("PROXY UNKNOWN\r\n", &info, &consumed));
    TEST_ASSERT_EQUAL_UINT8(1, info.version);
    TEST_ASSERT_FALSE(info.has_addr);
    TEST_ASSERT_EQUAL_size_t(15u, consumed);
}

// sec 2.1, on "UNKNOWN": "the rest of the line before the CRLF may be omitted by the sender, and
// the receiver must ignore anything presented before the CRLF is found".
void test_v1_unknown_ignores_everything_before_the_crlf(void)
{
    const char *raw = "PROXY UNKNOWN ffff::ffff ffff::ffff 65535 65535\r\n";
    ProxyInfo info;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(parse_line(raw, &info, &consumed));
    TEST_ASSERT_EQUAL_UINT8(1, info.version);
    TEST_ASSERT_FALSE(info.has_addr);
    TEST_ASSERT_EQUAL_size_t(strlen(raw), consumed);
}

// sec 2.1: "The receiver must not tolerate a single CR or LF character to end the line when a
// complete CRLF sequence is expected."
void test_v1_requires_a_complete_crlf(void)
{
    TEST_ASSERT_TRUE(line_is_refused("PROXY TCP4 192.168.0.1 192.168.0.11 56324 443\n"));
    TEST_ASSERT_TRUE(line_is_refused("PROXY TCP4 192.168.0.1 192.168.0.11 56324 443\r"));
    TEST_ASSERT_TRUE(line_is_refused("PROXY UNKNOWN\n"));
    TEST_ASSERT_TRUE(line_is_refused("PROXY UNKNOWN\r"));
}

// sec 2.1 publishes the worst case line and its length:
//   "PROXY UNKNOWN ffff:f...f:ffff ffff:f...f:ffff 65535 65535\r\n"
//   => 5 + 1 + 7 + 1 + 39 + 1 + 39 + 1 + 5 + 1 + 5 + 2 = 107 chars
// and then: "If the CRLF sequence is not found in the first 107 characters, the receiver should
// declare the line invalid."
//
// "PROXY UNKNOWN " is 5 + 1 + 7 + 1 = 14 octets and the CRLF is 2, so the filler between them runs
// 107 - 14 - 2 = 91 octets at the limit and 92 one octet past it. UNKNOWN makes the filler's own
// content irrelevant: the receiver ignores everything before the CRLF.
void test_v1_line_is_bounded_at_107_octets(void)
{
    char line[110];
    ProxyInfo info;
    size_t consumed = 0;

    memcpy(line, "PROXY UNKNOWN ", 14);
    memset(line + 14, 'f', 91);
    line[105] = '\r';
    line[106] = '\n';
    ProxyProtocol.parse_args.buf = (const uint8_t *)line;
    ProxyProtocol.parse_args.len = 107u;
    ProxyProtocol.parse_args.out = &info;
    ProxyProtocol.parse_args.consumed = &consumed;
    ProxyProtocol.parse(proxy_protocol_work);
    TEST_ASSERT_TRUE(ProxyProtocol.ok);
    TEST_ASSERT_EQUAL_size_t(107u, consumed);

    memset(line + 14, 'f', 92);
    line[106] = '\r';
    line[107] = '\n';
    ProxyProtocol.parse_args.buf = (const uint8_t *)line;
    ProxyProtocol.parse_args.len = 108u;
    ProxyProtocol.parse_args.out = &info;
    ProxyProtocol.parse_args.consumed = &consumed;
    ProxyProtocol.parse(proxy_protocol_work);
    TEST_ASSERT_FALSE(ProxyProtocol.ok);
}

// sec 2.1 fixes the field ranges: an IPv4 address is "a series of exactly 4 integers in the range
// [0..255] inclusive written in decimal representation separated by exactly one dot", and each
// port is "a decimal integer in the range [0..65535] inclusive". Then: "Any sequence which does
// not exactly match the protocol must be discarded and cause the receiver to abort the
// connection."
void test_v1_fields_outside_the_published_ranges_are_discarded(void)
{
    TEST_ASSERT_TRUE(line_is_refused("PROXY TCP4 256.0.0.1 10.0.0.1 1 2\r\n"));
    TEST_ASSERT_TRUE(line_is_refused("PROXY TCP4 10.0.0.1 256.0.0.1 1 2\r\n"));
    TEST_ASSERT_TRUE(line_is_refused("PROXY TCP4 1.2.3 10.0.0.1 1 2\r\n"));
    TEST_ASSERT_TRUE(line_is_refused("PROXY TCP4 1.2.3.4.5 10.0.0.1 1 2\r\n"));
    TEST_ASSERT_TRUE(line_is_refused("PROXY TCP4 1.2.3.4x 10.0.0.1 1 2\r\n"));
    TEST_ASSERT_TRUE(line_is_refused("PROXY TCP4 10.0.0.1 10.0.0.1 65536 2\r\n"));
    TEST_ASSERT_TRUE(line_is_refused("PROXY TCP4 10.0.0.1 10.0.0.1 1 65536\r\n"));
    TEST_ASSERT_TRUE(line_is_refused("PROXY TCP4 10.0.0.1 10.0.0.1 8x 2\r\n"));
}

// sec 2.1, stated for the addresses and again for each port: "Heading zeroes are not permitted in
// front of numbers in order to avoid any possible confusion with octal numbers." A single "0" is
// the number zero, not a heading zero, and 0 is inside both published ranges.
void test_v1_heading_zeroes_are_discarded(void)
{
    TEST_ASSERT_TRUE(line_is_refused("PROXY TCP4 010.0.0.1 10.0.0.1 1 2\r\n"));
    TEST_ASSERT_TRUE(line_is_refused("PROXY TCP4 10.0.0.1 10.00.0.1 1 2\r\n"));
    TEST_ASSERT_TRUE(line_is_refused("PROXY TCP4 10.0.0.1 10.0.0.1 0443 2\r\n"));
    TEST_ASSERT_TRUE(line_is_refused("PROXY TCP4 10.0.0.1 10.0.0.1 1 080\r\n"));

    ProxyInfo info;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(parse_line("PROXY TCP4 0.0.0.0 0.0.0.0 0 0\r\n", &info, &consumed));
    TEST_ASSERT_TRUE(info.has_addr);
}

// sec 2.1: "As of version 1, only "TCP4" ( \x54 \x43 \x50 \x34 ) for TCP over IPv4, and "TCP6"
// ( \x54 \x43 \x50 \x36 ) for TCP over IPv6 are allowed. Other, unsupported, or unknown protocols
// must be reported with the name "UNKNOWN"". The spec gives the octets, so the token is
// upper-case; anything else does not exactly match and falls under the discard rule.
void test_v1_an_unlisted_family_token_is_discarded(void)
{
    TEST_ASSERT_TRUE(line_is_refused("PROXY TCP 1.2.3.4 5.6.7.8 1 2\r\n"));
    TEST_ASSERT_TRUE(line_is_refused("PROXY WXYZ 1.2.3.4 5.6.7.8 1 2\r\n"));
    TEST_ASSERT_TRUE(line_is_refused("PROXY tcp4 1.2.3.4 5.6.7.8 1 2\r\n"));
    TEST_ASSERT_TRUE(line_is_refused("PROXY unknown\r\n"));
}

// sec 2.1 lists "TCP6" among the allowed families, so a well-formed TCP6 line matches the
// protocol. This codec carries IPv4 only, so it consumes the line and yields no IPv4 endpoint.
void test_v1_tcp6_is_allowed_but_carries_no_ipv4(void)
{
    const char *raw = "PROXY TCP6 ::1 ::1 1 2\r\n";
    ProxyInfo info;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(parse_line(raw, &info, &consumed));
    TEST_ASSERT_EQUAL_UINT8(1, info.version);
    TEST_ASSERT_FALSE(info.has_addr);
    TEST_ASSERT_EQUAL_size_t(strlen(raw), consumed);
}

// sec 2.1's ranges are inclusive at both ends: [0..255] per address octet, [0..65535] per port.
//   0.0.0.0         -> 0x00000000
//   255.255.255.255 -> 0xFFFFFFFF
void test_v1_the_published_range_endpoints_decode(void)
{
    ProxyInfo info;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(parse_line("PROXY TCP4 0.0.0.0 255.255.255.255 0 65535\r\n", &info, &consumed));
    TEST_ASSERT_TRUE(info.has_addr);
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, info.src_addr);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, info.dst_addr);
    TEST_ASSERT_EQUAL_UINT16(0, info.src_port);
    TEST_ASSERT_EQUAL_UINT16(65535, info.dst_port);
}

// sec 2.2 fixes every octet of a v2 TCP/IPv4 header:
//   [0..11]  the signature block, printed verbatim
//   [12]     "The highest four bits contains the version ... it must always be sent as \x2"; the
//            lowest four the command, "\x1 : PROXY" -> (2 << 4) | 1 = 0x21
//   [13]     "\x11 : TCP over IPv4"
//   [14..15] "the address length in bytes in network endian order"; for \x11 "Address length is
//            2*4 + 2*2 = 12 bytes" -> 0x00 0x0C
//   [16..19] "source layer 3 address in network byte order": 192.168.0.1  -> C0 A8 00 01
//   [20..23] "destination layer 3 address in network byte order": 192.168.0.11 -> C0 A8 00 0B
//   [24..25] source port, network byte order: 56324 = 0xDC * 256 + 0x04 -> DC 04
//   [26..27] destination port: 443 = 0x01 * 256 + 0xBB -> 01 BB
// and "the length of the protocol header in bytes is always exactly 16 + this value" -> 16+12 = 28.
void test_v2_published_layout(void)
{
    uint8_t buf[32];
    ProxyProtocol.v2_build_args.buf = buf;
    ProxyProtocol.v2_build_args.cap = sizeof(buf);
    ProxyProtocol.v2_build_args.src_addr = SRC;
    ProxyProtocol.v2_build_args.dst_addr = DST;
    ProxyProtocol.v2_build_args.src_port = 56324;
    ProxyProtocol.v2_build_args.dst_port = 443;
    ProxyProtocol.v2_build(proxy_protocol_work);
    size_t n = ProxyProtocol.n;

    static const uint8_t want[28] = {0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, 0x55, 0x49,
                                     0x54, 0x0A, 0x21, 0x11, 0x00, 0x0C, 0xC0, 0xA8, 0x00, 0x01,
                                     0xC0, 0xA8, 0x00, 0x0B, 0xDC, 0x04, 0x01, 0xBB};
    TEST_ASSERT_EQUAL_size_t(28u, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, buf, sizeof(want));
}

// Build then parse: the endpoints the builder wrote are the endpoints the parse reports, and the
// header runs the 16 + 12 = 28 octets sec 2.2 fixes.
void test_v2_round_trip(void)
{
    uint8_t buf[32];
    ProxyProtocol.v2_build_args.buf = buf;
    ProxyProtocol.v2_build_args.cap = sizeof(buf);
    ProxyProtocol.v2_build_args.src_addr = SRC;
    ProxyProtocol.v2_build_args.dst_addr = DST;
    ProxyProtocol.v2_build_args.src_port = 56324;
    ProxyProtocol.v2_build_args.dst_port = 443;
    ProxyProtocol.v2_build(proxy_protocol_work);
    size_t n = ProxyProtocol.n;
    ProxyInfo info;
    size_t consumed = 0;
    ProxyProtocol.parse_args.buf = buf;
    ProxyProtocol.parse_args.len = n;
    ProxyProtocol.parse_args.out = &info;
    ProxyProtocol.parse_args.consumed = &consumed;
    ProxyProtocol.parse(proxy_protocol_work);
    TEST_ASSERT_TRUE(ProxyProtocol.ok);
    TEST_ASSERT_EQUAL_UINT8(2, info.version);
    TEST_ASSERT_TRUE(info.has_addr);
    TEST_ASSERT_EQUAL_HEX32(SRC, info.src_addr);
    TEST_ASSERT_EQUAL_HEX32(DST, info.dst_addr);
    TEST_ASSERT_EQUAL_UINT16(56324, info.src_port);
    TEST_ASSERT_EQUAL_UINT16(443, info.dst_port);
    TEST_ASSERT_EQUAL_size_t(28u, consumed);
}

// sec 2.2, command "\x0 : LOCAL": "The receiver must accept this connection as valid and must use
// the real connection endpoints and discard the protocol block including the family which is
// ignored", and "Receivers MUST always consider this field to skip the appropriate number of bytes
// and must not assume zero is presented for LOCAL connections" - so a LOCAL header carrying a
// 12-octet block still runs 16 + 12 = 28 octets and still yields no endpoint.
void test_v2_local_command_yields_no_address(void)
{
    uint8_t hdr[28];
    ProxyInfo info;
    size_t consumed = 0;

    size_t n = v2_head(hdr, 0x20, 0x11, 0);
    TEST_ASSERT_EQUAL_size_t(16u, n);
    ProxyProtocol.parse_args.buf = hdr;
    ProxyProtocol.parse_args.len = n;
    ProxyProtocol.parse_args.out = &info;
    ProxyProtocol.parse_args.consumed = &consumed;
    ProxyProtocol.parse(proxy_protocol_work);
    TEST_ASSERT_TRUE(ProxyProtocol.ok);
    TEST_ASSERT_EQUAL_UINT8(2, info.version);
    TEST_ASSERT_FALSE(info.has_addr);
    TEST_ASSERT_EQUAL_size_t(16u, consumed);

    n = v2_head(hdr, 0x20, 0x11, 12);
    memset(hdr + 16, 0x5A, 12);
    TEST_ASSERT_EQUAL_size_t(28u, n);
    ProxyProtocol.parse_args.buf = hdr;
    ProxyProtocol.parse_args.len = n;
    ProxyProtocol.parse_args.out = &info;
    ProxyProtocol.parse_args.consumed = &consumed;
    ProxyProtocol.parse(proxy_protocol_work);
    TEST_ASSERT_TRUE(ProxyProtocol.ok);
    TEST_ASSERT_FALSE(info.has_addr);
    TEST_ASSERT_EQUAL_size_t(28u, consumed);
}

// sec 2.2: "\x21 : TCP over IPv6 ... Address length is 2*16 + 2*2 = 36 bytes", and "A receiver is
// not required to implement other ones, provided that it automatically falls back to the UNSPEC
// mode for the valid combinations above that it does not support". The length field carries the
// skip: 16 + 36 = 52.
void test_v2_a_valid_combination_it_does_not_implement_is_skipped(void)
{
    uint8_t hdr[52];
    memset(hdr, 0xAA, sizeof(hdr));
    size_t n = v2_head(hdr, 0x21, 0x21, 36);
    TEST_ASSERT_EQUAL_size_t(52u, n);

    ProxyInfo info;
    size_t consumed = 0;
    ProxyProtocol.parse_args.buf = hdr;
    ProxyProtocol.parse_args.len = n;
    ProxyProtocol.parse_args.out = &info;
    ProxyProtocol.parse_args.consumed = &consumed;
    ProxyProtocol.parse(proxy_protocol_work);
    TEST_ASSERT_TRUE(ProxyProtocol.ok);
    TEST_ASSERT_EQUAL_UINT8(2, info.version);
    TEST_ASSERT_FALSE(info.has_addr);
    TEST_ASSERT_EQUAL_size_t(52u, consumed);
}

// sec 2.2, the 13th byte: "The highest four bits contains the version. As of this specification, it
// must always be sent as \x2 and the receiver must only accept this value." The rest of the header
// is held at the LOCAL / AF_UNSPEC / zero-length form so only the version nibble moves.
void test_v2_only_version_2_is_accepted(void)
{
    uint8_t hdr[16];
    ProxyInfo info;
    size_t consumed = 0;
    for (unsigned v = 0; v < 16u; v++)
    {
        size_t n = v2_head(hdr, (uint8_t)(v << 4), 0x00, 0);
        if (v == 2u)
        {
            ProxyProtocol.parse_args.buf = hdr;
            ProxyProtocol.parse_args.len = n;
            ProxyProtocol.parse_args.out = &info;
            ProxyProtocol.parse_args.consumed = &consumed;
            ProxyProtocol.parse(proxy_protocol_work);
            TEST_ASSERT_TRUE(ProxyProtocol.ok);
        }
        else
        {
            ProxyProtocol.parse_args.buf = hdr;
            ProxyProtocol.parse_args.len = n;
            ProxyProtocol.parse_args.out = &info;
            ProxyProtocol.parse_args.consumed = &consumed;
            ProxyProtocol.parse(proxy_protocol_work);
            TEST_ASSERT_FALSE(ProxyProtocol.ok);
        }
    }
}

// sec 2.2, the low four bits of the 13th byte: "\x0 : LOCAL ... \x1 : PROXY ... other values are
// unassigned and must not be emitted by senders. Receivers must drop connections presenting
// unexpected values here."
void test_v2_an_unassigned_command_is_dropped(void)
{
    uint8_t hdr[16];
    ProxyInfo info;
    size_t consumed = 0;
    for (unsigned c = 2u; c < 16u; c++)
    {
        size_t n = v2_head(hdr, (uint8_t)(0x20u | c), 0x00, 0);
        ProxyProtocol.parse_args.buf = hdr;
        ProxyProtocol.parse_args.len = n;
        ProxyProtocol.parse_args.out = &info;
        ProxyProtocol.parse_args.consumed = &consumed;
        ProxyProtocol.parse(proxy_protocol_work);
        TEST_ASSERT_FALSE(ProxyProtocol.ok);
    }
}

// sec 2.2, the 14th byte: the address family is one of "0x0 : AF_UNSPEC", "0x1 : AF_INET",
// "0x2 : AF_INET6", "0x3 : AF_UNIX" and the transport one of "0x0 : UNSPEC", "0x1 : STREAM",
// "0x2 : DGRAM"; of both lists: "other values are unspecified and must not be emitted in version 2
// of this protocol and must be rejected as invalid by receivers".
void test_v2_an_unassigned_address_family_is_rejected(void)
{
    uint8_t hdr[16];
    ProxyInfo info;
    size_t consumed = 0;
    for (unsigned f = 4u; f < 16u; f++)
    {
        size_t n = v2_head(hdr, 0x21, (uint8_t)((f << 4) | 0x1u), 0);
        ProxyProtocol.parse_args.buf = hdr;
        ProxyProtocol.parse_args.len = n;
        ProxyProtocol.parse_args.out = &info;
        ProxyProtocol.parse_args.consumed = &consumed;
        ProxyProtocol.parse(proxy_protocol_work);
        TEST_ASSERT_FALSE(ProxyProtocol.ok);
    }
    for (unsigned p = 3u; p < 16u; p++)
    {
        size_t n = v2_head(hdr, 0x21, (uint8_t)(0x10u | p), 0);
        ProxyProtocol.parse_args.buf = hdr;
        ProxyProtocol.parse_args.len = n;
        ProxyProtocol.parse_args.out = &info;
        ProxyProtocol.parse_args.consumed = &consumed;
        ProxyProtocol.parse(proxy_protocol_work);
        TEST_ASSERT_FALSE(ProxyProtocol.ok);
    }
}

// sec 2: "The receiver MUST NOT start processing the connection before it receives a complete and
// valid PROXY protocol header." A v1 line without its CRLF, a v2 signature without its ver_cmd /
// fam / length, and a v2 header whose announced 12-octet block is absent are all incomplete.
void test_partial_headers_are_refused(void)
{
    ProxyInfo info;
    size_t consumed = 0;

    const char *v1 = "PROXY TCP4 192.168.0.1 192.168.0.11 56324 443";
    ProxyProtocol.parse_args.buf = (const uint8_t *)v1;
    ProxyProtocol.parse_args.len = strlen(v1);
    ProxyProtocol.parse_args.out = &info;
    ProxyProtocol.parse_args.consumed = &consumed;
    ProxyProtocol.parse(proxy_protocol_work);
    TEST_ASSERT_FALSE(ProxyProtocol.ok);

    uint8_t hdr[28];
    memset(hdr, 0, sizeof(hdr));
    (void)v2_head(hdr, 0x21, 0x11, 12);
    for (size_t len = 12; len < 28; len++)
    {
        ProxyProtocol.parse_args.buf = hdr;
        ProxyProtocol.parse_args.len = len;
        ProxyProtocol.parse_args.out = &info;
        ProxyProtocol.parse_args.consumed = &consumed;
        ProxyProtocol.parse(proxy_protocol_work);
        TEST_ASSERT_FALSE(ProxyProtocol.ok);
    }
    memset(hdr + 16, 0, 12);
    ProxyProtocol.parse_args.buf = hdr;
    ProxyProtocol.parse_args.len = 28u;
    ProxyProtocol.parse_args.out = &info;
    ProxyProtocol.parse_args.consumed = &consumed;
    ProxyProtocol.parse(proxy_protocol_work);
    TEST_ASSERT_TRUE(ProxyProtocol.ok);
}

// sec 2: both formats "were designed to ensure that the header cannot be confused with common
// higher level protocols such as HTTP", so a stream that carries neither prefix is not a header.
void test_a_stream_without_a_proxy_header_is_refused(void)
{
    ProxyInfo info;
    size_t consumed = 0;

    const char *http = "GET / HTTP/1.1\r\n";
    ProxyProtocol.parse_args.buf = (const uint8_t *)http;
    ProxyProtocol.parse_args.len = strlen(http);
    ProxyProtocol.parse_args.out = &info;
    ProxyProtocol.parse_args.consumed = &consumed;
    ProxyProtocol.parse(proxy_protocol_work);
    TEST_ASSERT_FALSE(ProxyProtocol.ok);

    const uint8_t tiny[2] = {0x00, 0x01};
    ProxyProtocol.parse_args.buf = tiny;
    ProxyProtocol.parse_args.len = sizeof(tiny);
    ProxyProtocol.parse_args.out = &info;
    ProxyProtocol.parse_args.consumed = &consumed;
    ProxyProtocol.parse(proxy_protocol_work);
    TEST_ASSERT_FALSE(ProxyProtocol.ok);
}

// A builder handed less room than the form needs writes nothing and reports nothing: no partial
// header can reach the wire, since a truncated one would be read as a different header.
void test_builders_fail_closed_on_a_short_buffer(void)
{
    char small[16];
    ProxyProtocol.v1_build_args.buf = small;
    ProxyProtocol.v1_build_args.cap = sizeof(small);
    ProxyProtocol.v1_build_args.src_addr = SRC;
    ProxyProtocol.v1_build_args.dst_addr = DST;
    ProxyProtocol.v1_build_args.src_port = 56324;
    ProxyProtocol.v1_build_args.dst_port = 443;
    ProxyProtocol.v1_build(proxy_protocol_work);
    TEST_ASSERT_EQUAL_size_t(0u, ProxyProtocol.n);
    uint8_t v2small[27];
    ProxyProtocol.v2_build_args.buf = v2small;
    ProxyProtocol.v2_build_args.cap = sizeof(v2small);
    ProxyProtocol.v2_build_args.src_addr = SRC;
    ProxyProtocol.v2_build_args.dst_addr = DST;
    ProxyProtocol.v2_build_args.src_port = 56324;
    ProxyProtocol.v2_build_args.dst_port = 443;
    ProxyProtocol.v2_build(proxy_protocol_work);
    TEST_ASSERT_EQUAL_size_t(0u, ProxyProtocol.n);
}

// A null pointer is reported, never written through.
void test_null_arguments_are_refused(void)
{
    ProxyInfo info;
    size_t consumed = 0;
    uint8_t any[16] = {0};
    ProxyProtocol.parse_args.buf = NULL;
    ProxyProtocol.parse_args.len = sizeof(any);
    ProxyProtocol.parse_args.out = &info;
    ProxyProtocol.parse_args.consumed = &consumed;
    ProxyProtocol.parse(proxy_protocol_work);
    TEST_ASSERT_FALSE(ProxyProtocol.ok);
    ProxyProtocol.parse_args.buf = any;
    ProxyProtocol.parse_args.len = sizeof(any);
    ProxyProtocol.parse_args.out = NULL;
    ProxyProtocol.parse_args.consumed = &consumed;
    ProxyProtocol.parse(proxy_protocol_work);
    TEST_ASSERT_FALSE(ProxyProtocol.ok);
    ProxyProtocol.parse_args.buf = any;
    ProxyProtocol.parse_args.len = sizeof(any);
    ProxyProtocol.parse_args.out = &info;
    ProxyProtocol.parse_args.consumed = NULL;
    ProxyProtocol.parse(proxy_protocol_work);
    TEST_ASSERT_FALSE(ProxyProtocol.ok);
    ProxyProtocol.v1_build_args.buf = NULL;
    ProxyProtocol.v1_build_args.cap = 64;
    ProxyProtocol.v1_build_args.src_addr = SRC;
    ProxyProtocol.v1_build_args.dst_addr = DST;
    ProxyProtocol.v1_build_args.src_port = 1;
    ProxyProtocol.v1_build_args.dst_port = 2;
    ProxyProtocol.v1_build(proxy_protocol_work);
    TEST_ASSERT_EQUAL_size_t(0u, ProxyProtocol.n);
    ProxyProtocol.v2_build_args.buf = NULL;
    ProxyProtocol.v2_build_args.cap = 64;
    ProxyProtocol.v2_build_args.src_addr = SRC;
    ProxyProtocol.v2_build_args.dst_addr = DST;
    ProxyProtocol.v2_build_args.src_port = 1;
    ProxyProtocol.v2_build_args.dst_port = 2;
    ProxyProtocol.v2_build(proxy_protocol_work);
    TEST_ASSERT_EQUAL_size_t(0u, ProxyProtocol.n);
}
