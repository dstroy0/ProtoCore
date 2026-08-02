// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file promisc.h
 * @brief Wi-Fi promiscuous (monitor) capture (PC_ENABLE_PROMISC) - passive 802.11 sniffing.
 *
 * A read-only capture path: instead of joining a network and terminating traffic, listen to
 * every 802.11 frame on a channel and hand it to a sink. The canonical wiring feeds the sink
 * into the forwarding plane (services/net/forward), so captured Wi-Fi frames are bridged to another
 * interface (e.g. Ethernet) for a wired collector - "capture on Wi-Fi, forward to Ethernet".
 *
 * Two host-testable pieces plus the ESP32 radio binding:
 *   - wifi_frame_parse(): decode the 802.11 MAC header (type/subtype, the to/from-DS address
 *     layout -> src / dst / bssid, sequence number, header length). Pure.
 *   - pcap_* : build the classic libpcap global + per-record headers (DLT_IEEE802_11) so a
 *     forwarded frame is a valid PCAP stream a wired Wireshark / tcpdump can read. Pure.
 *   - pc_promisc_begin() / _set_channel() / _end(): monitor-mode bring-up whose rx
 *     callback copies each frame (with RSSI + channel) to the registered sink. ESP32 only.
 *
 * Capture is strictly passive (no injection) and fail-closed: the sink is expected to drop, not
 * block, when its downstream is full, so the live data path is never stalled.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PROMISC_H
#define PROTOCORE_PROMISC_H

#include "protocore_config.h"

#if PC_ENABLE_PROMISC

#include "shared_primitives/pcap.h" // pc_pcap_* framing + PC_DLT_IEEE802_11

/** @brief 802.11 frame type (frame-control bits 2-3). */
typedef enum PROTO_ENUM_PACKED
{
    WIFI_FT_MGMT = 0,
    WIFI_FT_CTRL = 1,
    WIFI_FT_DATA = 2,
    WIFI_FT_EXT = 3,
} WifiFrameType;

/** @brief Decoded 802.11 MAC header. src / dst / bssid point into the frame (6 bytes) or null. */
typedef struct
{
    WifiFrameType type; ///< WifiFrameType
    uint8_t subtype;    ///< 0..15
    proto_bool to_ds;
    proto_bool from_ds;
    proto_bool protected_frame; ///< the Protected-Frame (WEP/CCMP) bit
    proto_bool is_qos;          ///< QoS data subtype (adds 2 header bytes)
    uint16_t seq;               ///< 12-bit sequence number
    uint16_t hdr_len;           ///< MAC header length (bytes)
    const uint8_t *dst;         ///< destination (receiver) MAC, per the to/from-DS layout
    const uint8_t *src;         ///< source (transmitter) MAC
    const uint8_t *bssid;       ///< BSSID (null for a WDS 4-address frame)
} WifiFrameInfo;

/**
 * @brief Parse an 802.11 MAC header (IEEE 802.11 §9.2 / §9.3.2, the to/from-DS address rules).
 * @return true on success; false if @p frame is shorter than the header its bits imply.
 */
proto_bool wifi_frame_parse(const uint8_t *frame, uint16_t len, WifiFrameInfo *out);

// libpcap framing lives in shared_primitives/pcap.h: pc_pcap_global_header() with
// PC_DLT_IEEE802_11 + pc_pcap_record_header() wrap a captured 802.11 frame as a valid PCAP.

/**
 * @brief Sink for one captured frame: the raw 802.11 bytes plus radio metadata.
 * @param frame   the 802.11 MAC frame (points into the driver buffer; copy if retained).
 * @param len     frame length in bytes.
 * @param rssi    received signal strength (dBm).
 * @param channel the channel it was captured on.
 */
typedef void (*pc_promisc_sink_fn)(const uint8_t *frame, uint16_t len, int8_t rssi, uint8_t channel);

/**
 * @brief Start promiscuous capture on @p channel; every frame is delivered to @p sink.
 *
 * Requires the radio to be up (init_wifi_physical() or init_wifi_radio_physical()).
 * Returns immediately.
 * @return true if capture started; false if @p sink is null or on host builds.
 */
proto_bool pc_promisc_begin(uint8_t channel, pc_promisc_sink_fn sink);

/** @brief Retune the capture to a different channel (1..14). */
void pc_promisc_set_channel(uint8_t channel);

/** @brief Stop promiscuous capture. */
void pc_promisc_end(void);

#endif // PC_ENABLE_PROMISC

#endif // PROTOCORE_PROMISC_H
