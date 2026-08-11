// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Per-file coverage for the key-epoch transitions: ssh_keymat.h's epoch pair, ssh_dh.c's install,
// and ssh_transport.c's two NEWKEYS switches.
//
// The oracle is RFC 4253 sec 7.3 (rfc-editor.org), quoted at each check:
//
//   "Key exchange ends by each side sending an SSH_MSG_NEWKEYS message. This message is sent with
//    the old keys and algorithms. All messages sent after this message MUST use the new keys and
//    algorithms."
//   "When this message is received, the new keys and algorithms MUST be used for receiving."
//
// Two sentences, two independent switches. That is what makes a second key epoch necessary: between
// our NEWKEYS and the peer's, the send path must already be on the new keys while the receive path
// is still on the old, so both sets have to exist at once. A single-epoch implementation cannot
// hold that, which is the audit's F3.

#include "network_drivers/presentation/ssh/transport/ssh_dh.h"
#include "network_drivers/presentation/ssh/transport/ssh_keymat.h"
#include "network_drivers/presentation/ssh/transport/ssh_packet.h"
#include "network_drivers/presentation/ssh/transport/ssh_transport.h"
#include <string.h>

#include <unity.h>

void setUp()
{
    ssh_transport_init(0);
    ssh_pkt_init(0);
}
void tearDown()
{
}

// Run one key derivation with a fixed, arbitrary K/H/session-id. The values do not matter here -
// what is under test is which epoch receives them and when each direction moves.
static void derive_once(void)
{
    static uint8_t K_be[256];
    static uint8_t H[32];
    for (int i = 0; i < 256; i++)
    {
        K_be[i] = (uint8_t)(i + 1);
    }
    for (int i = 0; i < 32; i++)
    {
        H[i] = (uint8_t)(0x40 + i);
    }
    TEST_ASSERT_TRUE(ssh_pkt_slot_storage(&ssh_pkt[0]));
    const SshKdfInputs in = {.work = ssh_pkt[0].crypto_work,
                             .K_be = K_be,
                             .H = H,
                             .session_id = H,
                             .h_len = 32,
                             .sid_len = 32,
                             .k_is_string = PROTO_FALSE,
                             .is512 = PROTO_FALSE};
    ssh_dh_derive_keys_sid(0, &in);
}

// A fresh connection has both directions on epoch 0, neither encrypted, and an exchange running.
static void test_fresh_session_starts_unencrypted_on_epoch_zero(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, ssh_sess[0].out.epoch);
    TEST_ASSERT_EQUAL_UINT8(0, ssh_sess[0].in.epoch);
    TEST_ASSERT_FALSE(ssh_sess[0].out.enc);
    TEST_ASSERT_FALSE(ssh_sess[0].in.enc);
    TEST_ASSERT_TRUE(ssh_sess[0].kex_active);
}

// The derivation lands in the epoch neither direction is reading, so the live one keeps working.
static void test_derive_installs_into_the_idle_epoch(void)
{
    derive_once();
    TEST_ASSERT_TRUE(ssh_keys[0][1].active);  // the one nobody is on
    TEST_ASSERT_FALSE(ssh_keys[0][0].active); // the live one, untouched
    TEST_ASSERT_EQUAL_UINT8(0, ssh_sess[0].out.epoch);
    TEST_ASSERT_EQUAL_UINT8(0, ssh_sess[0].in.epoch);
}

// "This message is sent with the old keys": after our NEWKEYS the send path is on the new epoch and
// the receive path is still on the old one, each moving on its own message.
//
// On a FIRST exchange the epoch being left holds nothing - the connection was unencrypted until now,
// so there are no old keys to keep. Sec 7.3's "old keys" only names something on a re-exchange,
// which is where both epochs being live at once is asserted (test_rekey_alternates_epochs).
static void test_directions_switch_independently(void)
{
    derive_once();
    ssh_newkeys_sent(0);

    TEST_ASSERT_EQUAL_UINT8(1, ssh_sess[0].out.epoch); // ours moved
    TEST_ASSERT_TRUE(ssh_sess[0].out.enc);
    TEST_ASSERT_EQUAL_UINT8(0, ssh_sess[0].in.epoch); // theirs has not
    TEST_ASSERT_FALSE(ssh_sess[0].in.enc);
}

