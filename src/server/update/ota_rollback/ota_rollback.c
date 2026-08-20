// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ota_rollback.c
 * @brief OTA rollback decision (pure) + the platform seam's commit/rollback.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_OTA_ROLLBACK

#include "server/update/ota_rollback/ota_rollback.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_HAS_VENDOR_OTA
#include "server/clock/clock.h" // protocore_millis()
#endif
void protocore_ota_rollback_decide(uint8_t *restrict work)
{
    (void)work;
    const OtaDecideArgs *a = &OtaRollbackV.decide_args;

    if (a->img_state != PROTOCORE_OTA_IMG_PENDING_VERIFY)
    {
        OtaRollbackV.action = PROTOCORE_OTA_WAIT; // not a freshly-updated image: nothing to do
        return;
    }
    if (a->self_test_ok)
    {
        OtaRollbackV.action = PROTOCORE_OTA_COMMIT;
        return;
    }
    if (a->ms_since_boot >= a->window_ms)
    {
        OtaRollbackV.action = PROTOCORE_OTA_ROLLBACK; // never confirmed in time -> self-heal
        return;
    }
    OtaRollbackV.action = PROTOCORE_OTA_WAIT;
}

#if PROTOCORE_HAS_VENDOR_OTA

void protocore_ota_rollback_state(uint8_t *restrict work)
{
    (void)work;
    OtaRollbackV.img_state = protocore_platform_img_state();
}

void protocore_ota_rollback_commit(uint8_t *restrict work)
{
    (void)work;
    protocore_platform_img_commit();
}

void protocore_ota_rollback_rollback(uint8_t *restrict work)
{
    (void)work;
    protocore_platform_img_rollback(); // does not return on a device
}

void protocore_ota_rollback_tick(uint8_t *restrict work)
{
    protocore_ota_rollback_state(work);
    OtaRollbackV.decide_args.img_state = OtaRollbackV.img_state;
    OtaRollbackV.decide_args.self_test_ok = OtaRollbackV.self_test_ok;
    OtaRollbackV.decide_args.ms_since_boot = Clock.ms;
    OtaRollbackV.decide_args.window_ms = PROTOCORE_OTA_CONFIRM_WINDOW_MS;
    protocore_ota_rollback_decide(work);
    if (OtaRollbackV.action == PROTOCORE_OTA_COMMIT)
    {
        protocore_ota_rollback_commit(work);
    }
    else if (OtaRollbackV.action == PROTOCORE_OTA_ROLLBACK)
    {
        protocore_ota_rollback_rollback(work);
    }
}

#else // no image partitions to read or mark

void protocore_ota_rollback_state(uint8_t *restrict work)
{
    (void)work;
    OtaRollbackV.img_state = PROTOCORE_OTA_IMG_UNDEFINED;
}
void protocore_ota_rollback_commit(uint8_t *restrict work)
{
    (void)work;
}
void protocore_ota_rollback_rollback(uint8_t *restrict work)
{
    (void)work;
}
void protocore_ota_rollback_tick(uint8_t *restrict work)
{
    (void)work;
    OtaRollbackV.action = PROTOCORE_OTA_WAIT;
}

#endif // PROTOCORE_HAS_VENDOR_OTA

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
OtaRollbackVars OtaRollbackV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_OTA_ROLLBACK
