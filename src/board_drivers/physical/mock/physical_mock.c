// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file physical_mock.c
 * @brief Layer 1 (Physical) - mock backend: the hot path with no silicon under it.
 *
 * PC_PHYSICAL_HAS_BACKEND is 1 here, so a test build reaches the same code a target does instead of
 * the no-op stubs in physical.c. That is the point: the stubs answer every readout with an empty
 * value, so a caller that mishandles a live link is indistinguishable from one that works. This
 * backend holds a link state that can actually be up, and answers from it.
 *
 * Semantics are copied from the ESP backend rather than invented, so a behavior that passes here
 * means the same thing on silicon. Where ESP asks the driver, this asks its own context; the
 * decisions (which interface is egress, how an SSID is truncated, when a MAC readout fails) are the
 * same. The link comes up instantly because there is no radio to wait on.
 */

#include "network_drivers/physical/physical.h"

#if PC_VENDOR_MOCK

#include <string.h> // memcpy / strnlen

// IEEE 802.11-2020 9.4.2.2: an SSID element carries at most 32 octets, so this is the standard's
// number and not a tuning choice. The +1 is the terminator pc_net_ssid() always writes.
#define PC_PHY_MOCK_SSID_MAX 32

// Compose a dotted quad into the network-byte-order u32 the pc_net_*_ip() contract returns. Every
// target in the list is little-endian, where lwIP's ip4_addr_get_u32() puts the first octet in the
// low byte, so this reproduces the layout a real netif hands back.
#define PC_PHY_MOCK_IP4(a, b, c, d)                                                                                    \
    (((uint32_t)(d) << 24) | ((uint32_t)(c) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(a))

// RFC 5737 TEST-NET-1 and RFC 3849 2001:db8::/32 are reserved for documentation, so a mock address
// that escapes into a log or a test expectation can never be mistaken for a real one.
#define PC_PHY_MOCK_STA_IP PC_PHY_MOCK_IP4(192, 0, 2, 10)
#define PC_PHY_MOCK_AP_IP PC_PHY_MOCK_IP4(192, 0, 2, 1)
#define PC_PHY_MOCK_ETH_IP PC_PHY_MOCK_IP4(192, 0, 2, 20)

/** @brief Every mutable the mock owns: one link state, read back by the pc_net_* / pc_phy_* calls. */
typedef struct
{
    proto_bool sta_up;        ///< station associated and holding an IP.
    proto_bool ap_up;         ///< softAP started.
    proto_bool eth_up;        ///< wired link up and holding an IP.
    proto_bool v6_up;         ///< IPv6 enabled and a global address configured.
    proto_bool radio_up;      ///< PHY started, with or without an association.
    proto_bool monitor_on;    ///< monitor mode active.
    uint8_t channel;          ///< current radio channel, 0 when the radio is down.
    int8_t rssi;              ///< station RSSI in dBm, reported only while associated.
    int8_t tx_dbm;            ///< transmit power cap last applied.
    pc_phy_ps ps;             ///< power-save mode last applied.
    pc_phy_frame_fn on_frame; ///< monitor-mode sink, NULL when not capturing.
    size_t ssid_len;          ///< bytes of ssid in use, excluding the terminator.
    char ssid[PC_PHY_MOCK_SSID_MAX + 1];
    uint8_t sta_mac[6]; ///< 802.11 station address.
    uint8_t eth_mac[6]; ///< wired PHY address, distinct so egress_mac is checkable.
    uint8_t v6[16];     ///< global IPv6 address, valid while v6_up.
} PhysicalMockCtx;

// Locally administered MACs (bit 1 of the first octet), which is what an address that was never
// assigned by the IEEE is required to set.
static PhysicalMockCtx s_mock = {
    .sta_up = PROTO_FALSE,
    .ap_up = PROTO_FALSE,
    .eth_up = PROTO_FALSE,
    .v6_up = PROTO_FALSE,
    .radio_up = PROTO_FALSE,
    .monitor_on = PROTO_FALSE,
    .channel = 0,
    .rssi = 0,
    .tx_dbm = 0,
    .ps = PC_PHY_PS_NONE,
    .on_frame = NULL,
    .ssid_len = 0,
    .ssid = {0},
    .sta_mac = {0x02, 0x00, 0x5E, 0x10, 0x00, 0x01},
    .eth_mac = {0x02, 0x00, 0x5E, 0x10, 0x00, 0x02},
    .v6 = {0x20, 0x01, 0x0D, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01},
};

// Copy an SSID in under the 802.11 cap, the same truncation pc_net_ssid() applies on the way out.
static void mock_set_ssid(const char *ssid)
{
    if (ssid == NULL)
    {
        s_mock.ssid[0] = '\0';
        s_mock.ssid_len = 0;
        return;
    }
    size_t n = strnlen(ssid, PC_PHY_MOCK_SSID_MAX);
    memcpy(s_mock.ssid, ssid, n);
    s_mock.ssid[n] = '\0';
    s_mock.ssid_len = n;
}

proto_bool init_wifi_physical(const char *ssid, const char *password)
{
    (void)password; // nothing authenticates here, so the passphrase is accepted and dropped
    mock_set_ssid(ssid);
    s_mock.radio_up = PROTO_TRUE;
    s_mock.sta_up = PROTO_TRUE;
    if (s_mock.channel == 0)
    {
        s_mock.channel = 1;
    }
    s_mock.rssi = -55; // a plausible mid-strength association
    return PROTO_TRUE;
}

proto_bool wifi_ready(void)
{
    return s_mock.sta_up;
}

proto_bool init_wifi_radio_physical(uint8_t channel)
{
    s_mock.radio_up = PROTO_TRUE;
    if (channel != 0)
    {
        s_mock.channel = channel; // 0 leaves the channel to whoever captures, per the header
    }
    return PROTO_TRUE;
}

proto_bool init_wifi_ap_physical(const char *ssid, const char *password)
{
    (void)ssid;
    (void)password;
    s_mock.radio_up = PROTO_TRUE;
    s_mock.ap_up = PROTO_TRUE;
    return PROTO_TRUE;
}

#if PC_ENABLE_ETHERNET
proto_bool init_eth_physical(void)
{
    s_mock.eth_up = PROTO_TRUE;
    return PROTO_TRUE;
}

proto_bool eth_ready(void)
{
    return s_mock.eth_up;
}
#else
proto_bool init_eth_physical(void)
{
    return PROTO_FALSE; // Ethernet compiled out
}

proto_bool eth_ready(void)
{
    return PROTO_FALSE;
}
#endif // PC_ENABLE_ETHERNET

#if PC_ENABLE_IPV6
proto_bool init_ipv6_physical(void)
{
    s_mock.v6_up = PROTO_TRUE;
    return PROTO_TRUE;
}

proto_bool net_global_ipv6(pc_ip *out)
{
    if (out == NULL || !s_mock.v6_up)
    {
        return PROTO_FALSE;
    }
    out->family = PC_IP_V6;
    memcpy(out->bytes, s_mock.v6, sizeof(s_mock.v6));
    return PROTO_TRUE;
}

proto_bool pc_ipv6_ready(void)
{
    return s_mock.v6_up;
}
#else
proto_bool init_ipv6_physical(void)
{
    return PROTO_FALSE; // IPv6 compiled out
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
#endif // PC_ENABLE_IPV6

uint32_t pc_net_egress_ip(void)
{
    // Wired wins over wireless, which is the order a stack picks a default route in when both are up.
    if (s_mock.eth_up)
    {
        return PC_PHY_MOCK_ETH_IP;
    }
    if (s_mock.sta_up)
    {
        return PC_PHY_MOCK_STA_IP;
    }
    if (s_mock.ap_up)
    {
        return PC_PHY_MOCK_AP_IP;
    }
    return 0;
}

pc_iface pc_net_egress(void)
{
    uint32_t egress = pc_net_egress_ip();
    if (egress == 0)
    {
        return PC_IFACE_ANY;
    }
    uint32_t sta = s_mock.sta_up ? PC_PHY_MOCK_STA_IP : 0;
    uint32_t ap = pc_net_ap_ip();
    return pc_net_classify_ip(egress, sta, ap); // the shared classifier, same as the ESP backend
}

uint32_t pc_net_ap_ip(void)
{
    return s_mock.ap_up ? PC_PHY_MOCK_AP_IP : 0;
}

int8_t pc_net_rssi(void)
{
    return s_mock.sta_up ? s_mock.rssi : 0;
}

proto_bool pc_net_mac(uint8_t out[6])
{
    if (out == NULL)
    {
        return PROTO_FALSE;
    }
    memcpy(out, s_mock.sta_mac, sizeof(s_mock.sta_mac));
    return PROTO_TRUE;
}

proto_bool pc_net_egress_mac(uint8_t out[6])
{
    // The address of whichever interface carries traffic, so it tracks pc_net_egress_ip()'s choice.
    if (out == NULL || pc_net_egress_ip() == 0)
    {
        return PROTO_FALSE;
    }
    if (s_mock.eth_up)
    {
        memcpy(out, s_mock.eth_mac, sizeof(s_mock.eth_mac));
        return PROTO_TRUE;
    }
    memcpy(out, s_mock.sta_mac, sizeof(s_mock.sta_mac));
    return PROTO_TRUE;
}

size_t pc_net_ssid(char *out, size_t cap)
{
    if (out == NULL || cap == 0)
    {
        return 0;
    }
    if (!s_mock.sta_up)
    {
        out[0] = '\0';
        return 0;
    }
    size_t n = s_mock.ssid_len;
    if (n >= cap)
    {
        n = cap - 1;
    }
    memcpy(out, s_mock.ssid, n);
    out[n] = '\0';
    return n;
}

uint8_t pc_net_channel(void)
{
    return s_mock.channel;
}

/* ------------------------------------------------------------------ radio control (L1 contract)
 * A radio that reports what it was told. The vendor half of pc_phy_* on silicon is unit conversion
 * and a driver call; with neither, the state is the whole implementation.
 */

proto_bool pc_phy_ps_set(pc_phy_ps mode)
{
    s_mock.ps = mode;
    return PROTO_TRUE;
}

pc_phy_ps pc_phy_ps_get(void)
{
    return s_mock.ps;
}

proto_bool pc_phy_tx_power_set(int8_t dbm)
{
    s_mock.tx_dbm = dbm;
    return PROTO_TRUE;
}

proto_bool pc_phy_monitor_begin(uint8_t channel, pc_phy_frame_fn cb)
{
    if (cb == NULL)
    {
        return PROTO_FALSE; // capture with nowhere to deliver is a caller bug, not a mode
    }
    s_mock.radio_up = PROTO_TRUE;
    s_mock.monitor_on = PROTO_TRUE;
    s_mock.on_frame = cb;
    if (channel != 0)
    {
        s_mock.channel = channel;
    }
    return PROTO_TRUE;
}

void pc_phy_monitor_set_channel(uint8_t channel)
{
    if (channel != 0)
    {
        s_mock.channel = channel;
    }
}

void pc_phy_monitor_end(void)
{
    s_mock.monitor_on = PROTO_FALSE;
    s_mock.on_frame = NULL;
}

#endif // PC_VENDOR_MOCK
