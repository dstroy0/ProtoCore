// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mock_platform.c
 * @brief Mock-vendor answers to the platform questions the core asks.
 *
 * The mock vendor is PROTOCORE_HOT running on a machine with no scheduler, so there is one execution
 * context and every comparison agrees. That leaves the pools' owner tripwire inert here, which is the
 * honest answer rather than a suppressed one: with a single context no borrow can cross tasks.
 */

#include "board_drivers/board_profiles/pc_platform.h"

#if PC_VENDOR_MOCK

uintptr_t pc_platform_context_id(void)
{
    return 1; // any nonzero constant: the tripwire treats 0 as "no owner recorded yet"
}

#endif // PC_VENDOR_MOCK
