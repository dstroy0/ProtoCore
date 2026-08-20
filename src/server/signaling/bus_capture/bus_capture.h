// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file bus_capture.h
 * @brief Wired field-bus listen-only capture (PROTOCORE_ENABLE_BUS_CAPTURE) - passive CAN sniffing.
 *
 * The wired counterpart to the Wi-Fi promiscuous tap: put the CAN controller in
 * **listen-only** mode - it receives and decodes every frame on the bus but never ACKs or
 * transmits, so it is invisible to the other nodes - and hand each frame to a sink. Wire the sink
 * into the forwarding plane (network_drivers/network/forward) to bridge captured CAN frames to another interface
 * (e.g. stream them to a wired collector over Ethernet), exactly like the Wi-Fi capture path.
 *
 * The pure piece is can_to_socketcan(): format a ::CanFrame as a 16-byte Linux **SocketCAN**
 * frame, which with the libpcap DLT_CAN_SOCKETCAN link type (shared/pcap/pcap.h) is a
 * capture Wireshark opens directly. The controller bring-up (listen-only) is the platform's
 * only and needs a CAN transceiver on the bus.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_BUS_CAPTURE_H
#define PROTOCORE_BUS_CAPTURE_H

#include "shared/can/can.h"   // the complete type a public struct below holds by value

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_BUS_CAPTURE

PROTOCORE_BEGIN_DECLS

// PROTOCORE_BUS_CAPTURE_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

#define PROTOCORE_SOCKETCAN_FRAME_LEN 16

#define PROTOCORE_CAN_EFF_FLAG 0x80000000u ///< extended (29-bit) identifier

#define PROTOCORE_CAN_RTR_FLAG 0x40000000u ///< remote-transmission-request frame

#define PROTOCORE_CAN_ERR_FLAG 0x20000000u ///< error message frame

/** @brief Sink for one captured CAN frame (already decoded into a ::CanFrame). */
typedef void (*bus_capture_sink_fn)(const CanFrame *frame);


/** @brief What can_to_socketcan takes: f, out, cap. */
typedef struct
{
    const CanFrame *f;
    uint8_t *out;
    size_t cap;
} BusCaptureCanToSocketcanArgs;

/** @brief What begin takes: tx_pin, rx_pin, bitrate, sink. */
typedef struct
{
    int tx_pin;
    int rx_pin;
    uint32_t bitrate; ///< bus bit rate (125000, 250000, 500000, or 1000000)
    bus_capture_sink_fn sink;
} BusCaptureBeginArgs;

/**
 * @brief Wired field-bus listen-only capture (PROTOCORE_ENABLE_BUS_CAPTURE) - passive CAN sniffing. The wired ...
 *
 * A caller sets the members a call takes, invokes it through ::BusCapture with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   BusCapture.can_to_socketcan_args.f = ...;
 *   BusCapture.can_to_socketcan_args.out = ...;
 *   BusCapture.can_to_socketcan_args.cap = ...;
 *   BusCapture.can_to_socketcan(work);
 *   // BusCapture.n is what the call reports
 *
 * @var BusCaptureNs::can_to_socketcan_args  what can_to_socketcan takes: f, out, cap
 * @var BusCaptureNs::begin_args  what begin takes: tx_pin, rx_pin, bitrate, sink
 * @var BusCaptureNs::ok  true if the driver installed and started; false on a bad bit rate, ...
 * @var BusCaptureNs::n  ::PROTOCORE_SOCKETCAN_FRAME_LEN, or 0 if out is null / cap is too ...
 * @var BusCaptureNs::can_to_socketcan  format f as a 16-byte Linux SocketCAN frame (for a ...
 * @var BusCaptureNs::begin  install the CAN controller in listen-only mode and start capturing. ...
 * @var BusCaptureNs::poll  drain any received frames, calling the sink for each. Call from ...
 * @var BusCaptureNs::end  stop capture and release the controller
 *
 * @c work is PROTOCORE_BUS_CAPTURE_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    BusCaptureCanToSocketcanArgs can_to_socketcan_args;
    BusCaptureBeginArgs begin_args;

    proto_bool ok;
    size_t n;

    void (*const can_to_socketcan)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const poll)(uint8_t *restrict work);
    void (*const end)(uint8_t *restrict work);
} BusCaptureNs;

/** @brief The one symbol this module exports. */
extern BusCaptureNs BusCapture;

/**
 * @brief The PROTOCORE_BUS_CAPTURE_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_bus_capture_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_BUS_CAPTURE

#endif // PROTOCORE_BUS_CAPTURE_H
