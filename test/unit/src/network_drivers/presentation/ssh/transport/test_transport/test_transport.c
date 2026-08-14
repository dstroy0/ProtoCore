// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// transport/transport.c (RFC 4253 sec 6, sec 6.1, sec 6.4, sec 7.2, sec 11.1, sec 11.4): the
// binary packet protocol, the keys a key exchange produces, and the two messages every
// implementation has to be able to send.

#include "network_drivers/presentation/ssh/transport/transport.h"
#include <stdint.h>
#include <unity.h>

static uint8_t s_wire[SSH_WIRE_CAP];
static uint8_t s_work[PROTOCORE_SSH_KDF_BORROW];
static uint8_t s_K[256];
static uint8_t s_H[32];

// The sec 7.2 expectations below came from an independent implementation of that section (python
// hashlib) reading the section text, not from this code - so a match means the derivation agrees
// with the RFC rather than with itself.
static const uint8_t K7_2_A[] = {0x9A, 0x5A, 0x36, 0x79, 0x1B, 0x45, 0x0D, 0x2D, 0xAF, 0xAA, 0x8D,
                                 0xEC, 0x44, 0xEC, 0x58, 0x69, 0xDE, 0x2A, 0x87, 0x9D, 0x29, 0xE7,
                                 0x93, 0x6F, 0xBF, 0xC0, 0xF7, 0xFF, 0xFE, 0xAE, 0x63, 0x88};
static const uint8_t K7_2_B[] = {0x9D, 0x43, 0x1D, 0x01, 0xC7, 0x4B, 0x3E, 0xD6, 0xB7, 0x3A, 0x8D,
                                 0x9B, 0x27, 0xCF, 0x45, 0x36, 0xF9, 0xDD, 0xE0, 0x18, 0x7F, 0x7F,
                                 0xE2, 0x4A, 0x99, 0xAC, 0x1F, 0x4A, 0xEB, 0x0A, 0x9B, 0xCA};
static const uint8_t K7_2_C[] = {0x5E, 0x9A, 0x63, 0x26, 0x40, 0xAE, 0xDE, 0xC3, 0xEA, 0x70, 0xFE,
                                 0x33, 0xCA, 0xFF, 0xBC, 0xB8, 0xD0, 0x91, 0xFE, 0x11, 0x9C, 0x08,
                                 0x49, 0x4E, 0xC1, 0xD0, 0xDF, 0xFB, 0xCE, 0x9C, 0xE4, 0xBA};
static const uint8_t K7_2_D[] = {0x34, 0x94, 0xDA, 0x71, 0x26, 0x75, 0xED, 0x70, 0xA9, 0x12, 0xDC,
                                 0x34, 0x79, 0x36, 0xD6, 0xDB, 0xB2, 0xF7, 0x46, 0x13, 0x6E, 0x7E,
                                 0x8F, 0x3B, 0x9F, 0x30, 0xDB, 0xB6, 0x5E, 0xFC, 0xC5, 0x55};
static const uint8_t K7_2_E[] = {0x74, 0xD9, 0x91, 0x97, 0x04, 0xDA, 0xE2, 0x03, 0x51, 0x86, 0xC5,
                                 0x10, 0x94, 0x94, 0x77, 0x51, 0x44, 0x64, 0x7E, 0x25, 0x29, 0x25,
                                 0xCB, 0xD2, 0x3B, 0x83, 0x28, 0xF9, 0x62, 0xFD, 0xBF, 0x93};
static const uint8_t K7_2_F[] = {0xB4, 0x99, 0x31, 0x6F, 0xF4, 0x22, 0xC3, 0x14, 0x33, 0x06, 0xAE,
                                 0x27, 0xA6, 0x70, 0x6D, 0x4B, 0x6D, 0xC3, 0xB2, 0xDD, 0x8A, 0x63,
                                 0x7F, 0x66, 0x00, 0xC7, 0x2B, 0xAC, 0xAC, 0xC6, 0x65, 0xE3};
