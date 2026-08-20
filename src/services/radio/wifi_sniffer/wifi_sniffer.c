// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wifi_sniffer.c
 * @brief 802.11 frame decode + traffic tally + RSSI roaming decision (see wifi_sniffer.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_WIFI_SNIFFER

#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "mmgr/protomem/protomem.h"
#include "services/radio/wifi_sniffer/wifi_sniffer.h"
#include "shared/pcap/pcap.h"

#include "server/clock/clock.h" // Clock.ms - the monotonic source the dwell schedule is stamped from

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_PROMISC
#include "services/radio/promisc/promisc.h" // the promiscuous-capture owner
#endif

// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_WIFI_SNIFFER_BORROW persistent bytes
} WifiSnifferOwnCtx;
static WifiSnifferOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_wifi_sniffer_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_WIFI_SNIFFER_BORROW).buf;
    }
    return s_own.span;
}

static void wifi_sniffer_scan_due(uint8_t *restrict work);
static void wifi_sniffer_scan_init(uint8_t *restrict work);
static void wifi_sniffer_scan_next(uint8_t *restrict work);
static void wifi_sniffer_stats_reset(uint8_t *restrict work);
static void wifi_sniffer_survey_reset(uint8_t *restrict work);

static void wifi_sniffer_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *frame = WifiSniffer.parse_args.frame;
    size_t len = WifiSniffer.parse_args.len;
    WifiFrame *out = WifiSniffer.parse_args.out;

    if (!frame || !out || len < 10) // FrameControl(2) + Duration(2) + Address1(6)
    {
        WifiSniffer.ok = PROTO_FALSE;
        return;
    }

    mem.set(out, 0, sizeof(*out));

    uint8_t fc0 = frame[0];
    uint8_t fc1 = frame[1];
    out->version = fc0 & 0x03;
    out->type = (fc0 >> 2) & 0x03;
    out->subtype = (fc0 >> 4) & 0x0F;
    out->to_ds = (fc1 & 0x01) != 0;
    out->from_ds = (fc1 & 0x02) != 0;
    out->retry = (fc1 & 0x08) != 0;
    out->protected_frame = (fc1 & 0x40) != 0;

    // Address1 always present at this length (offset 4).
    mem.cpy(out->addr1, frame + 4, 6);
    out->naddr = 1;
    if (len >= 16)
    {
        mem.cpy(out->addr2, frame + 10, 6);
        out->naddr = 2;
    }
    if (len >= 24)
    {
        mem.cpy(out->addr3, frame + 16, 6);
        out->naddr = 3;
    }
    WifiSniffer.ok = PROTO_TRUE;
}

static void wifi_sniffer_stats_reset(uint8_t *restrict work)
{
    (void)work;
    WifiStats *s = WifiSniffer.stats_reset_args.s;

    if (s)
    {
        mem.set(s, 0, sizeof(*s));
    }
}

static void wifi_sniffer_stats_add(uint8_t *restrict work)
{
    (void)work;
    WifiStats *s = WifiSniffer.stats_add_args.s;
    const WifiFrame *f = WifiSniffer.stats_add_args.f;

    if (!s || !f)
    {
        return;
    }
    switch (f->type)
    {
    case WIFI_TYPE_MGMT:
        s->mgmt++;
        break;
    case WIFI_TYPE_CTRL:
        s->ctrl++;
        break;
    case WIFI_TYPE_DATA:
        s->data++;
        break;
    default:
        s->other++;
        break;
    }
    s->total++;
}

static void wifi_sniffer_should_roam(uint8_t *restrict work)
{
    (void)work;
    int8_t cur_rssi = WifiSniffer.should_roam_args.cur_rssi;
    int8_t cand_rssi = WifiSniffer.should_roam_args.cand_rssi;
    uint8_t hysteresis_db = WifiSniffer.should_roam_args.hysteresis_db;

    // Both are negative dBm (stronger = closer to 0). Roam only if the candidate clears the current
    // by more than the hysteresis, computed in a wide signed type to avoid int8 overflow.
    int32_t margin = (int32_t)cand_rssi - (int32_t)cur_rssi;
    WifiSniffer.ok = margin > (int32_t)hysteresis_db;
}

