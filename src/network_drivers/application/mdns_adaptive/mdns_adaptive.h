// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mdns_adaptive.h
 * @brief Adaptive mDNS beacon scheduling: RF-aware backoff, TTL refresher, auto-sleep beacon
 *        (PC_ENABLE_MDNS_ADAPTIVE).
 *
 * The mDNS service (shipped) announces records with a TTL; caches on the network evict a record when its
 * TTL lapses, so a device must re-announce to stay discoverable. Two pressures shape *when* to announce:
 *
 *  - **Crowded RF**: on a busy 2.4 GHz channel, hammering announces just adds collisions. So back the
 *    announce interval off (toward a ceiling) when contention is high, and recover it toward the nominal
 *    cadence when the air is quiet.
 *  - **A continuous refresher**: re-announce at ~half the record TTL (RFC 6762 cache eviction is at TTL)
 *    so caches never lapse in steady state.
 *  - **Auto-sleep beacons**: before entering a sleep window that would run past the next refresh, announce
 *    *now* so the record survives the sleep instead of lapsing while the radio is off.
 *
 * These are the pure scheduling decisions - what interval, and is an announce due (incl. before a sleep).
 * The app owns the actual mDNS transmit. Wrap-safe time math, no heap, no stdlib, host-testable.
 */

#ifndef PROTOCORE_MDNS_ADAPTIVE_H
#define PROTOCORE_MDNS_ADAPTIVE_H

#include "protocore_config.h"

#if PC_ENABLE_MDNS_ADAPTIVE

/** @brief Adaptive beacon state. */
typedef struct
{
    uint32_t base_ms;   ///< nominal refresh cadence (e.g. TTL/2), and the backoff floor.
    uint32_t max_ms;    ///< backoff ceiling under heavy contention.
    uint32_t cur_ms;    ///< current adaptive interval.
    uint16_t hi_thresh; ///< contention count at/above which the interval backs off.
} MdnsBeacon;

/** @brief The continuous-refresher cadence for a record TTL: half the TTL, in milliseconds. */
uint32_t pc_mdns_refresh_interval(uint32_t ttl_s);

/** @brief Initialize a beacon. @p cur_ms starts at @p base_ms. */
void pc_mdns_beacon_init(MdnsBeacon *b, uint32_t base_ms, uint32_t max_ms, uint16_t hi_thresh);

/**
 * @brief Adapt the interval to observed RF contention (announces/collisions seen in the last window).
 *        contention >= hi_thresh doubles the interval (capped at max_ms); contention == 0 halves it back
 *        toward base_ms; moderate contention holds.
 * @return the new interval (ms).
 */
uint32_t pc_mdns_beacon_adapt(MdnsBeacon *b, uint16_t contention);

/** @brief Is an announce due now? (wrap-safe: elapsed since @p last_ms >= the current interval). */
proto_bool pc_mdns_beacon_due(const MdnsBeacon *b, uint32_t last_ms, uint32_t now_ms);

/**
 * @brief Auto-sleep beacon: should we announce *before* sleeping for @p sleep_ms?
 *        True when the elapsed-since-last plus the sleep would meet/exceed the interval - i.e. the record
 *        would lapse mid-sleep - so we refresh proactively before the radio goes off.
 */
proto_bool pc_mdns_beacon_presleep_due(const MdnsBeacon *b, uint32_t last_ms, uint32_t now_ms, uint32_t sleep_ms);

// ---------------------------------------------------------------------------
// Contention sampling
// ---------------------------------------------------------------------------

/**
 * @brief Turns a free-running frame counter into a per-window contention value.
 *
 * The RF-contention signal is "how many 802.11 frames went by in the last window". A promiscuous
 * capture only offers a monotonic running total, so this converts that total into a delta over a
 * fixed window and clamps it to the uint16 the adapt step takes. Pure and wrap-safe (the counter and
 * the clock both wrap), so the whole sampling policy is host-testable with synthetic inputs.
 */
