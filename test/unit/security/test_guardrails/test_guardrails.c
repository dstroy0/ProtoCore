// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the guardrails core (services/security/guardrails): the threshold
// evaluator and the JSON serializer. Sampling the live counters is ESP32-only.

#include "services/security/guardrails/guardrails.h"
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_eval_all_clear()
{
    pc_health h = {20000, 15000, 10000, 2048};
    TEST_ASSERT_EQUAL_UINT8(PC_BREACH_NONE, pc_guardrail_eval(&h, 8192, 4096, 512));
}

void test_eval_heap_breach()
{
    pc_health h = {4000, 3000, 8000, 2048}; // free_heap below 8192
    TEST_ASSERT_EQUAL_UINT8(PC_BREACH_HEAP, pc_guardrail_eval(&h, 8192, 4096, 512));
}

void test_eval_frag_and_stack()
{
    pc_health h = {20000, 15000, 1000, 256}; // largest block < 4096, stack < 512
    uint8_t b = pc_guardrail_eval(&h, 8192, 4096, 512);
    TEST_ASSERT_EQUAL_UINT8(PC_BREACH_FRAG | PC_BREACH_STACK, b);
}

void test_eval_all_breached()
{
    pc_health h = {100, 50, 100, 100};
    uint8_t b = pc_guardrail_eval(&h, 8192, 4096, 512);
    TEST_ASSERT_EQUAL_UINT8(PC_BREACH_HEAP | PC_BREACH_FRAG | PC_BREACH_STACK, b);
}

void test_json()
{
    pc_health h = {20000, 15000, 10000, 2048};
    char buf[128];
    int n = pc_health_json(&h, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING(
        "{\"free_heap\":20000,\"min_free_heap\":15000,\"largest_free_block\":10000,\"stack_free\":2048}", buf);
}

void test_json_small_buffer_fails_closed()
{
    pc_health h = {20000, 15000, 10000, 2048};
    char buf[8];
    TEST_ASSERT_EQUAL_INT(0, pc_health_json(&h, buf, sizeof(buf)));
}

void test_eval_null_health_is_clear()
{
    // A null health snapshot reports no breach (nothing to evaluate).
    TEST_ASSERT_EQUAL_UINT8(PC_BREACH_NONE, pc_guardrail_eval(NULL, 8192, 4096, 512));
}

void test_json_guards_fail_closed()
{
    pc_health h = {20000, 15000, 10000, 2048};
    char buf[128] = "x";
    // Null out or zero cap -> 0 (nothing written).
    TEST_ASSERT_EQUAL_INT(0, pc_health_json(&h, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(0, pc_health_json(&h, buf, 0));
    // Null health -> empty string, 0.
    TEST_ASSERT_EQUAL_INT(0, pc_health_json(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_host_sampler_stubs()
{
    // On host there are no live counters: sample() zeroes the snapshot (and no-ops on null), begin()
    // is a no-op, and check() reports no breach.
    pc_health h = {999, 999, 999, 999};
    pc_guardrails_sample(&h);
    TEST_ASSERT_EQUAL_UINT32(0, h.free_heap);
    TEST_ASSERT_EQUAL_UINT32(0, h.stack_free);
    pc_guardrails_sample(NULL); // null guard: must not crash
    pc_guardrails_begin(NULL);
    TEST_ASSERT_EQUAL_UINT8(PC_BREACH_NONE, pc_guardrails_check());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_eval_all_clear);
    RUN_TEST(test_eval_heap_breach);
    RUN_TEST(test_eval_frag_and_stack);
    RUN_TEST(test_eval_all_breached);
    RUN_TEST(test_json);
    RUN_TEST(test_json_small_buffer_fails_closed);
    RUN_TEST(test_eval_null_health_is_clear);
    RUN_TEST(test_json_guards_fail_closed);
    RUN_TEST(test_host_sampler_stubs);
    return UNITY_END();
}
