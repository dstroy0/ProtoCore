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
#include "core_setup/hal/host/host_platform.h" // protocore_host_set_heap / _stack: what the seam reports
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

// The snapshot and the floors ride on the handle, so each case binds them and reads the mask back
// off it.
static uint8_t eval(uint32_t free_heap, uint32_t min_free, uint32_t largest, uint32_t stack)
{
    protocore_health h = {free_heap, min_free, largest, stack};
    Guardrails.health = &h;
    Guardrails.floors.heap_min = HEAP_MIN;
    Guardrails.floors.frag_min_block = FRAG_MIN;
    Guardrails.floors.stack_min = STACK_MIN;
    Guardrails.eval(Guardrails.internal);
    return Guardrails.breaches;
}

static uint8_t eval_snapshot(protocore_health *h)
{
    Guardrails.health = h;
    Guardrails.floors.heap_min = HEAP_MIN;
    Guardrails.floors.frag_min_block = FRAG_MIN;
    Guardrails.floors.stack_min = STACK_MIN;
    Guardrails.eval(Guardrails.internal);
    return Guardrails.breaches;
}

static int json_of(protocore_health *h, char *out, size_t cap)
{
    Guardrails.health = h;
    Guardrails.out.out = out;
    Guardrails.out.cap = cap;
    Guardrails.json(Guardrails.internal);
    return Guardrails.n;
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

// min_free_heap is reported, not judged: there is no floor member for it, so a low-water mark at
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
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_BREACH_NONE, eval_snapshot(NULL));
}

// RFC 8259 sec 4 object form, sec 6 number form: four members in the order the snapshot declares
// them, each an unquoted integer with no leading zero and no plus sign.
static const char WANT[] =
    "{\"free_heap\":20000,\"min_free_heap\":15000,\"largest_free_block\":10000,\"stack_free\":2048}";

void test_json_is_an_rfc8259_object(void)
{
    protocore_health h = {20000, 15000, 10000, 2048};
    char buf[128];
    int n = json_of(&h, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(WANT, buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), n);
}

// sec 6: "leading zeros are not allowed", so a zero field is the single digit 0.
void test_json_writes_zero_as_one_digit(void)
{
    protocore_health h = {0, 0, 0, 0};
    char buf[128];
    TEST_ASSERT_TRUE(json_of(&h, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"free_heap\":0,\"min_free_heap\":0,\"largest_free_block\":0,\"stack_free\":0}", buf);
}

// The widest a field can be: 2^32-1 renders in full rather than wrapping or clipping.
void test_json_writes_the_full_32_bit_range(void)
{
    protocore_health h = {4294967295u, 4294967295u, 4294967295u, 4294967295u};
    char buf[160];
    TEST_ASSERT_TRUE(json_of(&h, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"free_heap\":4294967295,\"min_free_heap\":4294967295,"
                             "\"largest_free_block\":4294967295,\"stack_free\":4294967295}",
                             buf);
}

// The object plus its terminator is what fits; one octet less writes nothing at all, because half a
// JSON object is not a document a panel can parse.
void test_json_boundary_is_the_object_plus_its_terminator(void)
{
    protocore_health h = {20000, 15000, 10000, 2048};
    char exact[sizeof(WANT)];
    TEST_ASSERT_EQUAL_INT((int)sizeof(WANT) - 1, json_of(&h, exact, sizeof(exact)));
    TEST_ASSERT_EQUAL_STRING(WANT, exact);

    char one_short[sizeof(WANT) - 1];
    TEST_ASSERT_EQUAL_INT(0, json_of(&h, one_short, sizeof(one_short)));
    TEST_ASSERT_EQUAL_CHAR('\0', one_short[0]);
}

void test_json_refuses_null_and_zero_capacity(void)
{
    protocore_health h = {20000, 15000, 10000, 2048};
    char buf[128];
    buf[0] = 'x';
    TEST_ASSERT_EQUAL_INT(0, json_of(&h, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(0, json_of(&h, buf, 0));
    TEST_ASSERT_EQUAL_CHAR('x', buf[0]); // zero capacity leaves the buffer alone
    TEST_ASSERT_EQUAL_INT(0, json_of(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("", buf); // a null snapshot empties the destination
}

// The sampler reads the platform seam, which on host reports what the test stated. Nothing is
// invented: the figures out are the figures in.
void test_the_sampler_reports_the_stated_counters(void)
{
    protocore_host_set_heap(20000u, 15000u, 65536u, 10000u);
    protocore_host_set_stack(2048u);

    protocore_health h = {999, 999, 999, 999};
    Guardrails.health = &h;
    Guardrails.sample(Guardrails.internal);
    TEST_ASSERT_EQUAL_UINT32(20000u, h.free_heap);
    TEST_ASSERT_EQUAL_UINT32(15000u, h.min_free_heap);
    TEST_ASSERT_EQUAL_UINT32(10000u, h.largest_free_block);
    TEST_ASSERT_EQUAL_UINT32(2048u, h.stack_free);

    // A snapshot there is nowhere to write is not a crash.
    Guardrails.health = NULL;
    Guardrails.sample(Guardrails.internal);
}

// check() samples and judges in one step, so a device stated healthy reports no breach and one
// stated exhausted reports every floor it crossed. A counter of zero is a breach, not an all-clear.
void test_check_judges_what_the_sampler_read(void)
{
    protocore_host_set_heap(20000u, 15000u, 65536u, 10000u);
    protocore_host_set_stack(2048u);
    Guardrails.cb = NULL;
    Guardrails.begin(Guardrails.internal);
    Guardrails.check(Guardrails.internal);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_BREACH_NONE, Guardrails.breaches);

    protocore_host_set_heap(0u, 0u, 0u, 0u);
    protocore_host_set_stack(0u);
    Guardrails.check(Guardrails.internal);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_BREACH_HEAP | PROTOCORE_BREACH_FRAG | PROTOCORE_BREACH_STACK,
                            Guardrails.breaches);
}

// A check that breaches hands the callback the same mask it reports, and the snapshot it judged.
static uint8_t g_seen_mask;
static protocore_health g_seen_health;

static void on_breach(uint8_t breaches, const protocore_health *h)
{
    g_seen_mask = breaches;
    g_seen_health = *h;
}

// begin() installs the callback on the handle's own storage, so a second begin replaces it and a
// null one silences the report without changing what check() returns.
void test_begin_installs_and_replaces_the_callback(void)
{
    protocore_host_set_heap(0u, 0u, 0u, 0u); // every floor crossed, so a callback has something to report
    protocore_host_set_stack(0u);

    g_seen_mask = 0u;
    Guardrails.cb = on_breach;
    Guardrails.begin(Guardrails.internal);
    Guardrails.check(Guardrails.internal);
    TEST_ASSERT_EQUAL_UINT8(Guardrails.breaches, g_seen_mask); // the callback saw the mask check reports
    TEST_ASSERT_EQUAL_UINT32(0u, g_seen_health.free_heap);     // and the snapshot it judged

    g_seen_mask = 0xFFu;
    Guardrails.cb = NULL;
    Guardrails.begin(Guardrails.internal);
    Guardrails.check(Guardrails.internal);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, g_seen_mask); // replaced with none: the callback never ran
}
