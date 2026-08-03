// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/wisun: the CoAP client request builder (RFC 7252) + the FAN node registry.

#include "services/radio/wisun/wisun.h"
#include <string.h>
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

void test_build_coap_get(void)
{
    uint8_t buf[64];
    // CON GET "sensors/temp", msg id 0x1234, no token.
    size_t n = pc_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_GET, 0x1234, NULL, 0,
                                   "sensors/temp", NULL, 0, buf, sizeof(buf));
    // Header: 0x40 (ver1, CON, tkl0), code 0x01, mid 0x12 0x34.
    TEST_ASSERT_EQUAL_HEX8(0x40, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x12, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x34, buf[3]);
    // Option 1: Uri-Path "sensors" -> delta 11, len 7 -> 0xB7.
    TEST_ASSERT_EQUAL_HEX8(0xB7, buf[4]);
    TEST_ASSERT_EQUAL_MEMORY("sensors", buf + 5, 7);
    // Option 2: Uri-Path "temp" -> delta 0, len 4 -> 0x04.
    TEST_ASSERT_EQUAL_HEX8(0x04, buf[12]);
    TEST_ASSERT_EQUAL_MEMORY("temp", buf + 13, 4);
    TEST_ASSERT_EQUAL_size_t(17, n);
}

void test_build_coap_put_with_token_and_payload(void)
{
    uint8_t buf[64];
    const uint8_t token[2] = {0xAB, 0xCD};
    const uint8_t body[3] = {0x31, 0x32, 0x33};
    size_t n = pc_wisun_build_coap(WISUN_COAP_NON, WISUN_COAP_PUT, 0x0005, token, 2, "led", body,
                                   3, buf, sizeof(buf));
    // Header: 0x52 (ver=01, type NON=01, tkl=0010), code 0x03 (PUT), mid 0x00 0x05.
    TEST_ASSERT_EQUAL_HEX8(0x52, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x05, buf[3]);
    // Token.
    TEST_ASSERT_EQUAL_HEX8(0xAB, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0xCD, buf[5]);
    // Uri-Path "led" -> 0xB3.
    TEST_ASSERT_EQUAL_HEX8(0xB3, buf[6]);
    TEST_ASSERT_EQUAL_MEMORY("led", buf + 7, 3);
    // Payload marker + body.
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[10]);
    TEST_ASSERT_EQUAL_MEMORY(body, buf + 11, 3);
    TEST_ASSERT_EQUAL_size_t(14, n);
}

void test_build_coap_long_segment_extended_length(void)
{
    // A 13-char path segment forces the extended-length nibble (0xD).
    uint8_t buf[64];
    size_t n = pc_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_GET, 1, NULL, 0, "abcdefghijklm",
                                   NULL, 0, buf, sizeof(buf));
    // Option header: delta 11 (0xB), length 13 -> nibble 0xD, ext byte 13-13=0.
    TEST_ASSERT_EQUAL_HEX8(0xBD, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[5]); // extended length = 0
    TEST_ASSERT_EQUAL_MEMORY("abcdefghijklm", buf + 6, 13);
    TEST_ASSERT_EQUAL_size_t(4 + 2 + 13, n);
}

void test_build_coap_rejects_bad_args(void)
{
    uint8_t buf[64];
    uint8_t tok[9] = {0};
    TEST_ASSERT_EQUAL_size_t(0, pc_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_GET, 1, tok, 9,
                                                    "x", NULL, 0, buf,
                                                    sizeof(buf))); // tkl > 8
    uint8_t tiny[3];
    TEST_ASSERT_EQUAL_size_t(0, pc_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_GET, 1, NULL, 0,
                                                    "x", NULL, 0, tiny,
                                                    sizeof(tiny))); // too small
}

void test_node_registry(void)
{
    WisunNode storage[3];
    WisunFan fan;
    pc_ip br = pc_ip_from_v6_bytes((const uint8_t[16]){0xfd, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1});
    pc_wisun_init(&fan, &br, storage, 3);

    pc_ip n1, n2;
    pc_ip_parse("fd00::10", &n1);
    pc_ip_parse("fd00::11", &n2);
    TEST_ASSERT_EQUAL_INT(0, pc_wisun_node_register(&fan, &n1, 100));
    TEST_ASSERT_EQUAL_INT(1, pc_wisun_node_register(&fan, &n2, 101));
    // Re-register n1 refreshes, does not add.
    TEST_ASSERT_EQUAL_INT(0, pc_wisun_node_register(&fan, &n1, 200));
    TEST_ASSERT_EQUAL_size_t(2, pc_wisun_joined_count(&fan));
    size_t idx = 99;
    TEST_ASSERT_TRUE(pc_wisun_node_find(&fan, &n2, &idx));
    TEST_ASSERT_EQUAL_size_t(1, idx);

    char js[128];
    size_t jn = pc_wisun_nodes_json(&fan, js, sizeof(js));
    TEST_ASSERT_EQUAL_size_t(strlen(js), jn);
    TEST_ASSERT_EQUAL_STRING("[{\"addr\":\"fd00::10\",\"joined\":true},{\"addr\":\"fd00::11\",\"joined\":true}]", js);
    // Overflow path: a buffer too small to hold the array returns 0.
    char tiny[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_wisun_nodes_json(&fan, tiny, sizeof(tiny)));
}

