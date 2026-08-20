// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

static uint8_t ip_work[16]; // the borrow an entry takes; Ip never reads it

static uint8_t wisun_work[16]; // the borrow an entry takes; Wisun never reads it

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
    Ip.parse(ip_work);
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
    Wisun.build_coap_args.type = WISUN_COAP_CON;
    Wisun.build_coap_args.code = WISUN_COAP_GET;
    Wisun.build_coap_args.msg_id = 0x7d34;
    Wisun.build_coap_args.token = NULL;
    Wisun.build_coap_args.tkl = 0;
    Wisun.build_coap_args.uri_path = "temperature";
    Wisun.build_coap_args.payload = NULL;
    Wisun.build_coap_args.plen = 0;
    Wisun.build_coap_args.out = out;
    Wisun.build_coap_args.cap = sizeof(out);
    Wisun.build_coap(wisun_work);
    const size_t n = Wisun.n;
    TEST_ASSERT_EQUAL_size_t(16, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, 16);
}

// A leading slash is path syntax, not a segment, so it changes nothing on the wire.
void test_leading_slash_is_not_a_segment(void)
{
    uint8_t with[64];
    uint8_t without[64];
    Wisun.build_coap_args.type = WISUN_COAP_CON;
    Wisun.build_coap_args.code = WISUN_COAP_GET;
    Wisun.build_coap_args.msg_id = 0x7d34;
    Wisun.build_coap_args.token = NULL;
    Wisun.build_coap_args.tkl = 0;
    Wisun.build_coap_args.uri_path = "/temperature";
    Wisun.build_coap_args.payload = NULL;
    Wisun.build_coap_args.plen = 0;
    Wisun.build_coap_args.out = with;
    Wisun.build_coap_args.cap = sizeof(with);
    Wisun.build_coap(wisun_work);
    const size_t a = Wisun.n;
    Wisun.build_coap_args.type = WISUN_COAP_CON;
    Wisun.build_coap_args.code = WISUN_COAP_GET;
    Wisun.build_coap_args.msg_id = 0x7d34;
    Wisun.build_coap_args.token = NULL;
    Wisun.build_coap_args.tkl = 0;
    Wisun.build_coap_args.uri_path = "temperature";
    Wisun.build_coap_args.payload = NULL;
    Wisun.build_coap_args.plen = 0;
    Wisun.build_coap_args.out = without;
    Wisun.build_coap_args.cap = sizeof(without);
    Wisun.build_coap(wisun_work);
    const size_t b = Wisun.n;
    TEST_ASSERT_EQUAL_size_t(b, a);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(without, with, a);
}

// RFC 7252 sec 3: the Type field is 2 bits at B4-B5 of octet 0, so Non-confirmable is 0x50 where
// Confirmable is 0x40.
void test_type_field_selects_confirmable_or_not(void)
{
    uint8_t out[32];
    Wisun.build_coap_args.type = WISUN_COAP_CON;
    Wisun.build_coap_args.code = WISUN_COAP_GET;
    Wisun.build_coap_args.msg_id = 1;
    Wisun.build_coap_args.token = NULL;
    Wisun.build_coap_args.tkl = 0;
    Wisun.build_coap_args.uri_path = NULL;
    Wisun.build_coap_args.payload = NULL;
    Wisun.build_coap_args.plen = 0;
    Wisun.build_coap_args.out = out;
    Wisun.build_coap_args.cap = sizeof(out);
    Wisun.build_coap(wisun_work);
    TEST_ASSERT_EQUAL_size_t(4, Wisun.n);
    TEST_ASSERT_EQUAL_HEX8(0x40, out[0]);
    Wisun.build_coap_args.type = WISUN_COAP_NON;
    Wisun.build_coap_args.code = WISUN_COAP_GET;
    Wisun.build_coap_args.msg_id = 1;
    Wisun.build_coap_args.token = NULL;
    Wisun.build_coap_args.tkl = 0;
    Wisun.build_coap_args.uri_path = NULL;
    Wisun.build_coap_args.payload = NULL;
    Wisun.build_coap_args.plen = 0;
    Wisun.build_coap_args.out = out;
    Wisun.build_coap_args.cap = sizeof(out);
    Wisun.build_coap(wisun_work);
    TEST_ASSERT_EQUAL_size_t(4, Wisun.n);
    TEST_ASSERT_EQUAL_HEX8(0x50, out[0]);

    // The method code is the second octet: 0.01 GET is 1, 0.03 PUT is 3.
    TEST_ASSERT_EQUAL_HEX8(0x01, WISUN_COAP_GET);
    TEST_ASSERT_EQUAL_HEX8(0x03, WISUN_COAP_PUT);
    Wisun.build_coap_args.type = WISUN_COAP_CON;
    Wisun.build_coap_args.code = WISUN_COAP_PUT;
    Wisun.build_coap_args.msg_id = 0xABCD;
    Wisun.build_coap_args.token = NULL;
    Wisun.build_coap_args.tkl = 0;
    Wisun.build_coap_args.uri_path = NULL;
    Wisun.build_coap_args.payload = NULL;
    Wisun.build_coap_args.plen = 0;
    Wisun.build_coap_args.out = out;
    Wisun.build_coap_args.cap = sizeof(out);
    Wisun.build_coap(wisun_work);
    TEST_ASSERT_EQUAL_size_t(4, Wisun.n);
    TEST_ASSERT_EQUAL_HEX8(0x03, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0xAB, out[2]); // Message ID is network byte order
    TEST_ASSERT_EQUAL_HEX8(0xCD, out[3]);
}