static const uint8_t K7_2_C_64[] = {0x5E, 0x9A, 0x63, 0x26, 0x40, 0xAE, 0xDE, 0xC3, 0xEA, 0x70, 0xFE, 0x33, 0xCA,
                                    0xFF, 0xBC, 0xB8, 0xD0, 0x91, 0xFE, 0x11, 0x9C, 0x08, 0x49, 0x4E, 0xC1, 0xD0,
                                    0xDF, 0xFB, 0xCE, 0x9C, 0xE4, 0xBA, 0x70, 0xA0, 0x22, 0x24, 0x4C, 0xAA, 0xA6,
                                    0x87, 0x27, 0xB6, 0x37, 0x87, 0x06, 0xC2, 0x63, 0x4E, 0x21, 0x1F, 0xFC, 0x88,
                                    0x7E, 0xD2, 0x64, 0xA5, 0x86, 0xAC, 0xAB, 0x7D, 0xD7, 0x94, 0xBE, 0x26};
static const uint8_t K7_2_A_NOPAD[] = {0x24, 0x92, 0xD2, 0x52, 0x1E, 0xD9, 0x15, 0x1B, 0xA0, 0x3E, 0xEC,
                                       0xC7, 0x22, 0xDF, 0x6C, 0x05, 0xDD, 0xAB, 0xD5, 0x5F, 0xE0, 0xE9,
                                       0xCA, 0x5C, 0x66, 0x64, 0x86, 0xBF, 0x1C, 0xD7, 0xDA, 0x43};
static const uint8_t K7_2_A_REKEY[] = {0x74, 0x13, 0x0F, 0x1E, 0xBF, 0x30, 0xE2, 0xDC, 0x18, 0x21, 0xC8,
                                       0x39, 0x19, 0xB7, 0x42, 0x1F, 0xF5, 0xC8, 0x7A, 0x54, 0x3A, 0x50,
                                       0xB7, 0x76, 0xA3, 0x9E, 0x1C, 0xE0, 0x7B, 0x81, 0xCA, 0x27};

static uint32_t rd_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

// session_id == H is what the first key exchange produces (sec 7.2: "The exchange hash H from the
// first key exchange is additionally used as the session identifier").
static SshKdfInputs first_exchange(void)
{
    SshKdfInputs in = {.work = s_work,
                       .K_be = s_K,
                       .H = s_H,
                       .session_id = s_H,
                       .h_len = sizeof(s_H),
                       .sid_len = sizeof(s_H),
                       .k_is_string = PROTO_FALSE,
                       .is512 = PROTO_FALSE};
    return in;
}

static size_t frame(const uint8_t *payload, size_t len)
{
    SshDir dir = {PROTO_FALSE, 0}; // no cipher, no MAC: "Initially, the MAC algorithm MUST be none"
    size_t wlen = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, payload, len, s_wire, &wlen, sizeof(s_wire), &dir));
    return wlen;
}

void setUp(void)
{

    ssh_transport_init(0);
    ssh_pkt_init(0);

    for (size_t i = 0; i < sizeof(s_K); i++)
    {
        s_K[i] = 0;
    }
    for (size_t i = 0; i < 32; i++)
    {
        s_K[224 + i] = (uint8_t)(0x80 + i);
        s_H[i] = (uint8_t)((i * 7 + 3) & 0xFF);
    }

    ssh_transport_init(0);
    ssh_pkt_init(0);
}
void tearDown(void)
{
}

static void test_sec6_packet_length_excludes_itself_and_the_mac(void)
{
    const uint8_t payload[] = {SSH_MSG_IGNORE, 1, 2, 3};
    const size_t wlen = frame(payload, sizeof(payload));

    const uint32_t plen = rd_u32(s_wire);
    TEST_ASSERT_EQUAL_size_t((size_t)plen + 4u, wlen); // no MAC while unencrypted
}

