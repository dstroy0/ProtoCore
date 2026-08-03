// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ota_rollback.h
 * @brief OTA rollback protection / soft-brick safeguard (PC_ENABLE_OTA_ROLLBACK).
 *
 * After an OTA update the new image boots in the PENDING_VERIFY state. This service
 * decides, each tick, whether to commit it (a self-test passed), roll back to the
 * previous image (self-test failed, or the confirm window elapsed without success),
 * or keep waiting - so a bad update self-heals instead of soft-bricking. The
 * decision is a pure function (host-tested); the commit / rollback wrap esp_ota_ops
 * on ESP32. Needs the bootloader's app-rollback support.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_OTA_ROLLBACK_H
#define PROTOCORE_OTA_ROLLBACK_H

#include "protocore_config.h"

#if PC_ENABLE_OTA_ROLLBACK

/** @brief OTA image states (mirror esp_ota_img_states_t so the core is host-pure). These arrive from
 *  ESP-IDF as a uint8_t and are compared, so integer constants in a namespacing struct - cast-free. */
#define PC_OTA_IMG_NEW 0
#define PC_OTA_IMG_PENDING_VERIFY 1
#define PC_OTA_IMG_VALID 2
#define PC_OTA_IMG_INVALID 3
#define PC_OTA_IMG_ABORTED 4
#define PC_OTA_IMG_UNDEFINED 0xFF

/** @brief What the rollback tick should do. */
typedef enum PROTO_ENUM_PACKED
{
    PC_OTA_WAIT = 0,     ///< still pending, within the window: keep waiting.
    PC_OTA_COMMIT = 1,   ///< self-test passed: mark the image valid.
    PC_OTA_ROLLBACK = 2, ///< self-test failed or window elapsed: roll back + reboot.
} pc_ota_action;

// ---------------------------------------------------------------------------
// Host-testable decision core
// ---------------------------------------------------------------------------

/**
 * @brief Decide the rollback action.
 * @param img_state     current running image state (PC_OTA_IMG_*).
 * @param self_test_ok  application self-test result.
 * @param ms_since_boot uptime in ms.
 * @param window_ms     confirm window.
 * @return PC_OTA_WAIT / _COMMIT / _ROLLBACK. Only a PENDING_VERIFY image acts;
 *         any other state returns WAIT (nothing to do).
 */
pc_ota_action pc_ota_decide(uint8_t img_state, proto_bool self_test_ok, uint32_t ms_since_boot, uint32_t window_ms);

// ---------------------------------------------------------------------------
// ESP32 actions (no-op / stubs on host)
// ---------------------------------------------------------------------------

/** @brief Current running image's OTA state (pc_ota_img::PC_OTA_IMG_UNDEFINED on host). */
uint8_t pc_ota_img_state(void);

/** @brief Commit the running image (cancel rollback). */
void pc_ota_commit(void);

/** @brief Mark the running image invalid and reboot into the previous one. */
void pc_ota_rollback(void);

/**
 * @brief One rollback step: read the image state, decide with @p self_test_ok and
 *        millis(), and act. Call periodically until the image is committed.
 * @return the action taken.
 */
pc_ota_action pc_ota_rollback_tick(proto_bool self_test_ok);

#endif // PC_ENABLE_OTA_ROLLBACK
#endif // PROTOCORE_OTA_ROLLBACK_H
