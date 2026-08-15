// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file startup_cortex_m.c
 * @brief The cortex-M vector table and the entry the hardware jumps to.
 *
 * On reset this core reads word 0 of the table into sp and word 1 into pc, so the table is placed
 * at the start of FLASH by cortex_m.ld.in and both words are the linker's. The stack pointer is set
 * before the first instruction runs, which is why there is no assembly here.
 *
 * The table holds the sixteen architectural entries only. Device interrupts follow them and their
 * count is a part fact, so a profile extends the table rather than this file guessing at a length.
 *
 * Every handler but the entry is weak: a build that defines one by name overrides it, and a build
 * that does not gets a stop, so a fault is a halt at a known address instead of a silent return
 * into a corrupted frame.
 */

#include "core_setup/boot/protocore_boot.h"

extern uint32_t __protocore_stack_top[];
extern void protocore_boot_start(void);

/** @brief Where an unhandled exception stops. */
void protocore_default_handler(void)
{
    for (;;)
    {
    }
}

/**
 * @name Architectural exceptions
 * Weak, aliased to the stop, so a build overrides any of them by defining the name.
 * @{
 */
void protocore_nmi(void) __attribute__((weak, alias("protocore_default_handler")));
void protocore_hardfault(void) __attribute__((weak, alias("protocore_default_handler")));
void protocore_memmanage(void) __attribute__((weak, alias("protocore_default_handler")));
void protocore_busfault(void) __attribute__((weak, alias("protocore_default_handler")));
void protocore_usagefault(void) __attribute__((weak, alias("protocore_default_handler")));
void protocore_svc(void) __attribute__((weak, alias("protocore_default_handler")));
void protocore_debugmon(void) __attribute__((weak, alias("protocore_default_handler")));
void protocore_pendsv(void) __attribute__((weak, alias("protocore_default_handler")));
void protocore_systick(void) __attribute__((weak, alias("protocore_default_handler")));
/** @} */

/** @brief Word 1 of the table. sp is already set, so this is the shared reset path directly. */
void protocore_reset(void)
{
    protocore_boot_start();
}

typedef void (*protocore_vector)(void);

// Word 0 is the initial sp, word 1 the entry, then the thirteen architectural exception slots.
// The four reserved slots read zero, as the architecture states them.
__attribute__((used, section(".protocore_vectors"))) static const protocore_vector s_vectors[16] = {
    (protocore_vector)__protocore_stack_top,
    protocore_reset,
    protocore_nmi,
    protocore_hardfault,
    protocore_memmanage,
    protocore_busfault,
    protocore_usagefault,
    0,
    0,
    0,
    0,
    protocore_svc,
    protocore_debugmon,
    0,
    protocore_pendsv,
    protocore_systick,
};
