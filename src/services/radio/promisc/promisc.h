// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_PROMISC_H
#define PROTOCORE_PROMISC_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file promisc.h
 * @brief Wi-Fi promiscuous (monitor) capture (PROTOCORE_ENABLE_PROMISC) - passive 802.11 sniffing.
 *
 * A read-only capture path: instead of joining a network and terminating traffic, listen to
 * every 802.11 frame on a channel and hand it to a sink. The canonical wiring feeds the sink
 * into the forwarding plane (network_drivers/network/forward), so captured Wi-Fi frames are bridged to another
 * interface (e.g. Ethernet) for a wired collector - "capture on Wi-Fi, forward to Ethernet".
 *
 * Two host-testable pieces plus the ESP32 radio binding:
 * - wifi_frame_parse(): decode the 802.11 MAC header (type/subtype, the to/from-DS address
 * layout -> src / dst / bssid, sequence number, header length). Pure.
 * - pcap_* : build the classic libpcap global + per-record headers (DLT_IEEE802_11) so a
 * forwarded frame is a valid PCAP stream a wired Wireshark / tcpdump can read. Pure.
 * - protocore_promisc_begin() / _set_channel() / _end(): monitor-mode bring-up whose rx
 * callback copies each frame (with RSSI + channel) to the registered sink. ESP32 only.
 *
 * Capture is strictly passive (no injection) and fail-closed: the sink is expected to drop, not
 * block, when its downstream is full, so the live data path is never stalled.
 *
 * @c work is PROTOCORE_PROMISC_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

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
 * @brief Sink for one captured frame: the raw 802.11 bytes plus radio metadata.
 * @param frame   the 802.11 MAC frame (points into the driver buffer; copy if retained).
 * @param len     frame length in bytes.
 * @param rssi    received signal strength (dBm).
 * @param channel the channel it was captured on.
 */
typedef void (*protocore_promisc_sink_fn)(const uint8_t *frame, uint16_t len, int8_t rssi, uint8_t channel);

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*wifi_frame_parse)(uint8_t *restrict, const uint8_t *, uint16_t, WifiFrameInfo *);
    proto_bool (*begin)(uint8_t *restrict, uint8_t, protocore_promisc_sink_fn);
    void (*set_channel)(uint8_t *restrict, uint8_t);
    void (*end)(uint8_t *restrict);
} PromiscNs;
PROTOCORE_NS_LAYOUT(PromiscNs, wifi_frame_parse, begin, set_channel, end);

/**
 * @brief Parse an 802.11 MAC header (IEEE 802.11 §9.2 / §9.3.2, the .
 * @param work PROTOCORE_PROMISC_BORROW bytes the caller took. Not held past the call.
 * @param frame Frame
 * @param len Len
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_promisc_wifi_frame_parse(uint8_t *restrict work, const uint8_t *frame, uint16_t len,
                                              WifiFrameInfo *out);
/**
 * @brief Start promiscuous capture on channel; every frame is delivered to .
 * @param work PROTOCORE_PROMISC_BORROW bytes the caller took. Not held past the call.
 * @param channel Channel
 * @param sink Sink
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_promisc_begin(uint8_t *restrict work, uint8_t channel, protocore_promisc_sink_fn sink);
/**
 * @brief Retune the capture to a different channel (1..14).
 * @param work PROTOCORE_PROMISC_BORROW bytes the caller took. Not held past the call.
 * @param channel Channel
 */
void protocore_promisc_set_channel(uint8_t *restrict work, uint8_t channel);
/**
 * @brief Stop promiscuous capture.
 * @param work PROTOCORE_PROMISC_BORROW bytes the caller took. Not held past the call.
 */
void protocore_promisc_end(uint8_t *restrict work);

/**
 * @brief Sink for one captured frame: the raw 802.11 bytes plus radio metadata.
 * @param frame   the 802.11 MAC frame (points into the driver buffer; copy if retained).
 * @param len     frame length in bytes.
 * @param rssi    received signal strength (dBm).
 * @param channel the channel it was captured on.
 */
typedef void (*protocore_promisc_sink_fn)(const uint8_t *frame, uint16_t len, int8_t rssi, uint8_t channel);
/**
 * @brief The PROTOCORE_PROMISC_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_promisc_span(void);

/** @brief Module namespace. */
PROTOCORE_NS PromiscNs Promisc PROTOCORE_UNUSED = {.wifi_frame_parse = protocore_promisc_wifi_frame_parse,
                                                   .begin = protocore_promisc_begin,
                                                   .set_channel = protocore_promisc_set_channel,
                                                   .end = protocore_promisc_end};

PROTOCORE_END_DECLS

#endif // PROTOCORE_PROMISC_H
