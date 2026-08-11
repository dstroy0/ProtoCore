// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Per-file coverage for ssh/transport/ssh_packet.c - the RFC 4253 sec 6 binary packet protocol.
//
// Every expected value is taken from RFC 4253 sec 6 itself (rfc-editor.org), quoted at each check,
// so a failure means the framing drifted from the standard rather than from its own past behaviour:
//
//   uint32 packet_length | byte padding_length | byte[n1] payload | byte[n2] padding | byte[m] mac
//
//   packet_length  "The length of the packet in bytes, not including 'mac' or the 'packet_length'
//                   field itself."
//   padding        "There MUST be at least four bytes of padding ... The maximum amount of padding
//                   is 255 bytes."  The concatenation of packet_length || padding_length ||
//                   payload || random padding is "a multiple of the cipher block size or 8,
//                   whichever is larger", enforced "even when using stream ciphers".
//   minimum        "The minimum size of a packet is 16 (or the cipher block size, whichever is
//                   larger) bytes (plus 'mac')."
//   sec 6.4        the sequence number is "an implicit counter initialized to zero and incremented
//                   after each packet", wrapping at 2^32.
//
// The unencrypted direction is what these exercise: before NEWKEYS the cipher is "none" and the MAC
// is "none" (sec 6), so the frame is the plain layout above and every rule is checkable without a
// key exchange.

#include "network_drivers/presentation/ssh/transport/ssh_packet.h"
#include "network_drivers/presentation/ssh/transport/ssh_transport.h"
#include <string.h>

#include <unity.h>

// What the dispatch handler saw, so a round trip can be asserted on the delivered payload.
static uint8_t g_seen[512];
static size_t g_seen_len;
static uint8_t g_seen_type;
static int g_seen_count;

static void capture(uint8_t slot, uint8_t type, const uint8_t *payload, size_t len)
{
    (void)slot;
    g_seen_type = type;
    g_seen_len = len < sizeof(g_seen) ? len : sizeof(g_seen);
    memcpy(g_seen, payload, g_seen_len);
    g_seen_count++;
}

void setUp()
{
    ssh_transport_init(0);
    ssh_pkt_init(0);
    g_seen_len = 0;
    g_seen_type = 0;
    g_seen_count = 0;
    memset(g_seen, 0, sizeof(g_seen));
}
void tearDown()
{
}

// The direction a plain (pre-NEWKEYS) frame travels on: no cipher, epoch 0.
static const SshDir PLAIN = {PROTO_FALSE, 0};

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

// packet_length counts padding_length + payload + padding, and excludes itself and the mac.
static void test_s6_packet_length_excludes_itself_and_mac(void)
{
    static const uint8_t msg[5] = {SSH_MSG_IGNORE, 1, 2, 3, 4};
    uint8_t wire[256];
    size_t wlen = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, msg, sizeof(msg), wire, &wlen, sizeof(wire), &PLAIN));

    const uint32_t plen = be32(wire);
    const uint8_t pad = wire[4];
    TEST_ASSERT_EQUAL_UINT32(1u + sizeof(msg) + pad, plen); // padding_length + payload + padding
    TEST_ASSERT_EQUAL_size_t(4u + plen, wlen);              // no mac before NEWKEYS
}

// "There MUST be at least four bytes of padding" and "the maximum amount of padding is 255 bytes".
static void test_s6_padding_is_within_the_rfc_bounds(void)
{
    uint8_t msg[200];
    uint8_t wire[512];
    memset(msg, 0xA5, sizeof(msg));
    msg[0] = SSH_MSG_IGNORE;

    for (size_t n = 1; n <= sizeof(msg); n++)
    {
        ssh_pkt_init(0);
        size_t wlen = 0;
        TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, msg, n, wire, &wlen, sizeof(wire), &PLAIN));
        const uint8_t pad = wire[4];
        TEST_ASSERT_GREATER_OR_EQUAL_UINT8(4, pad);
        TEST_ASSERT_LESS_OR_EQUAL_UINT8(255, pad);
    }
}

