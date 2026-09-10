// repotools-stamp: code/code_verify/test/lanes.c 1cb20bfe7606914b
/* repo_tools - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
 * SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
 */
/**
 * @file lanes.c
 * @brief A fixture for verify_asm: one function that must vectorize and one that must not call out.
 * @author dstroy0 (Douglas Quigg) <dquigg123@gmail.com>
 * @date 2026-09-09
 *
 * @note This exists to be compiled and disassembled, never to be linked or run. It is the smallest
 *       thing that lets the checker be checked: a case that should pass, a case that should fail on
 *       a missing instruction, and a case that should fail on a forbidden one.
 * @note Kept free of any project header, because a fixture that needs a repository's own includes
 *       could only be built inside that repository.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**
 * @brief Counts matching 32 bit words, written so a vector target has something to widen.
 *
 * @param[in] left  First run [BORROWS].
 * @param[in] right Second run [BORROWS].
 * @param[in] count How many words.
 * @return          How many positions hold the same word.
 * @note No early exit and no branch inside the loop, which is what lets a compiler turn the body
 *       into a compare and a horizontal add instead of a scalar test per word.
 */
size_t lanes_matching(const uint32_t *left, const uint32_t *right, size_t count)
{
    size_t held = 0u;
    for (size_t at = 0u; at < count; at++)
    {
        held += (size_t)(left[at] == right[at]);
    }
    return held;
}

/**
 * @brief Copies a run word by word, as the case a `forbid` rule is written against.
 *
 * @param[out] into  Destination [BORROWS].
 * @param[in]  from  Source [BORROWS].
 * @param[in]  count How many words.
 * @note A compiler is entitled to replace this whole loop with a call to memcpy, and at -O2 it
 *       usually does. That is the substitution a hand written kernel does not want and cannot see
 *       from the source, which is why `forbid` exists.
 */
void lanes_copy(uint32_t *into, const uint32_t *from, size_t count)
{
    for (size_t at = 0u; at < count; at++)
    {
        into[at] = from[at];
    }
}
