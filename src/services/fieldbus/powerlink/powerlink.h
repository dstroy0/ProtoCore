// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file powerlink.h
 * @brief Ethernet POWERLINK (EPSG) basic frame codec (PC_ENABLE_POWERLINK).
 *
 * Ethernet POWERLINK is the EPSG real-time managed-node bus over raw L2 (ethertype 0x88AB, on the
 * shipped services/fieldbus/rawl2). The Managing Node (MN) runs an isochronous cycle: it multicasts a **SoC**
 * (Start of Cycle), unicasts a **PReq** (Poll Request) to each Controlled Node (CN), each CN answers with
 * a **PRes** (Poll Response) carrying its process data, then an **SoA** (Start of Async) opens the async
 * phase. Every EPL basic frame is:
 *
 *     [messageType : 1][destination node : 1][source node : 1][payload...]
 *
 * This builds and parses those frames (the four cyclic message types + the node addressing), so the MN
 * schedules the cycle and a CN answers with its PRes process image. Pure, zero heap, no stdlib,
 * host-testable; the raw-L2 transmit + the isochronous timing (the preempting-task model) are the device
 * step.
 */

#ifndef PROTOCORE_POWERLINK_H
#define PROTOCORE_POWERLINK_H

#include "protocore_config.h"

#if PC_ENABLE_POWERLINK

/** @brief EPL message types (EPSG DS 301). */
// POWERLINK message types + node ids: wire bytes, so integer constants in a namespacing struct.
#define EPL_MSG_SOC 0x01        ///< Start of Cycle (MN -> all, multicast).
#define EPL_MSG_PREQ 0x03       ///< Poll Request (MN -> CN, unicast).
#define EPL_MSG_PRES 0x04       ///< Poll Response (CN -> all, multicast, carries process data).
#define EPL_MSG_SOA 0x05        ///< Start of Async (MN -> all).
#define EPL_MSG_ASND 0x06       ///< Async Send.
#define EPL_NODE_BROADCAST 0xFF ///< broadcast node id (SoC/SoA destination).
#define EPL_NODE_MN 0xF0        ///< the Managing Node id (240).

/**
 * @brief Build an EPL basic frame: [messageType][dest][source][payload...].
 * @return the frame length (3 + payload_len), or 0 on overflow / bad args.
 */
size_t pc_epl_build(uint8_t msg_type, uint8_t dest, uint8_t source, const uint8_t *payload, size_t payload_len,
                    uint8_t *out, size_t cap);

/** @brief Convenience: build an SoC (MN -> broadcast, no payload). */
size_t pc_epl_soc(uint8_t source, uint8_t *out, size_t cap);

/** @brief Convenience: build a PReq to a CN carrying its output process image. */
size_t pc_epl_preq(uint8_t dest_cn, uint8_t source, const uint8_t *pdo, size_t pdo_len, uint8_t *out, size_t cap);

/** @brief Convenience: build a PRes from a CN carrying its input process image (multicast). */
size_t pc_epl_pres(uint8_t source_cn, const uint8_t *pdo, size_t pdo_len, uint8_t *out, size_t cap);

/** @brief Convenience: build an SoA (MN -> broadcast) that opens the asynchronous phase. @p payload is the
 *  SoA field block (NMT status, requested service id / target, EPL version), or null for a bare invite. */
size_t pc_epl_soa(uint8_t source, const uint8_t *payload, size_t payload_len, uint8_t *out, size_t cap);

/** @brief Convenience: build an ASnd (asynchronous send) from @p source to @p dest. @p payload is the ASnd
 *  service block (service id + service data). ASnd may be unicast to a node or broadcast (0xFF). */
size_t pc_epl_asnd(uint8_t dest, uint8_t source, const uint8_t *payload, size_t payload_len, uint8_t *out, size_t cap);

/** @brief A parsed EPL basic frame (payload points into the input). */
typedef struct
{
    uint8_t msg_type;
    uint8_t dest;
    uint8_t source;
    const uint8_t *payload;
    size_t payload_len;
} EplFrame;

/** @brief Parse an EPL basic frame. @return true if @p len >= 3 and the message type is known. */
proto_bool pc_epl_parse(const uint8_t *frame, size_t len, EplFrame *out);

#endif // PC_ENABLE_POWERLINK
#endif // PROTOCORE_POWERLINK_H
