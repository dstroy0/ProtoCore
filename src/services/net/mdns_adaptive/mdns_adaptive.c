// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mdns_adaptive.c
 * @brief Adaptive mDNS beacon scheduling (see mdns_adaptive.h).
 */

#include "services/net/mdns_adaptive/mdns_adaptive.h"

#if PC_ENABLE_MDNS_ADAPTIVE

#if PROTOCORE_HOT && PC_ENABLE_MDNS && PC_ENABLE_PROMISC
#include "network_drivers/physical/physical.h"      // pc_net_channel
#include "server/clock/clock.h"                     // pc_millis
#include "services/net/mdns_service/mdns_service.h" // pc_mdns_txt
#include "services/radio/promisc/promisc.h"         // pc_promisc_*
#endif
uint32_t pc_mdns_refresh_interval(uint32_t ttl_s)
{
    // Half the TTL, in ms; guard the *1000 against overflow.
    uint64_t half_ms = (uint64_t)ttl_s * 1000 / 2;
    return half_ms > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)half_ms;
}

void pc_mdns_beacon_init(MdnsBeacon *b, uint32_t base_ms, uint32_t max_ms, uint16_t hi_thresh)
{
    if (!b)
    {
        return;
    }
    b->base_ms = base_ms;
    b->max_ms = max_ms < base_ms ? base_ms : max_ms;
    b->cur_ms = base_ms;
    b->hi_thresh = hi_thresh ? hi_thresh : 1;
}

uint32_t pc_mdns_beacon_adapt(MdnsBeacon *b, uint16_t contention)
{
    if (!b)
    {
        return 0;
    }
    if (contention >= b->hi_thresh)
    {
        uint32_t up = b->cur_ms << 1;
        if (up < b->cur_ms || up > b->max_ms) // overflow or past ceiling
        {
            up = b->max_ms;
        }
        b->cur_ms = up;
    }
    else if (contention == 0)
    {
        uint32_t down = b->cur_ms >> 1;
        if (down < b->base_ms)
        {
            down = b->base_ms;
        }
        b->cur_ms = down;
    }
    return b->cur_ms;
}

proto_bool pc_mdns_beacon_due(const MdnsBeacon *b, uint32_t last_ms, uint32_t now_ms)
{
    if (!b)
    {
        return PROTO_FALSE;
    }
    uint32_t elapsed = now_ms - last_ms; // wrap-safe modular subtraction
    return elapsed >= b->cur_ms;
}

proto_bool pc_mdns_beacon_presleep_due(const MdnsBeacon *b, uint32_t last_ms, uint32_t now_ms, uint32_t sleep_ms)
{
    if (!b)
    {
        return PROTO_FALSE;
    }
    uint32_t elapsed = now_ms - last_ms;
    // Would the record lapse during the sleep? Compute in 64-bit so elapsed + sleep_ms cannot wrap.
    uint64_t after = (uint64_t)elapsed + sleep_ms;
    return after >= b->cur_ms;
}

// ---------------------------------------------------------------------------
// Contention sampling
// ---------------------------------------------------------------------------

void pc_mdns_contention_init(MdnsContentionWindow *w, uint32_t window_ms, uint32_t frames_now, uint32_t now_ms)
{
    if (!w)
    {
        return;
    }
    w->last_count = frames_now;
    w->last_ms = now_ms;
    w->window_ms = window_ms ? window_ms : 1000;
}

