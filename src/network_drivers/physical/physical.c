// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file physical.c
 * @brief Layer 1 (Physical) - the platform-neutral core: the handle, the registry, the classifier.
 *
 * Three things live here. The interface registry, which is this module's own state. The egress
 * classifier, which maps a live default-route IPv4 to the interface it belongs to (RFC 1122
 * sec 3.3.1.2). And the fallback seam, compiled when the selected backend has no PHY
 * (PROTOCORE_PHYSICAL_HAS_BACKEND == 0): no-op definitions of every seam name, so a target with no
 * link still builds and runs headless. The real bring-up lives in core_setup/physical/<vendor>/,
 * chosen by the PROTOCORE_VENDOR_* selector.
 *
 * The seam functions keep their own signatures: each one is defined once per build, here or in the
 * compiled backend, and the calls on @ref Physical wrap them.
 */

#include "physical.h"
#include "radio_power.h" // Radio: the layer carries the radio handle

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

#if !PROTOCORE_PHYSICAL_HAS_BACKEND
// No L1 backend for the selected part: safe no-ops. The radio bring-up calls "succeed" (nothing to
// do) while the link never reports ready, and every readout is empty.

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
    return PROTO_FALSE; // no interface without a backend
}
proto_bool net_global_ipv6(protocore_ip *out)
{
    (void)out;
    return PROTO_FALSE;
}
proto_bool protocore_ipv6_ready(void)
{
    return PROTO_FALSE;
}
uint32_t protocore_net_egress_ip(void)
{
    return 0;
}
protocore_if_kind protocore_net_egress(void)
{
    return PROTOCORE_IF_ANY;
}
uint32_t protocore_net_ap_ip(void)
{
    return 0;
}
int8_t protocore_net_rssi(void)
{
    return 0;
}
proto_bool protocore_net_mac(uint8_t *out)
{
    (void)out;
    return PROTO_FALSE;
}
proto_bool protocore_net_egress_mac(uint8_t *out)
{
    (void)out;
    return PROTO_FALSE;
}
// Radio control with no radio: report failure rather than pretending. A caller that asks for
// monitor mode on a target without a radio must be able to tell, and PROTOCORE_PHY_PS_NONE is the
// truthful answer when nothing can sleep.
proto_bool protocore_phy_ps_set(protocore_phy_ps mode)
{
    (void)mode;
    return PROTO_FALSE;
}
protocore_phy_ps protocore_phy_ps_get(void)
{
    return PROTOCORE_PHY_PS_NONE;
}
proto_bool protocore_phy_tx_power_set(int8_t dbm)
{
    (void)dbm;
    return PROTO_FALSE;
}
proto_bool protocore_phy_monitor_begin(uint8_t channel, protocore_phy_frame_fn on_frame)
{
    (void)channel;
    (void)on_frame;
    return PROTO_FALSE;
}
void protocore_phy_monitor_set_channel(uint8_t channel)
{
    (void)channel;
}
void protocore_phy_monitor_end(void)
{
}
size_t protocore_net_ssid(char *out, size_t cap)
{
    if (out != NULL && cap != 0)
    {
        out[0] = '\0';
    }
    return 0;
}
uint8_t protocore_net_channel(void)
{
    return 0;
}

#endif // !PROTOCORE_PHYSICAL_HAS_BACKEND

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

/**
 * @brief The layer's state and the calls that reach it - what PhysicalNs points at.
 *
 * @var PhysicalInternal::store  the registry rows
 * @var PhysicalInternal::ns     the handle a caller sets a call's members on
 */
struct PhysicalInternal
{
    struct PhysicalStorage *store;
    PhysicalNs *ns;
};

static struct PhysicalStorage s_store;

static struct PhysicalInternal s_physical = {.store = &s_store, .ns = &Physical};

// ---------------------------------------------------------------------------
// Link bring-up and readout: each call sets the seam's arguments from the handle and puts the
// seam's answer back on it.
// ---------------------------------------------------------------------------

static void phy_wifi_init(struct PhysicalInternal *restrict ctx)
{
    ctx->ns->ok = init_wifi_physical(ctx->ns->wifi.ssid, ctx->ns->wifi.password);
}

static void phy_wifi_ready(struct PhysicalInternal *restrict ctx)
{
    ctx->ns->ok = wifi_ready();
}

static void phy_wifi_radio_init(struct PhysicalInternal *restrict ctx)
{
    ctx->ns->ok = init_wifi_radio_physical(ctx->ns->wifi.channel);
}

static void phy_wifi_ap_init(struct PhysicalInternal *restrict ctx)
{
    ctx->ns->ok = init_wifi_ap_physical(ctx->ns->wifi.ssid, ctx->ns->wifi.password);
}

static void phy_wifi_ssid(struct PhysicalInternal *restrict ctx)
{
    ctx->ns->n = protocore_net_ssid(ctx->ns->read.text, ctx->ns->read.cap);
}

static void phy_wifi_channel(struct PhysicalInternal *restrict ctx)
{
    ctx->ns->u8 = protocore_net_channel();
}

static void phy_wifi_rssi(struct PhysicalInternal *restrict ctx)
{
    ctx->ns->i8 = protocore_net_rssi();
}

