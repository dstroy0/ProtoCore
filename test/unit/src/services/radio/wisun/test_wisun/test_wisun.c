// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Wi-SUN FAN border-router connector (services/radio/wisun/wisun.h).
//
// The FAN mesh is reached as ordinary CoAP over IPv6, so RFC 7252 governs every octet this module
// emits: sec 3 the 4-octet header, sec 3.1 the option (delta, length) nibble header, sec 5.10 the
// Uri-Path option number 11, and sec 3 the 0xFF payload marker. test_rfc7252_figure_16_request is
// the load-bearing case: RFC 7252 Appendix A prints that exact GET, names its Message ID 0x7d34 and
// states it is 16 octets long, so reproducing it octet for octet settles the header packing, the
// option delta, and the total length in one assertion.

#include "services/radio/wisun/wisun.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static protocore_ip addr(const char *text)
{
    protocore_ip ip;
    Ip.args.text = text;
    Ip.args.out = &ip;
    Ip.parse(Ip.internal);
    TEST_ASSERT_TRUE_MESSAGE(Ip.ok, text);
    return ip;
}

// RFC 7252 Appendix A, Figure 16: a Confirmable GET for coap://server/temperature with Message ID
// 0x7d34 and an empty Token, "a total of 16 bytes long".
//
//   octet 0 = Ver(1) << 6 | T(CON = 0) << 4 | TKL(0)        = 0x40
//   octet 1 = Code GET = 0.01                               = 0x01
//   octets 2-3 = Message ID, network byte order             = 7D 34
//   octet 4 = option Delta(0 + 11) << 4 | Length(11)        = 0xBB
//   octets 5-15 = "temperature"
void test_rfc7252_figure_16_request(void)
{
    static const uint8_t WANT[16] = {0x40, 0x01, 0x7D, 0x34, 0xBB, 't', 'e', 'm',
                                     'p',  'e',  'r',  'a',  't',  'u', 'r', 'e'};
    uint8_t out[64];
    const size_t n = protocore_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_GET, 0x7d34, NULL, 0, "temperature", NULL, 0,
                                                out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(16, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, 16);
}

// A leading slash is path syntax, not a segment, so it changes nothing on the wire.
void test_leading_slash_is_not_a_segment(void)
{
    uint8_t with[64];
    uint8_t without[64];
    const size_t a = protocore_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_GET, 0x7d34, NULL, 0, "/temperature", NULL,
                                                0, with, sizeof(with));
    const size_t b = protocore_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_GET, 0x7d34, NULL, 0, "temperature", NULL, 0,
                                                without, sizeof(without));
    TEST_ASSERT_EQUAL_size_t(b, a);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(without, with, a);
}

