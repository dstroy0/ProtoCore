// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file power_mgmt.c
 * @brief The power governor's decision + its device binding (see power_mgmt.h).
 */

#include "server/core/power_mgmt.h"
#include "mmgr/membuild.h" // protocore_sb frame builder

#if PROTOCORE_ENABLE_POWER_MGMT

// ---------------------------------------------------------------------------
// Pure decision
// ---------------------------------------------------------------------------

/**
 * @brief The governor's compile-time storage: what it latched about this boot.
 */
struct PowerMgmtStorage
{
    proto_bool brownout_latched;
    proto_bool boot_checked;
    proto_bool bt_released;
};

/**
 * @brief The governor's state and the calls that reach it - what PowerMgmtNs points at.
 *
 * @var PowerMgmtInternal::store  what it latched about this boot
 * @var PowerMgmtInternal::ns     the handle a caller sets a call's members on
 */
struct PowerMgmtInternal
{
    struct PowerMgmtStorage *store;
    PowerMgmtNs *ns;
};

static struct PowerMgmtStorage s_store = {PROTO_FALSE, PROTO_FALSE, PROTO_FALSE};

static struct PowerMgmtInternal s_pwr = {.store = &s_store, .ns = &Power};

static void power_defaults(struct PowerMgmtInternal *restrict ctx)
{
    PowerCfg *cfg = ctx->ns->cfg_out;
    if (!cfg)
    {
        return;
    }
    cfg->mhz_max = PROTOCORE_POWER_MHZ_MAX;
    cfg->mhz_min = PROTOCORE_POWER_MHZ_MIN;
    cfg->busy_pct = PROTOCORE_POWER_BUSY_PCT;
    cfg->temp_hot_c = PROTOCORE_POWER_TEMP_HOT_C;
    cfg->temp_cool_c = PROTOCORE_POWER_TEMP_COOL_C;
    cfg->recover_ms = PROTOCORE_POWER_RECOVER_MS;
}

static void power_decide(struct PowerMgmtInternal *restrict ctx)
{
    const PowerCfg *cfg = ctx->ns->plan_args.cfg;
    uint8_t load_pct = ctx->ns->plan_args.load_pct;
    const int16_t temp_c = ctx->ns->plan_args.temp_c;
    const proto_bool brownout_boot = ctx->ns->plan_args.brownout_boot;
    const uint32_t since_boot_ms = ctx->ns->plan_args.since_boot_ms;
    const proto_bool was_throttled = ctx->ns->plan_args.was_throttled;

    PowerPlan p;
    p.cpu_mhz = 0;
    p.throttled = PROTO_FALSE;
    p.recovering = PROTO_FALSE;
    if (!cfg)
    {
        ctx->ns->plan = p;
        return;
    }

    // Hysteresis: once throttled, hold it until the die drops to the *cool* threshold. With one
    // threshold a part sitting at the limit would flap between ceiling and floor every tick.
    // INT16_MIN means "no sensor on this part", which must not read as ice-cold and un-throttle.
    proto_bool have_temp = temp_c != INT16_MIN;
    if (have_temp)
    {
        p.throttled = was_throttled ? (temp_c > cfg->temp_cool_c) : (temp_c >= cfg->temp_hot_c);
    }
    else
    {
        p.throttled = PROTO_FALSE;
    }

    // A supply that just failed under load gets a gentle restart rather than an immediate return to
    // the clock that browned it out.
    p.recovering = brownout_boot && since_boot_ms < cfg->recover_ms;

    if (p.recovering || p.throttled)
    {
        p.cpu_mhz = cfg->mhz_min;
        ctx->ns->plan = p;
        return;
    }
    if (load_pct > 100)
    {
        load_pct = 100;
    }
    p.cpu_mhz = (load_pct >= cfg->busy_pct) ? cfg->mhz_max : cfg->mhz_min;
    ctx->ns->plan = p;
}

