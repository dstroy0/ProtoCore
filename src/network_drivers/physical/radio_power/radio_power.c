// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "network_drivers/physical/radio_power/radio_power.h"
#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "network_drivers/physical/physical/physical.h"

/**
 * @brief The module's compile-time storage: the keep-awake count.
 *
 * @var RadioStorage::held  outstanding busy_hold calls; zero puts PROTOCORE_RADIO_WIFI_PS back
 */
struct RadioStorage
{
    int held;
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define RADIO_POWER_OFF_CTX 0u
static_assert(RADIO_POWER_OFF_CTX + sizeof(struct RadioStorage) <= PROTOCORE_RADIO_POWER_BORROW,
              "PROTOCORE_RADIO_POWER_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define RADIO_POWER_CTX(w) ((struct RadioStorage *)(void *)((w) + RADIO_POWER_OFF_CTX))

#if PROTOCORE_ENABLE_RADIO_POWER

// Renders an 802.11-2020 11.2.3.2 power management mode as text.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_RADIO_POWER_BORROW persistent bytes, or null while the pool was short
} RadioOwnCtx;
static RadioOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_radio_power_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_plaintext_persist_span(PROTOCORE_RADIO_POWER_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

static void radio_ps_name(uint8_t *restrict work)
{
    switch (Radio.ps.mode)
    {
    case PROTOCORE_PHY_PS_MIN_MODEM:
        Radio.text = "min_modem";
        break;
    case PROTOCORE_PHY_PS_MAX_MODEM:
        Radio.text = "max_modem";
        break;
    case PROTOCORE_PHY_PS_NONE:
    default:
        Radio.text = "none";
        break;
    }
}

static void radio_power(uint8_t *restrict work)
{
    protocore_phy_ps_set((protocore_phy_ps)PROTOCORE_RADIO_WIFI_PS);
#if PROTOCORE_RADIO_MAX_TX_DBM > 0
    protocore_phy_tx_power_set((int8_t)PROTOCORE_RADIO_MAX_TX_DBM); // whole dBm; the backend owns the unit
#endif
}

static void radio_busy_hold(uint8_t *restrict work)
{
    if (RADIO_POWER_CTX(work)->held == 0)
    {
        protocore_phy_ps_set(PROTOCORE_PHY_PS_NONE); // active mode for the length of the transfer
    }
    RADIO_POWER_CTX(work)->held++;
}

static void radio_busy_release(uint8_t *restrict work)
{
    if (RADIO_POWER_CTX(work)->held > 0)
    {
        RADIO_POWER_CTX(work)->held--;
        if (RADIO_POWER_CTX(work)->held == 0)
        {
            radio_power(work); // last transfer done: the configured mode goes back on
        }
    }
}

#endif // PROTOCORE_ENABLE_RADIO_POWER

// 802.11-2020 6.3.2.2 MLME-POWERMGT.request: select active mode or PS mode.
static void radio_ps_set(uint8_t *restrict work)
{
    Radio.ok = protocore_phy_ps_set(Radio.ps.mode);
}

static void radio_ps_mode(uint8_t *restrict work)
{
    Radio.mode = protocore_phy_ps_get();
}

// 802.11-2020 11.7.6 transmit power selection, bounded by 11.7.5.
static void radio_tx_power_set(uint8_t *restrict work)
{
    Radio.ok = protocore_phy_tx_power_set(Radio.tx.dbm);
}

static void radio_monitor_begin(uint8_t *restrict work)
{
    Radio.ok = protocore_phy_monitor_begin(Radio.monitor.channel, Radio.monitor.on_frame);
}

static void radio_monitor_set_channel(uint8_t *restrict work)
{
    protocore_phy_monitor_set_channel(Radio.monitor.channel);
}

static void radio_monitor_end(uint8_t *restrict work)
{
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
    .monitor_end = radio_monitor_end};
