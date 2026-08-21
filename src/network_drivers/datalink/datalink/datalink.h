// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_DATALINK_H
#define PROTOCORE_DATALINK_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file datalink.h
 * @brief Layer 2 (Data Link) - the LINK LAYER of RFC 1122 sec 2.
 *
 * RFC 1122 sec 2.3.3 "Ethernet and IEEE 802 Encapsulation" states what a host on the cable does
 * with a frame: it MUST send and receive RFC 894 encapsulation (RFC 894 "Frame Format": Ether-Type
 * 0x0800, a data field of 46 to 1500 octets, zero-padded to the minimum), SHOULD receive RFC 1042
 * encapsulation intermixed with it (802.2 LLC/SNAP, K1=170), and MUST NOT send 802 packets using
 * K1=6. RFC 1122 sec 2.4 fixes the interface this layer presents upward: a receive carries the
 * link-layer-broadcast flag, a send carries the 5-bit TOS field.
 *
 * The platform's own link driver and IP stack port perform all of it - MAC framing, medium access,
 * address resolution - and this module is the seam a target with direct MAC-level access extends.
 * Its one call only reports the layer up.
 *
 * The module exports one symbol, @ref Datalink. Everything in datalink.c has internal linkage.
 *
 * No argument members: init takes none.
 * No storage member: the layer holds nothing of its own, so there is no state to hand out.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_DATALINK_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*init)(uint8_t *restrict);
} DatalinkNs;
PROTOCORE_NS_LAYOUT(DatalinkNs, init);

/**
 * @brief Bring the layer up: sets ok. The platform's link driver performs.
 * @param work PROTOCORE_DATALINK_BORROW bytes the caller took. Not held past the call.
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_datalink_init(uint8_t *restrict work);

/** @brief Module namespace. */
PROTOCORE_NS DatalinkNs Datalink PROTOCORE_UNUSED = {.init = protocore_datalink_init};

PROTOCORE_END_DECLS

#endif // PROTOCORE_DATALINK_H
