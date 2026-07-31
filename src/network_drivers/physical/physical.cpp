// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file physical.cpp
 * @brief Layer 1 (Physical) - vendor-neutral core.
 *
 * Holds the two things that are not silicon-specific: the pure IP-egress classifier (always compiled,
 * host-tested), and the fallback link stubs used when the selected vendor has no physical backend yet
 * (PC_PHYSICAL_HAS_BACKEND == 0 - host/native builds, or a vendor whose PHY driver is not written).
 * Each vendor's real bring-up lives in board_drivers/physical/<vendor>/
 * (board_drivers/physical/esp/physical_esp.cpp today), chosen
 * by the PC_VENDOR_* selector. The stubs never bring a link up, so a new target builds and runs headless
 * from day one - the L1 analogue of crypto/ falling back to its portable software field path.
 */

#include "physical.h"

// Pure classifier (always compiled, host-tested): map the live egress IP to the interface it belongs to.
pc_iface pc_net_classify_ip(uint32_t egress_ip, uint32_t sta_ip, uint32_t ap_ip)
{
    if (egress_ip == 0)
    {
        return pc_iface::PC_IFACE_ANY;
    }
    if (sta_ip != 0 && egress_ip == sta_ip)
    {
        return pc_iface::PC_IFACE_STA;
    }
    if (ap_ip != 0 && egress_ip == ap_ip)
    {
        return pc_iface::PC_IFACE_AP;
    }
    return pc_iface::PC_IFACE_ETH; // a live route that is neither WiFi IP -> wired
}

#if !PC_PHYSICAL_HAS_BACKEND
// No L1 backend for the selected vendor (host/native, or a not-yet-written PHY): safe no-ops. The radio
// bring-up calls "succeed" (nothing to do) while the link never reports ready, and every readout is empty.

bool init_wifi_physical(const char *, const char *)
{
    return true;
}
bool wifi_ready()
{
    return true;
}
bool init_wifi_radio_physical(uint8_t)
{
    return true;
}
bool init_wifi_ap_physical(const char *, const char *)
{
    return true;
}
bool init_eth_physical(void)
{
    return false; // no Ethernet PHY without a backend
}
bool eth_ready(void)
{
    return false;
}
bool init_ipv6_physical(void)
{
    return false; // no netif without a backend
}
bool net_global_ipv6(pc_ip *)
{
    return false;
}
bool pc_ipv6_ready(void)
{
    return false;
}
uint32_t pc_net_egress_ip(void)
{
    return 0;
}
pc_iface pc_net_egress(void)
{
    return pc_iface::PC_IFACE_ANY;
}
uint32_t pc_net_ap_ip(void)
{
    return 0;
}
int8_t pc_net_rssi(void)
{
    return 0;
}
bool pc_net_mac(uint8_t *)
{
    return false;
}
bool pc_net_egress_mac(uint8_t *)
{
    return false;
}
// Radio control with no radio: report failure rather than pretending. A caller that asks for
// monitor mode on a target without a radio must be able to tell, and pc_phy_ps_get() answering
// "always on" is the truthful answer when nothing can sleep.
bool pc_phy_ps_set(pc_phy_ps)
{
    return false;
}
pc_phy_ps pc_phy_ps_get(void)
{
    return pc_phy_ps::PC_PHY_PS_NONE;
}
bool pc_phy_tx_power_set(int8_t)
{
    return false;
}
bool pc_phy_monitor_begin(uint8_t, pc_phy_frame_fn)
{
    return false;
}
void pc_phy_monitor_set_channel(uint8_t)
{
}
void pc_phy_monitor_end(void)
{
}
size_t pc_net_ssid(char *out, size_t cap)
{
    if (out && cap)
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
