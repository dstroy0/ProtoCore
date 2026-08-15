// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the ESP-NOW envelope codec and peer registry
// (services/radio/espnow/espnow.h).
//
// No published standard governs this envelope: ESP-NOW is Espressif's own connectionless
// peer-to-peer protocol, and the three-octet magic / type / length header on top of it is this
// library's. The expectations here are therefore PROPERTIES, not spec vectors, with one exception:
// Espressif publishes the v1 ESP-NOW payload limit as 250 octets (ESP_NOW_MAX_DATA_LEN), because an
// IEEE 802.11 vendor-specific element's Length field is one octet, and
// test_payload_cap_is_the_radio_limit_less_the_header checks the module's cap is that limit less
// the header rather than a number of its own.
//
// test_decode_requires_the_declared_length_exactly is the load-bearing property: the envelope
// exists so a receiver can reject a truncated frame, and a decoder that accepted a short or padded
// one would hand the application a payload whose length it never verified.

#include "services/radio/espnow/espnow.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
    protocore_espnow_peers_reset();
}
void tearDown(void)
{
}

// The envelope is magic, type, declared length, then the payload verbatim.
void test_envelope_field_layout(void)
{
    static const uint8_t PAYLOAD[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t out[16];
    const size_t n = protocore_espnow_encode(0x42, PAYLOAD, sizeof(PAYLOAD), out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_ESPNOW_HDR + sizeof(PAYLOAD), n);
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_ESPNOW_MAGIC, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x42, out[1]);
    TEST_ASSERT_EQUAL_HEX8(sizeof(PAYLOAD), out[2]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, out + PROTOCORE_ESPNOW_HDR, sizeof(PAYLOAD));

    // A message with no payload is the header alone.
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_ESPNOW_HDR, protocore_espnow_encode(0x01, NULL, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0x00, out[2]);
}

// Encode then decode returns the type, the length and the payload octets unchanged, and the
// payload points into the caller's own buffer rather than a copy.
void test_encode_decode_round_trip(void)
{
    static const uint8_t PAYLOAD[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t wire[32];
    for (size_t len = 0; len <= sizeof(PAYLOAD); len++)
    {
        const size_t n = protocore_espnow_encode(0x7F, PAYLOAD, len, wire, sizeof(wire));
        TEST_ASSERT_EQUAL_size_t(PROTOCORE_ESPNOW_HDR + len, n);

        uint8_t type = 0;
        const uint8_t *payload = NULL;
        size_t plen = 99;
        TEST_ASSERT_TRUE(protocore_espnow_decode(wire, n, &type, &payload, &plen));
        TEST_ASSERT_EQUAL_HEX8(0x7F, type);
        TEST_ASSERT_EQUAL_size_t(len, plen);
        TEST_ASSERT_EQUAL_PTR(wire + PROTOCORE_ESPNOW_HDR, payload);
        if (len)
        {
            TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, payload, len);
        }
    }
}

// The declared length must match the octets that arrived, exactly: a frame cut short and a frame
// with anything after the payload are both refused.
void test_decode_requires_the_declared_length_exactly(void)
{
    static const uint8_t PAYLOAD[4] = {1, 2, 3, 4};
    uint8_t wire[16];
    const size_t n = protocore_espnow_encode(0x10, PAYLOAD, sizeof(PAYLOAD), wire, sizeof(wire));

    uint8_t type;
    const uint8_t *payload;
    size_t plen;
    TEST_ASSERT_TRUE(protocore_espnow_decode(wire, n, &type, &payload, &plen));
    TEST_ASSERT_FALSE(protocore_espnow_decode(wire, n - 1, &type, &payload, &plen)); // one octet short
    TEST_ASSERT_FALSE(protocore_espnow_decode(wire, n + 1, &type, &payload, &plen)); // one octet extra

    // The declared length itself is what is checked, not the octet count alone.
    wire[2] = 3;
    TEST_ASSERT_FALSE(protocore_espnow_decode(wire, n, &type, &payload, &plen));
    wire[2] = 5;
    TEST_ASSERT_FALSE(protocore_espnow_decode(wire, n, &type, &payload, &plen));
}