static void power_json(struct PowerMgmtInternal *restrict ctx)
{
    const PowerPlan *plan = ctx->ns->out_args.plan;
    const int16_t temp_c = ctx->ns->out_args.temp_c;
    char *out = ctx->ns->out_args.out;
    const size_t cap = ctx->ns->out_args.cap;

    ctx->ns->n = 0;
    if (!plan || !out || cap == 0)
    {
        return;
    }
    // The two forms differ by one field, so one builder emits both rather than two copies that can
    // drift apart.
    protocore_sb sb = {out, cap, 0, PROTO_TRUE};
    Sb.put(&sb, "{\"cpu_mhz\":");
    Sb.u32(&sb, (uint32_t)plan->cpu_mhz);
    Sb.put(&sb, ",\"throttled\":");
    Sb.put(&sb, plan->throttled ? "true" : "false");
    Sb.put(&sb, ",\"recovering\":");
    Sb.put(&sb, plan->recovering ? "true" : "false");
    Sb.put(&sb, ",\"temp_c\":");
    if (temp_c == INT16_MIN) // no sensor: report null rather than a sentinel that reads as a reading
    {
        Sb.put(&sb, "null");
    }
    else
    {
        Sb.i64(&sb, (int64_t)temp_c);
    }
    Sb.put(&sb, "}");
    size_t n = Sb.finish(&sb);
    if (n == 0)
    {
        // Fail closed: the builder writes the pieces that fit before it latches, so without this
        // the caller would be handed a partial object like `{"cpu_mhz":` and no way to know.
        out[0] = '\0';
        return;
    }
    ctx->ns->n = n;
}

// ---------------------------------------------------------------------------
// Device binding
// ---------------------------------------------------------------------------

#if PROTOCORE_HAS_VENDOR_PM

static void power_brownout(struct PowerMgmtInternal *restrict ctx)
{
    // Read once and latch: the reset reason describes this boot, so it must not change under a
    // caller polling it every tick through the recovery window.
    if (!ctx->store->boot_checked)
    {
        ctx->store->brownout_latched = protocore_platform_reset_was_brownout() ? PROTO_TRUE : PROTO_FALSE;
        ctx->store->boot_checked = PROTO_TRUE;
    }
    ctx->ns->ok = ctx->store->brownout_latched;
}

static void power_die_temp(struct PowerMgmtInternal *restrict ctx)
{
    ctx->ns->temp_c = protocore_platform_die_temp_c();
}

static void power_cpu_mhz(struct PowerMgmtInternal *restrict ctx)
{
    ctx->ns->mhz = protocore_platform_cpu_mhz();
}

static void power_apply(struct PowerMgmtInternal *restrict ctx)
{
    const PowerPlan *plan = ctx->ns->out_args.plan;

    ctx->ns->ok = PROTO_FALSE;
    if (!plan || plan->cpu_mhz == 0)
    {
        return;
    }
    if (protocore_platform_cpu_mhz() == plan->cpu_mhz)
    {
        return; // already there; re-setting the clock is not free
    }
    ctx->ns->ok = protocore_platform_set_cpu_mhz((uint32_t)plan->cpu_mhz) ? PROTO_TRUE : PROTO_FALSE;
}

#endif // PROTOCORE_HAS_VENDOR_PM

#if PROTOCORE_HAS_VENDOR_BT
static void power_gate_bt(struct PowerMgmtInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    if (ctx->store->bt_released)
    {
        return; // already handed back, so this call released nothing
    }
    proto_bool ok = protocore_platform_bt_release() ? PROTO_TRUE : PROTO_FALSE;
    ctx->store->bt_released = ok;
    ctx->ns->ok = ok;
}
#endif // PROTOCORE_HAS_VENDOR_BT

// Designated, so a member's position in the struct does not decide what it binds to.
PowerMgmtNs Power = {.defaults = power_defaults,
                     .decide = power_decide,
                     .json = power_json,
#if PROTOCORE_HAS_VENDOR_PM
                     .brownout = power_brownout,
                     .die_temp = power_die_temp,
                     .cpu_mhz = power_cpu_mhz,
                     .apply = power_apply,
#endif
#if PROTOCORE_HAS_VENDOR_BT
                     .gate_bt = power_gate_bt,
#endif
                     .internal = &s_pwr};

#endif // PROTOCORE_ENABLE_POWER_MGMT
