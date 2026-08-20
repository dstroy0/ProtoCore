// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file physical.h
 * @brief Layer 1 (Physical) - link bring-up, the interface registry, and live egress reporting.
 *
 * The IETF host model's lowest layer is the link layer (RFC 1122 sec 1.1.3): it names no physical
 * layer, and the media themselves are IEEE (802.3 wired, 802.11 wireless). What the IETF defines
 * for this layer is what the calls below carry - IP over Ethernet (RFC 894), IP over IEEE 802
 * networks (RFC 1042), the outbound route an interface is chosen by (RFC 1122 sec 3.3.1), IPv6
 * stateless address autoconfiguration (RFC 4862) and the address forms it produces (RFC 4291
 * sec 2.5.4 global unicast, sec 2.5.6 link-local), dual IP layer operation (RFC 4213 sec 2), and
 * the 6-octet hardware address ARP resolves to (RFC 826 sec "Packet format", ar$hln = 6).
 *
 * Bring-up runs in the backend the PROTOCORE_VENDOR_* selector compiled
 * (test/core_setup/physical/<vendor>/), reached through the seam declared below. Failover between
 * interfaces belongs to the stack, which reselects the default route when a link drops, so this
 * layer adds no manager and no tick: it reads the live default route each time it is asked which
 * interface carries outbound traffic (RFC 1122 sec 3.3.1.2 gateway selection).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PHYSICAL_H
#define PROTOCORE_PHYSICAL_H

#include "config/platform/platform.h" // PROTOCORE_VENDOR_* selector (picks the L1 backend)
#include "shared/ip/ip.h"

#include "protocore_config.h" // protocore_if_kind

// There is always a physical (L1) backend to drive. The bring-up (radio, Ethernet PHY, the stack's
// interface access) lives beside its owner - test/core_setup/physical/<vendor>/ for silicon,
// test/core_setup/hal/host/physical/ everywhere else, which is a link that can actually be up and
// answers from it. Every build compiles one of the two, so a link is driven for real on a host as
// well as on a part.
#ifndef PROTOCORE_PHYSICAL_HAS_BACKEND
#define PROTOCORE_PHYSICAL_HAS_BACKEND 1
#endif

// A backend may be written in C++ (it drives the platform's own WiFi and Ethernet objects), so the
// seam below carries C linkage and links against either language unchanged.
PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// The L1 backend seam. Not the caller API: every name here is defined once per build, by the
// backend the PROTOCORE_VENDOR_* selector compiled or by the software backend beside it. A caller
// reaches layer 1 through @ref Physical and never through a seam name.
// ---------------------------------------------------------------------------

/**
 * @brief Start an 802.11 station join to an access point.
 *
 * Returns immediately; association and address configuration are asynchronous. Poll wifi_ready().
 *
 * @param ssid     network SSID, null-terminated (IEEE 802.11-2020 9.4.2.2, at most 32 octets).
 * @param password WPA2 passphrase, null-terminated (IEEE 802.11i, not an IETF protocol).
 */
proto_bool init_wifi_physical(const char *ssid, const char *password);

/** @brief True when the station link is associated and holds an IPv4 address. */
proto_bool wifi_ready(void);

/**
 * @brief Start the radio in station mode without associating.
 *
 * Runs the PHY with no IP link, for peer-to-peer radio messaging and promiscuous capture. Pins the
 * radio to @p channel (1..14) when non-zero; 0 leaves the channel to a capture layer that sets its
 * own (services/radio/promisc).
 */
proto_bool init_wifi_radio_physical(uint8_t channel);

/**
 * @brief Start a softAP, with AP and station coexistence so a station link runs alongside it.
 *
 * @param ssid     softAP SSID, null-terminated.
 * @param password softAP passphrase, null-terminated; >= 8 characters for WPA2, "" for an open AP.
 */
proto_bool init_wifi_ap_physical(const char *ssid, const char *password);

/**
 * @brief Start a wired Ethernet link (PROTOCORE_ENABLE_ETHERNET).
 *
 * The PHY pins, type and clock come from the platform's own Ethernet build flags. Returns
 * immediately; poll eth_ready(). A wired route classifies as PROTOCORE_IF_ETH (RFC 894 framing).
 */
proto_bool init_eth_physical(void);

/** @brief True when the wired link is up and holds an IPv4 address. */
proto_bool eth_ready(void);

