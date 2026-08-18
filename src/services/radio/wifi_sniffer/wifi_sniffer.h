// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wifi_sniffer.h
 * @brief 802.11 frame decode + traffic tally + RSSI roaming decision (PROTOCORE_ENABLE_WIFI_SNIFFER).
 *
 * The ESP32 can run its WiFi MAC in promiscuous mode and hand raw 802.11 frames to a callback. Turning
 * those into a useful sniffer / traffic analyzer / RF-diagnostics panel means decoding the 802.11 MAC
 * header (frame control type/subtype + flags, and the three addresses whose roles - receiver /
 * transmitter / BSSID - depend on the ToDS/FromDS bits), tallying frames by type, and, for
 * channel-agility roaming, deciding when a candidate AP is enough stronger than the current one to switch.
 *
 * This is that pure decode + decision layer; the promiscuous-mode radio callback belongs to the app. No
 * heap, no stdlib, host-testable against captured frame bytes.
 */

#ifndef PROTOCORE_WIFI_SNIFFER_H
#define PROTOCORE_WIFI_SNIFFER_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_WIFI_SNIFFER

PROTOCORE_BEGIN_DECLS

// PROTOCORE_WIFI_SNIFFER_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

/** @brief 802.11 frame type (Frame Control bits 2-3). */
#define WIFI_TYPE_MGMT 0 ///< management (beacon, probe, auth, assoc, ...).
#define WIFI_TYPE_CTRL 1 ///< control (RTS/CTS/ACK, ...).
#define WIFI_TYPE_DATA 2 ///< data.
#define WIFI_TYPE_EXT 3  ///< extension.

/** @brief Sentinel for "no frame heard yet" in WifiChannelSurvey::best_rssi. */
#define PROTOCORE_WIFI_RSSI_NONE (-128)

/** @brief A decoded 802.11 MAC header. Addresses not present for the frame's length are left zeroed. */
typedef struct
{
    uint8_t version;            ///< protocol version (FC bits 0-1).
    uint8_t type;               ///< WIFI_TYPE_*.
    uint8_t subtype;            ///< FC bits 4-7.
    proto_bool to_ds;           ///< to the distribution system.
    proto_bool from_ds;         ///< from the distribution system.
    proto_bool retry;           ///< retransmission.
    proto_bool protected_frame; ///< the Protected Frame (WEP/WPA) flag.
    uint8_t naddr;              ///< number of addresses decoded (1..3).
    uint8_t addr1[6];           ///< receiver / destination (role varies by DS bits).
    uint8_t addr2[6];           ///< transmitter / source (present when naddr >= 2).
    uint8_t addr3[6];           ///< BSSID / source / dest (present when naddr >= 3).
} WifiFrame;

/** @brief Running per-type frame tally. */
typedef struct
{
    uint32_t mgmt;
    uint32_t ctrl;
    uint32_t data;
    uint32_t other;
    uint32_t total;
} WifiStats;

/** @brief Channel-hop schedule across [chan_first, chan_last]. */
typedef struct
{
    uint8_t chan_first;   ///< first channel of the sweep (1..14)
    uint8_t chan_last;    ///< last channel of the sweep (>= chan_first)
    uint8_t channel;      ///< channel currently dwelt on
    uint16_t dwell_ms;    ///< dwell per channel
    uint32_t last_hop_ms; ///< when the current dwell started
    uint32_t sweeps;      ///< completed wraps back to chan_first
} WifiScan;

/** @brief What was heard on one channel during the survey. */
typedef struct
{
    uint32_t frames;       ///< frames decoded on this channel
    int8_t best_rssi;      ///< strongest RSSI seen (dBm); PROTOCORE_WIFI_RSSI_NONE if nothing heard
    uint8_t best_bssid[6]; ///< transmitter of the strongest frame
} WifiChannelSurvey;

/** @brief Survey across the scanned channel range (index 0 == @c first). */
typedef struct
{
    WifiChannelSurvey ch[PROTOCORE_WIFI_SNIFFER_MAX_CHANNELS];
    uint8_t first; ///< channel represented by ch[0]
    uint8_t count; ///< channels tracked (<= PROTOCORE_WIFI_SNIFFER_MAX_CHANNELS)
} WifiSurvey;

