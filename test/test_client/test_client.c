// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the outbound TCP client transport (network_drivers/transport/client.cpp).
// The tcpip_api_call-marshaled lwIP path is ARDUINO-only; on native this file compiles to
// just the !NEED stub (no lwIP on the host, so every call fails closed / reports no
// connection). No other env's build_src_filter pulls this file in, so this is its only
// coverage.

#include "network_drivers/transport/client.h"
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_open_fails_closed()
{
    TEST_ASSERT_TRUE(pc_client_open("example.com", 80, 1000) < 0);
    TEST_ASSERT_EQUAL_INT(-1, pc_client_open(NULL, 0, 0));
}

void test_connected_always_false()
{
    TEST_ASSERT_FALSE(pc_client_connected(-1));
    TEST_ASSERT_FALSE(pc_client_connected(0));
    TEST_ASSERT_FALSE(pc_client_connected(1));
}

void test_is_closed_always_true()
{
    TEST_ASSERT_TRUE(pc_client_is_closed(-1));
    TEST_ASSERT_TRUE(pc_client_is_closed(0));
    TEST_ASSERT_TRUE(pc_client_is_closed(1));
}

void test_send_always_false()
{
    const uint8_t payload[4] = {1, 2, 3, 4};
    TEST_ASSERT_FALSE(pc_client_send(-1, payload, sizeof(payload)));
    TEST_ASSERT_FALSE(pc_client_send(0, payload, sizeof(payload)));
    TEST_ASSERT_FALSE(pc_client_send(0, NULL, 0));
}

void test_available_always_zero()
{
    TEST_ASSERT_EQUAL_size_t(0, pc_client_available(-1));
    TEST_ASSERT_EQUAL_size_t(0, pc_client_available(0));
}

void test_read_always_zero()
{
    uint8_t buf[8];
    TEST_ASSERT_EQUAL_size_t(0, pc_client_read(-1, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0, pc_client_read(0, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_size_t(0, pc_client_read(0, NULL, 0));
}

void test_close_is_a_noop()
{
    // No state to observe (the host build has none); just prove it does not crash
    // for any id, in or out of range.
    pc_client_close(-1);
    pc_client_close(0);
    pc_client_close(1);
    TEST_ASSERT_TRUE(true);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_open_fails_closed);
    RUN_TEST(test_connected_always_false);
    RUN_TEST(test_is_closed_always_true);
    RUN_TEST(test_send_always_false);
    RUN_TEST(test_available_always_zero);
    RUN_TEST(test_read_always_zero);
    RUN_TEST(test_close_is_a_noop);
    return UNITY_END();
}