proto_bool pc_mdns_contention_sample(MdnsContentionWindow *w, uint32_t frames_now, uint32_t now_ms, uint16_t *out)
{
    if (!w || !out)
    {
        return PROTO_FALSE;
    }
    uint32_t elapsed = now_ms - w->last_ms; // wrap-safe modular subtraction
    if (elapsed < w->window_ms)
    {
        return PROTO_FALSE;
    }
    // Modular difference, so a wrapped frame counter still yields the true count as long as fewer
    // than 2^32 frames passed in one window - which no radio does in a second.
    uint32_t delta = frames_now - w->last_count;
    *out = delta > 0xFFFF ? 0xFFFF : (uint16_t)delta;
    w->last_count = frames_now;
    w->last_ms = now_ms;
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Device binding
// ---------------------------------------------------------------------------

#if PROTOCORE_HOT && PC_ENABLE_MDNS && PC_ENABLE_PROMISC

/** @brief Owned state for the live adaptive announcer. */
typedef struct
{
    MdnsAdaptiveCfg cfg;
    MdnsBeacon beacon;
    MdnsContentionWindow window;
    volatile uint32_t frames; ///< running frame total, bumped in the capture callback.
    uint32_t last_announce_ms;
    uint16_t last_contention;
    uint32_t announces;
    uint8_t channel; ///< the channel capture is currently pinned to.
    proto_bool running;
} MdnsAdaptiveCtx;
static MdnsAdaptiveCtx s_ad = {};

// Promiscuous sink: the whole job is to count. Runs in the WiFi driver's callback context, so it
// only touches the running total - no parsing, no allocation, no blocking.
static void adaptive_sink(const uint8_t *frame, uint16_t len, int8_t rssi, uint8_t channel)
{
    (void)frame;
    (void)len;
    (void)rssi;
    (void)channel;
    s_ad.frames++;
}

proto_bool pc_mdns_adaptive_begin(const MdnsAdaptiveCfg *cfg)
{
    if (!cfg || s_ad.running)
    {
        return PROTO_FALSE;
    }
    uint8_t ch = pc_net_channel();
    if (ch == 0)
    {
        return PROTO_FALSE; // not associated: there is no channel to pin capture to
    }

    s_ad.cfg = *cfg;
    uint32_t now = pc_millis();
    uint32_t base = pc_mdns_refresh_interval(cfg->ttl_s);

    // Never let the backoff push the refresh past the TTL: a cache evicts the record at its TTL, so
    // announcing slower than that makes the device silently undiscoverable - the opposite of the
    // point. Cap the ceiling at 7/8 of the TTL, leaving margin for propagation. A longer TTL is how
    // you buy a wider adaptive range; the range is fundamentally bounded by [TTL/2, ~TTL).
    uint64_t ttl_ms = (uint64_t)cfg->ttl_s * 1000;
    uint32_t safe_ceiling = (uint32_t)(ttl_ms - ttl_ms / 8 > 0xFFFFFFFFu ? 0xFFFFFFFFu : ttl_ms - ttl_ms / 8);
    uint32_t ceiling = cfg->max_interval_ms < safe_ceiling ? cfg->max_interval_ms : safe_ceiling;
    pc_mdns_beacon_init(&s_ad.beacon, base, ceiling, cfg->hi_contention);
    s_ad.frames = 0;
    pc_mdns_contention_init(&s_ad.window, cfg->window_ms, 0, now);
    s_ad.last_announce_ms = now;
    s_ad.last_contention = 0;
    s_ad.announces = 0;
    s_ad.channel = ch;

    // Pin capture to the station's OWN channel and never hop, or the association drops.
    s_ad.running = pc_promisc_begin(ch, adaptive_sink);
    return s_ad.running;
}

void pc_mdns_adaptive_tick(void)
{
    if (!s_ad.running)
    {
        return;
    }
    uint32_t now = pc_millis();

    // Follow the station if it roamed to another channel, so capture stays on the live link.
    uint8_t ch = pc_net_channel();
    if (ch != 0 && ch != s_ad.channel)
    {
        pc_promisc_set_channel(ch);
        s_ad.channel = ch;
    }

    // Close a contention window if one elapsed, and let it move the interval.
    uint16_t c;
    if (pc_mdns_contention_sample(&s_ad.window, s_ad.frames, now, &c))
    {
        s_ad.last_contention = c;
        pc_mdns_beacon_adapt(&s_ad.beacon, c);
    }

    // Re-announce when the (adaptive) interval has elapsed. Re-applying the TXT at its current value
    // re-announces on every PCB with no goodbye - a refresh, not an evict.
    if (pc_mdns_beacon_due(&s_ad.beacon, s_ad.last_announce_ms, now))
    {
        pc_mdns_txt(s_ad.cfg.key, s_ad.cfg.value);
        s_ad.last_announce_ms = now;
        s_ad.announces++;
    }
}

void pc_mdns_adaptive_end(void)
{
    if (!s_ad.running)
    {
        return;
    }
    pc_promisc_end();
    s_ad.running = PROTO_FALSE;
}

uint32_t pc_mdns_adaptive_interval_ms(void)
{
    return s_ad.beacon.cur_ms;
}

uint16_t pc_mdns_adaptive_contention(void)
{
    return s_ad.last_contention;
}

uint32_t pc_mdns_adaptive_announces(void)
{
    return s_ad.announces;
}

#endif // PROTOCORE_HOT && PC_ENABLE_MDNS && PC_ENABLE_PROMISC

#endif // PC_ENABLE_MDNS_ADAPTIVE
