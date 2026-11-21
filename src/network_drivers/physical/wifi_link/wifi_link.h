// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wifi_link.h
 * @brief Layer 1 (Physical) - the 802.11 station link under the idemIP stack (PROTOCORE_ENABLE_IDEMIP).
 *
 * This module is the IdemIpPhyDriver of one Wi-Fi station. The stack module binds it as an interface
 * with ::protocore_wifi_link_driver and the two buffer arrays of this module.
 *
 * The receive ring is software. The radio gives each frame to ::protocore_wifi_link_frame_receive in
 * its own task. That call puts the frame into a free buffer of the ring. Where no buffer is free, the
 * link drops the frame and counts it.
 *
 * The entry rx_claim gives the frames out in arrival order. The entry rx_release names the buffer that
 * it gives back, and only that buffer becomes free again. A unit can thus keep a frame past later
 * frames, and no refill lands on its buffer.
 *
 * The radio copies a transmit frame before its send returns. A transmit buffer is thus free again at
 * the reap of the next tick.
 *
 * The radio is a seam: protocore_wifi_radio_start, protocore_wifi_radio_frame_send and
 * protocore_wifi_radio_mac_read. On an ESP part, test/core_setup/hal/esp/esp_wifi_radio.c answers it
 * with esp_wifi, and no esp_netif. Elsewhere the weak default of each one refuses.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_WIFI_LINK_H
#define PROTOCORE_WIFI_LINK_H

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_IDEMIP

#include "src/ethernet/phy.h" // IdemIpPhyDriver, the table that the stack binds

PROTOCORE_BEGIN_DECLS

/**
 * @brief What ::protocore_wifi_link_start takes: the network that the station joins.
 *
 * @var WifiLinkStartArgs::ssid     the SSID, a string of 1 to 32 octets [BORROWS]
 * @var WifiLinkStartArgs::password the passphrase, a string of 8 to 63 octets, or empty for an open network [BORROWS]
 */
typedef struct
{
    const char *ssid;
    const char *password;
} WifiLinkStartArgs;

/**
 * @brief One frame, as the radio gives it or takes it: an Ethernet II frame with no frame check sequence.
 *
 * @var WifiLinkFrameArgs::frame the first octet of the frame [BORROWS]
 * @var WifiLinkFrameArgs::len   octets in the frame
 */
typedef struct
{
    const uint8_t *frame;
    size_t len;
} WifiLinkFrameArgs;

/**
 * @brief What ::protocore_wifi_link_state_set takes.
 *
 * @var WifiLinkStateArgs::up PROTO_TRUE once the station joins its network, and PROTO_FALSE after it leaves
 */
typedef struct
{
    proto_bool up;
} WifiLinkStateArgs;

/**
 * @brief Starts the radio as a station, and joins the network of @p args.
 *
 * @param[in] args The network [BORROWS].
 * @return         PROTO_TRUE where the radio started. The association comes later, through
 *                 ::protocore_wifi_link_state_set.
 */
proto_bool protocore_wifi_link_start(const WifiLinkStartArgs *args);

/**
 * @brief Puts one received frame into a free buffer of the receive ring. The radio calls it in its own task.
 *
 * @param[in] args The frame [BORROWS]. The call copies it with memor, which needs the frame on a word boundary.
 * @return         PROTO_FALSE where the frame is not on a word boundary, where it is longer than
 *                 IDEMIP_DMA_FRAME_MAX, or where no buffer is free. The link then drops the frame and counts it
 *                 in ::protocore_wifi_link_dropped.
 */
proto_bool protocore_wifi_link_frame_receive(const WifiLinkFrameArgs *args);

/**
 * @brief Records whether the station is in its network. The radio calls it in its own task.
 *
 * @param[in] args The state [BORROWS].
 */
void protocore_wifi_link_state_set(const WifiLinkStateArgs *args);

/** @brief The IdemIpPhyDriver of the station, for StackBindArgs::driver. */
const IdemIpPhyDriver *protocore_wifi_link_driver(void);

/** @brief The IDEMIP_RX_DESCRIPTORS receive buffers, for StackBindArgs::rx_base. */
uint8_t *protocore_wifi_link_rx_base(void);

/** @brief The IDEMIP_TX_DESCRIPTORS transmit buffers, for StackBindArgs::tx_base. */
uint8_t *protocore_wifi_link_tx_base(void);

/** @brief The frames that the receive ring dropped since the start. */
uint32_t protocore_wifi_link_dropped(void);

/**
 * @brief The radio seam: starts the station and joins the network of @p args. The weak default refuses.
 *
 * @param[in] args The network [BORROWS].
 * @return         PROTO_TRUE where the radio started.
 */
proto_bool protocore_wifi_radio_start(const WifiLinkStartArgs *args);

/**
 * @brief The radio seam: sends one frame. The radio copies the frame before it returns. The weak default refuses.
 *
 * @param[in] args The frame [BORROWS].
 * @return         PROTO_TRUE where the radio took the frame.
 */
proto_bool protocore_wifi_radio_frame_send(const WifiLinkFrameArgs *args);

/**
 * @brief The radio seam: reads the hardware address of the station. The weak default refuses.
 *
 * @param[out] mac IDEMIP_MAC_LEN octets.
 * @return         PROTO_TRUE where @p mac holds the address.
 */
proto_bool protocore_wifi_radio_mac_read(uint8_t *mac);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_IDEMIP

#endif // PROTOCORE_WIFI_LINK_H