void test_registry_full_and_misses(void)
{
    WisunNode storage[2];
    WisunFan fan;
    pc_ip br;
    pc_ip_parse("fd00::1", &br);
    pc_wisun_init(&fan, &br, storage, 2);
    pc_ip a, b, c;
    pc_ip_parse("fd00::a", &a);
    pc_ip_parse("fd00::b", &b);
    pc_ip_parse("fd00::c", &c);
    TEST_ASSERT_EQUAL_INT(0, pc_wisun_node_register(&fan, &a, 1));
    TEST_ASSERT_EQUAL_INT(1, pc_wisun_node_register(&fan, &b, 2));
    TEST_ASSERT_EQUAL_INT(-1, pc_wisun_node_register(&fan, &c, 3)); // table full
    TEST_ASSERT_FALSE(pc_wisun_node_find(&fan, &c, NULL));       // not present
    TEST_ASSERT_EQUAL_size_t(2, pc_wisun_joined_count(&fan));
    // Bad args on init / register / build.
    pc_wisun_init(&fan, &br, NULL, 2); // null storage -> cap 0
    TEST_ASSERT_EQUAL_INT(-1, pc_wisun_node_register(&fan, &a, 1));
    uint8_t buf[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_wisun_build_coap(WISUN_COAP_CON, WISUN_COAP_GET, 1, NULL, 0,
                                                    "x", NULL, 0, NULL,
                                                    sizeof(buf))); // null out
}

void test_coap_length_ext()
{
    // A Uri-Path segment >= 269 bytes drives the 2-byte length-extension encoding.
    char path[302];
    path[0] = '/';
    for (int i = 1; i < 301; i++)
    {
        path[i] = 'A';
    }
    path[301] = '\0';
    uint8_t out[512];
    size_t n = pc_wisun_build_coap(0, 1, 1, NULL, 0, path, NULL, 0, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 269);
}