// RFC 7252 sec 3: TKL is the low nibble of octet 0 and the Token follows the 4-octet header.
void test_token_length_and_placement(void)
{
    static const uint8_t TOKEN[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t out[64];
    Wisun.build_coap_args.type = WISUN_COAP_CON;
    Wisun.build_coap_args.code = WISUN_COAP_GET;
    Wisun.build_coap_args.msg_id = 0x0001;
    Wisun.build_coap_args.token = TOKEN;
    Wisun.build_coap_args.tkl = sizeof(TOKEN);
    Wisun.build_coap_args.uri_path = "temp";
    Wisun.build_coap_args.payload = NULL;
    Wisun.build_coap_args.plen = 0;
    Wisun.build_coap_args.out = out;
    Wisun.build_coap_args.cap = sizeof(out);
    Wisun.build_coap(wisun_work);
    const size_t n = Wisun.n;
    TEST_ASSERT_EQUAL_size_t(4 + 4 + 1 + 4, n); // header + token + option header + "temp"
    TEST_ASSERT_EQUAL_HEX8(0x44, out[0]);       // Ver 1, CON, TKL 4
    TEST_ASSERT_EQUAL_HEX8_ARRAY(TOKEN, out + 4, 4);
    TEST_ASSERT_EQUAL_HEX8(0xB4, out[8]); // delta 11, length 4
    TEST_ASSERT_EQUAL_MEMORY("temp", out + 9, 4);

    // TKL 9-15 are reserved and MUST NOT be sent.
    for (uint8_t tkl = 9; tkl < 16; tkl++)
    {
        uint8_t big[16] = {0};
        Wisun.build_coap_args.type = WISUN_COAP_CON;
        Wisun.build_coap_args.code = WISUN_COAP_GET;
        Wisun.build_coap_args.msg_id = 1;
        Wisun.build_coap_args.token = big;
        Wisun.build_coap_args.tkl = tkl;
        Wisun.build_coap_args.uri_path = "a";
        Wisun.build_coap_args.payload = NULL;
        Wisun.build_coap_args.plen = 0;
        Wisun.build_coap_args.out = out;
        Wisun.build_coap_args.cap = sizeof(out);
        Wisun.build_coap(wisun_work);
        TEST_ASSERT_EQUAL_size_t(0, Wisun.n);
    }
    // A non-zero length with no token is refused rather than read from nothing.
    Wisun.build_coap_args.type = WISUN_COAP_CON;
    Wisun.build_coap_args.code = WISUN_COAP_GET;
    Wisun.build_coap_args.msg_id = 1;
    Wisun.build_coap_args.token = NULL;
    Wisun.build_coap_args.tkl = 4;
    Wisun.build_coap_args.uri_path = "a";
    Wisun.build_coap_args.payload = NULL;
    Wisun.build_coap_args.plen = 0;
    Wisun.build_coap_args.out = out;
    Wisun.build_coap_args.cap = sizeof(out);
    Wisun.build_coap(wisun_work);
    TEST_ASSERT_EQUAL_size_t(0, Wisun.n);
}

// RFC 7252 sec 5.10.1: each path segment is its own Uri-Path option. The first carries delta 11
// from option number 0; every later one repeats option 11, so its delta is 0.
void test_each_path_segment_is_its_own_option(void)
{
    static const uint8_t WANT[16] = {0x40, 0x01, 0x00, 0x2A,                     // header, MID 42
                                     0xB7, 's',  'e',  'n',  's', 'o', 'r', 's', // delta 11, len 7
                                     0x04, 't',  'e',  'm'};                     // delta 0, len 4 (cut below)
    uint8_t out[64];
    Wisun.build_coap_args.type = WISUN_COAP_CON;
    Wisun.build_coap_args.code = WISUN_COAP_GET;
    Wisun.build_coap_args.msg_id = 42;
    Wisun.build_coap_args.token = NULL;
    Wisun.build_coap_args.tkl = 0;
    Wisun.build_coap_args.uri_path = "sensors/temp";
    Wisun.build_coap_args.payload = NULL;
    Wisun.build_coap_args.plen = 0;
    Wisun.build_coap_args.out = out;
    Wisun.build_coap_args.cap = sizeof(out);
    Wisun.build_coap(wisun_work);
    const size_t n = Wisun.n;
    TEST_ASSERT_EQUAL_size_t(4 + 8 + 5, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, 16);
    TEST_ASSERT_EQUAL_HEX8('p', out[16]);

    // Three segments: one option each, the last two at delta 0.
    Wisun.build_coap_args.type = WISUN_COAP_CON;
    Wisun.build_coap_args.code = WISUN_COAP_GET;
    Wisun.build_coap_args.msg_id = 42;
    Wisun.build_coap_args.token = NULL;
    Wisun.build_coap_args.tkl = 0;
    Wisun.build_coap_args.uri_path = "a/b/c";
    Wisun.build_coap_args.payload = NULL;
    Wisun.build_coap_args.plen = 0;
    Wisun.build_coap_args.out = out;
    Wisun.build_coap_args.cap = sizeof(out);
    Wisun.build_coap(wisun_work);
    const size_t m = Wisun.n;
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
    Wisun.build_coap_args.type = WISUN_COAP_CON;
    Wisun.build_coap_args.code = WISUN_COAP_GET;
    Wisun.build_coap_args.msg_id = 1;
    Wisun.build_coap_args.token = NULL;
    Wisun.build_coap_args.tkl = 0;
    Wisun.build_coap_args.uri_path = SEG12;
    Wisun.build_coap_args.payload = NULL;
    Wisun.build_coap_args.plen = 0;
    Wisun.build_coap_args.out = out;
    Wisun.build_coap_args.cap = sizeof(out);
    Wisun.build_coap(wisun_work);
    size_t n = Wisun.n;
    TEST_ASSERT_EQUAL_size_t(4 + 1 + 12, n);
    TEST_ASSERT_EQUAL_HEX8(0xBC, out[4]); // delta 11, length 12, no extension

    // 13 is the first length that needs the extension, and its extension octet is 13 - 13 = 0.
    static const char SEG13[] = "abcdefghijklm";
    Wisun.build_coap_args.type = WISUN_COAP_CON;
    Wisun.build_coap_args.code = WISUN_COAP_GET;
    Wisun.build_coap_args.msg_id = 1;
    Wisun.build_coap_args.token = NULL;
    Wisun.build_coap_args.tkl = 0;
    Wisun.build_coap_args.uri_path = SEG13;
    Wisun.build_coap_args.payload = NULL;
    Wisun.build_coap_args.plen = 0;
    Wisun.build_coap_args.out = out;
    Wisun.build_coap_args.cap = sizeof(out);
    Wisun.build_coap(wisun_work);
    n = Wisun.n;
    TEST_ASSERT_EQUAL_size_t(4 + 2 + 13, n);
    TEST_ASSERT_EQUAL_HEX8(0xBD, out[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[5]);
    TEST_ASSERT_EQUAL_MEMORY(SEG13, out + 6, 13);

    // 20 octets: nibble 13, extension 20 - 13 = 7.
    static const char SEG20[] = "abcdefghijklmnopqrst";
    Wisun.build_coap_args.type = WISUN_COAP_CON;
    Wisun.build_coap_args.code = WISUN_COAP_GET;
    Wisun.build_coap_args.msg_id = 1;
    Wisun.build_coap_args.token = NULL;
    Wisun.build_coap_args.tkl = 0;
    Wisun.build_coap_args.uri_path = SEG20;
    Wisun.build_coap_args.payload = NULL;
    Wisun.build_coap_args.plen = 0;
    Wisun.build_coap_args.out = out;
    Wisun.build_coap_args.cap = sizeof(out);
    Wisun.build_coap(wisun_work);
    n = Wisun.n;
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
    Wisun.build_coap_args.type = WISUN_COAP_CON;
    Wisun.build_coap_args.code = WISUN_COAP_PUT;
    Wisun.build_coap_args.msg_id = 0x7d34;
    Wisun.build_coap_args.token = NULL;
    Wisun.build_coap_args.tkl = 0;
    Wisun.build_coap_args.uri_path = "temp";
    Wisun.build_coap_args.payload = BODY;
    Wisun.build_coap_args.plen = sizeof(BODY);
    Wisun.build_coap_args.out = out;
    Wisun.build_coap_args.cap = sizeof(out);
    Wisun.build_coap(wisun_work);
    const size_t n = Wisun.n;
    TEST_ASSERT_EQUAL_size_t(4 + 5 + 1 + 6, n);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[9]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(BODY, out + 10, 6);

    Wisun.build_coap_args.type = WISUN_COAP_CON;
    Wisun.build_coap_args.code = WISUN_COAP_PUT;
    Wisun.build_coap_args.msg_id = 0x7d34;
    Wisun.build_coap_args.token = NULL;
    Wisun.build_coap_args.tkl = 0;
    Wisun.build_coap_args.uri_path = "temp";
    Wisun.build_coap_args.payload = NULL;
    Wisun.build_coap_args.plen = 0;
    Wisun.build_coap_args.out = out;
    Wisun.build_coap_args.cap = sizeof(out);
    Wisun.build_coap(wisun_work);
    const size_t m = Wisun.n;
    TEST_ASSERT_EQUAL_size_t(9, m); // header + option, no marker
    Wisun.build_coap_args.type = WISUN_COAP_CON;
    Wisun.build_coap_args.code = WISUN_COAP_PUT;
    Wisun.build_coap_args.msg_id = 1;
    Wisun.build_coap_args.token = NULL;
    Wisun.build_coap_args.tkl = 0;
    Wisun.build_coap_args.uri_path = "a";
    Wisun.build_coap_args.payload = NULL;
    Wisun.build_coap_args.plen = 4;
    Wisun.build_coap_args.out = out;
    Wisun.build_coap_args.cap = sizeof(out);
    Wisun.build_coap(wisun_work);
    TEST_ASSERT_EQUAL_size_t(0, Wisun.n);
}

// A PDU that will not fit is refused whole; the builder never emits a half-formed message.
void test_build_refuses_a_short_buffer(void)
{
    uint8_t out[64];
    Wisun.build_coap_args.type = WISUN_COAP_CON;
    Wisun.build_coap_args.code = WISUN_COAP_GET;
    Wisun.build_coap_args.msg_id = 0x7d34;
    Wisun.build_coap_args.token = NULL;
    Wisun.build_coap_args.tkl = 0;
    Wisun.build_coap_args.uri_path = "temperature";
    Wisun.build_coap_args.payload = NULL;
    Wisun.build_coap_args.plen = 0;
    Wisun.build_coap_args.out = out;
    Wisun.build_coap_args.cap = sizeof(out);
    Wisun.build_coap(wisun_work);
    const size_t exact = Wisun.n;
    TEST_ASSERT_EQUAL_size_t(16, exact);
    for (size_t cap = 0; cap < exact; cap++)
    {
        Wisun.build_coap_args.type = WISUN_COAP_CON;
        Wisun.build_coap_args.code = WISUN_COAP_GET;
        Wisun.build_coap_args.msg_id = 0x7d34;
        Wisun.build_coap_args.token = NULL;
        Wisun.build_coap_args.tkl = 0;
        Wisun.build_coap_args.uri_path = "temperature";
        Wisun.build_coap_args.payload = NULL;
        Wisun.build_coap_args.plen = 0;
        Wisun.build_coap_args.out = out;
        Wisun.build_coap_args.cap = cap;
        Wisun.build_coap(wisun_work);
        TEST_ASSERT_EQUAL_size_t(0, Wisun.n);
    }
    Wisun.build_coap_args.type = WISUN_COAP_CON;
    Wisun.build_coap_args.code = WISUN_COAP_GET;
    Wisun.build_coap_args.msg_id = 0x7d34;
    Wisun.build_coap_args.token = NULL;
    Wisun.build_coap_args.tkl = 0;
    Wisun.build_coap_args.uri_path = "temperature";
    Wisun.build_coap_args.payload = NULL;
    Wisun.build_coap_args.plen = 0;
    Wisun.build_coap_args.out = out;
    Wisun.build_coap_args.cap = exact;
    Wisun.build_coap(wisun_work);
    TEST_ASSERT_EQUAL_size_t(exact, Wisun.n);
    Wisun.build_coap_args.type = WISUN_COAP_CON;
    Wisun.build_coap_args.code = WISUN_COAP_GET;
    Wisun.build_coap_args.msg_id = 1;
    Wisun.build_coap_args.token = NULL;
    Wisun.build_coap_args.tkl = 0;
    Wisun.build_coap_args.uri_path = "a";
    Wisun.build_coap_args.payload = NULL;
    Wisun.build_coap_args.plen = 0;
    Wisun.build_coap_args.out = NULL;
    Wisun.build_coap_args.cap = 32;
    Wisun.build_coap(wisun_work);
    TEST_ASSERT_EQUAL_size_t(0, Wisun.n);
}

// Registering a node twice keeps one entry and refreshes it, and the table refuses to grow past
// the storage the caller owns.
void test_node_registry(void)
{
    WisunNode storage[3];
    WisunFan fan;
    const protocore_ip br = addr("fd00::1");
    Wisun.init_args.fan = &fan;
    Wisun.init_args.border_router = &br;
    Wisun.init_args.storage = storage;
    Wisun.init_args.cap = 3;
    Wisun.init(wisun_work);
    TEST_ASSERT_EQUAL_size_t(0, fan.count);
    TEST_ASSERT_EQUAL_size_t(3, fan.cap);
    Wisun.joined_count_args.fan = &fan;
    Wisun.joined_count(wisun_work);
    TEST_ASSERT_EQUAL_size_t(0, Wisun.n);

    const protocore_ip a = addr("fd00::a");
    const protocore_ip b = addr("fd00::b");
    const protocore_ip c = addr("fd00::c");
    const protocore_ip d = addr("fd00::d");

    Wisun.node_register_args.fan = &fan;
    Wisun.node_register_args.addr = &a;
    Wisun.node_register_args.now = 100;
    Wisun.node_register(wisun_work);
    TEST_ASSERT_EQUAL_INT(0, Wisun.i32);
    Wisun.node_register_args.fan = &fan;
    Wisun.node_register_args.addr = &b;
    Wisun.node_register_args.now = 200;
    Wisun.node_register(wisun_work);
    TEST_ASSERT_EQUAL_INT(1, Wisun.i32);
    TEST_ASSERT_EQUAL_size_t(2, fan.count);

    // The same address again is the same entry, with a fresh last_seen.
    Wisun.node_register_args.fan = &fan;
    Wisun.node_register_args.addr = &a;
    Wisun.node_register_args.now = 300;
    Wisun.node_register(wisun_work);
    TEST_ASSERT_EQUAL_INT(0, Wisun.i32);
    TEST_ASSERT_EQUAL_size_t(2, fan.count);
    TEST_ASSERT_EQUAL_UINT32(300, storage[0].last_seen);
    TEST_ASSERT_EQUAL_UINT32(200, storage[1].last_seen);

    size_t idx = 99;
    Wisun.node_find_args.fan = &fan;
    Wisun.node_find_args.addr = &b;
    Wisun.node_find_args.idx = &idx;
    Wisun.node_find(wisun_work);
    TEST_ASSERT_TRUE(Wisun.ok);
    TEST_ASSERT_EQUAL_size_t(1, idx);
    Wisun.node_find_args.fan = &fan;
    Wisun.node_find_args.addr = &c;
    Wisun.node_find_args.idx = &idx;
    Wisun.node_find(wisun_work);
    TEST_ASSERT_FALSE(Wisun.ok);
    TEST_ASSERT_EQUAL_size_t(1, idx); // untouched on a miss

    Wisun.node_register_args.fan = &fan;
    Wisun.node_register_args.addr = &c;
    Wisun.node_register_args.now = 400;
    Wisun.node_register(wisun_work);
    TEST_ASSERT_EQUAL_INT(2, Wisun.i32);
    Wisun.joined_count_args.fan = &fan;
    Wisun.joined_count(wisun_work);
    TEST_ASSERT_EQUAL_size_t(3, Wisun.n);
    Wisun.node_register_args.fan = &fan;
    Wisun.node_register_args.addr = &d;
    Wisun.node_register_args.now = 500;
    Wisun.node_register(wisun_work);
    TEST_ASSERT_EQUAL_INT(-1, Wisun.i32); // full
    TEST_ASSERT_EQUAL_size_t(3, fan.count);

    // The border router is kept as given, and it is not itself a mesh node.
    Ip.args.ip = &fan.border_router;
    Ip.args.b = &br;
    Ip.equal(ip_work);
    TEST_ASSERT_TRUE(Ip.ok);
    Wisun.node_find_args.fan = &fan;
    Wisun.node_find_args.addr = &br;
    Wisun.node_find_args.idx = NULL;
    Wisun.node_find(wisun_work);
    TEST_ASSERT_FALSE(Wisun.ok);
}

// A connector with no storage holds nothing and refuses every registration.
void test_registry_without_storage(void)
{
    WisunFan fan;
    const protocore_ip br = addr("fd00::1");
    Wisun.init_args.fan = &fan;
    Wisun.init_args.border_router = &br;
    Wisun.init_args.storage = NULL;
    Wisun.init_args.cap = 8;
    Wisun.init(wisun_work);
    TEST_ASSERT_EQUAL_size_t(0, fan.cap);

    const protocore_ip a = addr("fd00::a");
    Wisun.node_register_args.fan = &fan;
    Wisun.node_register_args.addr = &a;
    Wisun.node_register_args.now = 1;
    Wisun.node_register(wisun_work);
    TEST_ASSERT_EQUAL_INT(-1, Wisun.i32);
    Wisun.node_find_args.fan = &fan;
    Wisun.node_find_args.addr = &a;
    Wisun.node_find_args.idx = NULL;
    Wisun.node_find(wisun_work);
    TEST_ASSERT_FALSE(Wisun.ok);
    Wisun.joined_count_args.fan = &fan;
    Wisun.joined_count(wisun_work);
    TEST_ASSERT_EQUAL_size_t(0, Wisun.n);

    // A null border router zeroes the field rather than leaving whatever was on the stack.
    WisunNode storage[1];
    Wisun.init_args.fan = &fan;
    Wisun.init_args.border_router = NULL;
    Wisun.init_args.storage = storage;
    Wisun.init_args.cap = 1;
    Wisun.init(wisun_work);
    Ip.args.ip = &fan.border_router;
    Ip.is_unspecified(ip_work);
    TEST_ASSERT_TRUE(Ip.ok);

    Wisun.init_args.fan = NULL;
    Wisun.init_args.border_router = &br;
    Wisun.init_args.storage = storage;
    Wisun.init_args.cap = 1;
    Wisun.init(wisun_work); // no state to write, no crash
    Wisun.node_register_args.fan = &fan;
    Wisun.node_register_args.addr = NULL;
    Wisun.node_register_args.now = 1;
    Wisun.node_register(wisun_work);
    TEST_ASSERT_EQUAL_INT(-1, Wisun.i32);
    Wisun.node_register_args.fan = NULL;
    Wisun.node_register_args.addr = &a;
    Wisun.node_register_args.now = 1;
    Wisun.node_register(wisun_work);
    TEST_ASSERT_EQUAL_INT(-1, Wisun.i32);
    Wisun.node_find_args.fan = NULL;
    Wisun.node_find_args.addr = &a;
    Wisun.node_find_args.idx = NULL;
    Wisun.node_find(wisun_work);
    TEST_ASSERT_FALSE(Wisun.ok);
    Wisun.joined_count_args.fan = NULL;
    Wisun.joined_count(wisun_work);
    TEST_ASSERT_EQUAL_size_t(0, Wisun.n);
}

// The node table serializes as JSON with each address in its RFC 5952 canonical text.
void test_nodes_json(void)
{
    WisunNode storage[2];
    WisunFan fan;
    const protocore_ip br = addr("fd00::1");
    Wisun.init_args.fan = &fan;
    Wisun.init_args.border_router = &br;
    Wisun.init_args.storage = storage;
    Wisun.init_args.cap = 2;
    Wisun.init(wisun_work);

    char out[128];
    Wisun.nodes_json_args.fan = &fan;
    Wisun.nodes_json_args.out = out;
    Wisun.nodes_json_args.cap = sizeof(out);
    Wisun.nodes_json(wisun_work);
    TEST_ASSERT_EQUAL_size_t(2, Wisun.n);
    TEST_ASSERT_EQUAL_STRING("[]", out);

    const protocore_ip a = addr("fd00:0000:0000:0000:0000:0000:0000:000a"); // canonicalizes to fd00::a
    const protocore_ip b = addr("2001:DB8::1");                             // and to lowercase
    Wisun.node_register_args.fan = &fan;
    Wisun.node_register_args.addr = &a;
    Wisun.node_register_args.now = 1;
    Wisun.node_register(wisun_work);
    TEST_ASSERT_EQUAL_INT(0, Wisun.i32);
    Wisun.node_register_args.fan = &fan;
    Wisun.node_register_args.addr = &b;
    Wisun.node_register_args.now = 2;
    Wisun.node_register(wisun_work);
    TEST_ASSERT_EQUAL_INT(1, Wisun.i32);

    Wisun.nodes_json_args.fan = &fan;
    Wisun.nodes_json_args.out = out;
    Wisun.nodes_json_args.cap = sizeof(out);
    Wisun.nodes_json(wisun_work);
    const size_t n = Wisun.n;
    TEST_ASSERT_EQUAL_STRING("[{\"addr\":\"fd00::a\",\"joined\":true},{\"addr\":\"2001:db8::1\",\"joined\":true}]",
                             out);
    TEST_ASSERT_EQUAL_size_t(strlen(out), n);

    // A buffer that cannot hold the whole document reports 0 rather than truncated JSON.
    for (size_t cap = 1; cap <= n; cap++)
    {
        char small[128];
        memset(small, 'x', sizeof(small));
        Wisun.nodes_json_args.fan = &fan;
        Wisun.nodes_json_args.out = small;
        Wisun.nodes_json_args.cap = cap;
        Wisun.nodes_json(wisun_work);
        TEST_ASSERT_EQUAL_size_t(0, Wisun.n);
    }
    Wisun.nodes_json_args.fan = &fan;
    Wisun.nodes_json_args.out = out;
    Wisun.nodes_json_args.cap = 0;
    Wisun.nodes_json(wisun_work);
    TEST_ASSERT_EQUAL_size_t(0, Wisun.n);
    Wisun.nodes_json_args.fan = NULL;
    Wisun.nodes_json_args.out = out;
    Wisun.nodes_json_args.cap = sizeof(out);
    Wisun.nodes_json(wisun_work);
    TEST_ASSERT_EQUAL_size_t(0, Wisun.n);
    Wisun.nodes_json_args.fan = &fan;
    Wisun.nodes_json_args.out = NULL;
    Wisun.nodes_json_args.cap = sizeof(out);
    Wisun.nodes_json(wisun_work);
    TEST_ASSERT_EQUAL_size_t(0, Wisun.n);
}
