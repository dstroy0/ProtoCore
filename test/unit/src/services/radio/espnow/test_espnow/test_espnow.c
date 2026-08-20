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
    Espnow.peers_reset(protocore_espnow_span());
}
void tearDown(void)
{
}

// The envelope is magic, type, declared length, then the payload verbatim.
void test_envelope_field_layout(void)
{
    static const uint8_t PAYLOAD[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t out[16];
    EspnowV.encode_args.type = 0x42;
    EspnowV.encode_args.payload = PAYLOAD;
    EspnowV.encode_args.len = sizeof(PAYLOAD);
    EspnowV.encode_args.out = out;
    EspnowV.encode_args.cap = sizeof(out);
    Espnow.encode(protocore_espnow_span());
    const size_t n = EspnowV.n;
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_ESPNOW_HDR + sizeof(PAYLOAD), n);
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_ESPNOW_MAGIC, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x42, out[1]);
    TEST_ASSERT_EQUAL_HEX8(sizeof(PAYLOAD), out[2]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, out + PROTOCORE_ESPNOW_HDR, sizeof(PAYLOAD));

    // A message with no payload is the header alone.
    EspnowV.encode_args.type = 0x01;
    EspnowV.encode_args.payload = NULL;
    EspnowV.encode_args.len = 0;
    EspnowV.encode_args.out = out;
    EspnowV.encode_args.cap = sizeof(out);
    Espnow.encode(protocore_espnow_span());
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_ESPNOW_HDR, EspnowV.n);
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
        EspnowV.encode_args.type = 0x7F;
        EspnowV.encode_args.payload = PAYLOAD;
        EspnowV.encode_args.len = len;
        EspnowV.encode_args.out = wire;
        EspnowV.encode_args.cap = sizeof(wire);
        Espnow.encode(protocore_espnow_span());
        const size_t n = EspnowV.n;
        TEST_ASSERT_EQUAL_size_t(PROTOCORE_ESPNOW_HDR + len, n);

        uint8_t type = 0;
        const uint8_t *payload = NULL;
        size_t plen = 99;
        EspnowV.decode_args.buf = wire;
        EspnowV.decode_args.len = n;
        EspnowV.decode_args.type = &type;
        EspnowV.decode_args.payload = &payload;
        EspnowV.decode_args.plen = &plen;
        Espnow.decode(protocore_espnow_span());
        TEST_ASSERT_TRUE(EspnowV.ok);
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
    EspnowV.encode_args.type = 0x10;
    EspnowV.encode_args.payload = PAYLOAD;
    EspnowV.encode_args.len = sizeof(PAYLOAD);
    EspnowV.encode_args.out = wire;
    EspnowV.encode_args.cap = sizeof(wire);
    Espnow.encode(protocore_espnow_span());
    const size_t n = EspnowV.n;

    uint8_t type;
    const uint8_t *payload;
    size_t plen;
    EspnowV.decode_args.buf = wire;
    EspnowV.decode_args.len = n;
    EspnowV.decode_args.type = &type;
    EspnowV.decode_args.payload = &payload;
    EspnowV.decode_args.plen = &plen;
    Espnow.decode(protocore_espnow_span());
    TEST_ASSERT_TRUE(EspnowV.ok);
    EspnowV.decode_args.buf = wire;
    EspnowV.decode_args.len = n - 1;
    EspnowV.decode_args.type = &type;
    EspnowV.decode_args.payload = &payload;
    EspnowV.decode_args.plen = &plen;
    Espnow.decode(protocore_espnow_span());
    TEST_ASSERT_FALSE(EspnowV.ok); // one octet short
    EspnowV.decode_args.buf = wire;
    EspnowV.decode_args.len = n + 1;
    EspnowV.decode_args.type = &type;
    EspnowV.decode_args.payload = &payload;
    EspnowV.decode_args.plen = &plen;
    Espnow.decode(protocore_espnow_span());
    TEST_ASSERT_FALSE(EspnowV.ok); // one octet extra

    // The declared length itself is what is checked, not the octet count alone.
    wire[2] = 3;
    EspnowV.decode_args.buf = wire;
    EspnowV.decode_args.len = n;
    EspnowV.decode_args.type = &type;
    EspnowV.decode_args.payload = &payload;
    EspnowV.decode_args.plen = &plen;
    Espnow.decode(protocore_espnow_span());
    TEST_ASSERT_FALSE(EspnowV.ok);
    wire[2] = 5;
    EspnowV.decode_args.buf = wire;
    EspnowV.decode_args.len = n;
    EspnowV.decode_args.type = &type;
    EspnowV.decode_args.payload = &payload;
    EspnowV.decode_args.plen = &plen;
    Espnow.decode(protocore_espnow_span());
    TEST_ASSERT_FALSE(EspnowV.ok);
}

