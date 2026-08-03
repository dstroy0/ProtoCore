// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file portable_platform.c
 * @brief Platform answers for the test build, which has no RTOS concept of a task.
 *
 * Returns a single constant context id. Everything compares equal, so the pools' owner tripwire is
 * inert here rather than raising a false alarm: with one context there is no cross-task borrow to
 * catch. A vendor that DOES have tasks must supply its own, exactly as Espressif does.
 */

#include "board_drivers/board_profiles/pc_platform.h"

// "Which execution context is running me" is answered by whoever owns the scheduler, and on the hot
// path that is always the vendor. Keying this on the path means a vendor whose backend was never
// written is an undefined reference rather than a silent wrong answer.
#if PROTOCORE_HOST

uintptr_t pc_platform_context_id(void)
{
    return 1; // any nonzero constant: the tripwire treats 0 as "no owner recorded yet"
}

#endif // PROTOCORE_HOST
