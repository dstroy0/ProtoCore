// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file phy.h
 * @brief The PHY contract: what a link driver supplies, and what the layers above it may ask.
 *
 * This names the seam only. The MII management register map (IEEE 802.3 Clause 22) and every
 * vendor register live with the binding in core_setup/, which is where vendor headers are
 * segregated; nothing here includes one or encodes a register number.
 */

#ifndef PROTOCORE_IDEMIP_PHY_H
#define PROTOCORE_IDEMIP_PHY_H

#include "idemIP/ethernet/mii.h"

PROTOCORE_BEGIN_DECLS

/** @brief Negotiated line rate, in megabits per second. */
typedef enum PROTO_ENUM_PACKED
{
    IDEMIP_PHY_SPEED_NONE = 0, ///< no link
    IDEMIP_PHY_SPEED_10 = 10,
    IDEMIP_PHY_SPEED_100 = 100,
    IDEMIP_PHY_SPEED_1000 = 1000,
} IdemIpPhySpeed;

/** @brief What the link reports about itself. */
typedef struct
{
    IdemIpPhySpeed speed;   ///< negotiated rate; NONE while the link is down.
    proto_bool full_duplex; ///< true when the link negotiated full duplex.
    proto_bool up;          ///< true once the link is usable.
} IdemIpPhyLink;

/**
 * @brief What a link driver supplies.
 *
 * Every call is from the driver's own context. A frame handed to @ref send is the caller's until
 * the call returns; a driver that transmits asynchronously copies or holds it itself.
 *
 * @var IdemIpPhyDriver::link   the link's current state
 * @var IdemIpPhyDriver::send   put one Ethernet II frame on the wire; false when it could not be queued
 * @var IdemIpPhyDriver::recv   take the next received frame into @p out; 0 when none is waiting
 * @var IdemIpPhyDriver::mac    this interface's 48-bit address (RFC 894)
 * @var IdemIpPhyDriver::mdio_read   read one MII management register (IEEE 802.3 Clause 22)
 * @var IdemIpPhyDriver::mdio_write  write one MII management register (IEEE 802.3 Clause 22)
 */
typedef struct
{
    IdemIpPhyLink (*link)(void);
    proto_bool (*send)(const uint8_t *frame, size_t len);
    size_t (*recv)(uint8_t *out, size_t cap);
    const uint8_t *(*mac)(void);
    // Clause 22 management: the driver owns the MDC/MDIO timing, this layer owns what the registers
    // mean (mii.h). Both address fields are 5 bits on the wire, so both are bounded here.
    proto_bool (*mdio_read)(uint8_t phy_addr, uint8_t reg, uint16_t *out);
    proto_bool (*mdio_write)(uint8_t phy_addr, uint8_t reg, uint16_t val);
} IdemIpPhyDriver;

/**
 * @brief Bind the link driver. One interface for now; a second would take an index.
 * @return false when @p drv is null or incomplete.
 */
proto_bool idemip_phy_bind(const IdemIpPhyDriver *drv);

/** @brief The bound driver, or null when none is bound. */
const IdemIpPhyDriver *idemip_phy_driver(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_IDEMIP_PHY_H
