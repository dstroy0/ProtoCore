// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

static void ota_decide(uint8_t *restrict work)
{
    (void)work;
    const OtaDecideArgs *a = &OtaRollback.decide_args;

    if (a->img_state != PROTOCORE_OTA_IMG_PENDING_VERIFY)
    {
        OtaRollback.action = PROTOCORE_OTA_WAIT; // not a freshly-updated image: nothing to do
        return;
    }
    if (a->self_test_ok)
    {
        OtaRollback.action = PROTOCORE_OTA_COMMIT;
        return;
    }
    if (a->ms_since_boot >= a->window_ms)
    {
        OtaRollback.action = PROTOCORE_OTA_ROLLBACK; // never confirmed in time -> self-heal
        return;
    }
    OtaRollback.action = PROTOCORE_OTA_WAIT;
}

#if PROTOCORE_HAS_VENDOR_OTA

static void ota_state(uint8_t *restrict work)
{
    (void)work;
    OtaRollback.img_state = protocore_platform_img_state();
}

static void ota_commit(uint8_t *restrict work)
{
    (void)work;
    protocore_platform_img_commit();
}

static void ota_rollback(uint8_t *restrict work)
{
    (void)work;
    protocore_platform_img_rollback(); // does not return on a device
}

static void ota_tick(uint8_t *restrict work)
{
    ota_state(work);
    OtaRollback.decide_args.img_state = OtaRollback.img_state;
    OtaRollback.decide_args.self_test_ok = OtaRollback.self_test_ok;
    OtaRollback.decide_args.ms_since_boot = Clock.ms;
    OtaRollback.decide_args.window_ms = PROTOCORE_OTA_CONFIRM_WINDOW_MS;
    ota_decide(work);
    if (OtaRollback.action == PROTOCORE_OTA_COMMIT)
    {
        ota_commit(work);
    }
    else if (OtaRollback.action == PROTOCORE_OTA_ROLLBACK)
    {
        ota_rollback(work);
    }
}

#else // no image partitions to read or mark

static void ota_state(uint8_t *restrict work)
{
    (void)work;
    OtaRollback.img_state = PROTOCORE_OTA_IMG_UNDEFINED;
}
static void ota_commit(uint8_t *restrict work)
{
    (void)work;
}
static void ota_rollback(uint8_t *restrict work)
{
    (void)work;
}
static void ota_tick(uint8_t *restrict work)
{
    (void)work;
    OtaRollback.action = PROTOCORE_OTA_WAIT;
}

#endif // PROTOCORE_HAS_VENDOR_OTA

// Designated, so a member's position in the struct does not decide what it binds to.
OtaRollbackNs OtaRollback = {
    .decide = ota_decide, .state = ota_state, .commit = ota_commit, .rollback = ota_rollback, .tick = ota_tick};

#endif // PROTOCORE_ENABLE_OTA_ROLLBACK