// RFC 7252 sec 3: the Type field is 2 bits at B4-B5 of octet 0, so Non-confirmable is 0x50 where
// Confirmable is 0x40.
void test_type_field_selects_confirmable_or_not(void)
{
    uint8_t out[32];
    TEST_ASSERT_EQUAL_size_t(
        4, protocore_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_GET, 1, NULL, 0, NULL, NULL, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x40, out[0]);
    TEST_ASSERT_EQUAL_size_t(
        4, protocore_wisun_build_coap(WISUN_COAP_NON, WISUN_COAP_GET, 1, NULL, 0, NULL, NULL, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x50, out[0]);

    // The method code is the second octet: 0.01 GET is 1, 0.03 PUT is 3.
    TEST_ASSERT_EQUAL_HEX8(0x01, WISUN_COAP_GET);
    TEST_ASSERT_EQUAL_HEX8(0x03, WISUN_COAP_PUT);
    TEST_ASSERT_EQUAL_size_t(4, protocore_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_PUT, 0xABCD, NULL, 0, NULL, NULL,
                                                           0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x03, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0xAB, out[2]); // Message ID is network byte order
    TEST_ASSERT_EQUAL_HEX8(0xCD, out[3]);
}

// RFC 7252 sec 3: TKL is the low nibble of octet 0 and the Token follows the 4-octet header.
void test_token_length_and_placement(void)
{
    static const uint8_t TOKEN[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t out[64];
    const size_t n = protocore_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_GET, 0x0001, TOKEN, sizeof(TOKEN), "temp",
                                                NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(4 + 4 + 1 + 4, n); // header + token + option header + "temp"
    TEST_ASSERT_EQUAL_HEX8(0x44, out[0]);       // Ver 1, CON, TKL 4
    TEST_ASSERT_EQUAL_HEX8_ARRAY(TOKEN, out + 4, 4);
    TEST_ASSERT_EQUAL_HEX8(0xB4, out[8]); // delta 11, length 4
    TEST_ASSERT_EQUAL_MEMORY("temp", out + 9, 4);

    // TKL 9-15 are reserved and MUST NOT be sent.
    for (uint8_t tkl = 9; tkl < 16; tkl++)
    {
        uint8_t big[16] = {0};
        TEST_ASSERT_EQUAL_size_t(
            0, protocore_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_GET, 1, big, tkl, "a", NULL, 0, out, sizeof(out)));
    }
    // A non-zero length with no token is refused rather than read from nothing.
    TEST_ASSERT_EQUAL_size_t(
        0, protocore_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_GET, 1, NULL, 4, "a", NULL, 0, out, sizeof(out)));
}

// RFC 7252 sec 5.10.1: each path segment is its own Uri-Path option. The first carries delta 11
// from option number 0; every later one repeats option 11, so its delta is 0.
void test_each_path_segment_is_its_own_option(void)
{
    static const uint8_t WANT[16] = {0x40, 0x01, 0x00, 0x2A,                     // header, MID 42
                                     0xB7, 's',  'e',  'n',  's', 'o', 'r', 's', // delta 11, len 7
                                     0x04, 't',  'e',  'm'};                     // delta 0, len 4 (cut below)
    uint8_t out[64];
    const size_t n = protocore_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_GET, 42, NULL, 0, "sensors/temp", NULL, 0,
                                                out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(4 + 8 + 5, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, 16);
    TEST_ASSERT_EQUAL_HEX8('p', out[16]);

    // Three segments: one option each, the last two at delta 0.
    const size_t m =
        protocore_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_GET, 42, NULL, 0, "a/b/c", NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(4 + 2 + 2 + 2, m);
    TEST_ASSERT_EQUAL_HEX8(0xB1, out[4]);
    TEST_ASSERT_EQUAL_HEX8('a', out[5]);
    TEST_ASSERT_EQUAL_HEX8(0x01, out[6]);
    TEST_ASSERT_EQUAL_HEX8('b', out[7]);
    TEST_ASSERT_EQUAL_HEX8(0x01, out[8]);
    TEST_ASSERT_EQUAL_HEX8('c', out[9]);
}

// RFC 7252 sec 3.1: a length of 13-268 sets the nibble to 13 and follows it with one extension
// octet holding (length - 13).
void test_option_length_extension(void)
{
    // A 12-octet segment is the widest that still fits the nibble.
    static const char SEG12[] = "abcdefghijkl";
    uint8_t out[512];
    size_t n = protocore_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_GET, 1, NULL, 0, SEG12, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(4 + 1 + 12, n);
    TEST_ASSERT_EQUAL_HEX8(0xBC, out[4]); // delta 11, length 12, no extension

    // 13 is the first length that needs the extension, and its extension octet is 13 - 13 = 0.
    static const char SEG13[] = "abcdefghijklm";
    n = protocore_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_GET, 1, NULL, 0, SEG13, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(4 + 2 + 13, n);
    TEST_ASSERT_EQUAL_HEX8(0xBD, out[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[5]);
    TEST_ASSERT_EQUAL_MEMORY(SEG13, out + 6, 13);

    // 20 octets: nibble 13, extension 20 - 13 = 7.
    static const char SEG20[] = "abcdefghijklmnopqrst";
    n = protocore_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_GET, 1, NULL, 0, SEG20, NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(4 + 2 + 20, n);
    TEST_ASSERT_EQUAL_HEX8(0xBD, out[4]);
    TEST_ASSERT_EQUAL_HEX8(0x07, out[5]);
}

// RFC 7252 sec 3: a non-empty payload is prefixed by the one-octet Payload Marker 0xFF, and no
// marker is emitted when there is no payload.
void test_payload_marker(void)
{
    static const uint8_t BODY[6] = {'2', '2', '.', '3', ' ', 'C'};
    uint8_t out[64];
    const size_t n = protocore_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_PUT, 0x7d34, NULL, 0, "temp", BODY,
                                                sizeof(BODY), out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(4 + 5 + 1 + 6, n);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[9]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(BODY, out + 10, 6);

    const size_t m =
        protocore_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_PUT, 0x7d34, NULL, 0, "temp", NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(9, m); // header + option, no marker
    TEST_ASSERT_EQUAL_size_t(
        0, protocore_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_PUT, 1, NULL, 0, "a", NULL, 4, out, sizeof(out)));
}

