// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file platform_defines.h
 * @brief Platform defines for the ProtoCore library.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// The stored image's state, in the order a rollback walks it. These values are the library's own;
// each arm maps its vendor's onto them, so a caller reads one set whatever it is running on.
#define PROTOCORE_PLATFORM_IMG_NEW 0
#define PROTOCORE_PLATFORM_IMG_PENDING_VERIFY 1
#define PROTOCORE_PLATFORM_IMG_VALID 2
#define PROTOCORE_PLATFORM_IMG_INVALID 3
#define PROTOCORE_PLATFORM_IMG_ABORTED 4
#define PROTOCORE_PLATFORM_IMG_UNDEFINED 0xFF
