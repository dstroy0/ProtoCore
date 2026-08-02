// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file can.h
 * @brief Shared CAN 2.0 frame type for the CAN-based industrial codecs (one source of truth).
 *
 * CANopen, J1939, DeviceNet, and NMEA 2000 all ride classic CAN frames (an 11-bit or
 * 29-bit identifier plus up to 8 data octets). They share this one frame struct so the
 * frame shape lives in a single place; each codec only builds and parses the id + data,
 * never the wire bus itself.
 *
 * The physical CAN controller is the application's: the ESP32's built-in TWAI peripheral
 * with a transceiver (e.g. SN65HVD230), or an external MCP2515 over SPI. The app hands a
 * received `CanFrame` to a codec's parser, or transmits a `CanFrame` a builder filled in.
 * That keeps these codecs pure and host-testable, exactly like the serial / TCP codecs.
 *
 * Header-only, not feature-gated: each CAN codec includes it behind its own
 * `PC_ENABLE_*` guard.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CAN_H
#define PROTOCORE_CAN_H

#include "protocore_config.h" // the entry point: types.h for the widths and PC_INLINE

#define PC_CAN_MAX_DLC 8               ///< classic CAN carries at most 8 data octets.
#define PC_CAN_STD_ID_MASK 0x7FFu      ///< 11-bit standard identifier.
#define PC_CAN_EXT_ID_MASK 0x1FFFFFFFu ///< 29-bit extended identifier.

/**
 * @brief One classic CAN 2.0 frame.
 *
 * @var CanFrame::id        11-bit (standard) or 29-bit (extended) identifier, right-aligned.
 * @var CanFrame::extended  true => the id is a 29-bit extended identifier.
 * @var CanFrame::rtr       true => remote-transmission-request frame (carries no data).
 * @var CanFrame::dlc       data length, 0..8.
 * @var CanFrame::data      payload octets; only the first @ref CanFrame::dlc are valid.
 */
typedef struct
{
    uint32_t id;
    proto_bool extended;
    proto_bool rtr;
    uint8_t dlc;
    uint8_t data[PC_CAN_MAX_DLC];
} CanFrame;

#endif // PROTOCORE_CAN_H