// A frame that does not start with the magic octet is not this envelope, so it is refused rather
// than reinterpreted.
void test_decode_requires_the_magic_octet(void)
{
    static const uint8_t PAYLOAD[2] = {0xAA, 0xBB};
    uint8_t wire[16];
    EspnowV.encode_args.type = 0x01;
    EspnowV.encode_args.payload = PAYLOAD;
    EspnowV.encode_args.len = sizeof(PAYLOAD);
    EspnowV.encode_args.out = wire;
    EspnowV.encode_args.cap = sizeof(wire);
    Espnow.encode(protocore_espnow_span());
    const size_t n = EspnowV.n;

    uint8_t type;
    const uint8_t *payload;
    size_t plen;
    for (unsigned v = 0; v < 256; v++)
    {
        wire[0] = (uint8_t)v;
        EspnowV.decode_args.buf = wire;
        EspnowV.decode_args.len = n;
        EspnowV.decode_args.type = &type;
        EspnowV.decode_args.payload = &payload;
        EspnowV.decode_args.plen = &plen;
        Espnow.decode(protocore_espnow_span());
        const proto_bool ok = EspnowV.ok;
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
    EspnowV.decode_args.buf = SHORT;
    EspnowV.decode_args.len = sizeof(SHORT);
    EspnowV.decode_args.type = &type;
    EspnowV.decode_args.payload = &payload;
    EspnowV.decode_args.plen = &plen;
    Espnow.decode(protocore_espnow_span());
    TEST_ASSERT_FALSE(EspnowV.ok);
    EspnowV.decode_args.buf = NULL;
    EspnowV.decode_args.len = 8;
    EspnowV.decode_args.type = &type;
    EspnowV.decode_args.payload = &payload;
    EspnowV.decode_args.plen = &plen;
    Espnow.decode(protocore_espnow_span());
    TEST_ASSERT_FALSE(EspnowV.ok);

    // Every output is optional: a caller that wants only the verdict passes none of them.
    static const uint8_t OK3[3] = {PROTOCORE_ESPNOW_MAGIC, 0x05, 0x00};
    EspnowV.decode_args.buf = OK3;
    EspnowV.decode_args.len = sizeof(OK3);
    EspnowV.decode_args.type = NULL;
    EspnowV.decode_args.payload = NULL;
    EspnowV.decode_args.plen = NULL;
    Espnow.decode(protocore_espnow_span());
    TEST_ASSERT_TRUE(EspnowV.ok);
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
    EspnowV.encode_args.type = 0x01;
    EspnowV.encode_args.payload = g_max;
    EspnowV.encode_args.len = PROTOCORE_ESPNOW_MAX_PAYLOAD;
    EspnowV.encode_args.out = g_frame;
    EspnowV.encode_args.cap = sizeof(g_frame);
    Espnow.encode(protocore_espnow_span());
    const size_t n = EspnowV.n;
    TEST_ASSERT_EQUAL_size_t(250, n);
    // The declared length still fits the one octet that carries it.
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_ESPNOW_MAX_PAYLOAD, g_frame[2]);

    EspnowV.encode_args.type = 0x01;
    EspnowV.encode_args.payload = g_max;
    EspnowV.encode_args.len = PROTOCORE_ESPNOW_MAX_PAYLOAD + 1;
    EspnowV.encode_args.out = g_frame;
    EspnowV.encode_args.cap = sizeof(g_frame);
    Espnow.encode(protocore_espnow_span());
    TEST_ASSERT_EQUAL_size_t(0, EspnowV.n);
}