static void test_sec6_payload_length_is_packet_minus_padding_minus_one(void)
{
    const uint8_t payload[] = {SSH_MSG_IGNORE, 9, 9, 9, 9, 9, 9};
    (void)frame(payload, sizeof(payload));

    const uint32_t plen = rd_u32(s_wire);
    const uint8_t pad = s_wire[4];
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(payload), plen - pad - 1u);
}

static void test_sec6_payload_is_carried_unchanged(void)
{
    const uint8_t payload[] = {SSH_MSG_IGNORE, 0xAA, 0xBB, 0xCC};
    (void)frame(payload, sizeof(payload));

    for (size_t k = 0; k < sizeof(payload); k++)
    {
        TEST_ASSERT_EQUAL_HEX8(payload[k], s_wire[5 + k]);
    }
}

static void test_sec6_payload_offset_matches_the_in_place_form(void)
{
    TEST_ASSERT_EQUAL_size_t(5u, (size_t)SSH_WIRE_PAYLOAD_OFF); // uint32 length + byte padding_length
}

static void test_sec6_at_least_four_bytes_of_padding(void)
{
    // Every payload length across two blocks, so the tightest case is covered rather than assumed.
    for (size_t len = 1; len <= 32; len++)
    {
        uint8_t payload[32];
        for (size_t k = 0; k < len; k++)
        {
            payload[k] = (uint8_t)k;
        }
        setUp();
        (void)frame(payload, len);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT8(4u, s_wire[4]);
    }
}

static void test_sec6_padding_never_exceeds_255(void)
{
    for (size_t len = 1; len <= 32; len++)
    {
        uint8_t payload[32];
        for (size_t k = 0; k < len; k++)
        {
            payload[k] = (uint8_t)k;
        }
        setUp();
        (void)frame(payload, len);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(255u, (uint32_t)s_wire[4]);
    }
}

static void test_sec6_total_is_a_multiple_of_eight_unencrypted(void)
{
    for (size_t len = 1; len <= 32; len++)
    {
        uint8_t payload[32];
        for (size_t k = 0; k < len; k++)
        {
            payload[k] = (uint8_t)k;
        }
        setUp();
        const size_t wlen = frame(payload, len);
        TEST_ASSERT_EQUAL_size_t(0u, wlen % 8u);
    }
}

static void test_sec6_minimum_packet_is_sixteen_bytes(void)
{
    const uint8_t one[] = {SSH_MSG_IGNORE};
    const size_t wlen = frame(one, sizeof(one));
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(16u, (uint32_t)wlen);
}

static void test_sec6_block_aligned_payload_still_pads(void)
{
    // 4 + 1 + 3 = 8, so a 3-byte payload would be aligned with no padding at all.
    const uint8_t payload[] = {SSH_MSG_IGNORE, 1, 2};
    const size_t wlen = frame(payload, sizeof(payload));
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(4u, s_wire[4]);
    TEST_ASSERT_EQUAL_size_t(0u, wlen % 8u);
    TEST_ASSERT_EQUAL_size_t(16u, wlen);
}

static void test_sec6_1_the_required_payload_size_is_carried(void)
{
    TEST_ASSERT_EQUAL_UINT32(32768u, (uint32_t)SSH_RFC_MAX_PAYLOAD);
}

static void test_sec6_1_reassembly_holds_a_full_payload(void)
{
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32((uint32_t)SSH_RFC_MAX_PAYLOAD, (uint32_t)SSH_RX_ASM_CAP);
}

static void test_sec6_1_oversized_payload_is_refused(void)
{
    SshDir dir = {PROTO_FALSE, 0};
    size_t wlen = 0;
    static uint8_t big[SSH_WIRE_CAP];
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_send(0, big, sizeof(big), s_wire, &wlen, sizeof(s_wire), &dir));
}

