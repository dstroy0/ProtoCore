// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the heap/stack guardrails (server/core/guardrails/guardrails.h).
//
// No standard governs a heap counter, so every expectation here except the serializer's is a
// PROPERTY, stated as such: the floor comparison is strict, the three breach bits are disjoint
// powers of two so an OR loses nothing, and each floor reads exactly one field. The serializer is
// anchored on RFC 8259: sec 4 fixes the object form (members separated by ',', name and value by
// ':') and sec 6 fixes the number form, which is what makes the emitted text a JSON object a panel
// can parse rather than a string that merely looks like one.
//
// test_the_floor_is_strictly_below is the load-bearing case: this evaluator is what sheds load or
// reboots, so an off-by-one at the floor either fires an episode early on a healthy device or
// misses the one byte before exhaustion.

#include "server/core/guardrails/guardrails.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// The floors every case below judges against, distinct so a swapped argument shows up.
#define HEAP_MIN 8192u
#define FRAG_MIN 4096u
#define STACK_MIN 512u

static uint8_t eval(uint32_t free_heap, uint32_t min_free, uint32_t largest, uint32_t stack)
{
    const protocore_health h = {free_heap, min_free, largest, stack};
    return protocore_guardrail_eval(&h, HEAP_MIN, FRAG_MIN, STACK_MIN);
}

// A guardrail trips when a value is BELOW its floor, so the floor itself is still healthy and one
// less than it is not. Checked on all three at once and on each boundary in turn.
void test_the_floor_is_strictly_below(void)
{
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_BREACH_NONE, eval(HEAP_MIN, 0, FRAG_MIN, STACK_MIN));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_BREACH_HEAP, eval(HEAP_MIN - 1u, 0, FRAG_MIN, STACK_MIN));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_BREACH_FRAG, eval(HEAP_MIN, 0, FRAG_MIN - 1u, STACK_MIN));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_BREACH_STACK, eval(HEAP_MIN, 0, FRAG_MIN, STACK_MIN - 1u));
}

// Each floor reads one field and no other: driving one field under its floor sets that bit alone.
void test_each_floor_reads_one_field(void)
{
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_BREACH_HEAP, eval(0, 0, FRAG_MIN, STACK_MIN));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_BREACH_FRAG, eval(HEAP_MIN, 0, 0, STACK_MIN));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_BREACH_STACK, eval(HEAP_MIN, 0, FRAG_MIN, 0));
}

// min_free_heap is reported, not judged: there is no floor argument for it, so a low-water mark at
// zero on an otherwise healthy device trips nothing.
void test_the_low_water_mark_is_not_a_guardrail(void)
{
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_BREACH_NONE, eval(HEAP_MIN, 0, FRAG_MIN, STACK_MIN));
}

// The three bits are OR'd into one mask, so they must be distinct powers of two for the mask to
// name every breach that happened rather than only the last one.
void test_the_breach_bits_are_disjoint_powers_of_two(void)
{
    const uint8_t all = PROTOCORE_BREACH_HEAP | PROTOCORE_BREACH_FRAG | PROTOCORE_BREACH_STACK;
    TEST_ASSERT_EQUAL_UINT8(0, PROTOCORE_BREACH_NONE);
    TEST_ASSERT_EQUAL_UINT8(0, PROTOCORE_BREACH_HEAP & PROTOCORE_BREACH_FRAG);
    TEST_ASSERT_EQUAL_UINT8(0, PROTOCORE_BREACH_HEAP & PROTOCORE_BREACH_STACK);
    TEST_ASSERT_EQUAL_UINT8(0, PROTOCORE_BREACH_FRAG & PROTOCORE_BREACH_STACK);
    TEST_ASSERT_EQUAL_UINT8(all, eval(0, 0, 0, 0));
}

// A snapshot nothing can be read from is not evidence of a breach.
void test_a_null_snapshot_reports_no_breach(void)
{
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_BREACH_NONE, protocore_guardrail_eval(NULL, HEAP_MIN, FRAG_MIN, STACK_MIN));
}

// RFC 8259 sec 4 object form, sec 6 number form: four members in the order the snapshot declares
// them, each an unquoted integer with no leading zero and no plus sign.
static const char WANT[] = "{\"free_heap\":20000,\"min_free_heap\":15000,\"largest_free_block\":10000,\"stack_free\":2048}";

void test_json_is_an_rfc8259_object(void)
{
    const protocore_health h = {20000, 15000, 10000, 2048};
    char buf[128];
    int n = protocore_health_json(&h, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(WANT, buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), n);
}

// sec 6: "leading zeros are not allowed", so a zero field is the single digit 0.
void test_json_writes_zero_as_one_digit(void)
{
    const protocore_health h = {0, 0, 0, 0};
    char buf[128];
    TEST_ASSERT_TRUE(protocore_health_json(&h, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"free_heap\":0,\"min_free_heap\":0,\"largest_free_block\":0,\"stack_free\":0}", buf);
}

// The widest a field can be: 2^32-1 renders in full rather than wrapping or clipping.
void test_json_writes_the_full_32_bit_range(void)
{
    const protocore_health h = {4294967295u, 4294967295u, 4294967295u, 4294967295u};
    char buf[160];
    TEST_ASSERT_TRUE(protocore_health_json(&h, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"free_heap\":4294967295,\"min_free_heap\":4294967295,"
                             "\"largest_free_block\":4294967295,\"stack_free\":4294967295}",
                             buf);
}

// The object plus its terminator is what fits; one octet less writes nothing at all, because half a
// JSON object is not a document a panel can parse.
void test_json_boundary_is_the_object_plus_its_terminator(void)
{
    const protocore_health h = {20000, 15000, 10000, 2048};
    char exact[sizeof(WANT)];
    TEST_ASSERT_EQUAL_INT((int)sizeof(WANT) - 1, protocore_health_json(&h, exact, sizeof(exact)));
    TEST_ASSERT_EQUAL_STRING(WANT, exact);

    char one_short[sizeof(WANT) - 1];
    TEST_ASSERT_EQUAL_INT(0, protocore_health_json(&h, one_short, sizeof(one_short)));
    TEST_ASSERT_EQUAL_CHAR('\0', one_short[0]);
}

void test_json_refuses_null_and_zero_capacity(void)
{
    const protocore_health h = {20000, 15000, 10000, 2048};
    char buf[128];
    buf[0] = 'x';
    TEST_ASSERT_EQUAL_INT(0, protocore_health_json(&h, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(0, protocore_health_json(&h, buf, 0));
    TEST_ASSERT_EQUAL_CHAR('x', buf[0]); // zero capacity leaves the buffer alone
    TEST_ASSERT_EQUAL_INT(0, protocore_health_json(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("", buf); // a null snapshot empties the destination
}

// There are no live counters off-target, so the sampler reports zeros and the check reports no
// breach rather than a false all-clear built on stale numbers.
void test_the_host_sampler_reports_no_counters(void)
{
    protocore_health h = {999, 999, 999, 999};
    protocore_guardrails_sample(&h);
    TEST_ASSERT_EQUAL_UINT32(0u, h.free_heap);
    TEST_ASSERT_EQUAL_UINT32(0u, h.min_free_heap);
    TEST_ASSERT_EQUAL_UINT32(0u, h.largest_free_block);
    TEST_ASSERT_EQUAL_UINT32(0u, h.stack_free);
    protocore_guardrails_sample(NULL);
    protocore_guardrails_begin(NULL);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_BREACH_NONE, protocore_guardrails_check());
}
