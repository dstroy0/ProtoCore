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
pc_iface pc_net_classify_ip(uint32_t egress_ip, uint32_t sta_ip, uint32_t ap_ip)
{
    if (egress_ip == 0)
    {
        return PC_IFACE_ANY;
    }
    if (sta_ip != 0 && egress_ip == sta_ip)
    {
        return PC_IFACE_STA;
    }
    if (ap_ip != 0 && egress_ip == ap_ip)
    {
        return PC_IFACE_AP;
    }
    return PC_IFACE_ETH; // a live route that is neither WiFi IP -> wired
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
pc_iface pc_net_egress(void)
{
    return PC_IFACE_ANY;
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

// The sub-tables and the layer handle. Defined here, in the vendor-neutral core, so they name
// whichever backend the PC_VENDOR_* selector compiled: the stubs below, board_drivers/physical/esp,
// or the mock. A caller reaches L1 through Physical and never through a vendor symbol.
static const PhysicalWifiNs s_wifi = {
    init_wifi_radio_physical, init_wifi_ap_physical, init_wifi_physical, wifi_ready, pc_net_ssid,
    pc_net_channel,           pc_net_rssi,           pc_net_ap_ip};

#if PC_ENABLE_ETHERNET
static const PhysicalEthNs s_eth = {init_eth_physical, eth_ready};
#endif

#if PC_ENABLE_IPV6
static const PhysicalIp6Ns s_ip6 = {init_ipv6_physical, net_global_ipv6, pc_ipv6_ready};
#endif

static const PhysicalLinkNs s_link = {pc_net_egress_mac, pc_net_classify_ip, pc_net_egress_ip, pc_net_egress,
                                      pc_net_mac};

const PhysicalNs Physical = {&s_wifi,
#if PC_ENABLE_ETHERNET
                             &s_eth,
#endif
#if PC_ENABLE_IPV6
                             &s_ip6,
#endif
                             &s_link, &Radio};
