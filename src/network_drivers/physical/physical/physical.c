// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file physical.c
 * @brief Layer 1 (Physical) - the platform-neutral core: the handle, the registry, the classifier.
 *
 * Two things live here. The interface registry, which is this module's own state, and the egress
 * classifier, which maps a live default-route IPv4 to the interface it belongs to (RFC 1122
 * sec 3.3.1.2). Neither touches hardware.
 *
 * The L1 seam is defined by the backend, always: the vendor's driver on silicon, chosen by the
 * PROTOCORE_VENDOR_* selector, and the software backend in test/core_setup/hal/host/physical/ on
 * everything else. There is no third arm - a seam that answers nothing makes a mishandled link
 * indistinguishable from a working one.
 */

#include "network_drivers/physical/physical/physical.h"
#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "network_drivers/physical/radio_power/radio_power.h"    // Radio: the layer carries the radio handle

// ---------------------------------------------------------------------------
// The seam this module implements itself
// ---------------------------------------------------------------------------

// Map the live egress IP to the interface it belongs to.
protocore_if_kind protocore_net_classify_ip(uint32_t egress_ip, uint32_t sta_ip, uint32_t ap_ip)
{
    if (egress_ip == 0)
    {
        return PROTOCORE_IF_ANY;
    }
    if (sta_ip != 0 && egress_ip == sta_ip)
    {
        return PROTOCORE_IF_WIFI_STA;
    }
    if (ap_ip != 0 && egress_ip == ap_ip)
    {
        return PROTOCORE_IF_WIFI_AP;
    }
    return PROTOCORE_IF_ETH; // a live route that is neither WiFi IP -> wired
}

// ---------------------------------------------------------------------------
// The layer's state: one registry row per interface the application registered, each carrying how
// to put octets on it. The forwarding plane reads this to fan a frame out; nothing else needs it.
// ---------------------------------------------------------------------------

// One registered interface: what it is, and the callback that puts octets on it.
typedef struct
{
    protocore_if_send_fn send; ///< how octets reach this interface
    void *ctx;                 ///< what that callback is handed back
    uint8_t id;                ///< the id a caller names it by
    protocore_if_kind kind;    ///< what it is (RFC 894 wired, RFC 1042 IEEE 802 wireless, a bus, a radio)
    proto_bool used;           ///< the row holds an interface
} IfaceRow;

/**
 * @brief The layer's compile-time storage: the registry rows.
 *
 * All of it BSS, so an interface costs no heap and nothing lands on a task stack.
 */