// A PDU that will not fit is refused whole; the builder never emits a half-formed message.
void test_build_refuses_a_short_buffer(void)
{
    uint8_t out[64];
    const size_t exact = protocore_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_GET, 0x7d34, NULL, 0, "temperature",
                                                    NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(16, exact);
    for (size_t cap = 0; cap < exact; cap++)
    {
        TEST_ASSERT_EQUAL_size_t(0, protocore_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_GET, 0x7d34, NULL, 0,
                                                               "temperature", NULL, 0, out, cap));
    }
    TEST_ASSERT_EQUAL_size_t(exact, protocore_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_GET, 0x7d34, NULL, 0,
                                                               "temperature", NULL, 0, out, exact));
    TEST_ASSERT_EQUAL_size_t(
        0, protocore_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_GET, 1, NULL, 0, "a", NULL, 0, NULL, 32));
}

// Registering a node twice keeps one entry and refreshes it, and the table refuses to grow past
// the storage the caller owns.
void test_node_registry(void)
{
    WisunNode storage[3];
    WisunFan fan;
    const protocore_ip br = addr("fd00::1");
    protocore_wisun_init(&fan, &br, storage, 3);
    TEST_ASSERT_EQUAL_size_t(0, fan.count);
    TEST_ASSERT_EQUAL_size_t(3, fan.cap);
    TEST_ASSERT_EQUAL_size_t(0, protocore_wisun_joined_count(&fan));

    const protocore_ip a = addr("fd00::a");
    const protocore_ip b = addr("fd00::b");
    const protocore_ip c = addr("fd00::c");
    const protocore_ip d = addr("fd00::d");

    TEST_ASSERT_EQUAL_INT(0, protocore_wisun_node_register(&fan, &a, 100));
    TEST_ASSERT_EQUAL_INT(1, protocore_wisun_node_register(&fan, &b, 200));
    TEST_ASSERT_EQUAL_size_t(2, fan.count);

    // The same address again is the same entry, with a fresh last_seen.
    TEST_ASSERT_EQUAL_INT(0, protocore_wisun_node_register(&fan, &a, 300));
    TEST_ASSERT_EQUAL_size_t(2, fan.count);
    TEST_ASSERT_EQUAL_UINT32(300, storage[0].last_seen);
    TEST_ASSERT_EQUAL_UINT32(200, storage[1].last_seen);

    size_t idx = 99;
    TEST_ASSERT_TRUE(protocore_wisun_node_find(&fan, &b, &idx));
    TEST_ASSERT_EQUAL_size_t(1, idx);
    TEST_ASSERT_FALSE(protocore_wisun_node_find(&fan, &c, &idx));
    TEST_ASSERT_EQUAL_size_t(1, idx); // untouched on a miss

    TEST_ASSERT_EQUAL_INT(2, protocore_wisun_node_register(&fan, &c, 400));
    TEST_ASSERT_EQUAL_size_t(3, protocore_wisun_joined_count(&fan));
    TEST_ASSERT_EQUAL_INT(-1, protocore_wisun_node_register(&fan, &d, 500)); // full
    TEST_ASSERT_EQUAL_size_t(3, fan.count);

    // The border router is kept as given, and it is not itself a mesh node.
    Ip.args.ip = &fan.border_router;
    Ip.args.b = &br;
    Ip.equal(Ip.internal);
    TEST_ASSERT_TRUE(Ip.ok);
    TEST_ASSERT_FALSE(protocore_wisun_node_find(&fan, &br, NULL));
}