// --- Channel-hop scan schedule ----------------------------------------------------------

static uint8_t clamp_channel(uint8_t c)
{
    if (c < 1)
    {
        return 1;
    }
    if (c > 14)
    {
        return 14;
    }
    return c;
}

static void wifi_sniffer_scan_init(uint8_t *restrict work)
{
    (void)work;
    WifiScan *s = WifiSniffer.scan_init_args.s;
    uint8_t first = WifiSniffer.scan_init_args.first;
    uint8_t last = WifiSniffer.scan_init_args.last;
    uint16_t dwell_ms = WifiSniffer.scan_init_args.dwell_ms;
    uint32_t now_ms = WifiSniffer.scan_init_args.now_ms;

    if (!s)
    {
        return;
    }
    s->chan_first = clamp_channel(first);
    s->chan_last = clamp_channel(last);
    if (s->chan_last < s->chan_first)
    {
        s->chan_last = s->chan_first;
    }
    s->channel = s->chan_first;
    s->dwell_ms = dwell_ms;
    s->last_hop_ms = now_ms;
    s->sweeps = 0;
}

static void wifi_sniffer_scan_due(uint8_t *restrict work)
{
    (void)work;
    const WifiScan *s = WifiSniffer.scan_due_args.s;
    uint32_t now_ms = WifiSniffer.scan_due_args.now_ms;

    if (!s)
    {
        WifiSniffer.ok = PROTO_FALSE;
        return;
    }
    // Unsigned subtraction is correct across a millis() rollover.
    WifiSniffer.ok = (now_ms - s->last_hop_ms) >= s->dwell_ms;
}

static void wifi_sniffer_scan_next(uint8_t *restrict work)
{
    (void)work;
    WifiScan *s = WifiSniffer.scan_next_args.s;
    uint32_t now_ms = WifiSniffer.scan_next_args.now_ms;

    if (!s)
    {
        WifiSniffer.value = 0;
        return;
    }
    if (s->channel >= s->chan_last)
    {
        s->channel = s->chan_first;
        s->sweeps++;
    }
    else
    {
        s->channel++;
    }
    s->last_hop_ms = now_ms;
    WifiSniffer.value = s->channel;
}

// --- Per-channel RSSI survey ------------------------------------------------------------

static void wifi_sniffer_survey_reset(uint8_t *restrict work)
{
    (void)work;
    WifiSurvey *s = WifiSniffer.survey_reset_args.s;
    uint8_t first = WifiSniffer.survey_reset_args.first;
    uint8_t count = WifiSniffer.survey_reset_args.count;

    if (!s)
    {
        return;
    }
    mem.set(s, 0, sizeof(*s));
    s->first = clamp_channel(first);
    s->count = (count > PROTOCORE_WIFI_SNIFFER_MAX_CHANNELS) ? (uint8_t)PROTOCORE_WIFI_SNIFFER_MAX_CHANNELS : count;
    for (uint8_t i = 0; i < PROTOCORE_WIFI_SNIFFER_MAX_CHANNELS; i++)
    {
        s->ch[i].best_rssi = PROTOCORE_WIFI_RSSI_NONE;
    }
}

// Index of `channel` in the survey table, or -1 if outside the tracked range.
static int survey_index(const WifiSurvey *s, uint8_t channel)
{
    if (!s || channel < s->first)
    {
        return -1;
    }
    int idx = (int)channel - (int)s->first;
    if (idx >= (int)s->count)
    {
        return -1;
    }
    return idx;
}