// A frame is written whole or not at all: a region that cannot hold header plus payload yields
// nothing rather than a truncated envelope whose declared length would then be a lie.
void test_encode_fails_closed(void)
{
    static const uint8_t PAYLOAD[4] = {1, 2, 3, 4};
    uint8_t out[8];
    EspnowV.encode_args.type = 0x01;
    EspnowV.encode_args.payload = PAYLOAD;
    EspnowV.encode_args.len = 4;
    EspnowV.encode_args.out = NULL;
    EspnowV.encode_args.cap = sizeof(out);
    Espnow.encode(protocore_espnow_span());
    TEST_ASSERT_EQUAL_size_t(0, EspnowV.n);
    EspnowV.encode_args.type = 0x01;
    EspnowV.encode_args.payload = PAYLOAD;
    EspnowV.encode_args.len = 4;
    EspnowV.encode_args.out = out;
    EspnowV.encode_args.cap = 6;
    Espnow.encode(protocore_espnow_span());
    TEST_ASSERT_EQUAL_size_t(0, EspnowV.n); // needs 7
    EspnowV.encode_args.type = 0x01;
    EspnowV.encode_args.payload = PAYLOAD;
    EspnowV.encode_args.len = 4;
    EspnowV.encode_args.out = out;
    EspnowV.encode_args.cap = 7;
    Espnow.encode(protocore_espnow_span());
    TEST_ASSERT_EQUAL_size_t(7, EspnowV.n);
}

// The registry answers membership, takes the same address twice without growing, and gives it back
// on a remove.
void test_peer_registry_membership(void)
{
    static const uint8_t A[6] = {0x24, 0x6F, 0x28, 0x01, 0x02, 0x03};
    static const uint8_t B[6] = {0x24, 0x6F, 0x28, 0x01, 0x02, 0x04}; // one octet apart

    Espnow.peer_count(protocore_espnow_span());
    TEST_ASSERT_EQUAL_INT(0, EspnowV.n);
    EspnowV.peer_has_args.mac = A;
    Espnow.peer_has(protocore_espnow_span());
    TEST_ASSERT_FALSE(EspnowV.ok);

    EspnowV.peer_add_args.mac = A;
    Espnow.peer_add(protocore_espnow_span());
    TEST_ASSERT_TRUE(EspnowV.ok);
    EspnowV.peer_has_args.mac = A;
    Espnow.peer_has(protocore_espnow_span());
    TEST_ASSERT_TRUE(EspnowV.ok);
    EspnowV.peer_has_args.mac = B;
    Espnow.peer_has(protocore_espnow_span());
    TEST_ASSERT_FALSE(EspnowV.ok); // a near miss is not a match
    Espnow.peer_count(protocore_espnow_span());
    TEST_ASSERT_EQUAL_INT(1, EspnowV.n);

    EspnowV.peer_add_args.mac = A;
    Espnow.peer_add(protocore_espnow_span());
    TEST_ASSERT_TRUE(EspnowV.ok); // idempotent
    Espnow.peer_count(protocore_espnow_span());
    TEST_ASSERT_EQUAL_INT(1, EspnowV.n);

    EspnowV.peer_add_args.mac = B;
    Espnow.peer_add(protocore_espnow_span());
    TEST_ASSERT_TRUE(EspnowV.ok);
    Espnow.peer_count(protocore_espnow_span());
    TEST_ASSERT_EQUAL_INT(2, EspnowV.n);

    EspnowV.peer_remove_args.mac = A;
    Espnow.peer_remove(protocore_espnow_span());
    TEST_ASSERT_TRUE(EspnowV.ok);
    EspnowV.peer_has_args.mac = A;
    Espnow.peer_has(protocore_espnow_span());
    TEST_ASSERT_FALSE(EspnowV.ok);
    EspnowV.peer_has_args.mac = B;
    Espnow.peer_has(protocore_espnow_span());
    TEST_ASSERT_TRUE(EspnowV.ok);
    EspnowV.peer_remove_args.mac = A;
    Espnow.peer_remove(protocore_espnow_span());
    TEST_ASSERT_FALSE(EspnowV.ok); // already gone
    Espnow.peer_count(protocore_espnow_span());
    TEST_ASSERT_EQUAL_INT(1, EspnowV.n);

    Espnow.peers_reset(protocore_espnow_span());
    Espnow.peer_count(protocore_espnow_span());
    TEST_ASSERT_EQUAL_INT(0, EspnowV.n);
    EspnowV.peer_has_args.mac = B;
    Espnow.peer_has(protocore_espnow_span());
    TEST_ASSERT_FALSE(EspnowV.ok);
}