// A frame that does not start with the magic octet is not this envelope, so it is refused rather
// than reinterpreted.
void test_decode_requires_the_magic_octet(void)
{
    static const uint8_t PAYLOAD[2] = {0xAA, 0xBB};
    uint8_t wire[16];
    const size_t n = protocore_espnow_encode(0x01, PAYLOAD, sizeof(PAYLOAD), wire, sizeof(wire));

    uint8_t type;
    const uint8_t *payload;
    size_t plen;
    for (unsigned v = 0; v < 256; v++)
    {
        wire[0] = (uint8_t)v;
        const proto_bool ok = protocore_espnow_decode(wire, n, &type, &payload, &plen);
        if (v == PROTOCORE_ESPNOW_MAGIC)
        {
            TEST_ASSERT_TRUE(ok);
        }
        else
        {
            TEST_ASSERT_FALSE(ok);
        }
    }
}

// Fewer octets than the header cannot carry a declared length at all.
void test_decode_fails_closed(void)
{
    static const uint8_t SHORT[2] = {PROTOCORE_ESPNOW_MAGIC, 0x01};
    uint8_t type;
    const uint8_t *payload;
    size_t plen;
    TEST_ASSERT_FALSE(protocore_espnow_decode(SHORT, sizeof(SHORT), &type, &payload, &plen));
    TEST_ASSERT_FALSE(protocore_espnow_decode(NULL, 8, &type, &payload, &plen));

    // Every output is optional: a caller that wants only the verdict passes none of them.
    static const uint8_t OK3[3] = {PROTOCORE_ESPNOW_MAGIC, 0x05, 0x00};
    TEST_ASSERT_TRUE(protocore_espnow_decode(OK3, sizeof(OK3), NULL, NULL, NULL));
}

// Espressif publishes the v1 ESP-NOW data limit as 250 octets, so the application payload this
// envelope can carry is 250 - 3 = 247, and one octet more has no encoding.
static uint8_t g_max[PROTOCORE_ESPNOW_MAX_PAYLOAD + 1];
static uint8_t g_frame[PROTOCORE_ESPNOW_MAX_PAYLOAD + PROTOCORE_ESPNOW_HDR + 1];
void test_payload_cap_is_the_radio_limit_less_the_header(void)
{
    TEST_ASSERT_EQUAL_INT(3, PROTOCORE_ESPNOW_HDR);
    TEST_ASSERT_EQUAL_INT(250 - PROTOCORE_ESPNOW_HDR, PROTOCORE_ESPNOW_MAX_PAYLOAD);

    memset(g_max, 0x5A, sizeof(g_max));
    const size_t n = protocore_espnow_encode(0x01, g_max, PROTOCORE_ESPNOW_MAX_PAYLOAD, g_frame, sizeof(g_frame));
    TEST_ASSERT_EQUAL_size_t(250, n);
    // The declared length still fits the one octet that carries it.
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_ESPNOW_MAX_PAYLOAD, g_frame[2]);

    TEST_ASSERT_EQUAL_size_t(
        0, protocore_espnow_encode(0x01, g_max, PROTOCORE_ESPNOW_MAX_PAYLOAD + 1, g_frame, sizeof(g_frame)));
}