typedef struct
{
    uint32_t last_count; ///< frame-counter value at the last emitted sample.
    uint32_t last_ms;    ///< time of the last emitted sample.
    uint32_t window_ms;  ///< how long a sampling window is.
} MdnsContentionWindow;

/** @brief Start sampling at @p now_ms, anchored to the current counter @p frames_now. */
void pc_mdns_contention_init(MdnsContentionWindow *w, uint32_t window_ms, uint32_t frames_now, uint32_t now_ms);

/**
 * @brief If a window has elapsed, report the frames counted in it and start the next window.
 *
 * @param frames_now the current running frame total (monotonic; a wrap is handled).
 * @param out        receives the frame count for the window (saturated at 0xFFFF).
 * @return true when a window closed and @p out was written; false if the window is not up yet.
 */
proto_bool pc_mdns_contention_sample(MdnsContentionWindow *w, uint32_t frames_now, uint32_t now_ms, uint16_t *out);

#if PROTOCORE_HOT && PC_ENABLE_MDNS && PC_ENABLE_PROMISC
// ---------------------------------------------------------------------------
// Device binding (needs PC_ENABLE_MDNS + PC_ENABLE_PROMISC)
// ---------------------------------------------------------------------------
//
// Ties the three shipped pieces together on hardware: a promiscuous capture pinned to the station's
// own channel supplies the contention count, the beacon scheduler turns that into an announce
// interval, and the announce is a TXT re-apply on the running mDNS responder (which re-announces on
// every PCB with no goodbye and no re-probe, so it refreshes the record rather than evicting it).
//
// Promiscuous capture is a radio-layer callback, not a socket, so unlike a second bind on UDP 5353
// it does not turn the responder announce-only - verified on hardware (the record keeps resolving in
// avahi while the capture runs). It is still the whole radio: this owns promiscuous mode, so it
// cannot run at the same time as the wifi_sniffer live channel-hopping binding.

/** @brief What to advertise and how aggressively to adapt. */
typedef struct
{
    const char *key;          ///< TXT key re-applied to re-announce (must already exist on the service).
    const char *value;        ///< its value (re-applied unchanged; this is the no-bye refresh).
    uint32_t ttl_s;           ///< record TTL; the base cadence is TTL/2.
    uint32_t max_interval_ms; ///< requested backoff ceiling; capped at ~7/8 of the TTL so the most
                              ///< backed-off refresh still beats cache eviction (a longer TTL buys range).
    uint16_t hi_contention;   ///< frames-per-window at/above which the interval backs off.
    uint32_t window_ms;       ///< contention sampling window (0 => a 1000 ms default).
} MdnsAdaptiveCfg;

/**
 * @brief Start adaptive announcing: begin promiscuous capture on the station's current channel and
 *        arm the beacon. Call after the mDNS service is up and the station is associated.
 * @return false if the station is not associated or promiscuous capture could not start.
 */
proto_bool pc_mdns_adaptive_begin(const MdnsAdaptiveCfg *cfg);

/**
 * @brief Advance the schedule: sample contention, adapt the interval, follow the station's channel
 *        if it roamed, and re-announce when due. Cheap; call every loop.
 */
void pc_mdns_adaptive_tick(void);

/** @brief Stop adaptive announcing and release promiscuous mode. */
void pc_mdns_adaptive_end(void);

/** @brief Current adaptive announce interval (ms) - for a diagnostics panel. */
uint32_t pc_mdns_adaptive_interval_ms(void);

/** @brief Frames counted in the most recently closed window - the live contention signal. */
uint16_t pc_mdns_adaptive_contention(void);

/** @brief Total announces sent since begin(). */
uint32_t pc_mdns_adaptive_announces(void);
#endif // PROTOCORE_HOT && PC_ENABLE_MDNS && PC_ENABLE_PROMISC

#endif // PC_ENABLE_MDNS_ADAPTIVE
#endif // PROTOCORE_MDNS_ADAPTIVE_H