/**
 * @brief Enable dual IP layer operation on the WiFi interface (RFC 4213 sec 2, PROTOCORE_ENABLE_IPV6).
 *
 * The interface autoconfigures a link-local address (RFC 4291 sec 2.5.6) and, when a router
 * advertises a prefix, a global unicast address (RFC 4862). Returns immediately; poll
 * protocore_ipv6_ready().
 */
proto_bool init_ipv6_physical(void);

/**
 * @brief The interface's global unicast IPv6 address (RFC 4291 sec 2.5.4).
 * @param[out] out receives the address with family PROTOCORE_IP_V6 when true is returned.
 */
proto_bool net_global_ipv6(protocore_ip *out);

/** @brief True once a global unicast IPv6 address is configured. */
proto_bool protocore_ipv6_ready(void);

/**
 * @brief Which interface carries outbound traffic (RFC 1122 sec 3.3.1.2).
 *
 * Reads the live default route, so it reflects the current state after any failover the stack
 * performed. PROTOCORE_IF_ETH / PROTOCORE_IF_WIFI_STA / PROTOCORE_IF_WIFI_AP, or PROTOCORE_IF_ANY
 * when no route is up.
 */
protocore_if_kind protocore_net_egress(void);

/** @brief IPv4 of the current default-route interface, network byte order (RFC 791 app. B), 0 if none. */
uint32_t protocore_net_egress_ip(void);

/** @brief softAP IPv4, network byte order, or 0 when the softAP is down. */
uint32_t protocore_net_ap_ip(void);

/** @brief Station link RSSI in dBm, or 0 when not associated. */
int8_t protocore_net_rssi(void);

/**
 * @brief Copy the 802.11 station hardware address into @p out (6 octets, RFC 826 ar$hln = 6).
 *
 * The station address, valid once the WiFi driver is up; a wired-only part reads back zeros. For
 * the address in use on the wire right now whatever the link type, use protocore_net_egress_mac().
 */
proto_bool protocore_net_mac(uint8_t out[6]);

/**
 * @brief Copy the hardware address of the current default-route interface into @p out (6 octets).
 *
 * Link-neutral: the Ethernet PHY's address on a wired route, the station address on a wireless one,
 * whichever interface protocore_net_egress_ip() reports. False when no route is up.
 */
proto_bool protocore_net_egress_mac(uint8_t out[6]);

/**
 * @brief Copy the associated SSID into @p out, null-terminated.
 * @return SSID length in octets, or 0 when not associated or @p cap is 0.
 */
size_t protocore_net_ssid(char *out, size_t cap);

/** @brief Station channel (1..14), or 0 when not associated. */
uint8_t protocore_net_channel(void);

/**
 * @brief Map a live default-route IPv4 to the interface it belongs to.
 *
 * Defined in physical.c, so it answers the same on every backend: an egress IP equal to the station
 * or softAP IP is that WiFi interface, any other live IP is a wired route, 0 is no route. A backend
 * calls it from its own egress readout; a caller reaches it as PhysicalNs::classify_ip.
 *
 * @param egress_ip default-route IPv4, network byte order, 0 if none.
 * @param sta_ip    station IPv4, network byte order, 0 if not associated.
 * @param ap_ip     softAP IPv4, network byte order, 0 if the softAP is down.
 */
protocore_if_kind protocore_net_classify_ip(uint32_t egress_ip, uint32_t sta_ip, uint32_t ap_ip);

/* --------------------------------------------------------------------------------------------
 * Radio control (L1 capability contract)
 *
 * Power save and monitor mode are properties of the radio, so they belong to the layer that owns
 * the radio. These names carry no platform vocabulary; the flavoring happens at the edge, in
 * test/core_setup/physical/<vendor>/. Each one reports failure when the selected backend has no radio
 * (PROTOCORE_PHYSICAL_HAS_BACKEND == 0), so callers build and run headless on any target. They are
 * reached through PhysicalNs::radio, which is radio_power.h's handle.
 * ------------------------------------------------------------------------------------------ */

/** @brief Radio power-save mode, in the library's own vocabulary (IEEE 802.11 power management). */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_PHY_PS_NONE = 0,      ///< Radio always on: lowest latency, highest average draw.
    PROTOCORE_PHY_PS_MIN_MODEM = 1, ///< Wake on every DTIM beacon.
    PROTOCORE_PHY_PS_MAX_MODEM = 2, ///< Wake on a longer listen interval: lowest draw, highest latency.
} protocore_phy_ps;

