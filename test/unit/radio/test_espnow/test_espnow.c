// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the ESP-NOW host-testable core (services/radio/espnow): the typed
// envelope codec (round-trip + rejection of corrupt/mismatched frames) and the
// bounded peer registry. The radio binding is ESP32-only and HW-verified.

#include "services/radio/espnow/espnow.h"

#include <unity.h>

void setUp()
{
    pc_espnow_peers_reset();
}
void tearDown()
{
}

void test_encode_decode_roundtrip()
{
    const uint8_t in[] = {1, 2, 3, 4, 5};
    uint8_t frame[64];
    size_t n = pc_espnow_encode(42, in, sizeof(in), frame, sizeof(frame));
    TEST_ASSERT_EQUAL_size_t(sizeof(in) + PC_ESPNOW_HDR, n);

    uint8_t type = 0;
    const uint8_t *payload = NULL;
    size_t plen = 0;
    TEST_ASSERT_TRUE(pc_espnow_decode(frame, n, &type, &payload, &plen));
    TEST_ASSERT_EQUAL_UINT8(42, type);
    TEST_ASSERT_EQUAL_size_t(sizeof(in), plen);
    TEST_ASSERT_EQUAL_MEMORY(in, payload, sizeof(in));
}

void test_encode_zero_length()
{
    uint8_t frame[8];
    size_t n = pc_espnow_encode(7, NULL, 0, frame, sizeof(frame));
    TEST_ASSERT_EQUAL_size_t(PC_ESPNOW_HDR, n);
    uint8_t type = 0;
    const uint8_t *p = NULL;
    size_t plen = 9;
    TEST_ASSERT_TRUE(pc_espnow_decode(frame, n, &type, &p, &plen));
    TEST_ASSERT_EQUAL_UINT8(7, type);
    TEST_ASSERT_EQUAL_size_t(0, plen);
}

void test_encode_rejects_oversize_and_small_buffer()
{
    uint8_t big[PC_ESPNOW_MAX_PAYLOAD + 1] = {0};
    uint8_t frame[300];
    TEST_ASSERT_EQUAL_size_t(0, pc_espnow_encode(1, big, sizeof(big), frame, sizeof(frame)));
    // valid payload but buffer too small
    uint8_t ok[10] = {0};
    TEST_ASSERT_EQUAL_size_t(0, pc_espnow_encode(1, ok, sizeof(ok), frame, 5));
}

void test_decode_rejects_corrupt()
{
    const uint8_t payload[] = {9, 9, 9};
    uint8_t frame[16];
    size_t n = pc_espnow_encode(3, payload, sizeof(payload), frame, sizeof(frame));

    uint8_t type;
    const uint8_t *p;
    size_t plen;
    // bad magic
    uint8_t bad = frame[0];
    frame[0] = 0x00;
    TEST_ASSERT_FALSE(pc_espnow_decode(frame, n, &type, &p, &plen));
    frame[0] = bad;
    // length mismatch (claim more than present)
    frame[2] = 200;
    TEST_ASSERT_FALSE(pc_espnow_decode(frame, n, &type, &p, &plen));
    // too short for a header
    TEST_ASSERT_FALSE(pc_espnow_decode(frame, 2, &type, &p, &plen));
    // trailing garbage (len shorter than buffer)
    frame[2] = (uint8_t)sizeof(payload);
    TEST_ASSERT_FALSE(pc_espnow_decode(frame, n + 1, &type, &p, &plen));
}

void test_encode_null_out_and_null_payload_nonzero_len()
{
    const uint8_t in[] = {1, 2, 3};
    // null out buffer is rejected regardless of otherwise-valid arguments.
    TEST_ASSERT_EQUAL_size_t(0, pc_espnow_encode(1, in, sizeof(in), NULL, 64));

    // Non-null out, positive length, but null payload: the memcpy is skipped
    // (no data to copy from) while the header is still written and the full
    // frame length is still returned.
    uint8_t frame[16];
    size_t n = pc_espnow_encode(5, NULL, 3, frame, sizeof(frame));
    TEST_ASSERT_EQUAL_size_t(3 + PC_ESPNOW_HDR, n);
    TEST_ASSERT_EQUAL_UINT8(PC_ESPNOW_MAGIC, frame[0]);
    TEST_ASSERT_EQUAL_UINT8(5, frame[1]);
    TEST_ASSERT_EQUAL_UINT8(3, frame[2]);
}

