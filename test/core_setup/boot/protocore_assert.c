// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_assert.c
 * @brief What a failed assert() reaches on a target with no C library behind it.
 *
 * assert() is used in mmgr/plaintext.c, mmgr/secure.c and across crypto/. The macro expands to a
 * call into the toolchain's failure hook, which every target measured here spells __assert_func and
 * which newlib implements by printing to stderr and calling abort - neither of which exists on a
 * bare-metal image. Defining it here is what makes the link resolve, and it hands the four facts
 * the macro captured to a seam a board can override.
 *
 * The default stops. An assert that failed means an invariant the code below it relies on is
 * already false, so continuing runs code whose preconditions do not hold.
 */

#include "test/core_setup/boot/protocore_boot.h"

/**
 * @brief Where a failed assertion lands: the file, the line, the enclosing function, the text.
 *
 * Weak and stopping, so a board overrides it by defining the name and may log, mark the image bad,
 * or reset instead. An override that returns still stops, in __assert_func below.
 */
__attribute__((weak)) void protocore_platform_assert_failed(const char *file, int line, const char *func,
                                                            const char *expr)
{
    (void)file;
    (void)line;
    (void)func;
    (void)expr;
}

/** @brief The hook assert() expands to. Hands the failure to the seam, then stops regardless. */
__attribute__((noreturn)) void __assert_func(const char *file, int line, const char *func, const char *expr)
{
    protocore_platform_assert_failed(file, line, func, expr);
    for (;;)
    {
    }
}
