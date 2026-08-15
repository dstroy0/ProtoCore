// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_trap.c
 * @brief Where a machine-mode trap lands, and the three registers that say what it was.
 *
 * The reset stub points mtvec at protocore_trap, which reads mcause, mepc and mtval into the
 * argument registers and calls this. mcause holds the exception code with its high bit set for an
 * interrupt, mepc the address of the instruction that trapped, and mtval the faulting address or
 * instruction word (RISC-V privileged ISA, Machine Cause / Exception Program Counter / Trap Value
 * registers).
 *
 * The default records the three values and returns; the stub stops afterwards regardless. A trap
 * reached here is an instruction that could not execute, so continuing runs code whose
 * preconditions do not hold.
 */

#include "core_setup/boot/protocore_boot.h"

#include <stdint.h>

/** @brief The last trap's cause, faulting PC and trap value. Read by a debugger or a board hook. */
protocore_trap_record protocore_last_trap;

/**
 * @brief What a machine-mode trap reaches.
 *
 * Weak, so a board overrides it by defining the name and may print, mark the image bad, or reset
 * instead. An override that returns still stops, in the reset stub.
 */
__attribute__((weak)) void protocore_platform_trap(uint32_t mcause, uint32_t mepc, uint32_t mtval)
{
    protocore_last_trap.mcause = mcause;
    protocore_last_trap.mepc = mepc;
    protocore_last_trap.mtval = mtval;
    protocore_last_trap.count++;
}
