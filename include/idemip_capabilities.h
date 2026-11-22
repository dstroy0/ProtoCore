// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file idemip_capabilities.h
 * @brief The capabilities and the counts of idemIP in ProtoCore. Only this header gives the IDEMIP_ENABLE_*
 *        values.
 *
 * idemIP sizes its tables from these values and has no default for the capabilities. The file
 * idemip_config.h of idemIP reads this header from the include path, before the first capability. Each
 * build path of ProtoCore has include/ on its include path. The Arduino IDE gives a library no compile
 * definition, and there this header is the only way to give the values.
 *
 * All five capabilities are on. The padded call of idemIP is off. A compile definition of the same name
 * replaces a value here.
 *
 * The receive buffers of each interface are PROTOCORE_RX_BUFFERS, from vendor/board_profiles/stack_profile.h,
 * the part of the board profile that idemIP reads. The pin budget below keeps IDEMIP_MAX_PINNED_FRAMES under
 * that count, as idemip_config.h asserts. The budget is 8 IPv4 fragments, 4 IPv6 fragments, and 2 segments
 * out of order on each of the 5 TCP connections. With the 4 frames that wait on ARP and the 4 that wait on
 * ND, the budget is 30.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_IDEMIP_CAPABILITIES_H
#define PROTOCORE_IDEMIP_CAPABILITIES_H

#ifndef IDEMIP_ENABLE_ETHERNET
#define IDEMIP_ENABLE_ETHERNET 1
#endif

#ifndef IDEMIP_ENABLE_IPV4
#define IDEMIP_ENABLE_IPV4 1
#endif

#ifndef IDEMIP_ENABLE_IPV6
#define IDEMIP_ENABLE_IPV6 1
#endif

#ifndef IDEMIP_ENABLE_TCP
#define IDEMIP_ENABLE_TCP 1
#endif

#ifndef IDEMIP_ENABLE_UDP
#define IDEMIP_ENABLE_UDP 1
#endif

#ifndef IDEMIP_ENABLE_TIME_DETERMINISM
#define IDEMIP_ENABLE_TIME_DETERMINISM 0
#endif

#ifndef IDEMIP_TIME_DETERMINISM_GRADE
#define IDEMIP_TIME_DETERMINISM_GRADE 2
#endif

#include "vendor/board_profiles/stack_profile.h" // PROTOCORE_RX_BUFFERS: the receive buffers of each interface

#ifndef IDEMIP_RX_DESCRIPTORS
#define IDEMIP_RX_DESCRIPTORS PROTOCORE_RX_BUFFERS
#endif

#ifndef IDEMIP_IP4_REASS_FRAGS
#define IDEMIP_IP4_REASS_FRAGS 8u
#endif

#ifndef IDEMIP_IP6_REASS_FRAGS
#define IDEMIP_IP6_REASS_FRAGS 4u
#endif

#ifndef IDEMIP_TCP_OOSEQ_SEGS
#define IDEMIP_TCP_OOSEQ_SEGS 2u
#endif

#endif // PROTOCORE_IDEMIP_CAPABILITIES_H
