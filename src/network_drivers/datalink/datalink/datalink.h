// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

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
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DATALINK_H
#define PROTOCORE_DATALINK_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

/**
 * @brief The data link layer (RFC 1122 sec 2 "LINK LAYER").
 *
 * A caller invokes a call through ::Datalink and reads the outcome off the same handle.
 *
 * @var DatalinkNs::ok        PROTO_TRUE once @ref init has run
 * @var DatalinkNs::init      bring the layer up: sets @ref ok. The platform's link driver performs
 *                            every RFC 1122 sec 2.3.3 encapsulation step.
 *
 * No argument members: init takes none.
 * No storage member: the layer holds nothing of its own, so there is no state to hand out.
 */
typedef struct
{
    proto_bool ok; ///< PROTO_TRUE once init has run
} DatalinkVars;

/** @brief The operands and the outcome. */
extern DatalinkVars DatalinkV;

/** @brief The entries. */
typedef struct
{
    void (*const init)(uint8_t *restrict work);
} DatalinkNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in DatalinkV or a region of the borrow at a fixed offset.
void protocore_datalink_init(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Datalink.init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const DatalinkNs Datalink __attribute__((unused)) = {
    .init = protocore_datalink_init,
};

PROTOCORE_END_DECLS

#endif