static void phy_wifi_ap_ip(struct PhysicalInternal *restrict ctx)
{
    ctx->ns->u32 = protocore_net_ap_ip();
}

static void phy_wifi_mac(struct PhysicalInternal *restrict ctx)
{
    ctx->ns->ok = protocore_net_mac(ctx->ns->read.mac);
}

static void phy_eth_init(struct PhysicalInternal *restrict ctx)
{
    ctx->ns->ok = init_eth_physical();
}

static void phy_eth_ready(struct PhysicalInternal *restrict ctx)
{
    ctx->ns->ok = eth_ready();
}

static void phy_ip6_init(struct PhysicalInternal *restrict ctx)
{
    ctx->ns->ok = init_ipv6_physical();
}

static void phy_ip6_global(struct PhysicalInternal *restrict ctx)
{
    ctx->ns->ok = net_global_ipv6(ctx->ns->read.ip6);
}

static void phy_ip6_ready(struct PhysicalInternal *restrict ctx)
{
    ctx->ns->ok = protocore_ipv6_ready();
}

static void phy_egress(struct PhysicalInternal *restrict ctx)
{
    ctx->ns->if_kind = protocore_net_egress();
}

static void phy_egress_ip(struct PhysicalInternal *restrict ctx)
{
    ctx->ns->u32 = protocore_net_egress_ip();
}

static void phy_egress_mac(struct PhysicalInternal *restrict ctx)
{
    ctx->ns->ok = protocore_net_egress_mac(ctx->ns->read.mac);
}

static void phy_classify_ip(struct PhysicalInternal *restrict ctx)
{
    ctx->ns->if_kind = protocore_net_classify_ip(ctx->ns->route.egress_ip, ctx->ns->route.sta_ip, ctx->ns->route.ap_ip);
}

// ---------------------------------------------------------------------------
// The interface registry
// ---------------------------------------------------------------------------

// The row holding the named id, or NULL.
static IfaceRow *row_of(struct PhysicalInternal *restrict ctx)
{
    for (uint8_t i = 0; i < PROTOCORE_PHY_MAX_IFACES; i++)
    {
        if (ctx->store->row[i].used && ctx->store->row[i].id == ctx->ns->iface.id)
        {
            return &ctx->store->row[i];
        }
    }
    return NULL;
}

static void phy_iface_add(struct PhysicalInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    if (ctx->ns->iface.send == NULL || row_of(ctx) != NULL)
    {
        return;
    }
    for (uint8_t i = 0; i < PROTOCORE_PHY_MAX_IFACES; i++)
    {
        if (ctx->store->row[i].used)
        {
            continue;
        }
        ctx->store->row[i].send = ctx->ns->iface.send;
        ctx->store->row[i].ctx = ctx->ns->iface.ctx;
        ctx->store->row[i].id = ctx->ns->iface.id;
        ctx->store->row[i].kind = ctx->ns->iface.kind;
        ctx->store->row[i].used = PROTO_TRUE;
        ctx->ns->ok = PROTO_TRUE;
        return;
    }
}

static void phy_iface_reset(struct PhysicalInternal *restrict ctx)
{
    // The used flag is the row: add writes every other field before setting it.
    for (uint8_t i = 0; i < PROTOCORE_PHY_MAX_IFACES; i++)
    {
        ctx->store->row[i].used = PROTO_FALSE;
    }
}

static void phy_iface_present(struct PhysicalInternal *restrict ctx)
{
    ctx->ns->ok = row_of(ctx) != NULL;
}

static void phy_iface_kind(struct PhysicalInternal *restrict ctx)
{
    const IfaceRow *r = row_of(ctx);

    ctx->ns->if_kind = (r == NULL) ? PROTOCORE_IF_ANY : r->kind;
}

static void phy_iface_at(struct PhysicalInternal *restrict ctx)
{
    const uint8_t i = ctx->ns->iface.i;

    ctx->ns->i16 = PROTOCORE_IF_NONE;
    if (i >= PROTOCORE_PHY_MAX_IFACES || !ctx->store->row[i].used)
    {
        return;
    }
    ctx->ns->i16 = (int16_t)ctx->store->row[i].id;
}

static void phy_iface_count(struct PhysicalInternal *restrict ctx)
{
    uint8_t n = 0;

    for (uint8_t i = 0; i < PROTOCORE_PHY_MAX_IFACES; i++)
    {
        if (ctx->store->row[i].used)
        {
            n++;
        }
    }
    ctx->ns->u8 = n;
}

static void phy_iface_send(struct PhysicalInternal *restrict ctx)
{
    IfaceRow *r = row_of(ctx);

    ctx->ns->ok = PROTO_FALSE;
    if (r == NULL)
    {
        return;
    }
    ctx->ns->ok = r->send(r->id, ctx->ns->iface.data, ctx->ns->iface.len, r->ctx);
}

// Designated, so a member's position in the struct does not decide what it binds to. The calls name
// the seam, so the handle reaches whichever backend the PROTOCORE_VENDOR_* selector compiled: the
// no-op definitions above, a part's backend under core_setup/physical/, or a suite's mock.
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
                       .radio = &Radio,
                       .internal = &s_physical};
