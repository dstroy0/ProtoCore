// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file physical.c
 * @brief Layer 1 (Physical) - vendor-neutral core.
 *
 * The two things here that are not silicon-specific: the IP-egress classifier, and the fallback
 * link stubs used when the selected vendor has no physical backend (PC_PHYSICAL_HAS_BACKEND == 0 -
 * host/native builds, or a vendor whose PHY driver is not written). Each vendor's real bring-up
 * lives in board_drivers/physical/<vendor>/, chosen by the PC_VENDOR_* selector. The stubs never
 * bring a link up, so a target without a backend still builds and runs headless.
 */

#include "physical.h"
#include "radio_power.h" // Radio: the layer carries the radio interface

// Map the live egress IP to the interface it belongs to.
pc_if_kind pc_net_classify_ip(uint32_t egress_ip, uint32_t sta_ip, uint32_t ap_ip)
{
    if (egress_ip == 0)
    {
        return PC_IF_ANY;
    }
    if (sta_ip != 0 && egress_ip == sta_ip)
    {
        return PC_IF_WIFI_STA;
    }
    if (ap_ip != 0 && egress_ip == ap_ip)
    {
        return PC_IF_WIFI_AP;
    }
    return PC_IF_ETH; // a live route that is neither WiFi IP -> wired
}

#if !PC_PHYSICAL_HAS_BACKEND
// No L1 backend for the selected vendor (host/native, or a not-yet-written PHY): safe no-ops. The radio
// bring-up calls "succeed" (nothing to do) while the link never reports ready, and every readout is empty.

proto_bool init_wifi_physical(const char *ssid, const char *pass)
{
    (void)ssid;
    (void)pass;
    return PROTO_TRUE;
}
proto_bool wifi_ready(void)
{
    return PROTO_TRUE;
}
proto_bool init_wifi_radio_physical(uint8_t channel)
{
    (void)channel;
    return PROTO_TRUE;
}
proto_bool init_wifi_ap_physical(const char *ssid, const char *pass)
{
    (void)ssid;
    (void)pass;
    return PROTO_TRUE;
}
proto_bool init_eth_physical(void)
{
    return PROTO_FALSE; // no Ethernet PHY without a backend
}
proto_bool eth_ready(void)
{
    return PROTO_FALSE;
}
proto_bool init_ipv6_physical(void)
{
    return PROTO_FALSE; // no netif without a backend
}
proto_bool net_global_ipv6(pc_ip *out)
{
    (void)out;
    return PROTO_FALSE;
}
proto_bool pc_ipv6_ready(void)
{
    return PROTO_FALSE;
}
uint32_t pc_net_egress_ip(void)
{
    return 0;
}
pc_if_kind pc_net_egress(void)
{
    return PC_IF_ANY;
}
uint32_t pc_net_ap_ip(void)
{
    return 0;
}
int8_t pc_net_rssi(void)
{
    return 0;
}
proto_bool pc_net_mac(uint8_t *out)
{
    (void)out;
    return PROTO_FALSE;
}
proto_bool pc_net_egress_mac(uint8_t *out)
{
    (void)out;
    return PROTO_FALSE;
}
// Radio control with no radio: report failure rather than pretending. A caller that asks for
// monitor mode on a target without a radio must be able to tell, and PC_PHY_PS_NONE is the
// truthful answer when nothing can sleep.
proto_bool pc_phy_ps_set(pc_phy_ps mode)
{
    (void)mode;
    return PROTO_FALSE;
}
pc_phy_ps pc_phy_ps_get(void)
{
    return PC_PHY_PS_NONE;
}
proto_bool pc_phy_tx_power_set(int8_t dbm)
{
    (void)dbm;
    return PROTO_FALSE;
}
proto_bool pc_phy_monitor_begin(uint8_t channel, pc_phy_frame_fn on_frame)
{
    (void)channel;
    (void)on_frame;
    return PROTO_FALSE;
}
void pc_phy_monitor_set_channel(uint8_t channel)
{
    (void)channel;
}
void pc_phy_monitor_end(void)
{
}
size_t pc_net_ssid(char *out, size_t cap)
{
    if (out != NULL && cap != 0)
    {
        out[0] = '\0';
    }
    return 0;
}
uint8_t pc_net_channel(void)
{
    return 0;
}

