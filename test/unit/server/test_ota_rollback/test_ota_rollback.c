// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the OTA rollback decision (server/update/ota_rollback). The esp_ota
// commit/rollback are ESP32-only; here we exercise the pure decision matrix.

#include "server/update/ota_rollback.h"
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_not_pending_waits()
{
    // A normally-booted (valid/undefined) image never rolls back.
    TEST_ASSERT_EQUAL_INT(PC_OTA_WAIT, pc_ota_decide(PC_OTA_IMG_VALID, PROTO_FALSE, 999999, 30000));
    TEST_ASSERT_EQUAL_INT(PC_OTA_WAIT, pc_ota_decide(PC_OTA_IMG_UNDEFINED, PROTO_FALSE, 999999, 30000));
}

void test_pending_self_test_ok_commits()
{
    TEST_ASSERT_EQUAL_INT(PC_OTA_COMMIT, pc_ota_decide(PC_OTA_IMG_PENDING_VERIFY, PROTO_TRUE, 1000, 30000));
}

void test_pending_within_window_waits()
{
    TEST_ASSERT_EQUAL_INT(PC_OTA_WAIT, pc_ota_decide(PC_OTA_IMG_PENDING_VERIFY, PROTO_FALSE, 5000, 30000));
}

void test_pending_window_elapsed_rolls_back()
{
    TEST_ASSERT_EQUAL_INT(PC_OTA_ROLLBACK, pc_ota_decide(PC_OTA_IMG_PENDING_VERIFY, PROTO_FALSE, 30000, 30000));
    TEST_ASSERT_EQUAL_INT(PC_OTA_ROLLBACK, pc_ota_decide(PC_OTA_IMG_PENDING_VERIFY, PROTO_FALSE, 40000, 30000));
}

void test_self_test_ok_beats_window()
{
    // A passing self-test commits even past the window.
    TEST_ASSERT_EQUAL_INT(PC_OTA_COMMIT, pc_ota_decide(PC_OTA_IMG_PENDING_VERIFY, PROTO_TRUE, 99999, 30000));
}

void test_host_platform_hooks_are_safe_noops()
{
    // On a host build there are no OTA partitions: img_state reports UNDEFINED and the
    // commit/rollback hooks are no-ops (the real rollback reboots), so rollback_tick, which
    // decides on an UNDEFINED (non-pending) image, always WAITs and never touches the flash.
    TEST_ASSERT_EQUAL_INT(PC_OTA_IMG_UNDEFINED, pc_ota_img_state());
    pc_ota_commit();
    pc_ota_rollback();
    TEST_ASSERT_EQUAL_INT(PC_OTA_WAIT, pc_ota_rollback_tick(PROTO_TRUE));
    TEST_ASSERT_EQUAL_INT(PC_OTA_WAIT, pc_ota_rollback_tick(PROTO_FALSE));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_not_pending_waits);
    RUN_TEST(test_pending_self_test_ok_commits);
    RUN_TEST(test_pending_within_window_waits);
    RUN_TEST(test_pending_window_elapsed_rolls_back);
    RUN_TEST(test_self_test_ok_beats_window);
    RUN_TEST(test_host_platform_hooks_are_safe_noops);
    return UNITY_END();
}
