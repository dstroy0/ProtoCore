// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ota_rollback.h
 * @brief OTA rollback protection / soft-brick safeguard (PROTOCORE_ENABLE_OTA_ROLLBACK).
 *
 * After an OTA update the new image boots in the PENDING_VERIFY state. This service
 * decides, each tick, whether to commit it (a self-test passed), roll back to the
 * previous image (self-test failed, or the confirm window elapsed without success),
 * or keep waiting - so a bad update self-heals instead of soft-bricking. The
 * decision is a pure function (host-tested); the commit / rollback reach the platform
 * seam. Needs the bootloader's app-rollback support.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_OTA_ROLLBACK_H
#define PROTOCORE_OTA_ROLLBACK_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_OTA_ROLLBACK

PROTOCORE_BEGIN_DECLS

/** @brief OTA image states, mirroring PROTOCORE_PLATFORM_IMG_* so the core is host-pure. These arrive
 *  from the platform seam as a uint8_t and are compared, so integer constants - cast-free. */
#define PROTOCORE_OTA_IMG_NEW 0
#define PROTOCORE_OTA_IMG_PENDING_VERIFY 1
#define PROTOCORE_OTA_IMG_VALID 2
#define PROTOCORE_OTA_IMG_INVALID 3
#define PROTOCORE_OTA_IMG_ABORTED 4
#define PROTOCORE_OTA_IMG_UNDEFINED 0xFF

/** @brief What the rollback tick should do. */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_OTA_WAIT = 0,     ///< still pending, within the window: keep waiting.
    PROTOCORE_OTA_COMMIT = 1,   ///< self-test passed: mark the image valid.
    PROTOCORE_OTA_ROLLBACK = 2, ///< self-test failed or window elapsed: roll back + reboot.
} protocore_ota_action;

// ---------------------------------------------------------------------------
// Host-testable decision core
// ---------------------------------------------------------------------------

/** @brief What the pure decision reads. */
typedef struct
{
    uint8_t img_state;       ///< the running image's state (PROTOCORE_OTA_IMG_*)
    proto_bool self_test_ok; ///< the application has confirmed itself healthy
    uint32_t ms_since_boot;  ///< how long this image has been running
    uint32_t window_ms;      ///< how long it has to confirm before the rollback self-heals
} OtaDecideArgs;

/** @brief The rollback's own calls, described only in ota_rollback.c. */
struct OtaRollbackInternal;

/**
 * @brief The OTA confirm-or-roll-back policy.
 *
 * A caller sets the members a call takes, invokes it through ::OtaRollback, and reads the outcome
 * off the same handle. The decision is pure; the commit and the rollback reach the platform seam.
 *
 * @var OtaRollbackNs::decide_args  what the pure decision reads
 * @var OtaRollbackNs::self_test_ok what a tick reports about the application's own health
 * @var OtaRollbackNs::action       the action a decide or a tick chose
 * @var OtaRollbackNs::img_state    the running image's state a lookup reports
 * @var OtaRollbackNs::decide       choose an action, reading nothing outside decide_args
 * @var OtaRollbackNs::state        the running image's state, from the platform seam
 * @var OtaRollbackNs::commit       mark the running image valid and cancel the pending rollback
 * @var OtaRollbackNs::rollback     mark it invalid and reboot into the previous one
 * @var OtaRollbackNs::tick         decide against the clock, then carry the decision out
 * @var OtaRollbackNs::internal     the calls that decide and act
 *
 * No storage member: the policy holds nothing between calls; the image state lives in the part.
 */
typedef struct
{
    OtaDecideArgs decide_args;
    proto_bool self_test_ok;

    protocore_ota_action action;
    uint8_t img_state;

    void (*decide)(struct OtaRollbackInternal *ctx);
    void (*state)(struct OtaRollbackInternal *ctx);
    void (*commit)(struct OtaRollbackInternal *ctx);
    void (*rollback)(struct OtaRollbackInternal *ctx);
    void (*tick)(struct OtaRollbackInternal *ctx);

    struct OtaRollbackInternal *internal;
} OtaRollbackNs;

/** @brief The one symbol this module exports. */
extern OtaRollbackNs OtaRollback;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_OTA_ROLLBACK

#endif // PROTOCORE_OTA_ROLLBACK_H
