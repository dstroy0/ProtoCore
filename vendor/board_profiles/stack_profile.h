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

// sdkconfig.h names the die, and it holds only definitions. Its absence on the host leaves each count below at
// its default.
#if defined(__has_include)
#if __has_include("sdkconfig.h")
#include "sdkconfig.h" // CONFIG_IDF_TARGET_ESP32: the classic die
#endif
#endif

// The classic ESP32 has the smallest ceiling of static data in the ESP family. It carries 2 TCP connections. It
// takes 8 receive buffers and 2 transmit buffers, and a pin budget of 7 below the receive count: 2 IPv4
// fragments, 1 IPv6 fragment, 1 segment out of order on each of the 2 TCP connections, 1 frame that waits on ARP
// and 1 that waits on ND.
#if defined(CONFIG_IDF_TARGET_ESP32)
#ifndef IDEMIP_TCP_PCBS
#define IDEMIP_TCP_PCBS 2u
#endif
#ifndef PROTOCORE_RX_BUFFERS
#define PROTOCORE_RX_BUFFERS 8u
#endif
#ifndef IDEMIP_TX_DESCRIPTORS
#define IDEMIP_TX_DESCRIPTORS 2u
#endif
#ifndef IDEMIP_IP4_REASS_FRAGS
#define IDEMIP_IP4_REASS_FRAGS 2u
#endif
#ifndef IDEMIP_IP6_REASS_FRAGS
#define IDEMIP_IP6_REASS_FRAGS 1u
#endif
#ifndef IDEMIP_TCP_OOSEQ_SEGS
#define IDEMIP_TCP_OOSEQ_SEGS 1u
#endif
#ifndef IDEMIP_ARP_PENDING
#define IDEMIP_ARP_PENDING 1u
#endif
#ifndef IDEMIP_ND6_PENDING
#define IDEMIP_ND6_PENDING 1u
#endif
#endif

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
