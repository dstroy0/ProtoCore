// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file portable_platform.cpp
 * @brief Platform answers for a target with no RTOS concept of a task - host builds included.
 *
 * Returns a single constant context id. Everything compares equal, so the pools' owner tripwire is
 * inert here rather than raising a false alarm: with one context there is no cross-task borrow to
 * catch. A vendor that DOES have tasks must supply its own, exactly as Espressif does.
 */

#include "board_drivers/board_profiles/pc_platform.h"

#if !PC_VENDOR_ESP

uintptr_t pc_platform_context_id(void)
{
    return 1; // any nonzero constant: the tripwire treats 0 as "no owner recorded yet"
}

#endif // !PC_VENDOR_ESP