// The concatenation is "a multiple of the cipher block size or 8, whichever is larger", enforced
// even with no cipher negotiated.
static void test_s6_frame_is_a_multiple_of_the_block_size(void)
{
    uint8_t msg[200];
    uint8_t wire[512];
    memset(msg, 0x5A, sizeof(msg));
    msg[0] = SSH_MSG_IGNORE;

    for (size_t n = 1; n <= sizeof(msg); n++)
    {
        ssh_pkt_init(0);
        size_t wlen = 0;
        TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, msg, n, wire, &wlen, sizeof(wire), &PLAIN));
        TEST_ASSERT_EQUAL_size_t(0, wlen % 8u);
    }
}

// "The minimum size of a packet is 16 (or the cipher block size, whichever is larger) bytes."
static void test_s6_minimum_packet_size(void)
{
    static const uint8_t one[1] = {SSH_MSG_IGNORE};
    uint8_t wire[256];
    size_t wlen = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, one, sizeof(one), wire, &wlen, sizeof(wire), &PLAIN));
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(16, wlen);
}

// A frame this layer produced is a frame it accepts, and the payload arrives byte for byte.
static void test_s6_round_trip_delivers_the_payload(void)
{
    static const uint8_t msg[9] = {SSH_MSG_IGNORE, 'p', 'a', 'y', 'l', 'o', 'a', 'd', '!'};
    uint8_t wire[256];
    size_t wlen = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, msg, sizeof(msg), wire, &wlen, sizeof(wire), &PLAIN));

    ssh_pkt_init(0); // a fresh receiver: sec 6.4 starts its counter at zero
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_recv(0, wire, wlen, capture, &PLAIN));
    TEST_ASSERT_EQUAL_INT(1, g_seen_count);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_IGNORE, g_seen_type);
    TEST_ASSERT_EQUAL_size_t(sizeof(msg), g_seen_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, g_seen, sizeof(msg));
}

// sec 6.4: "an implicit counter initialized to zero and incremented after each packet".
static void test_s6_4_sequence_starts_at_zero_and_counts(void)
{
    static const uint8_t msg[4] = {SSH_MSG_IGNORE, 1, 2, 3};
    uint8_t wire[256];
    size_t wlen = 0;

    TEST_ASSERT_EQUAL_UINT32(0, ssh_pkt[0].seq_no_send);
    for (uint32_t k = 1; k <= 5; k++)
    {
        TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, msg, sizeof(msg), wire, &wlen, sizeof(wire), &PLAIN));
        TEST_ASSERT_EQUAL_UINT32(k, ssh_pkt[0].seq_no_send);
    }

    ssh_pkt_init(0);
    TEST_ASSERT_EQUAL_UINT32(0, ssh_pkt[0].seq_no_recv);
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, msg, sizeof(msg), wire, &wlen, sizeof(wire), &PLAIN));
    ssh_pkt_init(0);
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_recv(0, wire, wlen, capture, &PLAIN));
    TEST_ASSERT_EQUAL_UINT32(1, ssh_pkt[0].seq_no_recv);
}

// A peer's frame claiming fewer than four padding bytes violates sec 6 and is refused.
static void test_s6_receive_refuses_padding_under_four(void)
{
    static const uint8_t msg[9] = {SSH_MSG_IGNORE, 1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t wire[256];
    size_t wlen = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, msg, sizeof(msg), wire, &wlen, sizeof(wire), &PLAIN));

    wire[4] = 3; // padding_length below the RFC minimum
    ssh_pkt_init(0);
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, wlen, capture, &PLAIN));
    TEST_ASSERT_EQUAL_INT(0, g_seen_count);
}

// padding_length must leave a payload: one at or past packet_length would underflow it.
static void test_s6_receive_refuses_padding_past_the_packet(void)
{
    static const uint8_t msg[9] = {SSH_MSG_IGNORE, 1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t wire[256];
    size_t wlen = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, msg, sizeof(msg), wire, &wlen, sizeof(wire), &PLAIN));

    wire[4] = (uint8_t)be32(wire); // padding_length == packet_length
    ssh_pkt_init(0);
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, wlen, capture, &PLAIN));
    TEST_ASSERT_EQUAL_INT(0, g_seen_count);
}