/** @brief What parse takes: frame, len, out. */
typedef struct
{
    const uint8_t *frame;
    size_t len;
    WifiFrame *out;
} WifiSnifferParseArgs;

/** @brief What stats_reset takes: s. */
typedef struct
{
    WifiStats *s;
} WifiSnifferStatsResetArgs;

/** @brief What stats_add takes: s, f. */
typedef struct
{
    WifiStats *s;
    const WifiFrame *f;
} WifiSnifferStatsAddArgs;

/** @brief What should_roam takes: cur_rssi, cand_rssi, hysteresis_db. */
typedef struct
{
    int8_t cur_rssi;
    int8_t cand_rssi;
    uint8_t hysteresis_db;
} WifiSnifferShouldRoamArgs;

/** @brief What scan_init takes: s, first, last, dwell_ms, now_ms. */
typedef struct
{
    WifiScan *s;
    uint8_t first;
    uint8_t last;
    uint16_t dwell_ms;
    uint32_t now_ms;
} WifiSnifferScanInitArgs;

/** @brief What scan_due takes: s, now_ms. */
typedef struct
{
    const WifiScan *s;
    uint32_t now_ms;
} WifiSnifferScanDueArgs;

/** @brief What scan_next takes: s, now_ms. */
typedef struct
{
    WifiScan *s;
    uint32_t now_ms;
} WifiSnifferScanNextArgs;

/** @brief What survey_reset takes: s, first, count. */
typedef struct
{
    WifiSurvey *s;
    uint8_t first;
    uint8_t count;
} WifiSnifferSurveyResetArgs;

/** @brief What survey_add takes: s, channel, rssi, f. */
typedef struct
{
    WifiSurvey *s;
    uint8_t channel;
    int8_t rssi;
    const WifiFrame *f;
} WifiSnifferSurveyAddArgs;

/** @brief What survey_get takes: s, channel. */
typedef struct
{
    const WifiSurvey *s;
    uint8_t channel;
} WifiSnifferSurveyGetArgs;

/** @brief What survey_best takes: s, exclude_channel, out_channel, ... */
typedef struct
{
    const WifiSurvey *s;
    uint8_t exclude_channel;
    uint8_t *out_channel;
    int8_t *out_rssi;
} WifiSnifferSurveyBestArgs;

/** @brief What begin takes: first_chan, last_chan, dwell_ms. */
typedef struct
{
    uint8_t first_chan;
    uint8_t last_chan;
    uint16_t dwell_ms;
} WifiSnifferBeginArgs;

