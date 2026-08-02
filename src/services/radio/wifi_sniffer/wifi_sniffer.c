// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wifi_sniffer.c
 * @brief 802.11 frame decode + traffic tally + RSSI roaming decision (see wifi_sniffer.h).
 */

#include "services/radio/wifi_sniffer/wifi_sniffer.h"

#if PC_ENABLE_WIFI_SNIFFER

#include <string.h>

#if PC_ENABLE_PROMISC
#include "services/radio/promisc/promisc.h" // the promiscuous-capture owner
#include "services/system/clock.h"          // pc_millis - the monotonic source
#endif

proto_bool pc_wifi_parse(const uint8_t *frame, size_t len, WifiFrame *out)
{
    if (!frame || !out || len < 10) // FrameControl(2) + Duration(2) + Address1(6)
    {
        return PROTO_FALSE;
    }

    memset(out, 0, sizeof(*out));

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
    memcpy(out->addr1, frame + 4, 6);
    out->naddr = 1;
    if (len >= 16)
    {
        memcpy(out->addr2, frame + 10, 6);
        out->naddr = 2;
    }
    if (len >= 24)
    {
        memcpy(out->addr3, frame + 16, 6);
        out->naddr = 3;
    }
    return PROTO_TRUE;
}

void pc_wifi_stats_reset(WifiStats *s)
{
    if (s)
    {
        memset(s, 0, sizeof(*s));
    }
}

void pc_wifi_stats_add(WifiStats *s, const WifiFrame *f)
{
    if (!s || !f)
    {
        return;
    }
    switch (f->type)
    {
    case WifiType::WIFI_TYPE_MGMT:
        s->mgmt++;
        break;
    case WifiType::WIFI_TYPE_CTRL:
        s->ctrl++;
        break;
    case WifiType::WIFI_TYPE_DATA:
        s->data++;
        break;
    default:
        s->other++;
        break;
    }
    s->total++;
}

