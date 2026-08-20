// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mdns_adaptive.h
 * @brief Adaptive mDNS beacon scheduling: RF-aware backoff, TTL refresher, auto-sleep beacon
 *        (PROTOCORE_ENABLE_MDNS_ADAPTIVE).
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_MDNS_ADAPTIVE

PROTOCORE_BEGIN_DECLS

// PROTOCORE_MDNS_ADAPTIVE_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

/** @brief Adaptive beacon state. */
typedef struct
{
    uint32_t base_ms;   ///< nominal refresh cadence (e.g. TTL/2), and the backoff floor.
    uint32_t max_ms;    ///< backoff ceiling under heavy contention.
    uint32_t cur_ms;    ///< current adaptive interval.
    uint16_t hi_thresh; ///< contention count at/above which the interval backs off.
} MdnsBeacon;

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

/** @brief What refresh_interval takes: ttl_s. */
typedef struct
{
    uint32_t ttl_s;
} MdnsAdaptiveRefreshIntervalArgs;

/** @brief What beacon_init takes: b, base_ms, max_ms, hi_thresh. */
typedef struct
{
    MdnsBeacon *b;
    uint32_t base_ms;
    uint32_t max_ms;
    uint16_t hi_thresh;
} MdnsAdaptiveBeaconInitArgs;

/** @brief What beacon_adapt takes: b, contention. */
typedef struct
{
    MdnsBeacon *b;
    uint16_t contention;
} MdnsAdaptiveBeaconAdaptArgs;

/** @brief What beacon_due takes: b, last_ms, now_ms. */
typedef struct
{
    const MdnsBeacon *b;
    uint32_t last_ms;
    uint32_t now_ms;
} MdnsAdaptiveBeaconDueArgs;

/** @brief What beacon_presleep_due takes: b, last_ms, now_ms, sleep_ms. */
typedef struct
{
    const MdnsBeacon *b;
    uint32_t last_ms;
    uint32_t now_ms;
    uint32_t sleep_ms;
} MdnsAdaptiveBeaconPresleepDueArgs;

/** @brief What contention_init takes: w, window_ms, frames_now, now_ms. */
typedef struct
{
    MdnsContentionWindow *w;
    uint32_t window_ms;
    uint32_t frames_now;
    uint32_t now_ms;
} MdnsAdaptiveContentionInitArgs;

/** @brief What contention_sample takes: w, frames_now, now_ms, out. */
typedef struct
{
    MdnsContentionWindow *w;
    uint32_t frames_now; ///< the current running frame total (monotonic; a wrap is handled)
    uint32_t now_ms;
    uint16_t *out; ///< receives the frame count for the window (saturated at 0xFFFF)
} MdnsAdaptiveContentionSampleArgs;

/** @brief What begin takes: cfg. */
typedef struct
{
    const MdnsAdaptiveCfg *cfg;
} MdnsAdaptiveBeginArgs;

/**
 * @brief Adaptive mDNS beacon scheduling: RF-aware backoff, TTL refresher, auto-sleep beacon
 * (PROTOCORE_ENABLE_MDNS_ADAPTIVE).
 *
 * A caller sets the members a call takes, invokes it through ::MdnsAdaptive with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   MdnsAdaptive.refresh_interval_args.ttl_s = ...;
 *   MdnsAdaptive.refresh_interval(work);
 *   // MdnsAdaptive.ms is what the call reports
 *
 * @var MdnsAdaptiveNs::refresh_interval_args  what refresh_interval takes: ttl_s
 * @var MdnsAdaptiveNs::beacon_init_args  what beacon_init takes: b, base_ms, max_ms, hi_thresh
 * @var MdnsAdaptiveNs::beacon_adapt_args  what beacon_adapt takes: b, contention
 * @var MdnsAdaptiveNs::beacon_due_args  what beacon_due takes: b, last_ms, now_ms
 * @var MdnsAdaptiveNs::beacon_presleep_due_args  what beacon_presleep_due takes: b, last_ms, now_ms, sleep_ms
 * @var MdnsAdaptiveNs::contention_init_args  what contention_init takes: w, window_ms, frames_now, now_ms
 * @var MdnsAdaptiveNs::contention_sample_args  what contention_sample takes: w, frames_now, now_ms, out
 * @var MdnsAdaptiveNs::begin_args  what begin takes: cfg
 * @var MdnsAdaptiveNs::ok  true when a window closed and out was written; false if the window ...
 * @var MdnsAdaptiveNs::ms  the new interval (ms)
 * @var MdnsAdaptiveNs::value  the value a call reports
 * @var MdnsAdaptiveNs::refresh_interval  the continuous-refresher cadence for a record TTL: half the TTL, in ...
 * @var MdnsAdaptiveNs::beacon_init  initialize a beacon. cur_ms starts at base_ms
 * @var MdnsAdaptiveNs::beacon_adapt  adapt the interval to observed RF contention (announces/collisions ...
 * @var MdnsAdaptiveNs::beacon_due  is an announce due now? (wrap-safe: elapsed since last_ms >= the ...
 * @var MdnsAdaptiveNs::beacon_presleep_due  auto-sleep beacon: should we announce *before* sleeping for ...
 * @var MdnsAdaptiveNs::contention_init  start sampling at now_ms, anchored to the current counter frames_now
 * @var MdnsAdaptiveNs::contention_sample  if a window has elapsed, report the frames counted in it and start ...
 * @var MdnsAdaptiveNs::begin  start adaptive announcing: begin promiscuous capture on the ...
 * @var MdnsAdaptiveNs::tick  advance the schedule: sample contention, adapt the interval, follow ...
 * @var MdnsAdaptiveNs::end  stop adaptive announcing and release promiscuous mode
 * @var MdnsAdaptiveNs::interval_ms  current adaptive announce interval (ms) - for a diagnostics panel
 * @var MdnsAdaptiveNs::contention  frames counted in the most recently closed window - the live ...
 * @var MdnsAdaptiveNs::announces  total announces sent since begin()
 *
 * @c work is PROTOCORE_MDNS_ADAPTIVE_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    MdnsAdaptiveRefreshIntervalArgs refresh_interval_args;
    MdnsAdaptiveBeaconInitArgs beacon_init_args;
    MdnsAdaptiveBeaconAdaptArgs beacon_adapt_args;
    MdnsAdaptiveBeaconDueArgs beacon_due_args;
    MdnsAdaptiveBeaconPresleepDueArgs beacon_presleep_due_args;
    MdnsAdaptiveContentionInitArgs contention_init_args;
    MdnsAdaptiveContentionSampleArgs contention_sample_args;
    MdnsAdaptiveBeginArgs begin_args;
    proto_bool ok;
    uint32_t ms;
    uint16_t value;
} MdnsAdaptiveVars;

/** @brief The operands and the outcome. */
extern MdnsAdaptiveVars MdnsAdaptiveV;

