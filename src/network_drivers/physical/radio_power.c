// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file radio_power.c
 * @brief 802.11 power management mode names (pure) + the phy apply/readback. See radio_power.h.
 *
 * IEEE Std 802.11-2020 11.2 (Power management), 11.7 (TPC procedures) and 6.3.2.2
 * (MLME-POWERMGT.request) are the normative source; no IETF RFC governs these calls.
 *
 * Every function here has internal linkage. The module reaches callers through @ref Radio, which is
 * the only symbol it exports, so nothing in this file can collide with a name anywhere else.
 */

#include "network_drivers/physical/radio_power.h"
#include "network_drivers/physical/physical.h"

/**
 * @brief The module's compile-time storage: the keep-awake count.
 *
 * @var RadioStorage::held  outstanding busy_hold calls; zero puts PROTOCORE_RADIO_WIFI_PS back
 */
struct RadioStorage
{
    int held;
};

/**
 * @brief The radio's state and the calls that reach it - what RadioNs points at.
 *
 * @var RadioInternal::store  the keep-awake count
 * @var RadioInternal::ns     the handle a caller sets a call's members on
 */
struct RadioInternal
{
    struct RadioStorage *store;
    RadioNs *ns;
};

static struct RadioStorage s_store;

static struct RadioInternal s_radio = {.store = &s_store, .ns = &Radio};

#if PROTOCORE_ENABLE_RADIO_POWER

// Renders an 802.11-2020 11.2.3.2 power management mode as text.
static void radio_ps_name(struct RadioInternal *restrict ctx)
{
    switch (ctx->ns->ps.mode)
    {
    case PROTOCORE_PHY_PS_MIN_MODEM:
        ctx->ns->text = "min_modem";
        break;
    case PROTOCORE_PHY_PS_MAX_MODEM:
        ctx->ns->text = "max_modem";
        break;
    case PROTOCORE_PHY_PS_NONE:
    default:
        ctx->ns->text = "none";
        break;
    }
}

#if PROTOCORE_PHYSICAL_HAS_BACKEND

static void radio_power(struct RadioInternal *restrict ctx)
{
    (void)ctx;
    protocore_phy_ps_set((protocore_phy_ps)PROTOCORE_RADIO_WIFI_PS);
#if PROTOCORE_RADIO_MAX_TX_DBM > 0
    protocore_phy_tx_power_set((int8_t)PROTOCORE_RADIO_MAX_TX_DBM); // whole dBm; the backend owns the unit
#endif
}

static void radio_busy_hold(struct RadioInternal *restrict ctx)
{
    if (ctx->store->held == 0)
    {
        protocore_phy_ps_set(PROTOCORE_PHY_PS_NONE); // active mode for the length of the transfer
    }
    ctx->store->held++;
}

static void radio_busy_release(struct RadioInternal *restrict ctx)
{
    if (ctx->store->held > 0)
    {
        ctx->store->held--;
        if (ctx->store->held == 0)
        {
            radio_power(ctx); // last transfer done: the configured mode goes back on
        }
    }
}

#else  // no radio backend

static void radio_power(struct RadioInternal *restrict ctx)
{
    (void)ctx;
}
static void radio_busy_hold(struct RadioInternal *restrict ctx)
{
    (void)ctx;
}
static void radio_busy_release(struct RadioInternal *restrict ctx)
{
    (void)ctx;
}
#endif // PROTOCORE_PHYSICAL_HAS_BACKEND

#endif // PROTOCORE_ENABLE_RADIO_POWER

// 802.11-2020 6.3.2.2 MLME-POWERMGT.request: select active mode or PS mode.
static void radio_ps_set(struct RadioInternal *restrict ctx)
{
    ctx->ns->ok = protocore_phy_ps_set(ctx->ns->ps.mode);
}

static void radio_ps_mode(struct RadioInternal *restrict ctx)
{
    ctx->ns->mode = protocore_phy_ps_get();
}

// 802.11-2020 11.7.6 transmit power selection, bounded by 11.7.5.
static void radio_tx_power_set(struct RadioInternal *restrict ctx)
{
    ctx->ns->ok = protocore_phy_tx_power_set(ctx->ns->tx.dbm);
}

static void radio_monitor_begin(struct RadioInternal *restrict ctx)
{
    ctx->ns->ok = protocore_phy_monitor_begin(ctx->ns->monitor.channel, ctx->ns->monitor.on_frame);
}

static void radio_monitor_set_channel(struct RadioInternal *restrict ctx)
{
    protocore_phy_monitor_set_channel(ctx->ns->monitor.channel);
}

static void radio_monitor_end(struct RadioInternal *restrict ctx)
{
    (void)ctx;
    protocore_phy_monitor_end();
}

// Designated, so a member's position in the struct does not decide what it binds to. The table is
// split by a feature flag, where a positional list shifts every member below the arm at once.
RadioNs Radio = {
#if PROTOCORE_ENABLE_RADIO_POWER
    .power = radio_power,
    .ps_name = radio_ps_name,
    .busy_hold = radio_busy_hold,
    .busy_release = radio_busy_release,
#endif
    .ps_set = radio_ps_set,
    .ps_mode = radio_ps_mode,
    .tx_power_set = radio_tx_power_set,
    .monitor_begin = radio_monitor_begin,
    .monitor_set_channel = radio_monitor_set_channel,
    .monitor_end = radio_monitor_end,
    .internal = &s_radio};
