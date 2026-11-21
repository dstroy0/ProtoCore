// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file stack_profile.h
 * @brief The counts of the idemIP stack (PROTOCORE_ENABLE_IDEMIP), which the sources of idemIP and of ProtoCore
 *        both read.
 *
 * The board profile reads this header through classic_defaults.h. The header idemip_capabilities.h reads it
 * alone, because the rest of the board profile reaches the vendor headers. Set a count here or with a compile
 * definition. Do not set it in a chip, PSRAM or flash file, because idemIP does not read those files.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_STACK_PROFILE_H
#define PROTOCORE_STACK_PROFILE_H

/**
 * @brief The receive buffers of each interface, each one frame of IDEMIP_DMA_BUF_STRIDE bytes. A power of two.
 *
 * @note Each buffer is one loculus of an MMgr ring. The count is thus MMGR_RING_LOCULI at most.
 * @note The count is above IDEMIP_MAX_PINNED_FRAMES. include/idemip_capabilities.h sets the pin budget below it.
 */
#ifndef PROTOCORE_RX_BUFFERS
#define PROTOCORE_RX_BUFFERS 32u
#endif

#endif // PROTOCORE_STACK_PROFILE_H
