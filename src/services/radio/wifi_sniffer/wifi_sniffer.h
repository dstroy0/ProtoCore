// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wifi_sniffer.h
 * @brief 802.11 frame decode + traffic tally + RSSI roaming decision (PC_ENABLE_WIFI_SNIFFER).
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

#include "protocore_config.h"

#if PC_ENABLE_WIFI_SNIFFER

/** @brief 802.11 frame type (Frame Control bits 2-3). */
#define WIFI_TYPE_MGMT 0 ///< management (beacon, probe, auth, assoc, ...).
#define WIFI_TYPE_CTRL 1 ///< control (RTS/CTS/ACK, ...).
#define WIFI_TYPE_DATA 2 ///< data.
#define WIFI_TYPE_EXT 3  ///< extension.

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

/**
 * @brief Decode the 802.11 MAC header of a captured frame.
 *
 * Requires at least the Frame Control + Duration + Address1 (10 bytes); Address2 is decoded at >= 16
 * bytes and Address3 at >= 24 bytes, with @p out->naddr reporting how many were present.
 * @return true if @p len >= 10 and @p frame is non-null; false otherwise.
 */
proto_bool pc_wifi_parse(const uint8_t *frame, size_t len, WifiFrame *out);

/** @brief Running per-type frame tally. */
typedef struct
{
    uint32_t mgmt;
    uint32_t ctrl;
    uint32_t data;
    uint32_t other;
    uint32_t total;
} WifiStats;

/** @brief Zero a tally. */
void pc_wifi_stats_reset(WifiStats *s);

/** @brief Fold one decoded frame into the tally. */
void pc_wifi_stats_add(WifiStats *s, const WifiFrame *f);

/**
 * @brief Channel-agility roaming decision.
 * @return true if @p cand_rssi exceeds @p cur_rssi by more than @p hysteresis_db (so a switch is worth it).
 */
proto_bool pc_wifi_should_roam(int8_t cur_rssi, int8_t cand_rssi, uint8_t hysteresis_db);

// --- Channel-hop scan schedule ----------------------------------------------------------
//
// A survey walks a channel range, dwelling on each long enough to hear its traffic, then wraps.
// The schedule is pure (wrap-safe time math on pc_millis()); driving the radio is the binding
// below.

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

/** @brief Start a sweep at @p first, dwelling @p dwell_ms per channel. Clamps to 1..14 and first<=last. */
void pc_wifi_scan_init(WifiScan *s, uint8_t first, uint8_t last, uint16_t dwell_ms, uint32_t now_ms);

/** @brief True once the current channel's dwell has elapsed (wrap-safe against a millis rollover). */
proto_bool pc_wifi_scan_due(const WifiScan *s, uint32_t now_ms);

/**
 * @brief Advance to the next channel (wrapping to chan_first and counting a sweep) and restart the dwell.
 * @return the new channel, or 0 if @p s is null.
 */
uint8_t pc_wifi_scan_next(WifiScan *s, uint32_t now_ms);

// --- Per-channel RSSI survey ------------------------------------------------------------

/** @brief What was heard on one channel during the survey. */
typedef struct
{
    uint32_t frames;       ///< frames decoded on this channel
    int8_t best_rssi;      ///< strongest RSSI seen (dBm); PC_WIFI_RSSI_NONE if nothing heard
    uint8_t best_bssid[6]; ///< transmitter of the strongest frame
} WifiChannelSurvey;

/** @brief Sentinel for "no frame heard yet" in WifiChannelSurvey::best_rssi. */
#define PC_WIFI_RSSI_NONE (-128)

/** @brief Survey across the scanned channel range (index 0 == @c first). */
typedef struct
{
    WifiChannelSurvey ch[PC_WIFI_SNIFFER_MAX_CHANNELS];
    uint8_t first; ///< channel represented by ch[0]
    uint8_t count; ///< channels tracked (<= PC_WIFI_SNIFFER_MAX_CHANNELS)
} WifiSurvey;

/** @brief Clear the survey to track @p count channels starting at @p first. */
void pc_wifi_survey_reset(WifiSurvey *s, uint8_t first, uint8_t count);

/** @brief Fold one captured frame (on @p channel, at @p rssi) into the survey. Out-of-range channels are ignored. */
void pc_wifi_survey_add(WifiSurvey *s, uint8_t channel, int8_t rssi, const WifiFrame *f);

/** @brief The survey entry for @p channel, or nullptr if it is outside the tracked range. */
const WifiChannelSurvey *pc_wifi_survey_get(const WifiSurvey *s, uint8_t channel);

/**
 * @brief Find the strongest channel heard, ignoring @p exclude_channel (pass 0 to exclude nothing).
 * @return true if any channel had traffic; @p out_channel / @p out_rssi are then set.
 */
proto_bool pc_wifi_survey_best(const WifiSurvey *s, uint8_t exclude_channel, uint8_t *out_channel, int8_t *out_rssi);

#if PC_ENABLE_PROMISC
// --- Live capture binding (ESP32) -------------------------------------------------------
//
// Drives the promiscuous-capture owner (services/radio/promisc) rather than installing a second radio
// callback: pc_promisc_begin() delivers each frame to an internal sink that decodes it
// (pc_wifi_parse), tallies it (pc_wifi_stats_add), and folds it into the survey. Call
// pc_wifi_sniffer_tick() from the loop to hop channels on the dwell schedule.
//
// Concurrency: the sink runs in the Wi-Fi driver's callback context, so the stats and survey are
// updated underneath a reader. They are deliberately lock-free - the accessors below hand back a
// live snapshot that may be a frame or two ahead of a value read a moment earlier, which is the
// right trade for a passive diagnostics panel (a lock in the radio callback would risk stalling
// capture). Counters are aligned 32-bit and RSSI is a byte, so a reader sees whole values, never
// torn ones; do not treat a report as an instantaneous consistent cut across channels.

/**
 * @brief Start a live channel-hopping sniff across [first_chan, last_chan].
 *
 * Requires the radio to be up (init_wifi_physical() or init_wifi_radio_physical()).
 * Resets the stats + survey.
 * @return true if capture started.
 */
proto_bool pc_wifi_sniffer_begin(uint8_t first_chan, uint8_t last_chan, uint16_t dwell_ms);

/** @brief Hop to the next channel when the dwell has elapsed. Cheap to call every loop. */
void pc_wifi_sniffer_tick(void);

/** @brief Stop capture. */
void pc_wifi_sniffer_end(void);

/** @brief The running traffic tally (never null). */
const WifiStats *pc_wifi_sniffer_stats(void);

/** @brief The per-channel survey (never null). */
const WifiSurvey *pc_wifi_sniffer_survey(void);

/** @brief The live scan schedule (never null) - current channel, sweeps completed. */
const WifiScan *pc_wifi_sniffer_scan(void);
#endif // PC_ENABLE_PROMISC

#endif // PC_ENABLE_WIFI_SNIFFER
#endif // PROTOCORE_WIFI_SNIFFER_H
