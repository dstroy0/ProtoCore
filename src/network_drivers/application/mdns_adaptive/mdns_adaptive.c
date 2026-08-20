// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mdns_adaptive.c
 * @brief Adaptive mDNS beacon scheduling (see mdns_adaptive.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_MDNS_ADAPTIVE

#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "network_drivers/application/mdns_adaptive/mdns_adaptive.h"
#include "shared/pcap/pcap.h"

#include "network_drivers/application/mdns_service/mdns_service.h" // protocore_mdns_txt
#include "network_drivers/physical/physical/physical.h"            // Physical.wifi_channel
#include "server/clock/clock.h"                                    // protocore_millis
#include "services/radio/promisc/promisc.h"                        // protocore_promisc_*
PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
void protocore_mdns_adaptive_beacon_adapt(uint8_t *restrict work);
void protocore_mdns_adaptive_beacon_due(uint8_t *restrict work);
void protocore_mdns_adaptive_beacon_init(uint8_t *restrict work);
void protocore_mdns_adaptive_contention_init(uint8_t *restrict work);
void protocore_mdns_adaptive_contention_sample(uint8_t *restrict work);
void protocore_mdns_adaptive_refresh_interval(uint8_t *restrict work);

void protocore_mdns_adaptive_refresh_interval(uint8_t *restrict work)
{
    (void)work;
    uint32_t ttl_s = MdnsAdaptiveV.refresh_interval_args.ttl_s;

    // Half the TTL, in ms; guard the *1000 against overflow.
    uint64_t half_ms = (uint64_t)ttl_s * 1000 / 2;
    MdnsAdaptiveV.ms = half_ms > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)half_ms;
}

void protocore_mdns_adaptive_beacon_init(uint8_t *restrict work)
{
    (void)work;
    MdnsBeacon *b = MdnsAdaptiveV.beacon_init_args.b;
    uint32_t base_ms = MdnsAdaptiveV.beacon_init_args.base_ms;
    uint32_t max_ms = MdnsAdaptiveV.beacon_init_args.max_ms;
    uint16_t hi_thresh = MdnsAdaptiveV.beacon_init_args.hi_thresh;

    if (!b)
    {
        return;
    }
    b->base_ms = base_ms;
    b->max_ms = max_ms < base_ms ? base_ms : max_ms;
    b->cur_ms = base_ms;
    b->hi_thresh = hi_thresh ? hi_thresh : 1;
}

void protocore_mdns_adaptive_beacon_adapt(uint8_t *restrict work)
{
    (void)work;
    MdnsBeacon *b = MdnsAdaptiveV.beacon_adapt_args.b;
    uint16_t contention = MdnsAdaptiveV.beacon_adapt_args.contention;

    if (!b)
    {
        MdnsAdaptiveV.ms = 0;
        return;
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
    MdnsAdaptiveV.ms = b->cur_ms;
}

void protocore_mdns_adaptive_beacon_due(uint8_t *restrict work)
{
    (void)work;
    const MdnsBeacon *b = MdnsAdaptiveV.beacon_due_args.b;
    uint32_t last_ms = MdnsAdaptiveV.beacon_due_args.last_ms;
    uint32_t now_ms = MdnsAdaptiveV.beacon_due_args.now_ms;

    if (!b)
    {
        MdnsAdaptiveV.ok = PROTO_FALSE;
        return;
    }
    uint32_t elapsed = now_ms - last_ms; // wrap-safe modular subtraction
    MdnsAdaptiveV.ok = elapsed >= b->cur_ms;
}

void protocore_mdns_adaptive_beacon_presleep_due(uint8_t *restrict work)
{
    (void)work;
    const MdnsBeacon *b = MdnsAdaptiveV.beacon_presleep_due_args.b;
    uint32_t last_ms = MdnsAdaptiveV.beacon_presleep_due_args.last_ms;
    uint32_t now_ms = MdnsAdaptiveV.beacon_presleep_due_args.now_ms;
    uint32_t sleep_ms = MdnsAdaptiveV.beacon_presleep_due_args.sleep_ms;

    if (!b)
    {
        MdnsAdaptiveV.ok = PROTO_FALSE;
        return;
    }
    uint32_t elapsed = now_ms - last_ms;
    // Would the record lapse during the sleep? Compute in 64-bit so elapsed + sleep_ms cannot wrap.
    uint64_t after = (uint64_t)elapsed + sleep_ms;
    MdnsAdaptiveV.ok = after >= b->cur_ms;
}

// ---------------------------------------------------------------------------
// Contention sampling
// ---------------------------------------------------------------------------

void protocore_mdns_adaptive_contention_init(uint8_t *restrict work)
{
    (void)work;
    MdnsContentionWindow *w = MdnsAdaptiveV.contention_init_args.w;
    uint32_t window_ms = MdnsAdaptiveV.contention_init_args.window_ms;
    uint32_t frames_now = MdnsAdaptiveV.contention_init_args.frames_now;
    uint32_t now_ms = MdnsAdaptiveV.contention_init_args.now_ms;

    if (!w)
    {
        return;
    }
    w->last_count = frames_now;
    w->last_ms = now_ms;
    w->window_ms = window_ms ? window_ms : 1000;
}

void protocore_mdns_adaptive_contention_sample(uint8_t *restrict work)
{
    (void)work;
    MdnsContentionWindow *w = MdnsAdaptiveV.contention_sample_args.w;
    uint32_t frames_now = MdnsAdaptiveV.contention_sample_args.frames_now;
    uint32_t now_ms = MdnsAdaptiveV.contention_sample_args.now_ms;
    uint16_t *out = MdnsAdaptiveV.contention_sample_args.out;

    if (!w || !out)
    {
        MdnsAdaptiveV.ok = PROTO_FALSE;
        return;
    }
    uint32_t elapsed = now_ms - w->last_ms; // wrap-safe modular subtraction
    if (elapsed < w->window_ms)
    {
        MdnsAdaptiveV.ok = PROTO_FALSE;
        return;
    }
    // Modular difference, so a wrapped frame counter still yields the true count as long as fewer
    // than 2^32 frames passed in one window - which no radio does in a second.
    uint32_t delta = frames_now - w->last_count;
    *out = delta > 0xFFFF ? 0xFFFF : (uint16_t)delta;
    w->last_count = frames_now;
    w->last_ms = now_ms;
    MdnsAdaptiveV.ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Device binding
// ---------------------------------------------------------------------------

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
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define MDNS_ADAPTIVE_OFF_CTX 0u
static_assert(MDNS_ADAPTIVE_OFF_CTX + sizeof(MdnsAdaptiveCtx) <= PROTOCORE_MDNS_ADAPTIVE_BORROW,
              "PROTOCORE_MDNS_ADAPTIVE_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(MDNS_ADAPTIVE_OFF_CTX % _Alignof(MdnsAdaptiveCtx) == 0,
              "MDNS_ADAPTIVE_OFF_CTX is not a multiple of alignof(MdnsAdaptiveCtx) - MDNS_ADAPTIVE_CTX() would return "
              "a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define MDNS_ADAPTIVE_CTX(w) ((MdnsAdaptiveCtx *)(void *)((w) + MDNS_ADAPTIVE_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_MDNS_ADAPTIVE_BORROW persistent bytes
} MdnsAdaptiveOwnCtx;
static MdnsAdaptiveOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_mdns_adaptive_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_MDNS_ADAPTIVE_BORROW).buf;
    }
    return s_own.span;
}