proto_bool pc_wifi_should_roam(int8_t cur_rssi, int8_t cand_rssi, uint8_t hysteresis_db)
{
    // Both are negative dBm (stronger = closer to 0). Roam only if the candidate clears the current
    // by more than the hysteresis, computed in a wide signed type to avoid int8 overflow.
    int32_t margin = (int32_t)cand_rssi - (int32_t)cur_rssi;
    return margin > (int32_t)hysteresis_db;
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

void pc_wifi_scan_init(WifiScan *s, uint8_t first, uint8_t last, uint16_t dwell_ms, uint32_t now_ms)
{
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

proto_bool pc_wifi_scan_due(const WifiScan *s, uint32_t now_ms)
{
    if (!s)
    {
        return PROTO_FALSE;
    }
    // Unsigned subtraction is correct across a millis() rollover.
    return (now_ms - s->last_hop_ms) >= s->dwell_ms;
}

uint8_t pc_wifi_scan_next(WifiScan *s, uint32_t now_ms)
{
    if (!s)
    {
        return 0;
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
    return s->channel;
}

// --- Per-channel RSSI survey ------------------------------------------------------------

void pc_wifi_survey_reset(WifiSurvey *s, uint8_t first, uint8_t count)
{
    if (!s)
    {
        return;
    }
    memset(s, 0, sizeof(*s));
    s->first = clamp_channel(first);
    s->count = (count > PC_WIFI_SNIFFER_MAX_CHANNELS) ? (uint8_t)PC_WIFI_SNIFFER_MAX_CHANNELS : count;
    for (uint8_t i = 0; i < PC_WIFI_SNIFFER_MAX_CHANNELS; i++)
    {
        s->ch[i].best_rssi = PC_WIFI_RSSI_NONE;
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

void pc_wifi_survey_add(WifiSurvey *s, uint8_t channel, int8_t rssi, const WifiFrame *f)
{
    int idx = survey_index(s, channel);
    if (idx < 0)
    {
        return;
    }
    WifiChannelSurvey *e = &s->ch[idx];
    e->frames++;
    if (e->best_rssi == PC_WIFI_RSSI_NONE || rssi > e->best_rssi)
    {
        e->best_rssi = rssi;
        // The transmitter address is the AP for a beacon; only present once addr2 was decoded.
        if (f && f->naddr >= 2)
        {
            memcpy(e->best_bssid, f->addr2, 6);
        }
    }
}

const WifiChannelSurvey *pc_wifi_survey_get(const WifiSurvey *s, uint8_t channel)
{
    int idx = survey_index(s, channel);
    return (idx < 0) ? NULL : &s->ch[idx];
}

proto_bool pc_wifi_survey_best(const WifiSurvey *s, uint8_t exclude_channel, uint8_t *out_channel, int8_t *out_rssi)
{
    if (!s)
    {
        return PROTO_FALSE;
    }
    proto_bool found = PROTO_FALSE;
    uint8_t best_ch = 0;
    int8_t best = PC_WIFI_RSSI_NONE;
    for (uint8_t i = 0; i < s->count; i++)
    {
        uint8_t ch = (uint8_t)(s->first + i);
        if (ch == exclude_channel || s->ch[i].best_rssi == PC_WIFI_RSSI_NONE)
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
    return found;
}

#if PC_ENABLE_PROMISC

/** @brief Owned state for the live channel-hopping sniff. */
typedef struct
{
    WifiStats stats;
    WifiSurvey survey;
    WifiScan scan;
    proto_bool running;
} WifiSnifferCtx;

static WifiSnifferCtx s_sniffer = {};

// The promisc sink: decode, tally, survey. Runs in the WiFi driver's callback context, so it only
// touches the owned context - no allocation, no blocking.
static void sniffer_sink(const uint8_t *frame, uint16_t len, int8_t rssi, uint8_t channel)
{
    WifiFrame f;
    if (!pc_wifi_parse(frame, len, &f))
    {
        return;
    }
    pc_wifi_stats_add(&s_sniffer.stats, &f);
    pc_wifi_survey_add(&s_sniffer.survey, channel, rssi, &f);
}

proto_bool pc_wifi_sniffer_begin(uint8_t first_chan, uint8_t last_chan, uint16_t dwell_ms)
{
    uint32_t now = pc_millis();
    pc_wifi_stats_reset(&s_sniffer.stats);
    pc_wifi_scan_init(&s_sniffer.scan, first_chan, last_chan, dwell_ms, now);
    pc_wifi_survey_reset(&s_sniffer.survey, s_sniffer.scan.chan_first,
                         (uint8_t)(s_sniffer.scan.chan_last - s_sniffer.scan.chan_first + 1));
    s_sniffer.running = pc_promisc_begin(s_sniffer.scan.channel, sniffer_sink);
    return s_sniffer.running;
}

void pc_wifi_sniffer_tick(void)
{
    if (!s_sniffer.running)
    {
        return;
    }
    uint32_t now = pc_millis();
    if (!pc_wifi_scan_due(&s_sniffer.scan, now))
    {
        return;
    }
    pc_promisc_set_channel(pc_wifi_scan_next(&s_sniffer.scan, now));
}

void pc_wifi_sniffer_end(void)
{
    if (!s_sniffer.running)
    {
        return;
    }
    pc_promisc_end();
    s_sniffer.running = PROTO_FALSE;
}

const WifiStats *pc_wifi_sniffer_stats(void)
{
    return &s_sniffer.stats;
}

const WifiSurvey *pc_wifi_sniffer_survey(void)
{
    return &s_sniffer.survey;
}

const WifiScan *pc_wifi_sniffer_scan(void)
{
    return &s_sniffer.scan;
}

#endif // PC_ENABLE_PROMISC

#endif // PC_ENABLE_WIFI_SNIFFER
