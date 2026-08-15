/* Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The riscv32 entry. Reset jumps here with no register set, so this establishes the two the ABI
 * needs before C is legal - gp for small-data addressing and sp for the frame - and then leaves.
 * Everything after that is protocore_boot_start() in startup_common.c.
 *
 * Relaxation is turned off across the gp load: the assembler would otherwise rewrite that very
 * instruction to be gp-relative, which cannot work when gp is what is being established.
 *
 * Placed in .protocore_entry, which riscv32.ld.in puts at the start of FLASH.
 */

    .section .protocore_entry, "ax"
    .globl protocore_reset
    .type protocore_reset, @function
    .align 2

protocore_reset:
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop

    la      sp, __protocore_stack_top

    /* No frame to return to, so the call is one-way and the stop below catches a return. */
    call    protocore_boot_start

.Lprotocore_stop:
    j       .Lprotocore_stop

    .size protocore_reset, . - protocore_reset