// Promiscuous sink: the whole job is to count. Runs in the WiFi driver's callback context, so it
// only touches the running total - no parsing, no allocation, no blocking.
static void adaptive_sink(const uint8_t *frame, uint16_t len, int8_t rssi, uint8_t channel)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_mdns_adaptive_span();

    (void)frame;
    (void)len;
    (void)rssi;
    (void)channel;
    MDNS_ADAPTIVE_CTX(work)->frames++;
}

void protocore_mdns_adaptive_begin(uint8_t *restrict work)
{
    const MdnsAdaptiveCfg *cfg = MdnsAdaptiveV.begin_args.cfg;

    if (!cfg || MDNS_ADAPTIVE_CTX(work)->running)
    {
        MdnsAdaptiveV.ok = PROTO_FALSE;
        return;
    }
    Physical.wifi_channel(protocore_physical_span());
    uint8_t ch = PhysicalV.u8;
    if (ch == 0)
    {
        MdnsAdaptiveV.ok = PROTO_FALSE; // not associated: there is no channel to pin capture to
        return;
    }

    MDNS_ADAPTIVE_CTX(work)->cfg = *cfg;
    uint32_t now = Clock.ms;
    MdnsAdaptiveV.refresh_interval_args.ttl_s = cfg->ttl_s;
    protocore_mdns_adaptive_refresh_interval(work);
    uint32_t base = MdnsAdaptiveV.ms;

    // Never let the backoff push the refresh past the TTL: a cache evicts the record at its TTL, so
    // announcing slower than that makes the device silently undiscoverable - the opposite of the
    // point. Cap the ceiling at 7/8 of the TTL, leaving margin for propagation. A longer TTL is how
    // you buy a wider adaptive range; the range is fundamentally bounded by [TTL/2, ~TTL).
    uint64_t ttl_ms = (uint64_t)cfg->ttl_s * 1000;
    uint32_t safe_ceiling = (uint32_t)(ttl_ms - ttl_ms / 8 > 0xFFFFFFFFu ? 0xFFFFFFFFu : ttl_ms - ttl_ms / 8);
    uint32_t ceiling = cfg->max_interval_ms < safe_ceiling ? cfg->max_interval_ms : safe_ceiling;
    MdnsAdaptiveV.beacon_init_args.b = &MDNS_ADAPTIVE_CTX(work)->beacon;
    MdnsAdaptiveV.beacon_init_args.base_ms = base;
    MdnsAdaptiveV.beacon_init_args.max_ms = ceiling;
    MdnsAdaptiveV.beacon_init_args.hi_thresh = cfg->hi_contention;
    protocore_mdns_adaptive_beacon_init(work);
    MDNS_ADAPTIVE_CTX(work)->frames = 0;
    MdnsAdaptiveV.contention_init_args.w = &MDNS_ADAPTIVE_CTX(work)->window;
    MdnsAdaptiveV.contention_init_args.window_ms = cfg->window_ms;
    MdnsAdaptiveV.contention_init_args.frames_now = 0;
    MdnsAdaptiveV.contention_init_args.now_ms = now;
    protocore_mdns_adaptive_contention_init(work);
    MDNS_ADAPTIVE_CTX(work)->last_announce_ms = now;
    MDNS_ADAPTIVE_CTX(work)->last_contention = 0;
    MDNS_ADAPTIVE_CTX(work)->announces = 0;
    MDNS_ADAPTIVE_CTX(work)->channel = ch;

    // Pin capture to the station's OWN channel and never hop, or the association drops.
    PromiscV.begin_args.channel = ch;
    PromiscV.begin_args.sink = adaptive_sink;
    Promisc.begin(protocore_promisc_span());
    MDNS_ADAPTIVE_CTX(work)->running = PromiscV.ok;
    MdnsAdaptiveV.ok = MDNS_ADAPTIVE_CTX(work)->running;
}

