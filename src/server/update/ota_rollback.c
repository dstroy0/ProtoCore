// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ota_rollback.c
 * @brief OTA rollback decision (pure) + the platform seam's commit/rollback.
 */

#include "server/update/ota_rollback.h"

#if PROTOCORE_ENABLE_OTA_ROLLBACK

#if PROTOCORE_HAS_VENDOR_OTA
#include "server/clock/clock.h" // protocore_millis()
#endif
/**
 * @brief The rollback's calls - what OtaRollbackNs points at.
 *
 * @var OtaRollbackInternal::ns  the handle a caller sets a call's members on
 */
struct OtaRollbackInternal
{
    OtaRollbackNs *ns;
};

static struct OtaRollbackInternal s_ota_rb = {.ns = &OtaRollback};

static void ota_decide(struct OtaRollbackInternal *restrict ctx)
{
    const OtaDecideArgs *a = &ctx->ns->decide_args;

    if (a->img_state != PROTOCORE_OTA_IMG_PENDING_VERIFY)
    {
        ctx->ns->action = PROTOCORE_OTA_WAIT; // not a freshly-updated image: nothing to do
        return;
    }
    if (a->self_test_ok)
    {
        ctx->ns->action = PROTOCORE_OTA_COMMIT;
        return;
    }
    if (a->ms_since_boot >= a->window_ms)
    {
        ctx->ns->action = PROTOCORE_OTA_ROLLBACK; // never confirmed in time -> self-heal
        return;
    }
    ctx->ns->action = PROTOCORE_OTA_WAIT;
}

#if PROTOCORE_HAS_VENDOR_OTA

static void ota_state(struct OtaRollbackInternal *restrict ctx)
{
    ctx->ns->img_state = protocore_platform_img_state();
}

static void ota_commit(struct OtaRollbackInternal *restrict ctx)
{
    (void)ctx;
    protocore_platform_img_commit();
}

static void ota_rollback(struct OtaRollbackInternal *restrict ctx)
{
    (void)ctx;
    protocore_platform_img_rollback(); // does not return on a device
}

static void ota_tick(struct OtaRollbackInternal *restrict ctx)
{
    ota_state(ctx);
    ctx->ns->decide_args.img_state = ctx->ns->img_state;
    ctx->ns->decide_args.self_test_ok = ctx->ns->self_test_ok;
    Clock.millis(Clock.internal);
    ctx->ns->decide_args.ms_since_boot = Clock.ms;
    ctx->ns->decide_args.window_ms = PROTOCORE_OTA_CONFIRM_WINDOW_MS;
    ota_decide(ctx);
    if (ctx->ns->action == PROTOCORE_OTA_COMMIT)
    {
        ota_commit(ctx);
    }
    else if (ctx->ns->action == PROTOCORE_OTA_ROLLBACK)
    {
        ota_rollback(ctx);
    }
}

#else // no image partitions to read or mark

static void ota_state(struct OtaRollbackInternal *restrict ctx)
{
    ctx->ns->img_state = PROTOCORE_OTA_IMG_UNDEFINED;
}
static void ota_commit(struct OtaRollbackInternal *restrict ctx)
{
    (void)ctx;
}
static void ota_rollback(struct OtaRollbackInternal *restrict ctx)
{
    (void)ctx;
}
static void ota_tick(struct OtaRollbackInternal *restrict ctx)
{
    ctx->ns->action = PROTOCORE_OTA_WAIT;
}

#endif // PROTOCORE_HAS_VENDOR_OTA

// Designated, so a member's position in the struct does not decide what it binds to.
OtaRollbackNs OtaRollback = {.decide = ota_decide,
                             .state = ota_state,
                             .commit = ota_commit,
                             .rollback = ota_rollback,
                             .tick = ota_tick,
                             .internal = &s_ota_rb};

#endif // PROTOCORE_ENABLE_OTA_ROLLBACK