// Two frames in one read are two dispatches: the layer extracts every complete packet it holds.
static void test_s6_two_packets_in_one_read(void)
{
    static const uint8_t a[3] = {SSH_MSG_IGNORE, 'a', 'a'};
    static const uint8_t b[3] = {SSH_MSG_DEBUG, 'b', 'b'};
    uint8_t wire[512];
    size_t la = 0;
    size_t lb = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, a, sizeof(a), wire, &la, sizeof(wire), &PLAIN));
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, b, sizeof(b), wire + la, &lb, sizeof(wire) - la, &PLAIN));

    ssh_pkt_init(0);
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_recv(0, wire, la + lb, capture, &PLAIN));
    TEST_ASSERT_EQUAL_INT(2, g_seen_count);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_DEBUG, g_seen_type); // the second one landed last
    TEST_ASSERT_EQUAL_UINT32(2, ssh_pkt[0].seq_no_recv);
}

// A frame split across reads is held until it is whole, then dispatched once.
static void test_s6_partial_frame_waits_for_the_rest(void)
{
    static const uint8_t msg[9] = {SSH_MSG_IGNORE, 1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t wire[256];
    size_t wlen = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, msg, sizeof(msg), wire, &wlen, sizeof(wire), &PLAIN));

    ssh_pkt_init(0);
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_recv(0, wire, wlen - 3, capture, &PLAIN));
    TEST_ASSERT_EQUAL_INT(0, g_seen_count); // incomplete: nothing dispatched yet
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_recv(0, wire + wlen - 3, 3, capture, &PLAIN));
    TEST_ASSERT_EQUAL_INT(1, g_seen_count);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, g_seen, sizeof(msg));
}

// sec 6.4: the sequence number "is initialized to zero for the first packet, and is incremented
// after every packet (regardless of whether encryption or MAC is in use). It is never reset, even
// if keys/algorithms are renegotiated later."
//
// This is the invariant a prefix-truncation attack turns on (CVE-2023-48795, "Terrapin"): a peer
// that restarts its count when NEWKEYS takes effect cannot tell that packets were deleted from the
// handshake, because the MAC of every later packet still verifies against the shifted numbering.
// A re-exchange moves each direction onto a new key epoch; the counters must walk straight past it.
static void test_s6_4_a_rekey_does_not_reset_the_sequence_numbers(void)
{
    static const uint8_t msg[5] = {SSH_MSG_IGNORE, 1, 2, 3, 4};
    uint8_t wire[256];
    size_t wlen = 0;

    for (int k = 0; k < 3; k++)
    {
        TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, msg, sizeof(msg), wire, &wlen, sizeof(wire), &PLAIN));
    }
    TEST_ASSERT_EQUAL_UINT32(3, ssh_pkt[0].seq_no_send);

    // Both directions take new keys, which is what a sec 9 re-exchange ends with.
    ssh_newkeys_sent(0);
    (void)ssh_newkeys_complete(0);

    TEST_ASSERT_EQUAL_UINT32(3, ssh_pkt[0].seq_no_send); // carried across, not restarted

    // And it keeps counting from where it was rather than from zero.
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, msg, sizeof(msg), wire, &wlen, sizeof(wire), &PLAIN));
    TEST_ASSERT_EQUAL_UINT32(4, ssh_pkt[0].seq_no_send);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_s6_packet_length_excludes_itself_and_mac);
    RUN_TEST(test_s6_padding_is_within_the_rfc_bounds);
    RUN_TEST(test_s6_frame_is_a_multiple_of_the_block_size);
    RUN_TEST(test_s6_minimum_packet_size);
    RUN_TEST(test_s6_round_trip_delivers_the_payload);
    RUN_TEST(test_s6_4_sequence_starts_at_zero_and_counts);
    RUN_TEST(test_s6_receive_refuses_padding_under_four);
    RUN_TEST(test_s6_receive_refuses_padding_past_the_packet);
    RUN_TEST(test_s6_two_packets_in_one_read);
    RUN_TEST(test_s6_partial_frame_waits_for_the_rest);
    RUN_TEST(test_s6_4_a_rekey_does_not_reset_the_sequence_numbers);
    return UNITY_END();
}
