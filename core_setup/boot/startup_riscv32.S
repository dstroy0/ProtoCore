/* ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The riscv32 entry. Reset jumps here with no register set, so this establishes the two the ABI
 * needs before C is legal - gp for small-data addressing and sp for the frame - points mtvec at a
 * trap catcher, and then leaves. Everything after that is protocore_boot_start() in
 * startup_common.c.
 *
 * Relaxation is turned off across the gp load: the assembler would otherwise rewrite that very
 * instruction to be gp-relative, which cannot work when gp is what is being established.
 *
 * mtvec holds the trap vector base in bits XLEN-1:2 and the mode in bits 1:0 (privileged ISA,
 * Machine Trap-Vector Base-Address Register). Mode 0 is Direct: every trap sets pc to the base.
 * protocore_trap is .align 2, so writing its address leaves both mode bits clear.
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

    la      t0, protocore_trap
    csrw    mtvec, t0

    /* No frame to return to, so the call is one-way and the stop below catches a return. */
    call    protocore_boot_start

.Lprotocore_stop:
    j       .Lprotocore_stop

    .size protocore_reset, . - protocore_reset

    .globl protocore_trap
    .type protocore_trap, @function
    .align 2

/* Every trap lands here. mcause says what it was, mepc where, mtval on what (privileged ISA,
   Machine Cause / Exception Program Counter / Trap Value registers); the three go to the seam in
   protocore_trap.c, and the stop below catches its return. sp is already established, because
   mtvec is written after it. */
protocore_trap:
    csrr    a0, mcause
    csrr    a1, mepc
    csrr    a2, mtval
    call    protocore_platform_trap

.Lprotocore_trap_stop:
    j       .Lprotocore_trap_stop

    .size protocore_trap, . - protocore_trap
