// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file IKEv2.ino
 * @brief IKEv2 (RFC 7296) message framing demo - build an IKE_SA_INIT initiator message, dump it, walk
 *        its payload chain, and optionally fire it at a VPN gateway on UDP 500 and parse the reply
 *        (PC_ENABLE_IKEV2).
 *
 * services/security/ikev2 is TIER 1 of an IKEv2 / IPsec stack: the pure wire codec. It frames the 28-octet IKE
 * header and the SA / KE / Nonce / ... payload chain and parses them back, but it does NOT do the
 * Diffie-Hellman math, the SKEYSEED / SK_* key derivation, or the SA state machine (those are later
 * tiers). So the KE public value and Nonce below are PLACEHOLDERS - this sketch shows that the codec
 * emits a structurally valid IKE_SA_INIT and can walk any IKEv2 message; it does NOT complete a
 * handshake. A real gateway will typically answer a well-formed (but cryptographically incomplete)
 * IKE_SA_INIT with an INVALID_KE_PAYLOAD / COOKIE notify, which the on_ike_reply() callback parses.
 *
 * The UDP socket is the library's own transport (services bind + exchange datagrams through
 * network_drivers/transport/udp.h - no outside UDP library): Udp.listener->listen() binds port 500 to receive
 * the responder's reply, and Udp.listener->sendto() sends the request FROM port 500 so the reply
 * (addressed back to :500) is delivered to our listener.
 *
 * Build flags (platformio.ini):  build_flags = -DPC_ENABLE_IKEV2=1
 */

#define PC_ENABLE_IKEV2 1

#include "protocore.h" // library entry header (pulls in Arduino + sets the src/ include root)
#include "network_drivers/physical/physical.h"
#include "network_drivers/transport/udp.h"
#include "services/security/ikev2/ikev2.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static const char *GATEWAY_IP = ""; // e.g. "192.168.1.1"; leave "" to only build + self-parse
static pc_ip g_gateway;                   // GATEWAY_IP, parsed once in setup()

static uint8_t msg[512];

// Build an IKE_SA_INIT initiator message: header + SA (AES-256-CBC / SHA2-256 PRF+INTEG / MODP2048) +
// KE + Nonce. The KE data and nonce are placeholders (the DH math is a later tier).
static size_t build_sa_init(uint8_t *buf, size_t cap)
{
    IkeHeader h;
    memset(&h, 0, sizeof(h));
    for (int i = 0; i < 8; i++)
    {
        h.init_spi[i] = (uint8_t)(0xA0 + i); // a fixed demo SPI (a real one is random)
    }
    h.next_payload = IKE_PL_SA;
    h.version = PC_IKE_VERSION;
    h.exchange = IKE_SA_INIT;
    h.flags = PC_IKE_FLAG_INITIATOR;
    h.message_id = 0;
    h.length = 0; // patched below

    size_t off = pc_ike_hdr_build(buf, cap, &h);

    IkeTransform tr[4] = {
        {IKE_TRANSFORM_ENCR, IKE_ENCR_AES_CBC, 256},
        {IKE_TRANSFORM_PRF, IKE_PRF_HMAC_SHA2_256, -1},
        {IKE_TRANSFORM_INTEG, IKE_INTEG_HMAC_SHA2_256_128, -1},
        {IKE_TRANSFORM_DH, IKE_DH_MODP2048, -1},
    };
    off += pc_ike_sa_build(buf + off, cap - off, IKE_PL_KE, 1, IKE_PROTO_IKE, nullptr, 0,
                           tr, 4);

    // placeholder DH public value + nonce (a real client fills these from the crypto tier)
    uint8_t ke_data[32], nonce[16];
    memset(ke_data, 0xAB, sizeof(ke_data));
    for (size_t i = 0; i < sizeof(nonce); i++)
    {
        nonce[i] = (uint8_t)(0x5A ^ i);
    }
    off +=
        pc_ike_ke_build(buf + off, cap - off, IKE_PL_NONCE, IKE_DH_MODP2048, ke_data, sizeof(ke_data));
    off += pc_ike_nonce_build(buf + off, cap - off, IKE_PL_NONE, nonce, sizeof(nonce));

    pc_ike_set_length(buf, cap, (uint32_t)off);
    return off;
}

