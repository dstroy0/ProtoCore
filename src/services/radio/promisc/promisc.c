// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file promisc.c
 * @brief promisc implementation: the pure 802.11 header parser + PCAP framing, and the ESP32
 *        esp_wifi promiscuous binding. The parser / PCAP builders are host-identical.
 */

#include "services/radio/promisc/promisc.h"
#include "network_drivers/physical/physical.h"

#if PC_ENABLE_PROMISC

#include <string.h>

#if PROTOCORE_HOT
#include <esp_wifi.h>
#endif
proto_bool wifi_frame_parse(const uint8_t *frame, uint16_t len, WifiFrameInfo *out)
{
    if (!frame || !out || len < 10) // FC(2) + Duration(2) + Addr1(6) - the shortest control frame
    {
        return PROTO_FALSE;
    }
    memset(out, 0, sizeof(*out));

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
        return PROTO_TRUE;
    }

    // Management / data / extension frames carry the full 3-address header + sequence control.
    if (len < 24)
    {
        return PROTO_FALSE;
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
        return PROTO_FALSE;
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
    else if (!out->to_ds && out->from_ds) // from the AP // GCOVR_EXCL_BR_LINE
    {
        out->dst = a1;
        out->bssid = a2;
        out->src = a3;
    }
    // to_ds == false is unreachable below: ruled out by the two branches above.
    else if (out->to_ds && !out->from_ds) // to the AP // GCOVR_EXCL_BR_LINE
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
    return PROTO_TRUE;
}

// libpcap framing (pc_pcap_global_header / pc_pcap_record_header) is in
// shared_primitives/pcap.h - shared with the other capture features.

// --- ESP32 radio binding -----------------------------------------------------------------
#if PROTOCORE_HOT

// All promiscuous-capture state, owned by one instance (internal linkage): the frame sink.
// One named owner, unreachable from any other translation unit.
typedef struct
{
    pc_promisc_sink_fn sink;
} PromiscCtx;
static PromiscCtx s_promisc;

proto_bool pc_promisc_begin(uint8_t channel, pc_promisc_sink_fn sink)
{
    if (!sink)
    {
        return PROTO_FALSE;
    }
    s_promisc.sink = sink;
    // pc_promisc_sink_fn and pc_phy_frame_fn are the same neutral shape, so the sink goes
    // straight down; the vendor packet struct is unwrapped in the backend.
    if (!pc_phy_monitor_begin(channel, sink))
    {
        s_promisc.sink = NULL;
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

void pc_promisc_set_channel(uint8_t channel)
{
    pc_phy_monitor_set_channel(channel);
}

void pc_promisc_end(void)
{
    pc_phy_monitor_end();
    s_promisc.sink = NULL;
}

#else // host build - no radio

proto_bool pc_promisc_begin(uint8_t, pc_promisc_sink_fn)
{
    return PROTO_FALSE;
}
void pc_promisc_set_channel(uint8_t)
{
    // host build: no radio, no channel to set
}
void pc_promisc_end(void)
{
    // host build: no radio, nothing to stop
}

#endif // PROTOCORE_HOT

#endif // PC_ENABLE_PROMISC