// "When this message is received, the new keys ... MUST be used for receiving", and once both
// directions have moved the epoch they left is released.
static void test_second_newkeys_completes_and_releases_the_old_epoch(void)
{
    derive_once();
    ssh_newkeys_sent(0);
    ssh_sess[0].phase = SSH_PHASE_NEWKEYS; // the phase a real exchange is in when the peer's arrives
    TEST_ASSERT_EQUAL_INT(0, ssh_newkeys_complete(0));

    TEST_ASSERT_EQUAL_UINT8(1, ssh_sess[0].in.epoch);
    TEST_ASSERT_TRUE(ssh_sess[0].in.enc);
    TEST_ASSERT_FALSE(ssh_sess[0].kex_active); // the exchange ended
    TEST_ASSERT_TRUE(ssh_keys[0][1].active);   // the new epoch stands
    TEST_ASSERT_FALSE(ssh_keys[0][0].active);  // the old one was released
}

// A re-exchange runs the same cycle the other way: install into 0, switch, release 1.
static void test_rekey_alternates_epochs(void)
{
    derive_once();
    ssh_newkeys_sent(0);
    ssh_sess[0].phase = SSH_PHASE_NEWKEYS;
    TEST_ASSERT_EQUAL_INT(0, ssh_newkeys_complete(0));
    TEST_ASSERT_EQUAL_UINT8(1, ssh_sess[0].out.epoch);

    derive_once(); // the re-key derives into the epoch both directions left
    TEST_ASSERT_TRUE(ssh_keys[0][0].active);
    TEST_ASSERT_EQUAL_UINT8(1, ssh_sess[0].out.epoch); // nothing moves until NEWKEYS

    ssh_newkeys_sent(0);
    TEST_ASSERT_EQUAL_UINT8(0, ssh_sess[0].out.epoch);
    TEST_ASSERT_EQUAL_UINT8(1, ssh_sess[0].in.epoch); // still on the old epoch, per sec 7.3
    // Here "the old keys" names something real, and both sets are live at once: the send path is on
    // the epoch just derived while the receive path is still decrypting with the previous one.
    TEST_ASSERT_TRUE(ssh_keys[0][0].active);
    TEST_ASSERT_TRUE(ssh_keys[0][1].active);

    ssh_sess[0].phase = SSH_PHASE_NEWKEYS;
    TEST_ASSERT_EQUAL_INT(0, ssh_newkeys_complete(0));
    TEST_ASSERT_EQUAL_UINT8(0, ssh_sess[0].in.epoch);
    TEST_ASSERT_FALSE(ssh_keys[0][1].active); // the epoch they left is released
}

// sec 7.3 places NEWKEYS at the end of a key exchange, so one arriving when none is running ends
// nothing and the caller drops the connection.
static void test_newkeys_outside_an_exchange_is_refused(void)
{
    derive_once();
    ssh_sess[0].phase = SSH_PHASE_OPEN; // no exchange in flight
    TEST_ASSERT_EQUAL_INT(-1, ssh_newkeys_complete(0));
    TEST_ASSERT_FALSE(ssh_sess[0].in.enc); // nothing switched
    TEST_ASSERT_EQUAL_UINT8(0, ssh_sess[0].in.epoch);
}

// Closing a connection clears both epochs, not just the live one.
static void test_wipe_clears_both_epochs(void)
{
    derive_once();
    ssh_newkeys_sent(0);
    TEST_ASSERT_TRUE(ssh_keys[0][1].active);
    ssh_keymat_wipe(0);
    TEST_ASSERT_FALSE(ssh_keys[0][0].active);
    TEST_ASSERT_FALSE(ssh_keys[0][1].active);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_fresh_session_starts_unencrypted_on_epoch_zero);
    RUN_TEST(test_derive_installs_into_the_idle_epoch);
    RUN_TEST(test_directions_switch_independently);
    RUN_TEST(test_second_newkeys_completes_and_releases_the_old_epoch);
    RUN_TEST(test_rekey_alternates_epochs);
    RUN_TEST(test_newkeys_outside_an_exchange_is_refused);
    RUN_TEST(test_wipe_clears_both_epochs);
    return UNITY_END();
}
