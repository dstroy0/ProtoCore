// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file physical_esp.c
 * @brief Layer 1 (Physical) - ESP backend: 802.11 radio + wired Ethernet (RMII / W5500 SPI) bring-up and
 *        live egress readout, straight off the Arduino-ESP32 WiFi/ETH wrappers and lwIP's default netif.
 *
 * The vendor-specific half of the physical layer. The common API is in network_drivers/physical/physical.h and the
 * vendor is chosen by the PC_VENDOR_* selector (board_drivers/board_profiles/pc_platform.h); this whole TU compiles to
 * nothing on any non-ESP vendor, where physical.cpp's fallback stubs stand in (PC_PHYSICAL_HAS_BACKEND == 0). WiFi
 * station bring-up is asynchronous (poll wifi_ready()); pc_net_egress() reads the live default-route netif and hands it
 * to the pure pc_net_classify_ip() that lives in physical.cpp. Adding STM/RP/TI = a sibling
 * board_drivers/physical/<vendor>/ TU guarded by that vendor's macro, no change here.
 */

#include "network_drivers/physical/physical.h"

#if PC_VENDOR_ESP

#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include <WiFi.h>
#include <esp_wifi.h> // esp_wifi_set_channel / esp_wifi_sta_get_ap_info (raw-radio bring-up + SSID readout)
#include <string.h>   // strnlen / memcpy
#if PC_ENABLE_ETHERNET
#include <ETH.h>
#endif
#if PC_ENABLE_IPV6
#include "lwip/ip6_addr.h"
#endif

proto_bool init_wifi_physical(const char *ssid, const char *password)
{
    WiFi.begin(ssid, password);
    return PROTO_TRUE;
}

proto_bool wifi_ready()
{
    return WiFi.isConnected();
}

proto_bool init_wifi_radio_physical(uint8_t channel)
{
    // Radio up but not associated: ESP-NOW and promiscuous capture want the PHY without an IP link.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    if (channel)
    {
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    }
    return PROTO_TRUE;
}

proto_bool init_wifi_ap_physical(const char *ssid, const char *password)
{
    WiFi.mode(WIFI_AP_STA); // coexist so a station link can run alongside the softAP
    return WiFi.softAP(ssid, password);
}

#if PC_ENABLE_ETHERNET
proto_bool init_eth_physical(void)
{
#if defined(PC_ETH_W5500) && PC_ETH_W5500 && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    // W5500 SPI Ethernet (arduino-esp32 3.x ETH SPI API): the HSPI host (SPI3) clocks the W5500 on the
    // PC_ETH_W5500_* pins (CS/INT/RST + SCK/MISO/MOSI). Needs CONFIG_ETH_SPI_ETHERNET_W5500 in the SDK
    // (default on for the S3). W5500 SPI is arduino-esp32 3.x only - the 2.x ETH library has no W5500.
    return ETH.begin(ETH_PHY_W5500, 1 /*phy addr*/, PC_ETH_W5500_CS, PC_ETH_W5500_INT, PC_ETH_W5500_RST, SPI3_HOST,
                     PC_ETH_W5500_SCK, PC_ETH_W5500_MISO, PC_ETH_W5500_MOSI, PC_ETH_W5500_SPI_MHZ);
#else
    // RMII PHY: pins / type / clock come from the ETH_PHY_* build flags (ETH.begin() defaults).
    return ETH.begin();
#endif
}
proto_bool eth_ready(void)
{
    return ETH.linkUp() && (uint32_t)ETH.localIP() != 0;
}
#else
proto_bool init_eth_physical(void)
{
    return PROTO_FALSE; // Ethernet not enabled (PC_ENABLE_ETHERNET)
}
proto_bool eth_ready(void)
{
    return PROTO_FALSE;
}
#endif

#if PC_ENABLE_IPV6

proto_bool init_ipv6_physical(void)
{
    // The WiFi wrapper's enable call was renamed in Arduino-ESP32 3.0; the address readout
    // below goes straight to lwIP, which is stable across both cores.
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    return WiFi.enableIPv6(PROTO_TRUE);
#else
    return WiFi.enableIpV6();
#endif
}

