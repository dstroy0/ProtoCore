// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file physical.h
 * @brief Layer 1 (Physical) - link bring-up and live egress-interface reporting.
 *
 * The "physical" link is the 802.11 radio or a wired Ethernet PHY, brought up by
 * the vendor backend selected with PC_VENDOR_* (board_drivers/physical/<vendor>/).
 * Failover between interfaces is owned by the network stack itself (it reselects the
 * default route when a link drops) - this layer adds no manager and no polling
 * tick; it only *reports* which interface currently carries outbound traffic via
 * pc_net_egress(), read on demand from the live default route so the answer is
 * always current.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PHYSICAL_H
#define PROTOCORE_PHYSICAL_H

#include "board_drivers/board_profiles/pc_platform.h" // PC_VENDOR_* selector (picks the L1 backend)
#include "protocore_config.h"                         // pc_iface
#include "shared_primitives/ip.h"

// Does the selected vendor ship a physical (L1) backend? The real bring-up (radio / Ethernet PHY /
// lwIP netif access) lives in a per-vendor subdir - physical/esp/ and physical/mock/ today; add
// physical/stm/, physical/rp/ as they land, and OR their vendor here. When 0 (host/native, or a
// vendor whose PHY driver is not written), physical.c supplies no-op stubs so the target still
// builds headless.
#if PC_VENDOR_ESP || PC_VENDOR_MOCK
#define PC_PHYSICAL_HAS_BACKEND 1
#else
#define PC_PHYSICAL_HAS_BACKEND 0
#endif

// The ESP backend is C++ (it calls the Arduino WiFi and ETH objects), so these names carry C
// linkage and the C callers above this layer link against them unchanged.
PROTO_BEGIN_DECLS

// ---------------------------------------------------------------------------
// The backend contract: what a vendor's physical driver implements.
//
// These are the seam between this layer and board_drivers/physical/<vendor>/. Nothing above L1
// names them. A caller reaches the interfaces through @ref Physical and the radio through
// @ref Radio, declared at the end of this file, which is what the sub-tables are built from.
// ---------------------------------------------------------------------------

/**
 * @brief Connect to a WiFi access point.
 *
 * Starts the station join and returns immediately; it does not block waiting for
 * association. Poll wifi_ready() to check link status.
 *
 * @param ssid     Network SSID (null-terminated).
 * @param password WPA2 passphrase (null-terminated).
 * @return Always returns true (the join is fire-and-forget).
 */
proto_bool init_wifi_physical(const char *ssid, const char *password);

/** @brief True if the WiFi station link is up (associated + an IP is assigned). */
proto_bool wifi_ready(void);

/**
 * @brief Bring the WiFi radio up in station mode WITHOUT associating to an AP.
 *
 * For raw-radio use that needs the PHY running but no IP link: ESP-NOW peer messaging and
 * promiscuous capture. Pins the radio to @p channel (1..14) when non-zero; pass 0 to leave the
 * channel to a capture layer that sets its own (services/radio/promisc). Returns immediately.
 *
 * @return true once the radio is started (always true on host builds).
 */
proto_bool init_wifi_radio_physical(uint8_t channel);

/**
 * @brief Bring up a softAP, enabling AP+STA coexistence so a station link can run alongside it.
 *
 * @param ssid     softAP SSID (null-terminated).
 * @param password softAP passphrase (null-terminated; >= 8 chars for WPA2, "" for an open AP).
 * @return true if the softAP started (false on host builds).
 */
proto_bool init_wifi_ap_physical(const char *ssid, const char *password);

/**
 * @brief Bring up a wired Ethernet link (PC_ENABLE_ETHERNET).
 *
 * A thin wrapper over the Arduino ETH library (`ETH.begin()`); the RMII PHY pins / type /
 * clock come from the standard `ETH_PHY_*` build flags for your board. Returns immediately
 * (bring-up is asynchronous); poll eth_ready(). The egress reporting already classifies a
 * wired route as PC_IFACE_ETH, so the server accepts on the link once it has an IP.
 *
 * @return true if ETH.begin() started the driver; false if Ethernet is disabled at build
 *         time or the driver failed to start (and always false on host builds).
 */
proto_bool init_eth_physical(void);

/** @brief True if the Ethernet link is up and an IP is assigned. */
proto_bool eth_ready(void);