/**
 * @brief One received frame, delivered in neutral terms.
 *
 * Not the platform's received-packet struct, so no platform type reaches a service. The FCS is
 * already stripped.
 *
 * @param frame   frame octets, valid only for the duration of the call.
 * @param len     frame length in octets, FCS excluded.
 * @param rssi    received signal strength, dBm.
 * @param channel channel the frame arrived on.
 */
typedef void (*protocore_phy_frame_fn)(const uint8_t *frame, uint16_t len, int8_t rssi, uint8_t channel);

/** @brief Apply a power-save mode. False when there is no radio backend. */
proto_bool protocore_phy_ps_set(protocore_phy_ps mode);

/** @brief The active power-save mode (PROTOCORE_PHY_PS_NONE when unsupported). */
protocore_phy_ps protocore_phy_ps_get(void);

/**
 * @brief Cap transmit power.
 * @param dbm maximum transmit power in whole dBm; the backend converts to its own unit.
 */
proto_bool protocore_phy_tx_power_set(int8_t dbm);

/** @brief Enter monitor mode on @p channel, delivering frames to @p cb. */
proto_bool protocore_phy_monitor_begin(uint8_t channel, protocore_phy_frame_fn cb);

/** @brief Retune monitor mode to @p channel. */
void protocore_phy_monitor_set_channel(uint8_t channel);

/** @brief Leave monitor mode. */
void protocore_phy_monitor_end(void);

#if PROTOCORE_PHYSICAL_HAS_BACKEND && !PROTOCORE_VENDOR_ESP
/**
 * @brief Hand one captured frame up to the armed monitor sink, the way the radio does.
 *
 * The mock backend's receive path: with no silicon to put the part in monitor mode, this is where
 * a frame enters, so a caller above is driven by a real delivery rather than by its own input.
 *
 * @return false when monitor mode is not running or the frame is empty.
 */
proto_bool protocore_phy_mock_deliver(const uint8_t *frame, uint16_t len, int8_t rssi, uint8_t channel);
#endif

// ---------------------------------------------------------------------------
// The interfaces this device has. An interface is an id, a kind, and the callback that puts octets
// on the wire - all three physical facts, which is why the registry is here and not in the
// forwarding plane that merely chooses between them. A device can carry several, of mixed kind: two
// Ethernet ports, a station and a softAP, a bus bridged to a socket.
// ---------------------------------------------------------------------------

// protocore_if_kind is protocore_config.h's: one vocabulary for the kind and the filter.

/**
 * @brief Put @p len octets on interface @p if_id.
 * @return true if the interface accepted them; false drops.
 */
typedef proto_bool (*protocore_if_send_fn)(uint8_t if_id, const uint8_t *data, uint16_t len, void *ctx);

/** @brief No interface. Reported by PhysicalNs::iface_at for an empty row. */
#define PROTOCORE_IF_NONE (-1)

/** @brief What an 802.11 bring-up takes: the station join, the softAP, and the radio-only start. */
typedef struct
{
    const char *ssid;     ///< SSID a join or a softAP names (IEEE 802.11-2020 9.4.2.2, <= 32 octets)
    const char *password; ///< WPA2 passphrase; "" opens the softAP
    uint8_t channel;      ///< channel a radio-only start pins (1..14); 0 leaves it to a capture layer
} PhysicalWifiArgs;

/** @brief Where a readout copies to. */
typedef struct
{
    uint8_t *mac;      ///< receives a 6-octet hardware address (RFC 826 ar$hln = 6)
    char *text;        ///< receives the SSID, null-terminated
    size_t cap;        ///< how much room text has
    protocore_ip *ip6; ///< receives the global unicast IPv6 address (RFC 4291 sec 2.5.4)
} PhysicalReadArgs;

/** @brief What the egress classifier judges: the live route and the two WiFi addresses. */
typedef struct
{
    uint32_t egress_ip; ///< default-route IPv4, network byte order (RFC 791 app. B); 0 if no route
    uint32_t sta_ip;    ///< station IPv4, network byte order; 0 if not associated
    uint32_t ap_ip;     ///< softAP IPv4, network byte order; 0 if the softAP is down
} PhysicalRouteArgs;

/** @brief What names an interface in the registry, and the frame a send puts on it. */
typedef struct
{
    protocore_if_send_fn send; ///< how octets reach the interface being registered
    void *ctx;                 ///< what that callback is handed back
    const uint8_t *data;       ///< the frame a send puts on the wire
    uint16_t len;              ///< its length in octets
    protocore_if_kind kind;    ///< what the interface being registered is
    uint8_t id;                ///< the interface a call acts on
    uint8_t i;                 ///< the registry row a lookup names
} PhysicalIfaceArgs;

