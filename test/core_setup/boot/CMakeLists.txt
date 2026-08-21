# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Bare metal, before main(). The reset entry, the vector table, the .data copy and .bss clear, and
# the freestanding answers a hosted libc would otherwise supply - assert, atomics on a core without
# them, the mem* family, a time source and a trap handler.
#
# ONE MODULE, NOT NINE. These are one arm: an image takes the whole runtime or none of it, and no
# consumer picks among them. test/bare.py is what links it, cross-compiling with -ffreestanding so
# that whatever is still undefined is what bare metal actually owes.
#
# NO GATE, THOUGH ONLY A BARE IMAGE LINKS IT. Every source here compiles on the host - verified,
# including startup_cortex_m.c, whose externs are the linker script's and resolve at link, not at
# compile. An OBJECT library contributes objects only to what links it, so being declared costs a
# host build nothing and being declared is what lets its dependency be checked.

protocore_add_module(test/core_setup/boot
  SOURCES
    protocore_assert.c
    protocore_atomic.c
    protocore_boot.c
    protocore_memfns.c
    protocore_sim_uart.c
    protocore_time.c
    protocore_trap.c
    startup_common.c
    startup_cortex_m.c
  DEPS
    mmgr/protomem
)