/**
 * @brief 802.11 frame decode + traffic tally + RSSI roaming decision (PROTOCORE_ENABLE_WIFI_SNIFFER).
 *
 * A caller sets the members a call takes, invokes it through ::WifiSniffer with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   WifiSniffer.parse_args.frame = ...;
 *   WifiSniffer.parse_args.len = ...;
 *   WifiSniffer.parse_args.out = ...;
 *   WifiSniffer.parse(work);
 *   // WifiSniffer.ok is what the call reports
 *
 * @var WifiSnifferNs::parse_args  what parse takes: frame, len, out
 * @var WifiSnifferNs::stats_reset_args  what stats_reset takes: s
 * @var WifiSnifferNs::stats_add_args  what stats_add takes: s, f
 * @var WifiSnifferNs::should_roam_args  what should_roam takes: cur_rssi, cand_rssi, hysteresis_db
 * @var WifiSnifferNs::scan_init_args  what scan_init takes: s, first, last, dwell_ms, now_ms
 * @var WifiSnifferNs::scan_due_args  what scan_due takes: s, now_ms
 * @var WifiSnifferNs::scan_next_args  what scan_next takes: s, now_ms
 * @var WifiSnifferNs::survey_reset_args  what survey_reset takes: s, first, count
 * @var WifiSnifferNs::survey_add_args  what survey_add takes: s, channel, rssi, f
 * @var WifiSnifferNs::survey_get_args  what survey_get takes: s, channel
 * @var WifiSnifferNs::survey_best_args  what survey_best takes: s, exclude_channel, out_channel,
 * @var WifiSnifferNs::begin_args  what begin takes: first_chan, last_chan, dwell_ms
 * @var WifiSnifferNs::ok  true if len >= 10 and frame is non-null; false otherwise
 * @var WifiSnifferNs::value  the new channel, or 0 if s is null
 * @var WifiSnifferNs::ptr  the pointer a call reports
 * @var WifiSnifferNs::stats_out   the running per-type tally the sniff fills
 * @var WifiSnifferNs::survey_out  the per-channel survey the sniff fills
 * @var WifiSnifferNs::scan_out    the channel-hop schedule the sniff is running
 * @var WifiSnifferNs::parse  decode the 802.11 MAC header of a captured frame. Requires at least ...
 * @var WifiSnifferNs::stats_reset  zero a tally
 * @var WifiSnifferNs::stats_add  fold one decoded frame into the tally
 * @var WifiSnifferNs::should_roam  channel-agility roaming decision
 * @var WifiSnifferNs::scan_init  start a sweep at first, dwelling dwell_ms per channel. Clamps to ...
 * @var WifiSnifferNs::scan_due  true once the current channel's dwell has elapsed (wrap-safe ...
 * @var WifiSnifferNs::scan_next  advance to the next channel (wrapping to chan_first and counting a ...
 * @var WifiSnifferNs::survey_reset  clear the survey to track count channels starting at first
 * @var WifiSnifferNs::survey_add  fold one captured frame (on channel, at rssi) into the survey. ...
 * @var WifiSnifferNs::survey_get  the survey entry for channel, or nullptr if it is outside the ...
 * @var WifiSnifferNs::survey_best  find the strongest channel heard, ignoring exclude_channel (pass 0 ...
 * @var WifiSnifferNs::begin  start a live channel-hopping sniff across [first_chan, last_chan]. ...
 * @var WifiSnifferNs::tick  hop to the next channel when the dwell has elapsed. Cheap to call ...
 * @var WifiSnifferNs::end  stop capture
 * @var WifiSnifferNs::stats  the running traffic tally (never null)
 * @var WifiSnifferNs::survey  the per-channel survey (never null)
 * @var WifiSnifferNs::scan  the live scan schedule (never null) - current channel, sweeps ...
 *
 * @c work is PROTOCORE_WIFI_SNIFFER_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    WifiSnifferParseArgs parse_args;
    WifiSnifferStatsResetArgs stats_reset_args;
    WifiSnifferStatsAddArgs stats_add_args;
    WifiSnifferShouldRoamArgs should_roam_args;
    WifiSnifferScanInitArgs scan_init_args;
    WifiSnifferScanDueArgs scan_due_args;
    WifiSnifferScanNextArgs scan_next_args;
    WifiSnifferSurveyResetArgs survey_reset_args;
    WifiSnifferSurveyAddArgs survey_add_args;
    WifiSnifferSurveyGetArgs survey_get_args;
    WifiSnifferSurveyBestArgs survey_best_args;
    WifiSnifferBeginArgs begin_args;

    proto_bool ok;
    uint8_t value;
    const WifiChannelSurvey *ptr;
    const WifiStats *stats_out;
    const WifiSurvey *survey_out;
    const WifiScan *scan_out;

    void (*const parse)(uint8_t *restrict work);
    void (*const stats_reset)(uint8_t *restrict work);
    void (*const stats_add)(uint8_t *restrict work);
    void (*const should_roam)(uint8_t *restrict work);
    void (*const scan_init)(uint8_t *restrict work);
    void (*const scan_due)(uint8_t *restrict work);
    void (*const scan_next)(uint8_t *restrict work);
    void (*const survey_reset)(uint8_t *restrict work);
    void (*const survey_add)(uint8_t *restrict work);
    void (*const survey_get)(uint8_t *restrict work);
    void (*const survey_best)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const tick)(uint8_t *restrict work);
    void (*const end)(uint8_t *restrict work);
    void (*const stats)(uint8_t *restrict work);
    void (*const survey)(uint8_t *restrict work);
    void (*const scan)(uint8_t *restrict work);
} WifiSnifferNs;

/** @brief The one symbol this module exports. */
extern WifiSnifferNs WifiSniffer;

/**
 * @brief The PROTOCORE_WIFI_SNIFFER_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_wifi_sniffer_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_WIFI_SNIFFER

#endif // PROTOCORE_WIFI_SNIFFER_H
