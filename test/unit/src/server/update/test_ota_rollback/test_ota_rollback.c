// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "server/update/ota_rollback/ota_rollback.h"

#include "test/core_setup/hal/host/host_platform.h"
#include "server/clock/clock.h"

#include <unity.h>

static uint8_t ota_rollback_work[16]; // the borrow an entry takes; OtaRollback never reads it

void setUp(void)
{
    protocore_host_platform_reset();
    Clock.ms = 0u;
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
    OtaRollback.decide(ota_rollback_work);
    return OtaRollback.action;
}

// An image that has confirmed itself is committed, so the update sticks across the next boot.
void test_a_confirmed_image_commits(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_COMMIT, decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_TRUE, 0u, 30000u));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_COMMIT, decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_TRUE, 29999u, 30000u));
}

// The window is measured from boot and closes at its own length: one millisecond short of it the
// image is still waiting, and at it the rollback fires. Past it the answer does not change back.
void test_the_confirm_window_closes_at_its_own_length(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_WAIT, decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_FALSE, 29999u, 30000u));
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
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_COMMIT, decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_TRUE, 60000u, 30000u));
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
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_WAIT, decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_FALSE, 10u, 30000u));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_WAIT, decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_FALSE, 10u, 30000u));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_COMMIT, decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_TRUE, 10u, 30000u));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_WAIT, decide(PROTOCORE_OTA_IMG_PENDING_VERIFY, PROTO_FALSE, 10u, 30000u));
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

// The seam reports the state the part holds, and reports back what a mark changed it to.
void test_the_seam_reports_the_state_the_part_holds(void)
{
    protocore_host_set_img_state(PROTOCORE_OTA_IMG_PENDING_VERIFY);
    OtaRollback.state(ota_rollback_work);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_OTA_IMG_PENDING_VERIFY, OtaRollback.img_state);

    OtaRollback.commit(ota_rollback_work);
    OtaRollback.state(ota_rollback_work);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_OTA_IMG_VALID, OtaRollback.img_state);

    protocore_host_set_img_state(PROTOCORE_OTA_IMG_PENDING_VERIFY);
    OtaRollback.rollback(ota_rollback_work);
    OtaRollback.state(ota_rollback_work);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_OTA_IMG_INVALID, OtaRollback.img_state);
}

// A tick reads the state through the seam and carries the decision back through it: a pending image
// whose self-test passed is marked valid, and nothing is rolled back.
void test_a_tick_commits_a_confirmed_image_through_the_seam(void)
{
    protocore_host_set_img_state(PROTOCORE_OTA_IMG_PENDING_VERIFY);
    Clock.ms = 0u;
    OtaRollback.self_test_ok = PROTO_TRUE;
    OtaRollback.action = PROTOCORE_OTA_ROLLBACK; // a value the tick has to overwrite

    OtaRollback.tick(ota_rollback_work);

    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_COMMIT, OtaRollback.action);
    TEST_ASSERT_TRUE(protocore_host_img_committed());
    TEST_ASSERT_FALSE(protocore_host_img_rolled_back());
}

// An image that never confirms itself is rolled back through the seam once the window has closed.
void test_a_tick_rolls_back_an_unconfirmed_image_through_the_seam(void)
{
    protocore_host_set_img_state(PROTOCORE_OTA_IMG_PENDING_VERIFY);
    Clock.ms = PROTOCORE_OTA_CONFIRM_WINDOW_MS;
    OtaRollback.self_test_ok = PROTO_FALSE;

    OtaRollback.tick(ota_rollback_work);

    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_ROLLBACK, OtaRollback.action);
    TEST_ASSERT_TRUE(protocore_host_img_rolled_back());
    TEST_ASSERT_FALSE(protocore_host_img_committed());
}

// A settled image is left alone: the tick reads it through the seam and marks nothing either way.
void test_a_tick_leaves_a_settled_image_alone_at_the_seam(void)
{
    protocore_host_set_img_state(PROTOCORE_OTA_IMG_VALID);
    Clock.ms = 999999u;
    OtaRollback.self_test_ok = PROTO_FALSE;
    OtaRollback.action = PROTOCORE_OTA_ROLLBACK;

    OtaRollback.tick(ota_rollback_work);

    TEST_ASSERT_EQUAL_INT(PROTOCORE_OTA_WAIT, OtaRollback.action);
    TEST_ASSERT_FALSE(protocore_host_img_committed());
    TEST_ASSERT_FALSE(protocore_host_img_rolled_back());
}
