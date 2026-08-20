// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

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
 *   - wifi_frame_parse(): decode the 802.11 MAC header (type/subtype, the to/from-DS address
 *     layout -> src / dst / bssid, sequence number, header length). Pure.
 *   - pcap_* : build the classic libpcap global + per-record headers (DLT_IEEE802_11) so a
 *     forwarded frame is a valid PCAP stream a wired Wireshark / tcpdump can read. Pure.
 *   - protocore_promisc_begin() / _set_channel() / _end(): monitor-mode bring-up whose rx
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_PROMISC

PROTOCORE_BEGIN_DECLS

// PROTOCORE_PROMISC_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

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
/** @brief What wifi_frame_parse takes: frame, len, out. */
typedef struct
{
    const uint8_t *frame;
    uint16_t len;
    WifiFrameInfo *out;
} PromiscWifiFrameParseArgs;
/** @brief What begin takes: channel, sink. */
typedef struct
{
    uint8_t channel;
    protocore_promisc_sink_fn sink;
} PromiscBeginArgs;
/** @brief What set_channel takes: channel. */
typedef struct
{
    uint8_t channel;
} PromiscSetChannelArgs;
/**
 * @brief Wi-Fi promiscuous (monitor) capture (PROTOCORE_ENABLE_PROMISC) - passive 802.11 sniffing.
 *
 * A caller sets the members a call takes, invokes it through ::Promisc with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Promisc.wifi_frame_parse_args.frame = ...;
 *   Promisc.wifi_frame_parse_args.len = ...;
 *   Promisc.wifi_frame_parse_args.out = ...;
 *   Promisc.wifi_frame_parse(work);
 *   // Promisc.ok is what the call reports
 *
 * @var PromiscNs::wifi_frame_parse_args  what wifi_frame_parse takes: frame, len, out
 * @var PromiscNs::begin_args  what begin takes: channel, sink
 * @var PromiscNs::set_channel_args  what set_channel takes: channel
 * @var PromiscNs::ok  true on success; false if frame is shorter than the header its bits ...
 * @var PromiscNs::wifi_frame_parse  parse an 802.11 MAC header (IEEE 802.11 §9.2 / §9.3.2, the ...
 * @var PromiscNs::begin  start promiscuous capture on channel; every frame is delivered to ...
 * @var PromiscNs::set_channel  retune the capture to a different channel (1..14)
 * @var PromiscNs::end  stop promiscuous capture
 *
 * @c work is PROTOCORE_PROMISC_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    PromiscWifiFrameParseArgs wifi_frame_parse_args;
    PromiscBeginArgs begin_args;
    PromiscSetChannelArgs set_channel_args;
    proto_bool ok;
} PromiscVars;

/** @brief The operands and the outcome. */
extern PromiscVars PromiscV;

/** @brief The entries. */
typedef struct
{
    void (*const wifi_frame_parse)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const set_channel)(uint8_t *restrict work);
    void (*const end)(uint8_t *restrict work);
} PromiscNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in PromiscV or a region of the borrow at a fixed offset.
void protocore_promisc_wifi_frame_parse(uint8_t *restrict work);
void protocore_promisc_begin(uint8_t *restrict work);
void protocore_promisc_set_channel(uint8_t *restrict work);
void protocore_promisc_end(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Promisc.wifi_frame_parse(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const PromiscNs Promisc __attribute__((unused)) = {
    .wifi_frame_parse = protocore_promisc_wifi_frame_parse,
    .begin = protocore_promisc_begin,
    .set_channel = protocore_promisc_set_channel,
    .end = protocore_promisc_end,
};

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

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_PROMISC

#endif // PROTOCORE_PROMISC_H