/**
 * @brief Layer 1: link bring-up, what the live link reports, and the interfaces this device has.
 *
 * A caller sets the members a call takes, invokes it through ::Physical, and reads the outcome off
 * the same handle. Bring-up and readout run in the compiled backend; the registry rows and the
 * egress classifier are this module's own, held behind @ref internal.
 *
 * @var PhysicalNs::wifi        what an 802.11 bring-up takes
 * @var PhysicalNs::read        where a readout copies to
 * @var PhysicalNs::route       what the egress classifier judges (RFC 1122 sec 3.3.1)
 * @var PhysicalNs::iface       what names an interface, and the frame a send puts on it
 * @var PhysicalNs::ok          a call's true/false outcome
 * @var PhysicalNs::if_kind     the interface kind a call reports
 * @var PhysicalNs::u32         an IPv4 a call reports, network byte order
 * @var PhysicalNs::n           the SSID length a readout wrote
 * @var PhysicalNs::u8          a channel or a registry count
 * @var PhysicalNs::i8          the RSSI a readout reports, dBm
 * @var PhysicalNs::i16         the interface id a registry row holds, or PROTOCORE_IF_NONE
 * @var PhysicalNs::wifi_init       start the station join to the named AP
 * @var PhysicalNs::wifi_ready      whether the station link is associated with an address
 * @var PhysicalNs::wifi_radio_init start the radio with no IP link, pinned to a channel
 * @var PhysicalNs::wifi_ap_init    start the softAP alongside the station
 * @var PhysicalNs::wifi_ssid       the associated SSID
 * @var PhysicalNs::wifi_channel    the station channel
 * @var PhysicalNs::wifi_rssi       the station RSSI
 * @var PhysicalNs::wifi_ap_ip      the softAP IPv4
 * @var PhysicalNs::wifi_mac        the station hardware address (RFC 826 ar$hln = 6)
 * @var PhysicalNs::eth_init        start the wired link (RFC 894)
 * @var PhysicalNs::eth_ready       whether the wired link is up with an address
 * @var PhysicalNs::ip6_init        enable dual IP layer operation (RFC 4213 sec 2)
 * @var PhysicalNs::ip6_global      the global unicast IPv6 address (RFC 4291 sec 2.5.4)
 * @var PhysicalNs::ip6_ready       whether a global unicast address is configured (RFC 4862)
 * @var PhysicalNs::egress          which interface carries outbound traffic (RFC 1122 sec 3.3.1.2)
 * @var PhysicalNs::egress_ip       that interface's IPv4
 * @var PhysicalNs::egress_mac      that interface's hardware address
 * @var PhysicalNs::classify_ip     map a route's IPv4 to the interface it belongs to
 * @var PhysicalNs::iface_add       register an interface and how to send on it
 * @var PhysicalNs::iface_reset     forget every interface
 * @var PhysicalNs::iface_present   whether the named id is registered
 * @var PhysicalNs::iface_kind      what the named id is
 * @var PhysicalNs::iface_at        the id held in a registry row
 * @var PhysicalNs::iface_count     registered interfaces
 * @var PhysicalNs::iface_send      put the held frame on the named id
 * @var PhysicalNs::radio           the radio handle, radio_power.h's
 */
typedef struct
{
    PhysicalWifiArgs wifi;
    PhysicalReadArgs read;
    PhysicalRouteArgs route;
    PhysicalIfaceArgs iface;
    proto_bool ok;
    protocore_if_kind if_kind;
    uint32_t u32;
    size_t n;
    uint8_t u8;
    int8_t i8;
    int16_t i16;
} PhysicalVars;

/** @brief The operands and the outcome. */
extern PhysicalVars PhysicalV;