// A frame is written whole or not at all: a region that cannot hold header plus payload yields
// nothing rather than a truncated envelope whose declared length would then be a lie.
void test_encode_fails_closed(void)
{
    static const uint8_t PAYLOAD[4] = {1, 2, 3, 4};
    uint8_t out[8];
    TEST_ASSERT_EQUAL_size_t(0, protocore_espnow_encode(0x01, PAYLOAD, 4, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, protocore_espnow_encode(0x01, PAYLOAD, 4, out, 6)); // needs 7
    TEST_ASSERT_EQUAL_size_t(7, protocore_espnow_encode(0x01, PAYLOAD, 4, out, 7));
}

// The registry answers membership, takes the same address twice without growing, and gives it back
// on a remove.
void test_peer_registry_membership(void)
{
    static const uint8_t A[6] = {0x24, 0x6F, 0x28, 0x01, 0x02, 0x03};
    static const uint8_t B[6] = {0x24, 0x6F, 0x28, 0x01, 0x02, 0x04}; // one octet apart

    TEST_ASSERT_EQUAL_INT(0, protocore_espnow_peer_count());
    TEST_ASSERT_FALSE(protocore_espnow_peer_has(A));

    TEST_ASSERT_TRUE(protocore_espnow_peer_add(A));
    TEST_ASSERT_TRUE(protocore_espnow_peer_has(A));
    TEST_ASSERT_FALSE(protocore_espnow_peer_has(B)); // a near miss is not a match
    TEST_ASSERT_EQUAL_INT(1, protocore_espnow_peer_count());

    TEST_ASSERT_TRUE(protocore_espnow_peer_add(A)); // idempotent
    TEST_ASSERT_EQUAL_INT(1, protocore_espnow_peer_count());

    TEST_ASSERT_TRUE(protocore_espnow_peer_add(B));
    TEST_ASSERT_EQUAL_INT(2, protocore_espnow_peer_count());

    TEST_ASSERT_TRUE(protocore_espnow_peer_remove(A));
    TEST_ASSERT_FALSE(protocore_espnow_peer_has(A));
    TEST_ASSERT_TRUE(protocore_espnow_peer_has(B));
    TEST_ASSERT_FALSE(protocore_espnow_peer_remove(A)); // already gone
    TEST_ASSERT_EQUAL_INT(1, protocore_espnow_peer_count());

    protocore_espnow_peers_reset();
    TEST_ASSERT_EQUAL_INT(0, protocore_espnow_peer_count());
    TEST_ASSERT_FALSE(protocore_espnow_peer_has(B));
}

// The table is fixed at build time and has no heap behind it, so it refuses one past its size and
// a freed slot is reusable.
void test_peer_registry_is_bounded(void)
{
    uint8_t mac[6] = {0x02, 0, 0, 0, 0, 0};
    for (int i = 0; i < PROTOCORE_ESPNOW_MAX_PEERS; i++)
    {
        mac[5] = (uint8_t)i;
        TEST_ASSERT_TRUE(protocore_espnow_peer_add(mac));
    }
    TEST_ASSERT_EQUAL_INT(PROTOCORE_ESPNOW_MAX_PEERS, protocore_espnow_peer_count());

    mac[5] = (uint8_t)PROTOCORE_ESPNOW_MAX_PEERS;
    TEST_ASSERT_FALSE(protocore_espnow_peer_add(mac)); // full

    mac[5] = 0;
    TEST_ASSERT_TRUE(protocore_espnow_peer_remove(mac));
    mac[5] = (uint8_t)PROTOCORE_ESPNOW_MAX_PEERS;
    TEST_ASSERT_TRUE(protocore_espnow_peer_add(mac)); // the freed slot takes it

    TEST_ASSERT_FALSE(protocore_espnow_peer_add(NULL));
}

// IEEE 802 broadcast: every octet of the group address is one.
void test_broadcast_address_is_all_ones(void)
{
    for (int i = 0; i < 6; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0xFF, PROTOCORE_ESPNOW_BROADCAST[i]);
    }
}

// This build has no radio, so nothing is ever transmitted: the two send calls answer no rather
// than pretending a frame went out, and bring-up answers no as well. Adding a peer still reaches
// the registry, which is the half that has no radio under it.
void test_radio_binding_reports_no_radio(void)
{
    static const uint8_t MAC[6] = {0x24, 0x6F, 0x28, 0x11, 0x22, 0x33};
    static const uint8_t PAYLOAD[2] = {1, 2};
    TEST_ASSERT_FALSE(protocore_espnow_begin(1, NULL));
    TEST_ASSERT_FALSE(protocore_espnow_send(MAC, 0x01, PAYLOAD, sizeof(PAYLOAD)));
    TEST_ASSERT_FALSE(protocore_espnow_broadcast(0x01, PAYLOAD, sizeof(PAYLOAD)));

    TEST_ASSERT_TRUE(protocore_espnow_add_peer(MAC));
    TEST_ASSERT_TRUE(protocore_espnow_peer_has(MAC));
}