static void test_sec6_undersized_wire_is_refused(void)
{
    const uint8_t payload[] = {SSH_MSG_IGNORE, 1, 2, 3};
    SshDir dir = {PROTO_FALSE, 0};
    size_t wlen = 0;
    uint8_t small[8]; // a conforming packet is at least 16
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_send(0, payload, sizeof(payload), small, &wlen, sizeof(small), &dir));
}

static void test_sec6_4_send_counter_starts_at_zero(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, ssh_pkt[0].seq_no_send);
}

static void test_sec6_4_send_counter_increments_per_packet(void)
{
    const uint8_t payload[] = {SSH_MSG_IGNORE, 7};
    (void)frame(payload, sizeof(payload));
    TEST_ASSERT_EQUAL_UINT32(1u, ssh_pkt[0].seq_no_send);
    (void)frame(payload, sizeof(payload));
    TEST_ASSERT_EQUAL_UINT32(2u, ssh_pkt[0].seq_no_send);
    (void)frame(payload, sizeof(payload));
    TEST_ASSERT_EQUAL_UINT32(3u, ssh_pkt[0].seq_no_send);
}

static void test_sec6_4_counters_are_per_slot(void)
{
    if (MAX_SSH_CONNS < 2)
    {
        TEST_IGNORE_MESSAGE("needs a second slot");
        return;
    }
    ssh_pkt_init(1);
    const uint8_t payload[] = {SSH_MSG_IGNORE, 7};
    (void)frame(payload, sizeof(payload));
    TEST_ASSERT_EQUAL_UINT32(1u, ssh_pkt[0].seq_no_send);
    TEST_ASSERT_EQUAL_UINT32(0u, ssh_pkt[1].seq_no_send);
}

static void test_slot_past_the_pool_is_refused(void)
{
    const uint8_t payload[] = {SSH_MSG_IGNORE};
    SshDir dir = {PROTO_FALSE, 0};
    size_t wlen = 0;
    TEST_ASSERT_EQUAL_INT(-1,
                          ssh_pkt_send(MAX_SSH_CONNS, payload, sizeof(payload), s_wire, &wlen, sizeof(s_wire), &dir));
}