void test_decode_null_buf_and_null_out_params()
{
    uint8_t type = 0;
    const uint8_t *p = NULL;
    size_t plen = 0;
    TEST_ASSERT_FALSE(pc_espnow_decode(NULL, 5, &type, &p, &plen));

    const uint8_t payload[] = {1, 2, 3};
    uint8_t frame[16];
    size_t n = pc_espnow_encode(9, payload, sizeof(payload), frame, sizeof(frame));
    // All out-params null: decode still succeeds, just skips writing them back.
    TEST_ASSERT_TRUE(pc_espnow_decode(frame, n, NULL, NULL, NULL));
}

void test_peer_has_and_remove_reject_null_mac()
{
    TEST_ASSERT_FALSE(pc_espnow_peer_has(NULL));
    TEST_ASSERT_FALSE(pc_espnow_peer_remove(NULL));
}

void test_peer_registry()
{
    uint8_t a[6] = {0x01, 0, 0, 0, 0, 0xAA};
    uint8_t b[6] = {0x02, 0, 0, 0, 0, 0xBB};
    TEST_ASSERT_EQUAL_INT(0, pc_espnow_peer_count());
    TEST_ASSERT_TRUE(pc_espnow_peer_add(a));
    TEST_ASSERT_TRUE(pc_espnow_peer_add(b));
    TEST_ASSERT_TRUE(pc_espnow_peer_add(a)); // idempotent
    TEST_ASSERT_EQUAL_INT(2, pc_espnow_peer_count());
    TEST_ASSERT_TRUE(pc_espnow_peer_has(a));
    TEST_ASSERT_TRUE(pc_espnow_peer_remove(a));
    TEST_ASSERT_FALSE(pc_espnow_peer_has(a));
    TEST_ASSERT_FALSE(pc_espnow_peer_remove(a)); // already gone
    TEST_ASSERT_EQUAL_INT(1, pc_espnow_peer_count());
}

void test_peer_table_full_fails_closed()
{
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < PC_ESPNOW_MAX_PEERS; i++)
    {
        mac[5] = (uint8_t)i;
        TEST_ASSERT_TRUE(pc_espnow_peer_add(mac));
    }
    mac[5] = 0xFF; // one too many
    TEST_ASSERT_FALSE(pc_espnow_peer_add(mac));
    TEST_ASSERT_EQUAL_INT(PC_ESPNOW_MAX_PEERS, pc_espnow_peer_count());
}

void test_broadcast_address()
{
    for (int i = 0; i < 6; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(0xFF, PC_ESPNOW_BROADCAST[i]);
    }
}

void test_peer_guard_and_host_stubs()
{
    pc_espnow_peers_reset();
    TEST_ASSERT_FALSE(pc_espnow_peer_add(NULL)); // null mac fails closed
    // Host build: the ESP-NOW bind functions are unavailable.
    TEST_ASSERT_FALSE(pc_espnow_begin(1, NULL));
    uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
    pc_espnow_add_peer(mac); // delegates to the pure peer registry
    uint8_t payload[2] = {0xAA, 0xBB};
    TEST_ASSERT_FALSE(pc_espnow_send(mac, 1, payload, sizeof(payload)));
    TEST_ASSERT_FALSE(pc_espnow_broadcast(1, payload, sizeof(payload)));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_encode_decode_roundtrip);
    RUN_TEST(test_encode_zero_length);
    RUN_TEST(test_encode_rejects_oversize_and_small_buffer);
    RUN_TEST(test_decode_rejects_corrupt);
    RUN_TEST(test_encode_null_out_and_null_payload_nonzero_len);
    RUN_TEST(test_decode_null_buf_and_null_out_params);
    RUN_TEST(test_peer_has_and_remove_reject_null_mac);
    RUN_TEST(test_peer_registry);
    RUN_TEST(test_peer_table_full_fails_closed);
    RUN_TEST(test_broadcast_address);
    RUN_TEST(test_peer_guard_and_host_stubs);
    return UNITY_END();
}
