// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mii.h
 * @brief The MII management register set, IEEE 802.3 Clause 22.
 *
 * Clause 22.2.4 fixes registers 0 through 15 for every PHY; 16 through 31 are the vendor's. A
 * management frame carries 16 bits of data, so every register here is 16 bits wide.
 *
 * Written as masks and shifts rather than bitfields: the bit order of a C bitfield is
 * implementation-defined, so a struct that matches on one toolchain silently reverses on another.
 */

#ifndef PROTOCORE_IDEMIP_MII_H
#define PROTOCORE_IDEMIP_MII_H

#include "idemIP/ethernet/ethernet.h"

PROTOCORE_BEGIN_DECLS

/** @brief PHY addresses a management interface can reach (Clause 22.2.4.5: 5 bits). */
#define IDEMIP_MII_PHY_ADDR_MAX 32u

/** @brief Registers per PHY (Clause 22.2.4: 5 bits of register address). */
#define IDEMIP_MII_REG_MAX 32u

// ---------------------------------------------------------------------------
// Register addresses (Clause 22.2.4)
// ---------------------------------------------------------------------------

#define IDEMIP_MII_BMCR 0x00u    ///< Basic Mode Control (22.2.4.1)
#define IDEMIP_MII_BMSR 0x01u    ///< Basic Mode Status (22.2.4.2)
#define IDEMIP_MII_PHYID1 0x02u  ///< PHY Identifier 1 (22.2.4.3)
#define IDEMIP_MII_PHYID2 0x03u  ///< PHY Identifier 2 (22.2.4.3)
#define IDEMIP_MII_ANAR 0x04u    ///< Auto-Negotiation Advertisement (22.2.4.4)
#define IDEMIP_MII_ANLPAR 0x05u  ///< Auto-Negotiation Link Partner Ability (22.2.4.5)
#define IDEMIP_MII_ANER 0x06u    ///< Auto-Negotiation Expansion (22.2.4.6)
#define IDEMIP_MII_MMDCTRL 0x0Du ///< MMD Access Control (22.2.4.14)
#define IDEMIP_MII_MMDAD 0x0Eu   ///< MMD Access Address/Data (22.2.4.15)

// ---------------------------------------------------------------------------
// Basic Mode Control, register 0 (Clause 22.2.4.1)
// ---------------------------------------------------------------------------

#define IDEMIP_BMCR_RESET (1u << 15)         ///< Reset the PHY; self-clearing.
#define IDEMIP_BMCR_LOOPBACK (1u << 14)      ///< Route transmit data to the receive path.
#define IDEMIP_BMCR_SPEED_100 (1u << 13)     ///< Speed select LSB: 100 Mb/s when set, 10 when clear.
#define IDEMIP_BMCR_ANEG_ENABLE (1u << 12)   ///< Enable auto-negotiation.
#define IDEMIP_BMCR_POWER_DOWN (1u << 11)    ///< Power down all but the management interface.
#define IDEMIP_BMCR_ISOLATE (1u << 10)       ///< Isolate the PHY from the MII.
#define IDEMIP_BMCR_ANEG_RESTART (1u << 9)   ///< Restart auto-negotiation; self-clearing.
#define IDEMIP_BMCR_FULL_DUPLEX (1u << 8)    ///< Full duplex when set, half when clear.
#define IDEMIP_BMCR_COLLISION_TEST (1u << 7) ///< Enable the collision test.
#define IDEMIP_BMCR_SPEED_1000 (1u << 6)     ///< Speed select MSB; with bit 13 selects 1000 Mb/s.

// ---------------------------------------------------------------------------
// Basic Mode Status, register 1 (Clause 22.2.4.2)
// ---------------------------------------------------------------------------

#define IDEMIP_BMSR_100BASE_T4 (1u << 15)      ///< 100BASE-T4 able.
#define IDEMIP_BMSR_100BASE_TX_FD (1u << 14)   ///< 100BASE-X full duplex able.
#define IDEMIP_BMSR_100BASE_TX_HD (1u << 13)   ///< 100BASE-X half duplex able.
#define IDEMIP_BMSR_10BASE_T_FD (1u << 12)     ///< 10 Mb/s full duplex able.
#define IDEMIP_BMSR_10BASE_T_HD (1u << 11)     ///< 10 Mb/s half duplex able.
#define IDEMIP_BMSR_100BASE_T2_FD (1u << 10)   ///< 100BASE-T2 full duplex able.
#define IDEMIP_BMSR_100BASE_T2_HD (1u << 9)    ///< 100BASE-T2 half duplex able.
#define IDEMIP_BMSR_EXT_STATUS (1u << 8)       ///< Extended status in register 15.
#define IDEMIP_BMSR_MF_PREAMBLE_SUPP (1u << 6) ///< Accepts management frames with preamble suppressed.
#define IDEMIP_BMSR_ANEG_COMPLETE (1u << 5)    ///< Auto-negotiation process completed.
#define IDEMIP_BMSR_REMOTE_FAULT (1u << 4)     ///< Remote fault detected; latching.
#define IDEMIP_BMSR_ANEG_ABLE (1u << 3)        ///< Able to auto-negotiate.
#define IDEMIP_BMSR_LINK_UP (1u << 2)          ///< Link is up; latching low.
#define IDEMIP_BMSR_JABBER (1u << 1)           ///< Jabber condition detected; latching.
#define IDEMIP_BMSR_EXT_CAPABILITY (1u << 0)   ///< Extended register set present.

// ---------------------------------------------------------------------------
// Auto-Negotiation Advertisement, register 4 (Clause 22.2.4.4)
// ---------------------------------------------------------------------------
// The technology ability field carries the same bit positions in the link partner's register 5,
// which is what makes a negotiated result readable as the AND of the two.

#define IDEMIP_ANAR_NEXT_PAGE (1u << 15)    ///< Next page able.
#define IDEMIP_ANAR_REMOTE_FAULT (1u << 13) ///< Signal a remote fault to the link partner.
#define IDEMIP_ANAR_PAUSE_ASYM (1u << 11)   ///< Asymmetric pause.
#define IDEMIP_ANAR_PAUSE (1u << 10)        ///< Symmetric pause.
#define IDEMIP_ANAR_100BASE_T4 (1u << 9)
#define IDEMIP_ANAR_100BASE_TX_FD (1u << 8)
#define IDEMIP_ANAR_100BASE_TX_HD (1u << 7)
#define IDEMIP_ANAR_10BASE_T_FD (1u << 6)
#define IDEMIP_ANAR_10BASE_T_HD (1u << 5)
#define IDEMIP_ANAR_SELECTOR_802_3 0x0001u ///< Selector field: IEEE 802.3.

PROTOCORE_END_DECLS

#endif // PROTOCORE_IDEMIP_MII_H
