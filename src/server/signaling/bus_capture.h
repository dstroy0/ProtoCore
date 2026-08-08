// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file bus_capture.h
 * @brief Wired field-bus listen-only capture (PC_ENABLE_BUS_CAPTURE) - passive CAN sniffing.
 *
 * The wired counterpart to the Wi-Fi promiscuous tap: put the CAN (TWAI) controller in
 * **listen-only** mode - it receives and decodes every frame on the bus but never ACKs or
 * transmits, so it is invisible to the other nodes - and hand each frame to a sink. Wire the sink
 * into the forwarding plane (network_drivers/network/forward) to bridge captured CAN frames to another interface
 * (e.g. stream them to a wired collector over Ethernet), exactly like the Wi-Fi capture path.
 *
 * The pure piece is can_to_socketcan(): format a ::CanFrame as a 16-byte Linux **SocketCAN**
 * frame, which with the libpcap DLT_CAN_SOCKETCAN link type (shared_primitives/pcap.h) is a
 * capture Wireshark opens directly. The TWAI bring-up (`driver/twai.h`, listen-only) is ESP32
 * only and needs a CAN transceiver on the bus.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_BUS_CAPTURE_H
#define PROTOCORE_BUS_CAPTURE_H

#include "protocore_config.h"

#if PC_ENABLE_BUS_CAPTURE

#include "shared_primitives/can.h"  // CanFrame
#include "shared_primitives/pcap.h" // PC_DLT_CAN_SOCKETCAN

/** @brief A Linux SocketCAN classic frame is 16 bytes on the wire (and in a PCAP record). */
#define PC_SOCKETCAN_FRAME_LEN 16

/** @brief SocketCAN can_id flag bits, set in the top of the big-endian can_id word. */
#define PC_CAN_EFF_FLAG 0x80000000u ///< extended (29-bit) identifier
#define PC_CAN_RTR_FLAG 0x40000000u ///< remote-transmission-request frame
#define PC_CAN_ERR_FLAG 0x20000000u ///< error message frame

/**
 * @brief Format @p f as a 16-byte Linux SocketCAN frame (for a `PC_DLT_CAN_SOCKETCAN` PCAP).
 *
 * `can_id` (big-endian) = the identifier ORed with EFF / RTR flags; then the data length, three
 * reserved octets, and eight data octets (RTR frames carry no data).
 * @return ::PC_SOCKETCAN_FRAME_LEN, or 0 if @p out is null / @p cap is too small.
 */
size_t can_to_socketcan(const CanFrame *f, uint8_t *out, size_t cap);

/** @brief Sink for one captured CAN frame (already decoded into a ::CanFrame). */
typedef void (*bus_capture_sink_fn)(const CanFrame *frame);

/**
 * @brief Install the TWAI (CAN) controller in listen-only mode and start capturing.
 *
 * Listen-only means the controller never sends an ACK or a frame, so it does not disturb the bus.
 * @param tx_pin,rx_pin the GPIOs wired to the CAN transceiver.
 * @param bitrate       bus bit rate (125000, 250000, 500000, or 1000000).
 * @return true if the driver installed and started; false on a bad bit rate, a driver error, or a
 *         host build.
 */
proto_bool bus_capture_begin(int tx_pin, int rx_pin, uint32_t bitrate, bus_capture_sink_fn sink);

/** @brief Drain any received frames, calling the sink for each. Call from loop(). */
void bus_capture_poll(void);

/** @brief Stop capture and release the TWAI driver. */
void bus_capture_end(void);

#endif // PC_ENABLE_BUS_CAPTURE

#endif // PROTOCORE_BUS_CAPTURE_H