/** @brief The entries. */
typedef struct
{
    void (*const wifi_init)(uint8_t *restrict work);
    void (*const wifi_ready)(uint8_t *restrict work);
    void (*const wifi_radio_init)(uint8_t *restrict work);
    void (*const wifi_ap_init)(uint8_t *restrict work);
    void (*const wifi_ssid)(uint8_t *restrict work);
    void (*const wifi_channel)(uint8_t *restrict work);
    void (*const wifi_rssi)(uint8_t *restrict work);
    void (*const wifi_ap_ip)(uint8_t *restrict work);
    void (*const wifi_mac)(uint8_t *restrict work);
    void (*const eth_init)(uint8_t *restrict work);
    void (*const eth_ready)(uint8_t *restrict work);
    void (*const ip6_init)(uint8_t *restrict work);
    void (*const ip6_global)(uint8_t *restrict work);
    void (*const ip6_ready)(uint8_t *restrict work);
    void (*const egress)(uint8_t *restrict work);
    void (*const egress_ip)(uint8_t *restrict work);
    void (*const egress_mac)(uint8_t *restrict work);
    void (*const classify_ip)(uint8_t *restrict work);
    void (*const iface_add)(uint8_t *restrict work);
    void (*const iface_reset)(uint8_t *restrict work);
    void (*const iface_present)(uint8_t *restrict work);
    void (*const iface_kind)(uint8_t *restrict work);
    void (*const iface_at)(uint8_t *restrict work);
    void (*const iface_count)(uint8_t *restrict work);
    void (*const iface_send)(uint8_t *restrict work);
} PhysicalNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in PhysicalV or a region of the borrow at a fixed offset.
void protocore_physical_wifi_init(uint8_t *restrict work);
void protocore_physical_wifi_ready(uint8_t *restrict work);
void protocore_physical_wifi_radio_init(uint8_t *restrict work);
void protocore_physical_wifi_ap_init(uint8_t *restrict work);
void protocore_physical_wifi_ssid(uint8_t *restrict work);
void protocore_physical_wifi_channel(uint8_t *restrict work);
void protocore_physical_wifi_rssi(uint8_t *restrict work);
void protocore_physical_wifi_ap_ip(uint8_t *restrict work);
void protocore_physical_wifi_mac(uint8_t *restrict work);
void protocore_physical_eth_init(uint8_t *restrict work);
void protocore_physical_eth_ready(uint8_t *restrict work);
void protocore_physical_ip6_init(uint8_t *restrict work);
void protocore_physical_ip6_global(uint8_t *restrict work);
void protocore_physical_ip6_ready(uint8_t *restrict work);
void protocore_physical_egress(uint8_t *restrict work);
void protocore_physical_egress_ip(uint8_t *restrict work);
void protocore_physical_egress_mac(uint8_t *restrict work);
void protocore_physical_classify_ip(uint8_t *restrict work);
void protocore_physical_iface_add(uint8_t *restrict work);
void protocore_physical_iface_reset(uint8_t *restrict work);
void protocore_physical_iface_present(uint8_t *restrict work);
void protocore_physical_iface_kind(uint8_t *restrict work);
void protocore_physical_iface_at(uint8_t *restrict work);
void protocore_physical_iface_count(uint8_t *restrict work);
void protocore_physical_iface_send(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Physical.wifi_init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const PhysicalNs Physical __attribute__((unused)) = {
    .wifi_init = protocore_physical_wifi_init,
    .wifi_ready = protocore_physical_wifi_ready,
    .wifi_radio_init = protocore_physical_wifi_radio_init,
    .wifi_ap_init = protocore_physical_wifi_ap_init,
    .wifi_ssid = protocore_physical_wifi_ssid,
    .wifi_channel = protocore_physical_wifi_channel,
    .wifi_rssi = protocore_physical_wifi_rssi,
    .wifi_ap_ip = protocore_physical_wifi_ap_ip,
    .wifi_mac = protocore_physical_wifi_mac,
    .eth_init = protocore_physical_eth_init,
    .eth_ready = protocore_physical_eth_ready,
    .ip6_init = protocore_physical_ip6_init,
    .ip6_global = protocore_physical_ip6_global,
    .ip6_ready = protocore_physical_ip6_ready,
    .egress = protocore_physical_egress,
    .egress_ip = protocore_physical_egress_ip,
    .egress_mac = protocore_physical_egress_mac,
    .classify_ip = protocore_physical_classify_ip,
    .iface_add = protocore_physical_iface_add,
    .iface_reset = protocore_physical_iface_reset,
    .iface_present = protocore_physical_iface_present,
    .iface_kind = protocore_physical_iface_kind,
    .iface_at = protocore_physical_iface_at,
    .iface_count = protocore_physical_iface_count,
    .iface_send = protocore_physical_iface_send,
};

/**
 * @brief The PROTOCORE_PHYSICAL_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_physical_span(void);

PROTOCORE_END_DECLS

#endif
