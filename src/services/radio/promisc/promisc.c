// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file promisc.c
 * @brief promisc implementation: the pure 802.11 header parser + PCAP framing, and the ESP32
 *        esp_wifi promiscuous binding. The parser / PCAP builders are host-identical.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_PROMISC

#include "mmgr/plaintext.h" // the persistent end this module's state is taken from
#include "mmgr/protomem.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/physical/radio_power.h" // Radio: the monitor-mode seam this drives
#include "services/radio/promisc/promisc.h"
#include "shared/pcap/pcap.h"

PROTOCORE_BEGIN_DECLS

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_PROMISC_BORROW persistent bytes, or null while the pool was short
} PromiscOwnCtx;
static PromiscOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_promisc_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_plaintext_persist_span(PROTOCORE_PROMISC_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

static void promisc_wifi_frame_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *frame = Promisc.wifi_frame_parse_args.frame;
    uint16_t len = Promisc.wifi_frame_parse_args.len;
    WifiFrameInfo *out = Promisc.wifi_frame_parse_args.out;

    if (!frame || !out || len < 10) // FC(2) + Duration(2) + Addr1(6) - the shortest control frame
    {
        Promisc.ok = PROTO_FALSE;
        return;
    }
    mem.set(out, 0, sizeof(*out));

    const uint8_t fc0 = frame[0];
    const uint8_t fc1 = frame[1];
    out->type = (WifiFrameType)((fc0 >> 2) & 0x3);
    out->subtype = (uint8_t)((fc0 >> 4) & 0xF);
    out->to_ds = (fc1 & 0x01) != 0;
    out->from_ds = (fc1 & 0x02) != 0;
    out->protected_frame = (fc1 & 0x40) != 0;

    if (out->type == WIFI_FT_CTRL)
    {
        // Control frames carry only Addr1 (the receiver); the rest vary by subtype.
        out->dst = frame + 4;
        out->hdr_len = 10;
        Promisc.ok = PROTO_TRUE;
        return;
    }

    // Management / data / extension frames carry the full 3-address header + sequence control.
    if (len < 24)
    {
        Promisc.ok = PROTO_FALSE;
        return;
    }
    out->seq = (uint16_t)(((uint16_t)frame[22] | ((uint16_t)frame[23] << 8)) >> 4);
    out->is_qos = (out->type == WIFI_FT_DATA) && (out->subtype & 0x08) != 0;

    const proto_bool has_addr4 = out->to_ds && out->from_ds; // WDS
    uint16_t hlen = 24;
    if (has_addr4)
    {
        hlen += 6;
    }
    if (out->is_qos)
    {
        hlen += 2;
    }
    if (out->is_qos && (fc1 & 0x80)) // Order bit on a QoS data frame -> HT Control field
    {
        hlen += 4;
    }
    if (len < hlen)
    {
        Promisc.ok = PROTO_FALSE;
        return;
    }
    out->hdr_len = hlen;

    const uint8_t *a1 = frame + 4;
    const uint8_t *a2 = frame + 10;
    const uint8_t *a3 = frame + 16;
    if (!out->to_ds && !out->from_ds) // IBSS / management
    {
        out->dst = a1;
        out->src = a2;
        out->bssid = a3;
    }
    // !to_ds && !from_ds is unreachable below: the `if` above already caught that combination.
    else if (!out->to_ds && out->from_ds)
    {
        out->dst = a1;
        out->bssid = a2;
        out->src = a3;
    }
    // to_ds == false is unreachable below: ruled out by the two branches above.
    else if (out->to_ds && !out->from_ds)
    {
        out->bssid = a1;
        out->src = a2;
        out->dst = a3;
    }
    else // WDS (4-address): A1=RA, A2=TA, A3=DA, A4=SA
    {
        out->dst = a3;
        out->src = frame + 24;
        out->bssid = NULL;
    }
    Promisc.ok = PROTO_TRUE;
}

// libpcap framing (Pcap.global_header / Pcap.record_header) is in shared/pcap/pcap.h - shared with
// the other capture features.

// --- radio binding -----------------------------------------------------------------------

// All promiscuous-capture state, owned by one instance (internal linkage): the frame sink.
// One named owner, unreachable from any other translation unit.
typedef struct
{
    protocore_promisc_sink_fn sink;
} PromiscCtx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define PROMISC_OFF_CTX 0u
static_assert(PROMISC_OFF_CTX + sizeof(PromiscCtx) <= PROTOCORE_PROMISC_BORROW,
              "PROTOCORE_PROMISC_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define PROMISC_CTX(w) ((PromiscCtx *)(void *)((w) + PROMISC_OFF_CTX))

static void promisc_begin(uint8_t *restrict work)
{
    uint8_t channel = Promisc.begin_args.channel;
    protocore_promisc_sink_fn sink = Promisc.begin_args.sink;

    if (!sink)
    {
        Promisc.ok = PROTO_FALSE;
        return;
    }
    PROMISC_CTX(work)->sink = sink;
    // protocore_promisc_sink_fn and protocore_phy_frame_fn are the same neutral shape, so the sink goes
    // straight down; the platform's received-packet struct is unwrapped in the backend.
    Radio.monitor.channel = channel;
    Radio.monitor.on_frame = sink;
    Radio.monitor_begin(protocore_radio_power_span());
    if (!Radio.ok)
    {
        PROMISC_CTX(work)->sink = NULL;
        Promisc.ok = PROTO_FALSE;
        return;
    }
    Promisc.ok = PROTO_TRUE;
}

static void promisc_set_channel(uint8_t *restrict work)
{
    (void)work;
    uint8_t channel = Promisc.set_channel_args.channel;

    Radio.monitor.channel = channel;
    Radio.monitor_set_channel(protocore_radio_power_span());
}

static void promisc_end(uint8_t *restrict work)
{

    Radio.monitor_end(protocore_radio_power_span());
    PROMISC_CTX(work)->sink = NULL;
}

PromiscNs Promisc = {
    .wifi_frame_parse = promisc_wifi_frame_parse,
    .begin = promisc_begin,
    .set_channel = promisc_set_channel,
    .end = promisc_end,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_PROMISC
