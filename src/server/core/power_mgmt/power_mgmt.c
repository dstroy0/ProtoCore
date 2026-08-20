// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file power_mgmt.c
 * @brief The power governor's decision + its device binding (see power_mgmt.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_POWER_MGMT

#include "mmgr/membuild/membuild.h"   // protocore_sb frame builder
#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "server/core/power_mgmt/power_mgmt.h"

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

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define POWER_MGMT_OFF_CTX 0u
static_assert(POWER_MGMT_OFF_CTX + sizeof(struct PowerMgmtStorage) <= PROTOCORE_POWER_MGMT_BORROW,
              "PROTOCORE_POWER_MGMT_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define POWER_MGMT_CTX(w) ((struct PowerMgmtStorage *)(void *)((w) + POWER_MGMT_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_POWER_MGMT_BORROW persistent bytes
} PowerOwnCtx;
static PowerOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_power_mgmt_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_POWER_MGMT_BORROW).buf;
    }
    return s_own.span;
}

void protocore_power_defaults(uint8_t *restrict work)
{
    (void)work;
    PowerCfg *cfg = PowerV.cfg_out;
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

void protocore_power_decide(uint8_t *restrict work)
{
    (void)work;
    const PowerCfg *cfg = PowerV.plan_args.cfg;
    uint8_t load_pct = PowerV.plan_args.load_pct;
    const int16_t temp_c = PowerV.plan_args.temp_c;
    const proto_bool brownout_boot = PowerV.plan_args.brownout_boot;
    const uint32_t since_boot_ms = PowerV.plan_args.since_boot_ms;
    const proto_bool was_throttled = PowerV.plan_args.was_throttled;

    PowerPlan p;
    p.cpu_mhz = 0;
    p.throttled = PROTO_FALSE;
    p.recovering = PROTO_FALSE;
    if (!cfg)
    {
        PowerV.plan = p;
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
        p.throttled = was_throttled; // no reading crosses no threshold, so the held state stands
    }

    // A supply that just failed under load gets a gentle restart rather than an immediate return to
    // the clock that browned it out.
    p.recovering = brownout_boot && since_boot_ms < cfg->recover_ms;

    if (p.recovering || p.throttled)
    {
        p.cpu_mhz = cfg->mhz_min;
        PowerV.plan = p;
        return;
    }
    if (load_pct > 100)
    {
        load_pct = 100;
    }
    p.cpu_mhz = (load_pct >= cfg->busy_pct) ? cfg->mhz_max : cfg->mhz_min;
    PowerV.plan = p;
}

void protocore_power_json(uint8_t *restrict work)
{
    const PowerPlan *plan = PowerV.out_args.plan;
    const int16_t temp_c = PowerV.out_args.temp_c;
    char *out = PowerV.out_args.out;
    const size_t cap = PowerV.out_args.cap;

    PowerV.n = 0;
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
    PowerV.n = n;
}

// ---------------------------------------------------------------------------
// Device binding
// ---------------------------------------------------------------------------

#if PROTOCORE_HAS_VENDOR_PM

void protocore_power_brownout(uint8_t *restrict work)
{
    // Read once and latch: the reset reason describes this boot, so it must not change under a
    // caller polling it every tick through the recovery window.
    if (!POWER_MGMT_CTX(work)->boot_checked)
    {
        POWER_MGMT_CTX(work)->brownout_latched = protocore_platform_reset_was_brownout() ? PROTO_TRUE : PROTO_FALSE;
        POWER_MGMT_CTX(work)->boot_checked = PROTO_TRUE;
    }
    PowerV.ok = POWER_MGMT_CTX(work)->brownout_latched;
}

void protocore_power_die_temp(uint8_t *restrict work)
{
    (void)work;
    PowerV.temp_c = protocore_platform_die_temp_c();
}

void protocore_power_cpu_mhz(uint8_t *restrict work)
{
    (void)work;
    PowerV.mhz = protocore_platform_cpu_mhz();
}

void protocore_power_apply(uint8_t *restrict work)
{
    (void)work;
    const PowerPlan *plan = PowerV.out_args.plan;

    PowerV.ok = PROTO_FALSE;
    if (!plan || plan->cpu_mhz == 0)
    {
        return;
    }
    if (protocore_platform_cpu_mhz() == plan->cpu_mhz)
    {
        return; // already there; re-setting the clock is not free
    }
    PowerV.ok = protocore_platform_set_cpu_mhz((uint32_t)plan->cpu_mhz) ? PROTO_TRUE : PROTO_FALSE;
}

#endif // PROTOCORE_HAS_VENDOR_PM

#if PROTOCORE_HAS_VENDOR_BT
void protocore_power_gate_bt(uint8_t *restrict work)
{
    PowerV.ok = PROTO_FALSE;
    if (POWER_MGMT_CTX(work)->bt_released)
    {
        return; // already handed back, so this call released nothing
    }
    proto_bool ok = protocore_platform_bt_release() ? PROTO_TRUE : PROTO_FALSE;
    POWER_MGMT_CTX(work)->bt_released = ok;
    PowerV.ok = ok;
}
#endif // PROTOCORE_HAS_VENDOR_BT

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
PowerVars PowerV;

#endif // PROTOCORE_ENABLE_POWER_MGMT
