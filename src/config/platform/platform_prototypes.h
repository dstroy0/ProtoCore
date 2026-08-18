// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file platform_prototypes.h
 * @brief Platform prototypes for the ProtoCore library.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PLATFORM_PROTOTYPES_H
#define PROTOCORE_PLATFORM_PROTOTYPES_H

// ---------------------------------------------------------------------------
// Execution context identity
// ---------------------------------------------------------------------------
//
// "Which execution context is running me" is a platform question, not a core one, so the core asks
// here instead of naming an RTOS. Used by the pools' debug owner tripwire to catch a borrow crossing
// tasks; it is only ever compared for equality, never interpreted.
//
// Returns 0 where there is no such concept (host builds): a single context, so every comparison
// trivially agrees and the tripwire is a no-op rather than a false alarm.
uintptr_t protocore_platform_context_id(void);

#endif // PROTOCORE_PLATFORM_PROTOTYPES_H
