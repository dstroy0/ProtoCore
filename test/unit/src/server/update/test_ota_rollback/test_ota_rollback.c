// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the OTA confirm-or-roll-back policy (server/update/ota_rollback.h).
//
// No standard publishes this decision, so every expectation here is category 3: a property the
// policy must hold whatever the implementation. The load-bearing one is
// test_the_confirm_window_closes_at_its_own_length - the whole safeguard is that a new image which
// never confirms itself self-heals, and the instant it stops waiting is the only thing that decides
// whether a soft-bricked device recovers or sits at the boundary forever. The second is
// test_only_a_pending_image_is_ever_acted_on: rolling back an image that was already committed
// would undo a good update.

#include "server/update/ota_rollback.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static protocore_ota_action decide(uint8_t img_state, proto_bool self_test_ok, uint32_t ms_since_boot,
                                   uint32_t window_ms)
{
    OtaRollback.decide_args.img_state = img_state;
    OtaRollback.decide_args.self_test_ok = self_test_ok;
    OtaRollback.decide_args.ms_since_boot = ms_since_boot;
    OtaRollback.decide_args.window_ms = window_ms;
    OtaRollback.decide(OtaRollback.internal);
    return OtaRollback.action;
}

// An image that has confirmed itself is committed, so the update sticks across the next boot.
void test_a_confirmed_image_commits(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_COMMIT,
                          decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_TRUE, 0u, 30000u));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_COMMIT,
                          decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_TRUE, 29999u, 30000u));
}

// The window is measured from boot and closes at its own length: one millisecond short of it the
// image is still waiting, and at it the rollback fires. Past it the answer does not change back.
void test_the_confirm_window_closes_at_its_own_length(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_WAIT,
                          decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_FALSE, 29999u, 30000u));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_ROLLBACK,
                          decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_FALSE, 30000u, 30000u));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_ROLLBACK,
                          decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_FALSE, 30001u, 30000u));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_ROLLBACK,
                          decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_FALSE, 4294967295u, 30000u));

    // A window of zero has already closed at boot, so an image that has to confirm and has not
    // rolls back on the first tick.
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_ROLLBACK, decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_FALSE, 0u, 0u));
}

// A passed self-test is read before the window, so an image that confirms late still commits rather
// than rolling back a build that has proved itself healthy.
void test_a_late_confirmation_still_commits(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_COMMIT,
                          decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_TRUE, 60000u, 30000u));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_COMMIT, decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_TRUE, 5u, 0u));
}

// Only a freshly-updated image is pending confirmation. Every other image state is already settled,
// so the policy waits whatever the self-test says and however long the device has been up - a
// rollback there would undo an update that was already committed.
void test_only_a_pending_image_is_ever_acted_on(void)
{
    static const uint8_t SETTLED[] = {PROTOCORE_OTA_IMG_NEW, PROTOCORE_OTA_IMG_VALID, PROTOCORE_OTA_IMG_INVALID,
                                      PROTOCORE_OTA_IMG_ABORTED, PROTOCORE_OTA_IMG_UNDEFINED};
    for (size_t i = 0; i < sizeof(SETTLED) / sizeof(SETTLED[0]); i++)
    {
        TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_WAIT, decide(SETTLED[i], PROTO_FALSE, 0u, 30000u));
        TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_WAIT, decide(SETTLED[i], PROTO_TRUE, 0u, 30000u));
        TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_WAIT, decide(SETTLED[i], PROTO_FALSE, 999999u, 30000u));
        TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_WAIT, decide(SETTLED[i], PROTO_TRUE, 999999u, 30000u));
    }

    // A state outside the defined set is not pending either, so it is left alone as well.
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_WAIT, decide(0x7Fu, PROTO_FALSE, 999999u, 30000u));
}

// The decision reads its arguments and nothing else, so asking twice gives the same answer and no
// earlier call leaks into a later one.
void test_the_decision_carries_nothing_between_calls(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_ROLLBACK,
                          decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_FALSE, 30000u, 30000u));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_WAIT,
                          decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_FALSE, 10u, 30000u));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_WAIT,
                          decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_FALSE, 10u, 30000u));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_COMMIT,
                          decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_TRUE, 10u, 30000u));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_WAIT,
                          decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_FALSE, 10u, 30000u));
}

// The three image states the enum names as distinct really are distinct values, so a comparison
// against PENDING_VERIFY cannot match another state by accident.
void test_the_image_states_are_distinct(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, PROTOCORE_OTA_IMG_NEW);
    TEST_ASSERT_EQUAL_UINT8(1u, PROTOCORE_OTA_IMG_PENDING_VERIFY);
    TEST_ASSERT_EQUAL_UINT8(2u, PROTOCORE_OTA_IMG_VALID);
    TEST_ASSERT_EQUAL_UINT8(3u, PROTOCORE_OTA_IMG_INVALID);
    TEST_ASSERT_EQUAL_UINT8(4u, PROTOCORE_OTA_IMG_ABORTED);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, PROTOCORE_OTA_IMG_UNDEFINED);
}

// Off target there are no image partitions to read or mark, so the seam reports no image and the
// tick does nothing rather than acting on a state it does not have.
void test_the_platform_seam_reports_no_image_off_target(void)
{
    OtaRollback.state(OtaRollback.internal);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_OTA_IMG_UNDEFINED, OtaRollback.img_state);

    OtaRollback.action = PROTOCORE_OTA_ROLLBACK; // a value the tick has to overwrite
    OtaRollback.self_test_ok = PROTO_TRUE;
    OtaRollback.tick(OtaRollback.internal);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_WAIT, OtaRollback.action);

    // The commit and the rollback reach a seam that owns no partitions, so both return.
    OtaRollback.commit(OtaRollback.internal);
    OtaRollback.rollback(OtaRollback.internal);
    OtaRollback.state(OtaRollback.internal);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_OTA_IMG_UNDEFINED, OtaRollback.img_state);
}