// A connector with no storage holds nothing and refuses every registration.
void test_registry_without_storage(void)
{
    WisunFan fan;
    const protocore_ip br = addr("fd00::1");
    protocore_wisun_init(&fan, &br, NULL, 8);
    TEST_ASSERT_EQUAL_size_t(0, fan.cap);

    const protocore_ip a = addr("fd00::a");
    TEST_ASSERT_EQUAL_INT(-1, protocore_wisun_node_register(&fan, &a, 1));
    TEST_ASSERT_FALSE(protocore_wisun_node_find(&fan, &a, NULL));
    TEST_ASSERT_EQUAL_size_t(0, protocore_wisun_joined_count(&fan));

    // A null border router zeroes the field rather than leaving whatever was on the stack.
    WisunNode storage[1];
    protocore_wisun_init(&fan, NULL, storage, 1);
    Ip.args.ip = &fan.border_router;
    Ip.is_unspecified(Ip.internal);
    TEST_ASSERT_TRUE(Ip.ok);

    protocore_wisun_init(NULL, &br, storage, 1); // no state to write, no crash
    TEST_ASSERT_EQUAL_INT(-1, protocore_wisun_node_register(&fan, NULL, 1));
    TEST_ASSERT_EQUAL_INT(-1, protocore_wisun_node_register(NULL, &a, 1));
    TEST_ASSERT_FALSE(protocore_wisun_node_find(NULL, &a, NULL));
    TEST_ASSERT_EQUAL_size_t(0, protocore_wisun_joined_count(NULL));
}

// The node table serializes as JSON with each address in its RFC 5952 canonical text.
void test_nodes_json(void)
{
    WisunNode storage[2];
    WisunFan fan;
    const protocore_ip br = addr("fd00::1");
    protocore_wisun_init(&fan, &br, storage, 2);

    char out[128];
    TEST_ASSERT_EQUAL_size_t(2, protocore_wisun_nodes_json(&fan, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("[]", out);

    const protocore_ip a = addr("fd00:0000:0000:0000:0000:0000:0000:000a"); // canonicalizes to fd00::a
    const protocore_ip b = addr("2001:DB8::1");                             // and to lowercase
    TEST_ASSERT_EQUAL_INT(0, protocore_wisun_node_register(&fan, &a, 1));
    TEST_ASSERT_EQUAL_INT(1, protocore_wisun_node_register(&fan, &b, 2));

    const size_t n = protocore_wisun_nodes_json(&fan, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("[{\"addr\":\"fd00::a\",\"joined\":true},{\"addr\":\"2001:db8::1\",\"joined\":true}]",
                             out);
    TEST_ASSERT_EQUAL_size_t(strlen(out), n);

    // A buffer that cannot hold the whole document reports 0 rather than truncated JSON.
    for (size_t cap = 1; cap <= n; cap++)
    {
        char small[128];
        memset(small, 'x', sizeof(small));
        TEST_ASSERT_EQUAL_size_t(0, protocore_wisun_nodes_json(&fan, small, cap));
    }
    TEST_ASSERT_EQUAL_size_t(0, protocore_wisun_nodes_json(&fan, out, 0));
    TEST_ASSERT_EQUAL_size_t(0, protocore_wisun_nodes_json(NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, protocore_wisun_nodes_json(&fan, NULL, sizeof(out)));
}
