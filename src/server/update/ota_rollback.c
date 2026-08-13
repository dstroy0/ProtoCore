// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ota_rollback.c
 * @brief OTA rollback decision (pure) + esp_ota_ops commit/rollback (ESP32).
 */

#include "server/update/ota_rollback.h"

#if PROTOCORE_ENABLE_OTA_ROLLBACK

#if PROTOCORE_HAS_VENDOR_OTA
#include "esp_ota_ops.h"
#include "server/clock/clock.h" // protocore_millis() (pulls in Arduino millis())
#endif
protocore_ota_action protocore_ota_decide(uint8_t img_state, proto_bool self_test_ok, uint32_t ms_since_boot,
                                          uint32_t window_ms)
{
    if (img_state != PROTOCORE_OTA_IMG_PENDING_VERIFY)
    {
        return PROTOCORE_OTA_WAIT; // not a freshly-updated image: nothing to do
    }
    if (self_test_ok)
    {
        return PROTOCORE_OTA_COMMIT;
    }
    if (ms_since_boot >= window_ms)
    {
        return PROTOCORE_OTA_ROLLBACK; // never confirmed in time -> self-heal
    }
    return PROTOCORE_OTA_WAIT;
}

#if PROTOCORE_HAS_VENDOR_OTA

uint8_t protocore_ota_img_state(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    if (!running || esp_ota_get_state_partition(running, &st) != ESP_OK)
    {
        return PROTOCORE_OTA_IMG_UNDEFINED;
    }
    return (uint8_t)st;
}

void protocore_ota_commit(void)
{
    esp_ota_mark_app_valid_cancel_rollback();
}

void protocore_ota_rollback(void)
{
    esp_ota_mark_app_invalid_rollback_and_reboot(); // does not return
}

protocore_ota_action protocore_ota_rollback_tick(proto_bool self_test_ok)
{
    protocore_ota_action a = protocore_ota_decide(protocore_ota_img_state(), self_test_ok, protocore_millis(),
                                                  PROTOCORE_OTA_CONFIRM_WINDOW_MS);
    if (a == PROTOCORE_OTA_COMMIT)
    {
        protocore_ota_commit();
    }
    else if (a == PROTOCORE_OTA_ROLLBACK)
    {
        protocore_ota_rollback();
    }
    return a;
}

#else // host build - no OTA partitions

uint8_t protocore_ota_img_state(void)
{
    return PROTOCORE_OTA_IMG_UNDEFINED;
}
void protocore_ota_commit(void)
{
}
void protocore_ota_rollback(void)
{
}
protocore_ota_action protocore_ota_rollback_tick(proto_bool self_test_ok)
{
    (void)self_test_ok;
    return PROTOCORE_OTA_WAIT;
}

#endif // PROTOCORE_HAS_VENDOR_OTA

#endif // PROTOCORE_ENABLE_OTA_ROLLBACK
