// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file startup_common.c
 * @brief The reset path once a stack pointer exists, shared by every architecture.
 *
 * Both linker scripts export the same seven addresses, so the step that turns them into regions and
 * runs the loops is written once. What differs per architecture is only how sp came to be set:
 * cortex-M has the hardware read it out of the vector table, riscv32 loads it in its entry stub.
 * Both arrive here.
 */

#include "core_setup/boot/protocore_boot.h"

/**
 * @name Linker-exported addresses
 * Declared as arrays: these are addresses the linker script places, not objects with values.
 * @{
 */
extern uint32_t __protocore_data_load[];
extern uint32_t __protocore_data_start[];
extern uint32_t __protocore_data_end[];
extern uint32_t __protocore_bss_start[];
extern uint32_t __protocore_bss_end[];
extern uint32_t __protocore_stack_low[];
extern uint32_t __protocore_stack_top[];
/** @} */

extern int main(void);

/**
 * @brief Bring-up that has to happen before .data can be copied: flash timing, PLL, external RAM.
 *
 * Runs on a zeroed-nothing machine - .data holds image values, .bss holds whatever reset left - so
 * it may not read a static. Weak and empty, so a board file overrides it by defining the name.
 */
__attribute__((weak)) void protocore_platform_early_init(void)
{
}

/**
 * @brief Peripheral setup: clocks handed out, pins muxed, buses opened.
 *
 * Runs after .data and .bss are good, so statics are readable and a driver may keep state.
 */
__attribute__((weak)) void protocore_platform_periph_init(void)
{
}

/** @brief Copy .data, zero .bss, paint the stack, enter main. Does not return. */
void protocore_boot_start(void)
{
    protocore_platform_early_init();

    // Differencing uint32_t pointers gives words, which is what the loops count in.
    const protocore_boot_regions r = {
        .data_load = __protocore_data_load,
        .data_run = __protocore_data_start,
        .data_words = (uint32_t)(__protocore_data_end - __protocore_data_start),
        .bss = __protocore_bss_start,
        .bss_words = (uint32_t)(__protocore_bss_end - __protocore_bss_start),
        .stack_low = __protocore_stack_low,
        .stack_words = (uint32_t)(__protocore_stack_top - __protocore_stack_low),
    };

    protocore_boot_init_memory(&r);
    protocore_boot_paint_stack(r.stack_low, r.stack_words);

    protocore_platform_periph_init();

    (void)main();

    for (;;)
    {
    }
}
