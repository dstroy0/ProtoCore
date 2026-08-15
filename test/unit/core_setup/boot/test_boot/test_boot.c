// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the reset handler's loops (core_setup/boot/protocore_boot.h).
//
// No standard governs a startup file, so every expectation here is a PROPERTY, stated as such. The
// load-bearing one is that each loop writes exactly the words it was given and not one more:
// .data and .bss are adjacent in both linker scripts, so a copy that runs one word long writes
// through the front of .bss, and a zero that runs one word long writes through the front of the
// stack. Neither shows up as a crash - it shows up much later as a variable that was correct at
// reset and is not now. The guard words on both sides of every region are what catch it.
//
// The stack paint is the other half of protocore_platform_stack_free() on a target with no RTOS to
// ask: paint at reset, count what is still painted later, and the difference is the high-water
// mark.

#include "core_setup/boot/protocore_boot.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

#define GUARD 0xA5A5A5A5u

// A region with a guard word on each side, so a loop that overruns either end is caught.
#define REGION_WORDS 8u
static uint32_t s_src[REGION_WORDS];
static uint32_t s_dst[REGION_WORDS + 2u];
static uint32_t s_bss[REGION_WORDS + 2u];

static void arm_regions(void)
{
    for (uint32_t i = 0; i < REGION_WORDS; ++i)
    {
        s_src[i] = 0x11110000u + i;
    }
    for (uint32_t i = 0; i < REGION_WORDS + 2u; ++i)
    {
        s_dst[i] = GUARD;
        s_bss[i] = 0xDEADBEEFu;
    }
    s_bss[0] = GUARD;
    s_bss[REGION_WORDS + 1u] = GUARD;
}

// The regions as a linker script would hand them over: payload starting one word in, so index 0 and
// the last index are the guards.
static protocore_boot_regions armed(uint32_t data_words, uint32_t bss_words)
{
    const protocore_boot_regions r = {
        .data_load = s_src,
        .data_run = &s_dst[1],
        .data_words = data_words,
        .bss = &s_bss[1],
        .bss_words = bss_words,
        .stack_low = NULL,
        .stack_words = 0,
    };
    return r;
}

// The copy reproduces the image exactly, in order, and stops at the word count it was given.
void test_data_is_copied_word_for_word(void)
{
    arm_regions();
    const protocore_boot_regions r = armed(REGION_WORDS, 0);
    protocore_boot_init_memory(&r);

    for (uint32_t i = 0; i < REGION_WORDS; ++i)
    {
        TEST_ASSERT_EQUAL_HEX32(0x11110000u + i, s_dst[1u + i]);
    }
    TEST_ASSERT_EQUAL_HEX32(GUARD, s_dst[0]);
    TEST_ASSERT_EQUAL_HEX32(GUARD, s_dst[REGION_WORDS + 1u]);
}

// .bss ends up zero, and only .bss: the words on both sides keep their guard.
void test_bss_is_zeroed_and_nothing_else(void)
{
    arm_regions();
    const protocore_boot_regions r = armed(0, REGION_WORDS);
    protocore_boot_init_memory(&r);

    for (uint32_t i = 0; i < REGION_WORDS; ++i)
    {
        TEST_ASSERT_EQUAL_HEX32(0u, s_bss[1u + i]);
    }
    TEST_ASSERT_EQUAL_HEX32(GUARD, s_bss[0]);
    TEST_ASSERT_EQUAL_HEX32(GUARD, s_bss[REGION_WORDS + 1u]);
}

// Both loops in one call, which is the order a reset runs them in.
void test_a_reset_does_both(void)
{
    arm_regions();
    const protocore_boot_regions r = armed(REGION_WORDS, REGION_WORDS);
    protocore_boot_init_memory(&r);

    TEST_ASSERT_EQUAL_HEX32(0x11110000u, s_dst[1]);
    TEST_ASSERT_EQUAL_HEX32(0u, s_bss[1]);
    TEST_ASSERT_EQUAL_HEX32(GUARD, s_dst[0]);
    TEST_ASSERT_EQUAL_HEX32(GUARD, s_bss[REGION_WORDS + 1u]);
}