#endif // !PC_PHYSICAL_HAS_BACKEND

// ---------------------------------------------------------------------------
// The interface registry. One row per interface the application registered, each carrying how to
// put bytes on it. The forwarding plane reads this to fan a frame out; nothing else needs it.
// ---------------------------------------------------------------------------

typedef struct
{
    pc_if_send_fn send;
    void *ctx;
    uint8_t id;
    pc_if_kind kind;
    proto_bool used;
} IfaceRow;

// Every registered interface, owned by one instance (internal linkage).
typedef struct
{
    IfaceRow row[PC_PHY_MAX_IFACES];
} IfaceCtx;
static IfaceCtx s_iface;

static IfaceRow *row_of(uint8_t id)
{
    for (uint8_t i = 0; i < PC_PHY_MAX_IFACES; i++)
    {
        if (s_iface.row[i].used && s_iface.row[i].id == id)
        {
            return &s_iface.row[i];
        }
    }
    return NULL;
}

static proto_bool iface_add(uint8_t id, pc_if_kind kind, pc_if_send_fn send, void *ctx)
{
    if (send == NULL || row_of(id) != NULL)
    {
        return PROTO_FALSE;
    }
    for (uint8_t i = 0; i < PC_PHY_MAX_IFACES; i++)
    {
        if (s_iface.row[i].used)
        {
            continue;
        }
        s_iface.row[i].send = send;
        s_iface.row[i].ctx = ctx;
        s_iface.row[i].id = id;
        s_iface.row[i].kind = kind;
        s_iface.row[i].used = PROTO_TRUE;
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

static void iface_reset(void)
{
    // The used flag is the row: add() writes every other field before setting it.
    for (uint8_t i = 0; i < PC_PHY_MAX_IFACES; i++)
    {
        s_iface.row[i].used = PROTO_FALSE;
    }
}

static proto_bool iface_present(uint8_t id)
{
    return row_of(id) != NULL;
}

static pc_if_kind iface_kind(uint8_t id)
{
    const IfaceRow *r = row_of(id);
    if (r == NULL)
    {
        return PC_IF_ANY;
    }
    return r->kind;
}

static int16_t iface_at(uint8_t i)
{
    if (i >= PC_PHY_MAX_IFACES || !s_iface.row[i].used)
    {
        return PC_IF_NONE;
    }
    return (int16_t)s_iface.row[i].id;
}

static uint8_t iface_count(void)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < PC_PHY_MAX_IFACES; i++)
    {
        if (s_iface.row[i].used)
        {
            n++;
        }
    }
    return n;
}

static proto_bool iface_send(uint8_t id, const uint8_t *data, uint16_t len)
{
    IfaceRow *r = row_of(id);
    if (r == NULL)
    {
        return PROTO_FALSE;
    }
    return r->send(r->id, data, len, r->ctx);
}

static const PhysicalIfaceNs s_iface_ns = {iface_add, iface_reset, iface_present, iface_kind,
                                           iface_at,  iface_count, iface_send};

// The sub-tables and the layer handle. Defined here, in the vendor-neutral core, so they name
// whichever backend the PC_VENDOR_* selector compiled: the stubs below, board_drivers/physical/esp,
// or the mock. A caller reaches L1 through Physical and never through a vendor symbol.
static const PhysicalWifiNs s_wifi = {
    init_wifi_radio_physical, init_wifi_ap_physical, init_wifi_physical, wifi_ready, pc_net_ssid,
    pc_net_channel,           pc_net_rssi,           pc_net_ap_ip};

static const PhysicalEthNs s_eth = {init_eth_physical, eth_ready};

static const PhysicalIp6Ns s_ip6 = {init_ipv6_physical, net_global_ipv6, pc_ipv6_ready};

static const PhysicalLinkNs s_link = {pc_net_egress_mac, pc_net_classify_ip, pc_net_egress_ip, pc_net_egress,
                                      pc_net_mac};

const PhysicalNs Physical = {&s_wifi, &s_eth, &s_ip6, &s_link, &s_iface_ns, &Radio};