static void wifi_sniffer_survey_add(uint8_t *restrict work)
{
    (void)work;
    WifiSurvey *s = WifiSniffer.survey_add_args.s;
    uint8_t channel = WifiSniffer.survey_add_args.channel;
    int8_t rssi = WifiSniffer.survey_add_args.rssi;
    const WifiFrame *f = WifiSniffer.survey_add_args.f;

    int idx = survey_index(s, channel);
    if (idx < 0)
    {
        return;
    }
    WifiChannelSurvey *e = &s->ch[idx];
    e->frames++;
    if (e->best_rssi == PROTOCORE_WIFI_RSSI_NONE || rssi > e->best_rssi)
    {
        e->best_rssi = rssi;
        // The transmitter address is the AP for a beacon; only present once addr2 was decoded.
        if (f && f->naddr >= 2)
        {
            mem.cpy(e->best_bssid, f->addr2, 6);
        }
    }
}

static void wifi_sniffer_survey_get(uint8_t *restrict work)
{
    (void)work;
    const WifiSurvey *s = WifiSniffer.survey_get_args.s;
    uint8_t channel = WifiSniffer.survey_get_args.channel;

    int idx = survey_index(s, channel);
    WifiSniffer.ptr = (idx < 0) ? NULL : &s->ch[idx];
}

static void wifi_sniffer_survey_best(uint8_t *restrict work)
{
    (void)work;
    const WifiSurvey *s = WifiSniffer.survey_best_args.s;
    uint8_t exclude_channel = WifiSniffer.survey_best_args.exclude_channel;
    uint8_t *out_channel = WifiSniffer.survey_best_args.out_channel;
    int8_t *out_rssi = WifiSniffer.survey_best_args.out_rssi;

    if (!s)
    {
        WifiSniffer.ok = PROTO_FALSE;
        return;
    }
    proto_bool found = PROTO_FALSE;
    uint8_t best_ch = 0;
    int8_t best = PROTOCORE_WIFI_RSSI_NONE;
    for (uint8_t i = 0; i < s->count; i++)
    {
        uint8_t ch = (uint8_t)(s->first + i);
        if (ch == exclude_channel || s->ch[i].best_rssi == PROTOCORE_WIFI_RSSI_NONE)
        {
            continue;
        }
        if (!found || s->ch[i].best_rssi > best)
        {
            found = PROTO_TRUE;
            best = s->ch[i].best_rssi;
            best_ch = ch;
        }
    }
    if (found)
    {
        if (out_channel)
        {
            *out_channel = best_ch;
        }
        if (out_rssi)
        {
            *out_rssi = best;
        }
    }
    WifiSniffer.ok = found;
}

/** @brief Owned state for the live channel-hopping sniff. */
typedef struct
{
    WifiStats stats;
    WifiSurvey survey;
    WifiScan scan;
    proto_bool running;
} WifiSnifferCtx;

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define WIFI_SNIFFER_OFF_CTX 0u
static_assert(WIFI_SNIFFER_OFF_CTX + sizeof(WifiSnifferCtx) <= PROTOCORE_WIFI_SNIFFER_BORROW,
              "PROTOCORE_WIFI_SNIFFER_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(
    WIFI_SNIFFER_OFF_CTX % _Alignof(WifiSnifferCtx) == 0,
    "WIFI_SNIFFER_OFF_CTX is not a multiple of alignof(WifiSnifferCtx) - WIFI_SNIFFER_CTX() would return a misaligned "
    "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define WIFI_SNIFFER_CTX(w) ((WifiSnifferCtx *)(void *)((w) + WIFI_SNIFFER_OFF_CTX))

#if PROTOCORE_ENABLE_PROMISC

// The promisc sink: decode, tally, survey. Runs in the WiFi driver's callback context, so it only
// touches the owned context - no allocation, no blocking.
static void sniffer_sink(const uint8_t *frame, uint16_t len, int8_t rssi, uint8_t channel)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_wifi_sniffer_span();

    WifiFrame f;
    WifiSniffer.parse_args.frame = frame;
    WifiSniffer.parse_args.len = len;
    WifiSniffer.parse_args.out = &f;
    WifiSniffer.parse(work);
    if (!WifiSniffer.ok)
    {
        return;
    }
    WifiSniffer.stats_add_args.s = &WIFI_SNIFFER_CTX(work)->stats;
    WifiSniffer.stats_add_args.f = &f;
    WifiSniffer.stats_add(work);
    WifiSniffer.survey_add_args.s = &WIFI_SNIFFER_CTX(work)->survey;
    WifiSniffer.survey_add_args.channel = channel;
    WifiSniffer.survey_add_args.rssi = rssi;
    WifiSniffer.survey_add_args.f = &f;
    WifiSniffer.survey_add(work);
}