// A section a build emitted nothing into has a zero word count, and a zero-length loop writes
// nowhere: an empty .data must not disturb the .bss that follows it.
void test_empty_regions_write_nothing(void)
{
    arm_regions();
    const protocore_boot_regions r = armed(0, 0);
    protocore_boot_init_memory(&r);

    for (uint32_t i = 0; i < REGION_WORDS + 2u; ++i)
    {
        TEST_ASSERT_EQUAL_HEX32(GUARD, s_dst[i]);
    }
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, s_bss[1]);
}

// Nothing to copy into and nothing to copy from is not a fault: a null region is skipped, and the
// other one still runs.
void test_null_regions_are_skipped(void)
{
    arm_regions();
    protocore_boot_init_memory(NULL);
    TEST_ASSERT_EQUAL_HEX32(GUARD, s_dst[1]);

    protocore_boot_regions r = armed(REGION_WORDS, REGION_WORDS);
    r.data_run = NULL;
    protocore_boot_init_memory(&r);
    TEST_ASSERT_EQUAL_HEX32(0u, s_bss[1]);  // .bss still zeroed
    TEST_ASSERT_EQUAL_HEX32(GUARD, s_dst[1]); // .data skipped

    arm_regions();
    r = armed(REGION_WORDS, REGION_WORDS);
    r.bss = NULL;
    protocore_boot_init_memory(&r);
    TEST_ASSERT_EQUAL_HEX32(0x11110000u, s_dst[1]);  // .data still copied
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, s_bss[1]); // .bss skipped
}

// The stack. Static, so it is not the frame this test runs on and the paint's own ceiling does not
// land in the middle of it.
#define STACK_WORDS 64u
static uint32_t s_stack[STACK_WORDS];

// The paint stops at the calling frame, so it reports how far it got and every word it claims is
// painted, with the rest untouched.
void test_paint_covers_the_region_below_the_frame(void)
{
    for (uint32_t i = 0; i < STACK_WORDS; ++i)
    {
        s_stack[i] = GUARD;
    }
    const uint32_t painted = protocore_boot_paint_stack(s_stack, STACK_WORDS);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(STACK_WORDS, painted);

    for (uint32_t i = 0; i < painted; ++i)
    {
        TEST_ASSERT_EQUAL_HEX32(PROTOCORE_BOOT_STACK_PAINT, s_stack[i]);
    }
    for (uint32_t i = painted; i < STACK_WORDS; ++i)
    {
        TEST_ASSERT_EQUAL_HEX32(GUARD, s_stack[i]);
    }
}

// Unused is counted up from the low end, in bytes, and stops at the first word the stack has been
// down to. That word is the high-water mark.
void test_unused_counts_the_paint_from_the_low_end(void)
{
    for (uint32_t i = 0; i < STACK_WORDS; ++i)
    {
        s_stack[i] = PROTOCORE_BOOT_STACK_PAINT;
    }
    TEST_ASSERT_EQUAL_UINT32(STACK_WORDS << 2, protocore_boot_stack_unused(s_stack, STACK_WORDS));

    s_stack[10] = 0u; // the deepest the stack ever went
    TEST_ASSERT_EQUAL_UINT32(10u << 2, protocore_boot_stack_unused(s_stack, STACK_WORDS));

    s_stack[0] = 0u; // used to the floor: nothing left
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_boot_stack_unused(s_stack, STACK_WORDS));
}

// A stack that was never painted reads as fully used rather than fully free, so an unpainted build
// under-reports headroom instead of claiming headroom it cannot prove.
void test_an_unpainted_stack_reports_nothing_free(void)
{
    for (uint32_t i = 0; i < STACK_WORDS; ++i)
    {
        s_stack[i] = 0u;
    }
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_boot_stack_unused(s_stack, STACK_WORDS));
}

// Paint then measure, which is the pair as a target uses it: whatever the paint claimed is what the
// count reports back.
void test_paint_and_measure_agree(void)
{
    for (uint32_t i = 0; i < STACK_WORDS; ++i)
    {
        s_stack[i] = 0u;
    }
    const uint32_t painted = protocore_boot_paint_stack(s_stack, STACK_WORDS);
    TEST_ASSERT_EQUAL_UINT32(painted << 2, protocore_boot_stack_unused(s_stack, STACK_WORDS));
}

// A null stack is not a region to paint or to measure.
void test_a_null_stack_is_not_a_fault(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_boot_paint_stack(NULL, STACK_WORDS));
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_boot_stack_unused(NULL, STACK_WORDS));
}