proto_bool net_global_ipv6(pc_ip *out)
{
    if (!out || !netif_default)
    {
        return PROTO_FALSE;
    }
    for (int8_t i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++)
    {
        if (!ip6_addr_isvalid(netif_ip6_addr_state(netif_default, i)))
        {
            continue;
        }
        const ip6_addr_t *a6 = netif_ip6_addr(netif_default, i);
        if (!ip6_addr_isglobal(a6))
        {
            continue;
        }
        out->family = PC_IP_V6;
        memcpy(out->bytes, a6->addr, 16); // lwIP holds the 16 bytes in network order
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

proto_bool pc_ipv6_ready(void)
{
    pc_ip tmp;
    return net_global_ipv6(&tmp);
}
#else
proto_bool init_ipv6_physical(void)
{
    return PROTO_FALSE; // IPv6 not enabled (PC_ENABLE_IPV6)
}
proto_bool net_global_ipv6(pc_ip *)
{
    return PROTO_FALSE;
}
proto_bool pc_ipv6_ready(void)
{
    return PROTO_FALSE;
}
#endif

uint32_t pc_net_egress_ip(void)
{
    // netif_default is the current default-route interface (the egress).
    return netif_default ? ip4_addr_get_u32(ip_2_ip4(&netif_default->ip_addr)) : 0;
}

pc_iface pc_net_egress(void)
{
    uint32_t egress = pc_net_egress_ip();
    if (egress == 0)
    {
        return PC_IFACE_ANY;
    }
    uint32_t sta = WiFi.isConnected() ? (uint32_t)WiFi.localIP() : 0;
    uint32_t ap = pc_net_ap_ip();
    return pc_net_classify_ip(egress, sta, ap);
}

uint32_t pc_net_ap_ip(void)
{
    return (WiFi.getMode() & WIFI_AP) ? (uint32_t)WiFi.softAPIP() : 0;
}

int8_t pc_net_rssi(void)
{
    return WiFi.isConnected() ? (int8_t)WiFi.RSSI() : 0;
}

proto_bool pc_net_mac(uint8_t out[6])
{
    if (!out)
    {
        return PROTO_FALSE;
    }
    WiFi.macAddress(out);
    return PROTO_TRUE;
}

proto_bool pc_net_egress_mac(uint8_t out[6])
{
    // The egress interface's own link-layer address, straight off the live default netif - the Ethernet PHY's
    // MAC on a wired link, the WiFi STA MAC on a wireless one. Independent of which driver started.
    if (!out || !netif_default || netif_default->hwaddr_len < 6)
    {
        return PROTO_FALSE;
    }
    memcpy(out, netif_default->hwaddr, 6);
    return PROTO_TRUE;
}

size_t pc_net_ssid(char *out, size_t cap)
{
    if (!out || cap == 0)
    {
        return 0;
    }
    wifi_ap_record_t info; // heap-free SSID readout (WiFi.SSID() would allocate an Arduino String)
    if (esp_wifi_sta_get_ap_info(&info) != ESP_OK)
    {
        out[0] = '\0';
        return 0;
    }
    size_t n = strnlen((const char *)info.ssid, sizeof(info.ssid));
    if (n >= cap)
    {
        n = cap - 1;
    }
    memcpy(out, info.ssid, n);
    out[n] = '\0';
    return n;
}

uint8_t pc_net_channel(void)
{
    return (uint8_t)WiFi.channel();
}

/* ------------------------------------------------------------------ radio control (L1 contract)
 * The vendor half of pc_phy_*: mode translation, the quarter-dBm transmit unit, and the
 * received-packet struct all stay on this side of the boundary. The core sees whole dBm, its own
 * power-save vocabulary, and a plain frame pointer.
 */

static wifi_ps_type_t to_esp_ps(pc_phy_ps mode)
{
    if (mode == PC_PHY_PS_MIN_MODEM)
    {
        return WIFI_PS_MIN_MODEM;
    }
    if (mode == PC_PHY_PS_MAX_MODEM)
    {
        return WIFI_PS_MAX_MODEM;
    }
    return WIFI_PS_NONE;
}

// One named owner for the monitor-mode sink (owner-context guard).
typedef struct
{
    pc_phy_frame_fn sink;
} PhyMonitorCtx;
static PhyMonitorCtx s_phy_monitor = {NULL};

// Vendor shape in, neutral shape out. sig_len includes the 4-byte FCS, which no caller wants.
static void phy_monitor_trampoline(void *buf, wifi_promiscuous_pkt_type_t)
{
    if (!s_phy_monitor.sink || !buf)
    {
        return;
    }
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    uint16_t len = (uint16_t)pkt->rx_ctrl.sig_len;
    if (len < 4)
    {
        return;
    }
    s_phy_monitor.sink(pkt->payload, (uint16_t)(len - 4), (int8_t)pkt->rx_ctrl.rssi, (uint8_t)pkt->rx_ctrl.channel);
}

proto_bool pc_phy_ps_set(pc_phy_ps mode)
{
    return esp_wifi_set_ps(to_esp_ps(mode)) == ESP_OK;
}

pc_phy_ps pc_phy_ps_get(void)
{
    wifi_ps_type_t m = WIFI_PS_NONE;
    if (esp_wifi_get_ps(&m) != ESP_OK)
    {
        return PC_PHY_PS_NONE;
    }
    if (m == WIFI_PS_MIN_MODEM)
    {
        return PC_PHY_PS_MIN_MODEM;
    }
    if (m == WIFI_PS_MAX_MODEM)
    {
        return PC_PHY_PS_MAX_MODEM;
    }
    return PC_PHY_PS_NONE;
}

proto_bool pc_phy_tx_power_set(int8_t dbm)
{
    // The vendor API takes quarter-dBm; that unit is vendor detail, so it converts here.
    return esp_wifi_set_max_tx_power((int8_t)(dbm * 4)) == ESP_OK;
}

proto_bool pc_phy_monitor_begin(uint8_t channel, pc_phy_frame_fn cb)
{
    if (!cb)
    {
        return PROTO_FALSE;
    }
    s_phy_monitor.sink = cb;
    esp_wifi_set_promiscuous(PROTO_TRUE);
    esp_wifi_set_promiscuous_rx_cb(&phy_monitor_trampoline);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    return PROTO_TRUE;
}

void pc_phy_monitor_set_channel(uint8_t channel)
{
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

void pc_phy_monitor_end(void)
{
    esp_wifi_set_promiscuous(PROTO_FALSE);
    s_phy_monitor.sink = NULL;
}

#endif // PC_VENDOR_ESP