static void wifi_sniffer_begin(uint8_t *restrict work)
{
    uint8_t first_chan = WifiSniffer.begin_args.first_chan;
    uint8_t last_chan = WifiSniffer.begin_args.last_chan;
    uint16_t dwell_ms = WifiSniffer.begin_args.dwell_ms;

    uint32_t now = Clock.ms;
    WifiSniffer.stats_reset_args.s = &WIFI_SNIFFER_CTX(work)->stats;
    wifi_sniffer_stats_reset(work);
    WifiSniffer.scan_init_args.s = &WIFI_SNIFFER_CTX(work)->scan;
    WifiSniffer.scan_init_args.first = first_chan;
    WifiSniffer.scan_init_args.last = last_chan;
    WifiSniffer.scan_init_args.dwell_ms = dwell_ms;
    WifiSniffer.scan_init_args.now_ms = now;
    wifi_sniffer_scan_init(work);
    WifiSniffer.survey_reset_args.s = &WIFI_SNIFFER_CTX(work)->survey;
    WifiSniffer.survey_reset_args.first = WIFI_SNIFFER_CTX(work)->scan.chan_first;
    WifiSniffer.survey_reset_args.count =
        (uint8_t)(WIFI_SNIFFER_CTX(work)->scan.chan_last - WIFI_SNIFFER_CTX(work)->scan.chan_first + 1);
    wifi_sniffer_survey_reset(work);
    Promisc.begin_args.channel = WIFI_SNIFFER_CTX(work)->scan.channel;
    Promisc.begin_args.sink = sniffer_sink;
    Promisc.begin(protocore_promisc_span());
    WIFI_SNIFFER_CTX(work)->running = Promisc.ok;
    WifiSniffer.ok = WIFI_SNIFFER_CTX(work)->running;
}

static void wifi_sniffer_tick(uint8_t *restrict work)
{
    if (!WIFI_SNIFFER_CTX(work)->running)
    {
        return;
    }
    uint32_t now = Clock.ms;
    WifiSniffer.scan_due_args.s = &WIFI_SNIFFER_CTX(work)->scan;
    WifiSniffer.scan_due_args.now_ms = now;
    wifi_sniffer_scan_due(work);
    if (!WifiSniffer.ok)
    {
        return;
    }
    WifiSniffer.scan_next_args.s = &WIFI_SNIFFER_CTX(work)->scan;
    WifiSniffer.scan_next_args.now_ms = now;
    wifi_sniffer_scan_next(work);
    Promisc.set_channel_args.channel = WifiSniffer.value;
    Promisc.set_channel(protocore_promisc_span());
}

static void wifi_sniffer_end(uint8_t *restrict work)
{
    if (!WIFI_SNIFFER_CTX(work)->running)
    {
        return;
    }
    Promisc.end(protocore_promisc_span());
    WIFI_SNIFFER_CTX(work)->running = PROTO_FALSE;
}

static void wifi_sniffer_stats(uint8_t *restrict work)
{
    WifiSniffer.stats_out = &WIFI_SNIFFER_CTX(work)->stats;
}