/**
 * @brief Enable IPv6 (dual-stack) on the Wi-Fi interface (PC_ENABLE_IPV6).
 *
 * Turns on IPv6 for the netif so it acquires a SLAAC link-local address and, if the network
 * advertises a prefix, a global address. Returns immediately (address configuration is
 * asynchronous); poll pc_ipv6_ready(). The listeners already bind IPADDR_TYPE_ANY, so the server
 * answers over IPv6 as soon as an address is up.
 *
 * @return true if IPv6 was enabled; false if disabled at build time or on host builds.
 */
proto_bool init_ipv6_physical(void);

/**
 * @brief The interface's global (routable) IPv6 address, if it has one.
 * @param[out] out receives the address (family PC_IP_V6) when true is returned.
 * @return true if a valid global IPv6 address is assigned; false otherwise (incl. host builds).
 */
proto_bool net_global_ipv6(pc_ip *out);

/** @brief True once the interface has a global IPv6 address (see net_global_ipv6()). */
proto_bool pc_ipv6_ready(void);

/**
 * @brief Which interface currently carries outbound traffic.
 *
 * Reads the live lwIP default route, so it reflects the current state after any
 * failover the stack performed - no polling, no cached state. Returns PC_IFACE_ETH /
 * PC_IFACE_STA / PC_IFACE_AP, or PC_IFACE_ANY when no route is up (and on host builds).
 */
pc_iface pc_net_egress(void);

/** @brief IPv4 (network byte order) of the current egress interface, or 0 if none. */
uint32_t pc_net_egress_ip(void);

/** @brief softAP IPv4 (network byte order), or 0 if the softAP is not up (and on host builds). */
uint32_t pc_net_ap_ip(void);

/** @brief Station link RSSI in dBm, or 0 if not associated (and on host builds). */
int8_t pc_net_rssi(void);

/**
 * @brief Copy the WiFi station interface MAC (6 bytes) into @p out.
 *
 * This is specifically the 802.11 STA address (what ESP-NOW and WiFi diagnostics want). It is only valid once
 * the WiFi driver is up; on an Ethernet-only device (e.g. the P4 that never starts WiFi) it reads back as
 * zeros. For "the MAC this device is actually using on the wire right now", regardless of link type, use
 * pc_net_egress_mac().
 *
 * @return true on success; false if @p out is null or on a host build (out is left untouched).
 */
proto_bool pc_net_mac(uint8_t out[6]);

/**
 * @brief Copy the MAC of the current egress interface (the live default-route netif) into @p out.
 *
 * Vendor- and link-neutral: returns the Ethernet PHY's MAC on a wired link, the WiFi STA MAC on a wireless
 * one - whichever netif currently carries outbound traffic (the same interface pc_net_egress_ip() reports).
 *
 * @return true and fills @p out when a default interface with a 6-byte hwaddr exists; false otherwise (no
 *         egress up, @p out null, or a host build), leaving @p out untouched.
 */
proto_bool pc_net_egress_mac(uint8_t out[6]);

/**
 * @brief Copy the associated SSID (null-terminated) into @p out.
 * @return the SSID length in bytes, or 0 if not associated, @p cap is 0, or on a host build.
 */
size_t pc_net_ssid(char *out, size_t cap);

/** @brief Station WiFi channel (1..14), or 0 if not associated (and on host builds). */
uint8_t pc_net_channel(void);

/**
 * @brief Classify an egress IPv4 against the WiFi station / softAP IPs (pure helper,
 *        exposed for unit testing).
 *
 * A live egress IP equal to the station or softAP IP is that WiFi interface; any
 * other live IP is a wired (Ethernet) route; 0 is no route.
 *
 * @param egress_ip Current default-route IPv4 (network order), 0 if none.
 * @param sta_ip    WiFi station IPv4 (network order), 0 if not connected.
 * @param ap_ip     softAP IPv4 (network order), 0 if the softAP is not up.
 */
pc_iface pc_net_classify_ip(uint32_t egress_ip, uint32_t sta_ip, uint32_t ap_ip);

/* --------------------------------------------------------------------------------------------
 * Radio control (L1 capability contract)
 *
 * Power save and monitor mode are properties of the radio, so they belong to the layer that owns
 * the radio. The core's API names no vendor; the flavoring happens at the edge, in
 * board_drivers/physical/<vendor>/.
 *
 * Every entry point below returns false / does nothing when the selected vendor has no radio
 * backend (PC_PHYSICAL_HAS_BACKEND == 0), so callers build and run headless on any target.
 * ------------------------------------------------------------------------------------------ */

