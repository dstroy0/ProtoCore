// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_boot.h
 * @brief The part of a reset handler that is not architecture: initialized data, zeroed data, stack.
 *
 * A reset handler does four things before C code is allowed to run: point the stack somewhere,
 * copy .data from the image to RAM, zero .bss, and paint the stack so its use can be measured
 * later. The first is a register write and lives in the per-arch startup; the other three are
 * loops over addresses the linker script exports, which is why they are here, in C, taking their
 * regions as an argument. A host test passes its own arrays and runs the same loops the silicon
 * runs.
 *
 * The paint is what protocore_platform_stack_free() reads on a target with no RTOS to ask.
 */

#ifndef PROTOCORE_BOOT_H
#define PROTOCORE_BOOT_H

#include <stdint.h>

/** @brief The word written across the unused stack, and the value scanned for to measure it. */
#define PROTOCORE_BOOT_STACK_PAINT 0xC0DEFACEu

/**
 * @brief The regions a linker script exports, in words.
 *
 * @var protocore_boot_regions::data_load  .data as it sits in the image, at its load address
 * @var protocore_boot_regions::data_run   .data where it is read from, at its run address
 * @var protocore_boot_regions::data_words words to copy between them
 * @var protocore_boot_regions::bss        the zero-initialized region
 * @var protocore_boot_regions::bss_words  words to zero
 * @var protocore_boot_regions::stack_low  lowest word of the stack
 * @var protocore_boot_regions::stack_words words the stack spans
 */
typedef struct
{
    const uint32_t *data_load;
    uint32_t *data_run;
    uint32_t data_words;
    uint32_t *bss;
    uint32_t bss_words;
    uint32_t *stack_low;
    uint32_t stack_words;
} protocore_boot_regions;

/** @brief Copy .data to its run address and zero .bss. A null region is skipped, not faulted. */
void protocore_boot_init_memory(const protocore_boot_regions *r);

/**
 * @brief Write the paint across the stack below the calling frame. Returns words written.
 *
 * The stack grows down and this runs on it, so the frame address is the ceiling: words at or above
 * it belong to this call and are left alone.
 */
uint32_t protocore_boot_paint_stack(uint32_t *stack_low, uint32_t stack_words);

/** @brief Bytes of stack still carrying the paint, counted up from the low end. */
uint32_t protocore_boot_stack_unused(const uint32_t *stack_low, uint32_t stack_words);

/**
 * @brief What the last machine-mode trap reported.
 *
 * @var protocore_trap_record::mcause  the exception code, high bit set for an interrupt
 * @var protocore_trap_record::mepc    the address of the instruction that trapped
 * @var protocore_trap_record::mtval   the faulting address, or the instruction word
 * @var protocore_trap_record::count   traps recorded since reset
 */
typedef struct
{
    uint32_t mcause;
    uint32_t mepc;
    uint32_t mtval;
    uint32_t count;
} protocore_trap_record;

/** @brief The last trap's registers. Zero until one is taken. */
extern protocore_trap_record protocore_last_trap;

/** @brief What a machine-mode trap reaches. Weak; the reset stub stops after it returns. */
void protocore_platform_trap(uint32_t mcause, uint32_t mepc, uint32_t mtval);

#endif // PROTOCORE_BOOT_H
