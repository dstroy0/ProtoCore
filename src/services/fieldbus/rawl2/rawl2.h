// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rawl2.h
 * @brief Raw Layer-2 Ethernet frame codec (PC_ENABLE_RAWL2).
 *
 * The host-testable core of raw-L2 frame TX/RX: build and parse Ethernet II frames (and 802.1Q
 * VLAN-tagged frames) so the app can inject/receive arbitrary L2 frames - the basis for the raw-L2
 * industrial protocols (PROFINET DCP, IEC 61850 GOOSE, POWERLINK, SERCOS) and for custom management /
 * proprietary MAC framing. On device the bytes go out through the vendor L2 transmit path
 * (wired or Wi-Fi); the MAC normally appends the FCS, so the builder emits the frame
 * without it and `pc_eth_fcs` is provided for the cases that need it.
 *
 *   Ethernet II:  [dst MAC 6][src MAC 6][ethertype 2][payload]
 *   802.1Q:       [dst 6][src 6][0x8100][TCI 2][ethertype 2][payload]
 *
 * Pure, zero heap, no stdlib, host-testable.
 */

#ifndef PROTOCORE_RAWL2_H
#define PROTOCORE_RAWL2_H

#include "protocore_config.h"

#if PC_ENABLE_RAWL2

// Ethernet II framing sizes + ethertypes: wire values, so integer constants in a namespacing struct.
#define ETH_ALEN 6            ///< MAC address length.
#define ETH_HDR_LEN 14        ///< dst + src + ethertype.
#define ETH_VLAN_HDR_LEN 18   ///< with the 4-octet 802.1Q tag.
#define ETH_TPID_8021Q 0x8100 ///< 802.1Q tag protocol id.
#define V4 0x0800
#define ETHERTYPE_ARP 0x0806
#define ETHERTYPE_PROFINET 0x8892 ///< PROFINET RT / DCP.
#define ETHERTYPE_GOOSE 0x88B8    ///< IEC 61850 GOOSE.
#define ETHERTYPE_POWERLINK 0x88AB

/**
 * @brief Build an Ethernet II frame (no FCS).
 * @return the frame length (14 + payload_len), or 0 if it won't fit or a pointer is null.
 */
size_t pc_eth_build(const uint8_t *dst, const uint8_t *src, uint16_t ethertype, const uint8_t *payload,
                    size_t payload_len, uint8_t *out, size_t cap);

/**
 * @brief Build an 802.1Q VLAN-tagged Ethernet frame (no FCS).
 * @param pcp   priority code point (0..7).
 * @param dei   drop-eligible indicator.
 * @param vid   VLAN id (0..4095).
 * @return the frame length (18 + payload_len), or 0 on overflow.
 */
size_t pc_eth_build_vlan(const uint8_t *dst, const uint8_t *src, uint8_t pcp, proto_bool dei, uint16_t vid,
                         uint16_t ethertype, const uint8_t *payload, size_t payload_len, uint8_t *out, size_t cap);

/** @brief A parsed Ethernet frame (pointers into the input). */
typedef struct
{
    const uint8_t *dst;
    const uint8_t *src;
    proto_bool vlan;
    uint8_t pcp;
    uint16_t vid;
    uint16_t ethertype;
    const uint8_t *payload;
    size_t payload_len;
} EthFrame;

/** @brief Parse an Ethernet II / 802.1Q frame (FCS not expected). @return true if well-formed. */
proto_bool pc_eth_parse(const uint8_t *frame, size_t len, EthFrame *out);

/** @brief IEEE 802.3 frame check sequence (CRC-32, reflected, init 0xFFFFFFFF, xorout 0xFFFFFFFF). */
uint32_t pc_eth_fcs(const uint8_t *bytes, size_t len);

#endif // PC_ENABLE_RAWL2
#endif // PROTOCORE_RAWL2_H