struct PhysicalStorage
{
    IfaceRow row[PROTOCORE_PHY_MAX_IFACES];
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define PHYSICAL_OFF_CTX 0u
static_assert(PHYSICAL_OFF_CTX + sizeof(struct PhysicalStorage) <= PROTOCORE_PHYSICAL_BORROW,
              "PROTOCORE_PHYSICAL_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define PHYSICAL_CTX(w) ((struct PhysicalStorage *)(void *)((w) + PHYSICAL_OFF_CTX))

// ---------------------------------------------------------------------------
// Link bring-up and readout: each call sets the seam's arguments from the handle and puts the
// seam's answer back on it.
// ---------------------------------------------------------------------------

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_PHYSICAL_BORROW persistent bytes, or null while the pool was short
} PhysicalOwnCtx;
static PhysicalOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_physical_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_plaintext_persist_span(PROTOCORE_PHYSICAL_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

static void phy_wifi_init(uint8_t *restrict work)
{
    (void)work;
    Physical.ok = init_wifi_physical(Physical.wifi.ssid, Physical.wifi.password);
}

static void phy_wifi_ready(uint8_t *restrict work)
{
    (void)work;
    Physical.ok = wifi_ready();
}

static void phy_wifi_radio_init(uint8_t *restrict work)
{
    (void)work;
    Physical.ok = init_wifi_radio_physical(Physical.wifi.channel);
}

static void phy_wifi_ap_init(uint8_t *restrict work)
{
    (void)work;
    Physical.ok = init_wifi_ap_physical(Physical.wifi.ssid, Physical.wifi.password);
}

static void phy_wifi_ssid(uint8_t *restrict work)
{
    (void)work;
    Physical.n = protocore_net_ssid(Physical.read.text, Physical.read.cap);
}

static void phy_wifi_channel(uint8_t *restrict work)
{
    (void)work;
    Physical.u8 = protocore_net_channel();
}

static void phy_wifi_rssi(uint8_t *restrict work)
{
    (void)work;
    Physical.i8 = protocore_net_rssi();
}

static void phy_wifi_ap_ip(uint8_t *restrict work)
{
    (void)work;
    Physical.u32 = protocore_net_ap_ip();
}

static void phy_wifi_mac(uint8_t *restrict work)
{
    (void)work;
    Physical.ok = protocore_net_mac(Physical.read.mac);
}

static void phy_eth_init(uint8_t *restrict work)
{
    (void)work;
    Physical.ok = init_eth_physical();
}

static void phy_eth_ready(uint8_t *restrict work)
{
    (void)work;
    Physical.ok = eth_ready();
}

static void phy_ip6_init(uint8_t *restrict work)
{
    (void)work;
    Physical.ok = init_ipv6_physical();
}

static void phy_ip6_global(uint8_t *restrict work)
{
    (void)work;
    Physical.ok = net_global_ipv6(Physical.read.ip6);
}

static void phy_ip6_ready(uint8_t *restrict work)
{
    (void)work;
    Physical.ok = protocore_ipv6_ready();
}

static void phy_egress(uint8_t *restrict work)
{
    (void)work;
    Physical.if_kind = protocore_net_egress();
}

static void phy_egress_ip(uint8_t *restrict work)
{
    (void)work;
    Physical.u32 = protocore_net_egress_ip();
}

static void phy_egress_mac(uint8_t *restrict work)
{
    (void)work;
    Physical.ok = protocore_net_egress_mac(Physical.read.mac);
}

static void phy_classify_ip(uint8_t *restrict work)
{
    (void)work;
    Physical.if_kind = protocore_net_classify_ip(Physical.route.egress_ip, Physical.route.sta_ip, Physical.route.ap_ip);
}

// ---------------------------------------------------------------------------
// The interface registry
// ---------------------------------------------------------------------------

// The row holding the named id, or NULL.
static IfaceRow *row_of(uint8_t *restrict work)
{
    for (uint8_t i = 0; i < PROTOCORE_PHY_MAX_IFACES; i++)
    {
        if (PHYSICAL_CTX(work)->row[i].used && PHYSICAL_CTX(work)->row[i].id == Physical.iface.id)
        {
            return &PHYSICAL_CTX(work)->row[i];
        }
    }
    return NULL;
}

static void phy_iface_add(uint8_t *restrict work)
{
    Physical.ok = PROTO_FALSE;
    if (Physical.iface.send == NULL || row_of(work) != NULL)
    {
        return;
    }
    for (uint8_t i = 0; i < PROTOCORE_PHY_MAX_IFACES; i++)
    {
        if (PHYSICAL_CTX(work)->row[i].used)
        {
            continue;
        }
        PHYSICAL_CTX(work)->row[i].send = Physical.iface.send;
        PHYSICAL_CTX(work)->row[i].ctx = Physical.iface.ctx;
        PHYSICAL_CTX(work)->row[i].id = Physical.iface.id;
        PHYSICAL_CTX(work)->row[i].kind = Physical.iface.kind;
        PHYSICAL_CTX(work)->row[i].used = PROTO_TRUE;
        Physical.ok = PROTO_TRUE;
        return;
    }
}

static void phy_iface_reset(uint8_t *restrict work)
{
    // The used flag is the row: add writes every other field before setting it.
    for (uint8_t i = 0; i < PROTOCORE_PHY_MAX_IFACES; i++)
    {
        PHYSICAL_CTX(work)->row[i].used = PROTO_FALSE;
    }
}

static void phy_iface_present(uint8_t *restrict work)
{
    Physical.ok = row_of(work) != NULL;
}

static void phy_iface_kind(uint8_t *restrict work)
{
    const IfaceRow *r = row_of(work);

    Physical.if_kind = (r == NULL) ? PROTOCORE_IF_ANY : r->kind;
}

static void phy_iface_at(uint8_t *restrict work)
{
    const uint8_t i = Physical.iface.i;

    Physical.i16 = PROTOCORE_IF_NONE;
    if (i >= PROTOCORE_PHY_MAX_IFACES || !PHYSICAL_CTX(work)->row[i].used)
    {
        return;
    }
    Physical.i16 = (int16_t)PHYSICAL_CTX(work)->row[i].id;
}

static void phy_iface_count(uint8_t *restrict work)
{
    uint8_t n = 0;

    for (uint8_t i = 0; i < PROTOCORE_PHY_MAX_IFACES; i++)
    {
        if (PHYSICAL_CTX(work)->row[i].used)
        {
            n++;
        }
    }
    Physical.u8 = n;
}

static void phy_iface_send(uint8_t *restrict work)
{
    IfaceRow *r = row_of(work);

    Physical.ok = PROTO_FALSE;
    if (r == NULL)
    {
        return;
    }
    Physical.ok = r->send(r->id, Physical.iface.data, Physical.iface.len, r->ctx);
}

// Designated, so a member's position in the struct does not decide what it binds to. The calls name
// the seam, so the handle reaches whichever backend the PROTOCORE_VENDOR_* selector compiled: the
// no-op definitions above, a part's backend under test/core_setup/physical/, or a suite's mock.
PhysicalNs Physical = {.wifi_init = phy_wifi_init,
                       .wifi_ready = phy_wifi_ready,
                       .wifi_radio_init = phy_wifi_radio_init,
                       .wifi_ap_init = phy_wifi_ap_init,
                       .wifi_ssid = phy_wifi_ssid,
                       .wifi_channel = phy_wifi_channel,
                       .wifi_rssi = phy_wifi_rssi,
                       .wifi_ap_ip = phy_wifi_ap_ip,
                       .wifi_mac = phy_wifi_mac,
                       .eth_init = phy_eth_init,
                       .eth_ready = phy_eth_ready,
                       .ip6_init = phy_ip6_init,
                       .ip6_global = phy_ip6_global,
                       .ip6_ready = phy_ip6_ready,
                       .egress = phy_egress,
                       .egress_ip = phy_egress_ip,
                       .egress_mac = phy_egress_mac,
                       .classify_ip = phy_classify_ip,
                       .iface_add = phy_iface_add,
                       .iface_reset = phy_iface_reset,
                       .iface_present = phy_iface_present,
                       .iface_kind = phy_iface_kind,
                       .iface_at = phy_iface_at,
                       .iface_count = phy_iface_count,
                       .iface_send = phy_iface_send,
                       .radio = &Radio};