static void test_sec7_2_initial_iv_client_to_server_is_A(void)
{
    SshKdfInputs in = first_exchange();
    uint8_t out[32];
    ssh_kdf_derive(&in, 'A', out, sizeof(out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(K7_2_A, out, sizeof(out));
}

static void test_sec7_2_initial_iv_server_to_client_is_B(void)
{
    SshKdfInputs in = first_exchange();
    uint8_t out[32];
    ssh_kdf_derive(&in, 'B', out, sizeof(out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(K7_2_B, out, sizeof(out));
}

static void test_sec7_2_encryption_key_client_to_server_is_C(void)
{
    SshKdfInputs in = first_exchange();
    uint8_t out[32];
    ssh_kdf_derive(&in, 'C', out, sizeof(out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(K7_2_C, out, sizeof(out));
}

static void test_sec7_2_encryption_key_server_to_client_is_D(void)
{
    SshKdfInputs in = first_exchange();
    uint8_t out[32];
    ssh_kdf_derive(&in, 'D', out, sizeof(out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(K7_2_D, out, sizeof(out));
}

static void test_sec7_2_integrity_key_client_to_server_is_E(void)
{
    SshKdfInputs in = first_exchange();
    uint8_t out[32];
    ssh_kdf_derive(&in, 'E', out, sizeof(out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(K7_2_E, out, sizeof(out));
}

static void test_sec7_2_integrity_key_server_to_client_is_F(void)
{
    SshKdfInputs in = first_exchange();
    uint8_t out[32];
    ssh_kdf_derive(&in, 'F', out, sizeof(out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(K7_2_F, out, sizeof(out));
}

static void test_sec7_2_the_six_labels_give_six_distinct_keys(void)
{
    SshKdfInputs in = first_exchange();
    uint8_t keys[6][32];
    const char labels[6] = {'A', 'B', 'C', 'D', 'E', 'F'};
    for (int k = 0; k < 6; k++)
    {
        ssh_kdf_derive(&in, labels[k], keys[k], 32);
    }
    for (int a = 0; a < 6; a++)
    {
        for (int b = a + 1; b < 6; b++)
        {
            proto_bool same = PROTO_TRUE;
            for (int n = 0; n < 32; n++)
            {
                if (keys[a][n] != keys[b][n])
                {
                    same = PROTO_FALSE;
                    break;
                }
            }
            TEST_ASSERT_FALSE(same);
        }
    }
}

static void test_sec7_2_extension_chains_k1_into_k2(void)
{
    SshKdfInputs in = first_exchange();
    uint8_t out[64];
    ssh_kdf_derive(&in, 'C', out, sizeof(out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(K7_2_C_64, out, sizeof(out));
}

static void test_sec7_2_key_data_is_taken_from_the_beginning(void)
{
    SshKdfInputs in = first_exchange();
    uint8_t k32[32], k64[64], k16[16];
    ssh_kdf_derive(&in, 'C', k32, sizeof(k32));
    ssh_kdf_derive(&in, 'C', k64, sizeof(k64));
    ssh_kdf_derive(&in, 'C', k16, sizeof(k16));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(k32, k64, 32);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(k16, k32, 16);
}

static void test_rfc4251_sec5_mpint_padding_changes_the_key(void)
{
    for (size_t i = 0; i < sizeof(s_K); i++)
    {
        s_K[i] = 0;
    }
    for (size_t i = 0; i < 16; i++)
    {
        s_K[240 + i] = (uint8_t)(0x01 + i);
    }
    SshKdfInputs in = first_exchange();
    uint8_t out[32];
    ssh_kdf_derive(&in, 'A', out, sizeof(out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(K7_2_A_NOPAD, out, sizeof(out));
}

static void test_sec7_2_rekey_uses_the_new_h_with_the_first_session_id(void)
{
    uint8_t h2[32];
    for (size_t i = 0; i < sizeof(h2); i++)
    {
        h2[i] = (uint8_t)((i * 11 + 5) & 0xFF);
    }
    SshKdfInputs in = first_exchange();
    in.H = h2;           // the re-exchange's hash
    in.session_id = s_H; // still the first exchange's

    uint8_t out[32];
    ssh_kdf_derive(&in, 'A', out, sizeof(out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(K7_2_A_REKEY, out, sizeof(out));
}

static void test_sec7_2_rekey_keys_differ_from_the_first_exchange(void)
{
    SshKdfInputs first = first_exchange();
    uint8_t k1[32];
    ssh_kdf_derive(&first, 'C', k1, sizeof(k1));

    uint8_t h2[32];
    for (size_t i = 0; i < sizeof(h2); i++)
    {
        h2[i] = (uint8_t)((i * 11 + 5) & 0xFF);
    }
    SshKdfInputs second = first_exchange();
    second.H = h2;
    uint8_t k2[32];
    ssh_kdf_derive(&second, 'C', k2, sizeof(k2));

    proto_bool same = PROTO_TRUE;
    for (int n = 0; n < 32; n++)
    {
        if (k1[n] != k2[n])
        {
            same = PROTO_FALSE;
            break;
        }
    }
    TEST_ASSERT_FALSE(same);
}

static void test_sec7_3_two_key_epochs_exist_per_slot(void)
{
    TEST_ASSERT_EQUAL_size_t(2u, sizeof(ssh_keys[0]) / sizeof(ssh_keys[0][0]));
}

static void test_out_len_is_clamped_to_the_chain(void)
{
    SshKdfInputs in = first_exchange();
    uint8_t out[SSH_KDF_MAX + 32];
    for (size_t i = 0; i < sizeof(out); i++)
    {
        out[i] = 0xEE;
    }
    ssh_kdf_derive(&in, 'A', out, sizeof(out));
    for (size_t i = SSH_KDF_MAX; i < sizeof(out); i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0xEE, out[i]); // nothing written past the bound
    }
}

static void test_sec11_1_field_order(void)
{
    static const char desc[] = "service not available";
    uint8_t out[64];
    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_build_disconnect(SSH_DISCONNECT_SERVICE_NOT_AVAILABLE, desc, sizeof(desc) - 1, out,
                                                      &n, sizeof(out)));

    TEST_ASSERT_EQUAL(SSH_MSG_DISCONNECT, out[0]);
    TEST_ASSERT_EQUAL_UINT32(7u, rd_u32(out + 1));                           // RFC 4250 sec 4.2.2
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(sizeof(desc) - 1), rd_u32(out + 5)); // description length
    TEST_ASSERT_EQUAL_CHAR('s', (char)out[9]);
    TEST_ASSERT_EQUAL_UINT32(0u, rd_u32(out + 9 + sizeof(desc) - 1)); // language tag: empty string
    TEST_ASSERT_EQUAL_size_t(13u + sizeof(desc) - 1u, n);
}

static void test_sec11_1_empty_description(void)
{
    uint8_t out[32];
    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_build_disconnect(SSH_DISCONNECT_PROTOCOL_ERROR, "", 0, out, &n, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(13u, n);
    TEST_ASSERT_EQUAL_UINT32(0u, rd_u32(out + 5));
    TEST_ASSERT_EQUAL_UINT32(0u, rd_u32(out + 9));
}

static void test_sec11_1_undersized_buffer_builds_nothing(void)
{
    static const char desc[] = "too many authentication failures";
    uint8_t out[16];
    size_t n = 0xFFu;
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_build_disconnect(SSH_DISCONNECT_NO_MORE_AUTH_METHODS_AVAILABLE, desc,
                                                       sizeof(desc) - 1, out, &n, sizeof(out)));
}

static void test_rfc4250_reason_codes(void)
{
    TEST_ASSERT_EQUAL_UINT32(2u, (uint32_t)SSH_DISCONNECT_PROTOCOL_ERROR);
    TEST_ASSERT_EQUAL_UINT32(5u, (uint32_t)SSH_DISCONNECT_MAC_ERROR);
    TEST_ASSERT_EQUAL_UINT32(7u, (uint32_t)SSH_DISCONNECT_SERVICE_NOT_AVAILABLE);
    TEST_ASSERT_EQUAL_UINT32(11u, (uint32_t)SSH_DISCONNECT_BY_APPLICATION);
    TEST_ASSERT_EQUAL_UINT32(12u, (uint32_t)SSH_DISCONNECT_TOO_MANY_CONNECTIONS);
    TEST_ASSERT_EQUAL_UINT32(14u, (uint32_t)SSH_DISCONNECT_NO_MORE_AUTH_METHODS_AVAILABLE);
}

static void test_sec11_4_field_order(void)
{
    uint8_t out[SSH_UNIMPLEMENTED_LEN];
    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_unimplemented(0, out, &n, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(5u, n);
    TEST_ASSERT_EQUAL(SSH_MSG_UNIMPLEMENTED, out[0]);
}

static void test_rfc4250_unimplemented_is_message_3(void)
{
    TEST_ASSERT_EQUAL_UINT32(3u, (uint32_t)SSH_MSG_UNIMPLEMENTED);
}

static void test_sec11_4_carries_the_rejected_sequence_number(void)
{
    uint8_t out[SSH_UNIMPLEMENTED_LEN];
    size_t n = 0;

    ssh_pkt[0].seq_no_recv = 1u; // nothing received yet past the first
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_unimplemented(0, out, &n, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT32(0u, rd_u32(out + 1));

    ssh_pkt[0].seq_no_recv = 42u;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_unimplemented(0, out, &n, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT32(41u, rd_u32(out + 1));
}

static void test_sec6_4_sequence_number_wraps(void)
{
    uint8_t out[SSH_UNIMPLEMENTED_LEN];
    size_t n = 0;
    ssh_pkt[0].seq_no_recv = 0u; // the packet before the wrap point is 0xFFFFFFFF
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_unimplemented(0, out, &n, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, rd_u32(out + 1));
}

static void test_sec11_4_undersized_buffer_builds_nothing(void)
{
    uint8_t out[4]; // one short of the five it needs
    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_unimplemented(0, out, &n, sizeof(out)));
}

static void test_sec11_4_slot_past_the_pool_is_refused(void)
{
    uint8_t out[SSH_UNIMPLEMENTED_LEN];
    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_unimplemented(MAX_SSH_CONNS, out, &n, sizeof(out)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sec6_packet_length_excludes_itself_and_the_mac);
    RUN_TEST(test_sec6_payload_length_is_packet_minus_padding_minus_one);
    RUN_TEST(test_sec6_payload_is_carried_unchanged);
    RUN_TEST(test_sec6_payload_offset_matches_the_in_place_form);
    RUN_TEST(test_sec6_at_least_four_bytes_of_padding);
    RUN_TEST(test_sec6_padding_never_exceeds_255);
    RUN_TEST(test_sec6_total_is_a_multiple_of_eight_unencrypted);
    RUN_TEST(test_sec6_minimum_packet_is_sixteen_bytes);
    RUN_TEST(test_sec6_block_aligned_payload_still_pads);
    RUN_TEST(test_sec6_1_the_required_payload_size_is_carried);
    RUN_TEST(test_sec6_1_reassembly_holds_a_full_payload);
    RUN_TEST(test_sec6_1_oversized_payload_is_refused);
    RUN_TEST(test_sec6_undersized_wire_is_refused);
    RUN_TEST(test_sec6_4_send_counter_starts_at_zero);
    RUN_TEST(test_sec6_4_send_counter_increments_per_packet);
    RUN_TEST(test_sec6_4_counters_are_per_slot);
    RUN_TEST(test_slot_past_the_pool_is_refused);
    RUN_TEST(test_sec7_2_initial_iv_client_to_server_is_A);
    RUN_TEST(test_sec7_2_initial_iv_server_to_client_is_B);
    RUN_TEST(test_sec7_2_encryption_key_client_to_server_is_C);
    RUN_TEST(test_sec7_2_encryption_key_server_to_client_is_D);
    RUN_TEST(test_sec7_2_integrity_key_client_to_server_is_E);
    RUN_TEST(test_sec7_2_integrity_key_server_to_client_is_F);
    RUN_TEST(test_sec7_2_the_six_labels_give_six_distinct_keys);
    RUN_TEST(test_sec7_2_extension_chains_k1_into_k2);
    RUN_TEST(test_sec7_2_key_data_is_taken_from_the_beginning);
    RUN_TEST(test_rfc4251_sec5_mpint_padding_changes_the_key);
    RUN_TEST(test_sec7_2_rekey_uses_the_new_h_with_the_first_session_id);
    RUN_TEST(test_sec7_2_rekey_keys_differ_from_the_first_exchange);
    RUN_TEST(test_sec7_3_two_key_epochs_exist_per_slot);
    RUN_TEST(test_out_len_is_clamped_to_the_chain);
    RUN_TEST(test_sec11_1_field_order);
    RUN_TEST(test_sec11_1_empty_description);
    RUN_TEST(test_sec11_1_undersized_buffer_builds_nothing);
    RUN_TEST(test_rfc4250_reason_codes);
    RUN_TEST(test_sec11_4_field_order);
    RUN_TEST(test_rfc4250_unimplemented_is_message_3);
    RUN_TEST(test_sec11_4_carries_the_rejected_sequence_number);
    RUN_TEST(test_sec6_4_sequence_number_wraps);
    RUN_TEST(test_sec11_4_undersized_buffer_builds_nothing);
    RUN_TEST(test_sec11_4_slot_past_the_pool_is_refused);
    return UNITY_END();
}