/** @brief Radio power-save mode, in the library's own vocabulary. */
typedef enum PROTO_ENUM_PACKED
{
    PC_PHY_PS_NONE = 0,      ///< Radio always on: lowest latency, highest average draw.
    PC_PHY_PS_MIN_MODEM = 1, ///< Wake on every DTIM beacon.
    PC_PHY_PS_MAX_MODEM = 2, ///< Wake on a longer listen interval: lowest draw, highest latency.
} pc_phy_ps;

/**
 * @brief One received frame, delivered in neutral terms.
 *
 * Deliberately not the vendor's received-packet struct, so no vendor type reaches a service. The
 * FCS is already stripped.
 *
 * @param frame   Frame bytes, valid only for the duration of the call.
 * @param len     Frame length in bytes, FCS excluded.
 * @param rssi    Received signal strength, dBm.
 * @param channel Channel the frame arrived on.
 */
typedef void (*pc_phy_frame_fn)(const uint8_t *frame, uint16_t len, int8_t rssi, uint8_t channel);

/** @brief Apply a power-save mode. @return false if there is no radio backend. */
proto_bool pc_phy_ps_set(pc_phy_ps mode);

/** @brief Read the active power-save mode (PC_PHY_PS_NONE when unsupported). */
pc_phy_ps pc_phy_ps_get(void);

/**
 * @brief Cap transmit power.
 * @param dbm Maximum transmit power in whole dBm; the backend converts to its own unit.
 * @return false if there is no radio backend.
 */
proto_bool pc_phy_tx_power_set(int8_t dbm);

/** @brief Enter monitor mode on @p channel, delivering frames to @p cb. */
proto_bool pc_phy_monitor_begin(uint8_t channel, pc_phy_frame_fn cb);

/** @brief Retune monitor mode to @p channel. */
void pc_phy_monitor_set_channel(uint8_t channel);

/** @brief Leave monitor mode. */
void pc_phy_monitor_end(void);

/** @brief The Wi-Fi station and softAP interface. */
typedef struct
{
    proto_bool (*init_radio)(uint8_t channel);
    proto_bool (*init_ap)(const char *ssid, const char *password);
    proto_bool (*init)(const char *ssid, const char *password);
    proto_bool (*ready)(void);
    size_t (*ssid)(char *out, size_t cap);
    uint8_t (*channel)(void);
    int8_t (*rssi)(void);
    uint32_t (*ap_ip)(void);
} PhysicalWifiNs;

/** @brief The wired Ethernet interface. */
typedef struct
{
    proto_bool (*init)(void);
    proto_bool (*ready)(void);
} PhysicalEthNs;

/** @brief The IPv6 dual-stack interface. */
typedef struct
{
    proto_bool (*init)(void);
    proto_bool (*global_addr)(pc_ip *out);
    proto_bool (*ready)(void);
} PhysicalIp6Ns;

/** @brief What the live link reports about itself. */
typedef struct
{
    proto_bool (*egress_mac)(uint8_t out[6]);
    pc_iface (*classify_ip)(uint32_t egress_ip, uint32_t sta_ip, uint32_t ap_ip);
    uint32_t (*egress_ip)(void);
    pc_iface (*egress)(void);
    proto_bool (*mac)(uint8_t out[6]);
} PhysicalLinkNs;

/**
 * @brief The radio interface, defined in radio_power.h.
 *
 * Named here rather than included: radio_power.h needs this file's pc_phy_ps and pc_phy_frame_fn, so
 * the dependency runs one way. A child is a pointer, so its declaration is all this needs.
 */
typedef struct RadioNs RadioNs;

/**
 * @brief Layer 1: the interfaces this device actually has.
 *
 * A child is a pointer because a table in one translation unit is not a constant
 * expression in another, the same reason Tcp carries conn, listener and client that way.
 * A child behind a feature flag is declared under it, so the layer names only what the
 * image contains.
 */
typedef struct
{
    const PhysicalWifiNs *wifi;
#if PC_ENABLE_ETHERNET
    const PhysicalEthNs *eth;
#endif
#if PC_ENABLE_IPV6
    const PhysicalIp6Ns *ip6;
#endif
    const PhysicalLinkNs *link;
    const RadioNs *radio;
} PhysicalNs;

/** @brief The one symbol this module exports. */
extern const PhysicalNs Physical;

PROTO_END_DECLS

#endif