void test_coap_overflow_and_emit_fail()
{
    uint8_t out[64];
    // Header fits (cap == 4) but no room for even the first option header -> emit fails -> build 0.
    TEST_ASSERT_EQUAL_size_t(0, pc_wisun_build_coap(0, 1, 1, NULL, 0, "/a", NULL, 0, out, 4));
    // Header + no options, but the payload marker + payload overflow the buffer.
    uint8_t pl[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_EQUAL_size_t(0, pc_wisun_build_coap(0, 1, 1, NULL, 0, NULL, pl, sizeof(pl), out, 10));
    // Option header byte fits but the value does not -> emit_option second cap check.
    TEST_ASSERT_EQUAL_size_t(0, pc_wisun_build_coap(0, 1, 1, NULL, 0, "/abcde", NULL, 0, out, 6));
}

void test_coap_arg_guards()
{
    uint8_t out[64];
    uint8_t tok[2] = {1, 2};
    TEST_ASSERT_EQUAL_size_t(
        0, pc_wisun_build_coap(0, 1, 1, NULL, 0, "/a", NULL, 0, NULL, sizeof(out))); // null out
    TEST_ASSERT_EQUAL_size_t(0,
                             pc_wisun_build_coap(0, 1, 1, NULL, 9, "/a", NULL, 0, out, sizeof(out))); // tkl > 8
    TEST_ASSERT_EQUAL_size_t(
        0, pc_wisun_build_coap(0, 1, 1, NULL, 2, "/a", NULL, 0, out, sizeof(out))); // tkl w/ null token
    uint8_t pl[2] = {1, 2};
    TEST_ASSERT_EQUAL_size_t(
        0, pc_wisun_build_coap(0, 1, 1, tok, 2, "/a", NULL, 2, out, sizeof(out))); // plen w/ null payload
    TEST_ASSERT_EQUAL_size_t(0, pc_wisun_build_coap(0, 1, 1, NULL, 0, "/a", NULL, 0, out, 3)); // cap < 4+tkl
    (void)pl;
}

void test_wisun_null_guards()
{
    pc_wisun_init(NULL, NULL, NULL, 0); // null fan -> no-op
    WisunFan fan;
    WisunNode storage[4];
    pc_wisun_init(&fan, NULL, storage, 4); // null border_router -> zeroed
    size_t idx = 0;
    TEST_ASSERT_FALSE(pc_wisun_node_find(NULL, NULL, &idx)); // null fan
    TEST_ASSERT_EQUAL_size_t(0, pc_wisun_joined_count(NULL));   // null fan
    char buf[64];
    TEST_ASSERT_EQUAL_size_t(0, pc_wisun_nodes_json(NULL, buf, sizeof(buf))); // null fan
    TEST_ASSERT_EQUAL_size_t(0, pc_wisun_nodes_json(&fan, buf, 0));              // zero cap
}

void test_node_register_null_fan_and_addr(void)
{
    // Cover the individual OR-arms of pc_wisun_node_register's guard that the other tests
    // don't reach: a null fan (short-circuits before ever touching fan->nodes), and a null
    // addr against an otherwise-valid fan/nodes.
    WisunNode storage[2];
    WisunFan fan;
    pc_ip br;
    pc_ip_parse("fd00::1", &br);
    pc_wisun_init(&fan, &br, storage, 2);
    pc_ip a;
    pc_ip_parse("fd00::a", &a);
    TEST_ASSERT_EQUAL_INT(-1, pc_wisun_node_register(NULL, &a, 1));   // null fan
    TEST_ASSERT_EQUAL_INT(-1, pc_wisun_node_register(&fan, NULL, 1)); // null addr
}

void test_node_find_partial_null_and_found_idx_null(void)
{
    // pc_wisun_node_find's guard is the same 3-arm OR; test_wisun_null_guards only covers a
    // fully-null call, so the "fan valid but nodes null" and "fan/nodes valid but addr null"
    // arms are exercised here. Also cover the found-but-caller-doesn't-want-the-index path
    // (idx == NULL) on a hit, which the other tests never trigger.
    WisunNode storage[2];
    WisunFan fan;
    pc_ip br;
    pc_ip_parse("fd00::1", &br);
    pc_wisun_init(&fan, &br, storage, 2);
    pc_ip a;
    pc_ip_parse("fd00::a", &a);
    TEST_ASSERT_EQUAL_INT(0, pc_wisun_node_register(&fan, &a, 1));

    TEST_ASSERT_TRUE(pc_wisun_node_find(&fan, &a, NULL)); // found, idx not requested

    size_t idx = 99;
    TEST_ASSERT_FALSE(pc_wisun_node_find(&fan, NULL, &idx)); // null addr, valid fan/nodes

    WisunFan bare_fan;
    pc_wisun_init(&bare_fan, &br, NULL, 0);
    TEST_ASSERT_FALSE(pc_wisun_node_find(&bare_fan, &a, &idx)); // valid fan, null nodes
}

void test_joined_count_and_json_unjoined_and_null_nodes(void)
{
    // pc_wisun_joined_count's guard needs "fan valid, nodes null" (the other tests only cover
    // a fully-null fan). The joined/unjoined ternary in both pc_wisun_joined_count's loop and
    // pc_wisun_nodes_json's serializer only ever sees joined==true elsewhere, since
    // pc_wisun_node_register always sets it; flip one entry directly through the caller-owned
    // storage (a node "leaving" the mesh) to exercise the false side of both. Also cover
    // pc_wisun_nodes_json's null-out arm against an otherwise-valid fan/cap.
    WisunNode storage[2];
    WisunFan fan;
    pc_ip br;
    pc_ip_parse("fd00::1", &br);
    pc_wisun_init(&fan, &br, storage, 2);
    pc_ip a, b;
    pc_ip_parse("fd00::a", &a);
    pc_ip_parse("fd00::b", &b);
    TEST_ASSERT_EQUAL_INT(0, pc_wisun_node_register(&fan, &a, 1));
    TEST_ASSERT_EQUAL_INT(1, pc_wisun_node_register(&fan, &b, 2));
    storage[1].joined = false;

    TEST_ASSERT_EQUAL_size_t(1, pc_wisun_joined_count(&fan));

    char js[128];
    size_t jn = pc_wisun_nodes_json(&fan, js, sizeof(js));
    TEST_ASSERT_EQUAL_size_t(strlen(js), jn);
    TEST_ASSERT_NOT_NULL(strstr(js, "\"joined\":true"));
    TEST_ASSERT_NOT_NULL(strstr(js, "\"joined\":false"));

    WisunFan bare_fan;
    pc_wisun_init(&bare_fan, &br, NULL, 0);
    TEST_ASSERT_EQUAL_size_t(0, pc_wisun_joined_count(&bare_fan)); // valid fan, null nodes

    TEST_ASSERT_EQUAL_size_t(0, pc_wisun_nodes_json(&fan, NULL, sizeof(js))); // null out
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_build_coap_get);
    RUN_TEST(test_build_coap_put_with_token_and_payload);
    RUN_TEST(test_build_coap_long_segment_extended_length);
    RUN_TEST(test_build_coap_rejects_bad_args);
    RUN_TEST(test_node_registry);
    RUN_TEST(test_registry_full_and_misses);
    RUN_TEST(test_coap_length_ext);
    RUN_TEST(test_coap_overflow_and_emit_fail);
    RUN_TEST(test_coap_arg_guards);
    RUN_TEST(test_wisun_null_guards);
    RUN_TEST(test_node_register_null_fan_and_addr);
    RUN_TEST(test_node_find_partial_null_and_found_idx_null);
    RUN_TEST(test_joined_count_and_json_unjoined_and_null_nodes);
    return UNITY_END();
}