static void hexdump(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        Serial.printf("%02x ", buf[i]);
        if ((i & 15) == 15)
        {
            Serial.println();
        }
    }
    Serial.println();
}

// Parse a whole IKEv2 message and print its header + each payload in the chain.
static void parse_and_print(const uint8_t *buf, size_t len, const char *what)
{
    IkeHeader h;
    if (!pc_ike_hdr_parse(buf, len, &h))
    {
        Serial.printf("[ike] %s: too short for a header\n", what);
        return;
    }
    Serial.printf("[ike] %s: exch=%u flags=0x%02x msgid=%u len=%u\n", what, (unsigned)h.exchange, (unsigned)h.flags,
                  (unsigned)h.message_id, (unsigned)h.length);

    IkePayloadIter it;
    pc_ike_payload_iter_init(&it, h.next_payload, buf + PC_IKE_HDR_LEN, len - PC_IKE_HDR_LEN);
    IkePayload pl;
    while (pc_ike_payload_next(&it, &pl))
    {
        Serial.printf("[ike]   payload type=%u body=%u", (unsigned)pl.type, (unsigned)pl.body_len);
        if (pl.type == IKE_PL_SA)
        {
            IkeProposalRef prop;
            if (pc_ike_sa_first_proposal(pl.body, pl.body_len, &prop))
            {
                Serial.printf("  (SA proposal %u, %u transforms)", (unsigned)prop.proposal_num,
                              (unsigned)prop.num_transforms);
            }
        }
        else if (pl.type == IKE_PL_KE)
        {
            uint16_t group = 0;
            const uint8_t *d = nullptr;
            size_t dl = 0;
            if (pc_ike_ke_parse(pl.body, pl.body_len, &group, &d, &dl))
            {
                Serial.printf("  (KE group %u, %u bytes)", (unsigned)group, (unsigned)dl);
            }
        }
        else if (pl.type == IKE_PL_NOTIFY)
        {
            IkeProtocol proto = IKE_PROTO_NONE;
            uint8_t ss = 0;
            uint16_t type = 0;
            const uint8_t *spi = nullptr, *d = nullptr;
            size_t dl = 0;
            if (pc_ike_notify_parse(pl.body, pl.body_len, &proto, &type, &spi, &ss, &d, &dl))
            {
                Serial.printf("  (NOTIFY type %u)", (unsigned)type);
            }
        }
        Serial.println();
    }
}

// UDP receive callback (lwIP thread): the gateway's reply to our IKE_SA_INIT.
static void on_ike_reply(const uint8_t *data, size_t len, const struct pc_udp_peer *peer, void *ctx)
{
    (void)peer;
    (void)ctx;
    parse_and_print(data, len, "gateway reply");
}

static void run_once()
{
    size_t n = build_sa_init(msg, sizeof(msg));
    Serial.printf("[ike] built IKE_SA_INIT, %u bytes:\n", (unsigned)n);
    hexdump(msg, n);
    parse_and_print(msg, n, "self");

    if (!GATEWAY_IP[0])
    {
        Serial.println("[ike] no GATEWAY_IP set - build + self-parse only");
        return;
    }

    // Bind :500 to receive the reply, then send FROM :500 so the responder's reply reaches our listener.
    if (!Udp.listener->listen(PC_IKEV2_PORT, on_ike_reply, nullptr))
    {
        Serial.println("[ike] could not bind UDP 500");
        return;
    }
    if (!Udp.listener->sendto(PC_IKEV2_PORT, &g_gateway, PC_IKEV2_PORT, msg, n))
    {
        Serial.println("[ike] send failed");
        return;
    }
    Serial.printf("[ike] sent to %s:%u - a reply prints from the UDP callback\n", GATEWAY_IP, PC_IKEV2_PORT);
}

void setup()
{
    Ip.parse(GATEWAY_IP, &g_gateway); // the tag becomes an address once, here

    Serial.begin(115200);
    init_wifi_physical(SSID, PASSWORD);
    while (!wifi_ready())
    {
        delay(250);
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
}

void loop()
{
    static bool done = false;
    if (!done && millis() > 2000)
    {
        done = true;
        run_once();
        Serial.println("[ike] request issued");
    }
    delay(10);
}