/** @brief The entries. */
typedef struct
{
    void (*const refresh_interval)(uint8_t *restrict work);
    void (*const beacon_init)(uint8_t *restrict work);
    void (*const beacon_adapt)(uint8_t *restrict work);
    void (*const beacon_due)(uint8_t *restrict work);
    void (*const beacon_presleep_due)(uint8_t *restrict work);
    void (*const contention_init)(uint8_t *restrict work);
    void (*const contention_sample)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const tick)(uint8_t *restrict work);
    void (*const end)(uint8_t *restrict work);
    void (*const interval_ms)(uint8_t *restrict work);
    void (*const contention)(uint8_t *restrict work);
    void (*const announces)(uint8_t *restrict work);
} MdnsAdaptiveNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in MdnsAdaptiveV or a region of the borrow at a fixed offset.
void protocore_mdns_adaptive_refresh_interval(uint8_t *restrict work);
void protocore_mdns_adaptive_beacon_init(uint8_t *restrict work);
void protocore_mdns_adaptive_beacon_adapt(uint8_t *restrict work);
void protocore_mdns_adaptive_beacon_due(uint8_t *restrict work);
void protocore_mdns_adaptive_beacon_presleep_due(uint8_t *restrict work);
void protocore_mdns_adaptive_contention_init(uint8_t *restrict work);
void protocore_mdns_adaptive_contention_sample(uint8_t *restrict work);
void protocore_mdns_adaptive_begin(uint8_t *restrict work);
void protocore_mdns_adaptive_tick(uint8_t *restrict work);
void protocore_mdns_adaptive_end(uint8_t *restrict work);
void protocore_mdns_adaptive_interval_ms(uint8_t *restrict work);
void protocore_mdns_adaptive_contention(uint8_t *restrict work);
void protocore_mdns_adaptive_announces(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `MdnsAdaptive.refresh_interval(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const MdnsAdaptiveNs MdnsAdaptive __attribute__((unused)) = {
    .refresh_interval = protocore_mdns_adaptive_refresh_interval,
    .beacon_init = protocore_mdns_adaptive_beacon_init,
    .beacon_adapt = protocore_mdns_adaptive_beacon_adapt,
    .beacon_due = protocore_mdns_adaptive_beacon_due,
    .beacon_presleep_due = protocore_mdns_adaptive_beacon_presleep_due,
    .contention_init = protocore_mdns_adaptive_contention_init,
    .contention_sample = protocore_mdns_adaptive_contention_sample,
    .begin = protocore_mdns_adaptive_begin,
    .tick = protocore_mdns_adaptive_tick,
    .end = protocore_mdns_adaptive_end,
    .interval_ms = protocore_mdns_adaptive_interval_ms,
    .contention = protocore_mdns_adaptive_contention,
    .announces = protocore_mdns_adaptive_announces,
};

/**
 * @brief The PROTOCORE_MDNS_ADAPTIVE_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_mdns_adaptive_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MDNS_ADAPTIVE

#endif // PROTOCORE_MDNS_ADAPTIVE_H