static void wifi_sniffer_survey(uint8_t *restrict work)
{
    WifiSniffer.survey_out = &WIFI_SNIFFER_CTX(work)->survey;
}

static void wifi_sniffer_scan(uint8_t *restrict work)
{
    WifiSniffer.scan_out = &WIFI_SNIFFER_CTX(work)->scan;
}

#else // no promiscuous capture: the tables are here, nothing feeds them

// The schedule and the tables are set up the same way, so a caller reads the same shape either
// way. There is no source to put in promiscuous mode, so the sniff does not start.
static void wifi_sniffer_begin(uint8_t *restrict work)
{
    uint8_t first_chan = WifiSniffer.begin_args.first_chan;
    uint8_t last_chan = WifiSniffer.begin_args.last_chan;
    uint16_t dwell_ms = WifiSniffer.begin_args.dwell_ms;

    const uint32_t now = Clock.ms;
    WifiSniffer.stats_reset_args.s = &WIFI_SNIFFER_CTX(work)->stats;
    wifi_sniffer_stats_reset(work);
    WifiSniffer.scan_init_args.s = &WIFI_SNIFFER_CTX(work)->scan;
    WifiSniffer.scan_init_args.first = first_chan;
    WifiSniffer.scan_init_args.last = last_chan;
    WifiSniffer.scan_init_args.dwell_ms = dwell_ms;
    WifiSniffer.scan_init_args.now_ms = now;
    wifi_sniffer_scan_init(work);
    WifiSniffer.survey_reset_args.s = &WIFI_SNIFFER_CTX(work)->survey;
    WifiSniffer.survey_reset_args.first = WIFI_SNIFFER_CTX(work)->scan.chan_first;
    WifiSniffer.survey_reset_args.count =
        (uint8_t)(WIFI_SNIFFER_CTX(work)->scan.chan_last - WIFI_SNIFFER_CTX(work)->scan.chan_first + 1);
    wifi_sniffer_survey_reset(work);
    WIFI_SNIFFER_CTX(work)->running = PROTO_FALSE;
    WifiSniffer.ok = PROTO_FALSE;
}

// Hopping moves a capture that is not running, so the dwell schedule stands still.
static void wifi_sniffer_tick(uint8_t *restrict work)
{
    (void)work;
}

static void wifi_sniffer_end(uint8_t *restrict work)
{
    WIFI_SNIFFER_CTX(work)->running = PROTO_FALSE;
}

static void wifi_sniffer_stats(uint8_t *restrict work)
{
    WifiSniffer.stats_out = &WIFI_SNIFFER_CTX(work)->stats;
}

static void wifi_sniffer_survey(uint8_t *restrict work)
{
    WifiSniffer.survey_out = &WIFI_SNIFFER_CTX(work)->survey;
}

static void wifi_sniffer_scan(uint8_t *restrict work)
{
    WifiSniffer.scan_out = &WIFI_SNIFFER_CTX(work)->scan;
}

#endif // PROTOCORE_ENABLE_PROMISC

WifiSnifferNs WifiSniffer = {
    .parse = wifi_sniffer_parse,
    .stats_reset = wifi_sniffer_stats_reset,
    .stats_add = wifi_sniffer_stats_add,
    .should_roam = wifi_sniffer_should_roam,
    .scan_init = wifi_sniffer_scan_init,
    .scan_due = wifi_sniffer_scan_due,
    .scan_next = wifi_sniffer_scan_next,
    .survey_reset = wifi_sniffer_survey_reset,
    .survey_add = wifi_sniffer_survey_add,
    .survey_get = wifi_sniffer_survey_get,
    .survey_best = wifi_sniffer_survey_best,
    .begin = wifi_sniffer_begin,
    .tick = wifi_sniffer_tick,
    .end = wifi_sniffer_end,
    .stats = wifi_sniffer_stats,
    .survey = wifi_sniffer_survey,
    .scan = wifi_sniffer_scan,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_WIFI_SNIFFER