void protocore_mdns_adaptive_tick(uint8_t *restrict work)
{
    if (!MDNS_ADAPTIVE_CTX(work)->running)
    {
        return;
    }
    uint32_t now = Clock.ms;

    // Follow the station if it roamed to another channel, so capture stays on the live link.
    Physical.wifi_channel(protocore_physical_span());
    uint8_t ch = PhysicalV.u8;
    if (ch != 0 && ch != MDNS_ADAPTIVE_CTX(work)->channel)
    {
        PromiscV.set_channel_args.channel = ch;
        Promisc.set_channel(protocore_promisc_span());
        MDNS_ADAPTIVE_CTX(work)->channel = ch;
    }

    // Close a contention window if one elapsed, and let it move the interval.
    uint16_t c;
    MdnsAdaptiveV.contention_sample_args.w = &MDNS_ADAPTIVE_CTX(work)->window;
    MdnsAdaptiveV.contention_sample_args.frames_now = MDNS_ADAPTIVE_CTX(work)->frames;
    MdnsAdaptiveV.contention_sample_args.now_ms = now;
    MdnsAdaptiveV.contention_sample_args.out = &c;
    protocore_mdns_adaptive_contention_sample(work);
    if (MdnsAdaptiveV.ok)
    {
        MDNS_ADAPTIVE_CTX(work)->last_contention = c;
        MdnsAdaptiveV.beacon_adapt_args.b = &MDNS_ADAPTIVE_CTX(work)->beacon;
        MdnsAdaptiveV.beacon_adapt_args.contention = c;
        protocore_mdns_adaptive_beacon_adapt(work);
    }

    // Re-announce when the (adaptive) interval has elapsed. Re-applying the TXT at its current value
    // re-announces on every PCB with no goodbye - a refresh, not an evict.
    MdnsAdaptiveV.beacon_due_args.b = &MDNS_ADAPTIVE_CTX(work)->beacon;
    MdnsAdaptiveV.beacon_due_args.last_ms = MDNS_ADAPTIVE_CTX(work)->last_announce_ms;
    MdnsAdaptiveV.beacon_due_args.now_ms = now;
    protocore_mdns_adaptive_beacon_due(work);
    if (MdnsAdaptiveV.ok)
    {
        MdnsServiceV.txt_args.key = MDNS_ADAPTIVE_CTX(work)->cfg.key;
        MdnsServiceV.txt_args.value = MDNS_ADAPTIVE_CTX(work)->cfg.value;
        MdnsService.txt(protocore_mdns_service_span());
        MDNS_ADAPTIVE_CTX(work)->last_announce_ms = now;
        MDNS_ADAPTIVE_CTX(work)->announces++;
    }
}

void protocore_mdns_adaptive_end(uint8_t *restrict work)
{
    if (!MDNS_ADAPTIVE_CTX(work)->running)
    {
        return;
    }
    Promisc.end(protocore_promisc_span());
    MDNS_ADAPTIVE_CTX(work)->running = PROTO_FALSE;
}

void protocore_mdns_adaptive_interval_ms(uint8_t *restrict work)
{
    MdnsAdaptiveV.ms = MDNS_ADAPTIVE_CTX(work)->beacon.cur_ms;
}

void protocore_mdns_adaptive_contention(uint8_t *restrict work)
{
    MdnsAdaptiveV.value = MDNS_ADAPTIVE_CTX(work)->last_contention;
}

void protocore_mdns_adaptive_announces(uint8_t *restrict work)
{
    MdnsAdaptiveV.ms = MDNS_ADAPTIVE_CTX(work)->announces;
}

/** @brief The operands and the outcome. */
MdnsAdaptiveVars MdnsAdaptiveV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MDNS_ADAPTIVE