// The table is fixed at build time and has no heap behind it, so it refuses one past its size and
// a freed slot is reusable.
void test_peer_registry_is_bounded(void)
{
    uint8_t mac[6] = {0x02, 0, 0, 0, 0, 0};
    for (int i = 0; i < PROTOCORE_ESPNOW_MAX_PEERS; i++)
    {
        mac[5] = (uint8_t)i;
        EspnowV.peer_add_args.mac = mac;
        Espnow.peer_add(protocore_espnow_span());
        TEST_ASSERT_TRUE(EspnowV.ok);
    }
    Espnow.peer_count(protocore_espnow_span());
    TEST_ASSERT_EQUAL_INT(PROTOCORE_ESPNOW_MAX_PEERS, EspnowV.n);

    mac[5] = (uint8_t)PROTOCORE_ESPNOW_MAX_PEERS;
    EspnowV.peer_add_args.mac = mac;
    Espnow.peer_add(protocore_espnow_span());
    TEST_ASSERT_FALSE(EspnowV.ok); // full

    mac[5] = 0;
    EspnowV.peer_remove_args.mac = mac;
    Espnow.peer_remove(protocore_espnow_span());
    TEST_ASSERT_TRUE(EspnowV.ok);
    mac[5] = (uint8_t)PROTOCORE_ESPNOW_MAX_PEERS;
    EspnowV.peer_add_args.mac = mac;
    Espnow.peer_add(protocore_espnow_span());
    TEST_ASSERT_TRUE(EspnowV.ok); // the freed slot takes it

    EspnowV.peer_add_args.mac = NULL;
    Espnow.peer_add(protocore_espnow_span());
    TEST_ASSERT_FALSE(EspnowV.ok);
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
    EspnowV.begin_args.channel = 1;
    EspnowV.begin_args.cb = NULL;
    Espnow.begin(protocore_espnow_span());
    TEST_ASSERT_FALSE(EspnowV.ok);
    EspnowV.send_args.mac = MAC;
    EspnowV.send_args.type = 0x01;
    EspnowV.send_args.payload = PAYLOAD;
    EspnowV.send_args.len = sizeof(PAYLOAD);
    Espnow.send(protocore_espnow_span());
    TEST_ASSERT_FALSE(EspnowV.ok);
    EspnowV.broadcast_args.type = 0x01;
    EspnowV.broadcast_args.payload = PAYLOAD;
    EspnowV.broadcast_args.len = sizeof(PAYLOAD);
    Espnow.broadcast(protocore_espnow_span());
    TEST_ASSERT_FALSE(EspnowV.ok);

    EspnowV.add_peer_args.mac = MAC;
    Espnow.add_peer(protocore_espnow_span());
    TEST_ASSERT_TRUE(EspnowV.ok);
    EspnowV.peer_has_args.mac = MAC;
    Espnow.peer_has(protocore_espnow_span());
    TEST_ASSERT_TRUE(EspnowV.ok);
}
