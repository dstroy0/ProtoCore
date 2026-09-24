// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file IKEv2.ino
 * @brief IKEv2 (RFC 7296) message framing demo - build an IKE_SA_INIT initiator message, dump it, walk
 *        its payload chain, and optionally fire it at a VPN gateway on UDP 500 and parse the reply
 *        (PROTOCORE_ENABLE_IKEV2).
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
 * network_drivers/transport/udp/udp.h - no outside UDP library): UdpListener.listen() binds port 500 to receive
 * the responder's reply, and UdpListener.sendto() sends the request FROM port 500 so the reply
 * (addressed back to :500) is delivered to our listener.
 *
 * Build flags (platformio.ini):  build_flags = -DPROTOCORE_ENABLE_IKEV2=1
 */

#define PROTOCORE_ENABLE_IKEV2 1

#include "protocore.h" // library entry header (pulls in Arduino + sets the src/ include root)
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/transport/udp/udp.h"
#include "services/security/ikev2/ikev2/ikev2.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static const char *GATEWAY_IP = ""; // e.g. "192.168.1.1"; leave "" to only build + self-parse
static protocore_ip g_gateway;                   // GATEWAY_IP, parsed once in setup()

static uint8_t msg[512];
static uint8_t ike_work[16]; // the borrow an Ike entry takes; the codec never reads it

// Build an IKE_SA_INIT initiator message: header + SA (AES-256-CBC / SHA2-256 PRF+INTEG / MODP2048) +
// KE + Nonce. The KE data and nonce are placeholders (the DH math is a later tier).
static size_t build_sa_init(uint8_t *buf, size_t cap)
{
    IkeHeader &h = IkeV.hdr;
    memset(&h, 0, sizeof(h));
    for (int i = 0; i < 8; i++)
    {
        h.init_spi[i] = (uint8_t)(0xA0 + i); // a fixed demo SPI (a real one is random)
    }
    h.next_payload = IKE_PL_SA;
    h.version = PROTOCORE_IKE_VERSION;
    h.exchange = IKE_SA_INIT;
    h.flags = PROTOCORE_IKE_FLAG_INITIATOR;
    h.message_id = 0;
    h.length = 0; // patched below

    IkeV.out.buf = buf;
    IkeV.out.cap = cap;
    Ike.hdr_build(ike_work);
    size_t off = IkeV.n;

    IkeTransform tr[4] = {
        {IKE_TRANSFORM_ENCR, IKE_ENCR_AES_CBC, 256},
        {IKE_TRANSFORM_PRF, IKE_PRF_HMAC_SHA2_256, -1},
        {IKE_TRANSFORM_INTEG, IKE_INTEG_HMAC_SHA2_256_128, -1},
        {IKE_TRANSFORM_DH, IKE_DH_MODP2048, -1},
    };
    IkeV.out.buf = buf + off;
    IkeV.out.cap = cap - off;
    IkeV.pl.next_payload = IKE_PL_KE;
    IkeV.prop.proposal_num = 1;
    IkeV.prop.protocol_id = IKE_PROTO_IKE;
    IkeV.prop.spi = nullptr;
    IkeV.prop.spi_size = 0;
    IkeV.prop.transforms = tr;
    IkeV.prop.num_transforms = 4;
    Ike.sa_build(ike_work);
    off += IkeV.n;

    // placeholder DH public value + nonce (a real client fills these from the crypto tier)
    uint8_t ke_data[32], nonce[16];
    memset(ke_data, 0xAB, sizeof(ke_data));
    for (size_t i = 0; i < sizeof(nonce); i++)
    {
        nonce[i] = (uint8_t)(0x5A ^ i);
    }
    IkeV.out.buf = buf + off;
    IkeV.out.cap = cap - off;
    IkeV.pl.next_payload = IKE_PL_NONCE;
    IkeV.pl.data = ke_data;
    IkeV.pl.data_len = sizeof(ke_data);
    IkeV.ke.dh_group = IKE_DH_MODP2048;
    Ike.ke_build(ike_work);
    off += IkeV.n;

    IkeV.out.buf = buf + off;
    IkeV.out.cap = cap - off;
    IkeV.pl.next_payload = IKE_PL_NONE;
    IkeV.pl.data = nonce;
    IkeV.pl.data_len = sizeof(nonce);
    Ike.nonce_build(ike_work);
    off += IkeV.n;

    IkeV.out.buf = buf;
    IkeV.out.cap = cap;
    IkeV.msg.length = (uint32_t)off;
    Ike.set_length(ike_work);
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
    IkeV.wire.msg = buf;
    IkeV.wire.len = len;
    Ike.hdr_parse(ike_work);
    if (!IkeV.ok)
    {
        Serial.printf("[ike] %s: too short for a header\n", what);
        return;
    }
    Serial.printf("[ike] %s: exch=%u flags=0x%02x msgid=%u len=%u\n", what, (unsigned)IkeV.hdr.exchange,
                  (unsigned)IkeV.hdr.flags,
                  (unsigned)IkeV.hdr.message_id, (unsigned)IkeV.hdr.length);

    IkePayloadIter it;
    IkeV.walk.chain = &it;
    IkeV.walk.first_type = IkeV.hdr.next_payload;
    IkeV.wire.msg = buf + PROTOCORE_IKE_HDR_LEN;
    IkeV.wire.len = len - PROTOCORE_IKE_HDR_LEN;
    Ike.payload_iter_init(ike_work);
    for (Ike.payload_next(ike_work); IkeV.ok; Ike.payload_next(ike_work))
    {
        const IkePayload pl = IkeV.payload; // the parses below reuse IkeV
        Serial.printf("[ike]   payload type=%u body=%u", (unsigned)pl.type, (unsigned)pl.body_len);
        if (pl.type == IKE_PL_SA)
        {
            IkeV.wire.msg = pl.body;
            IkeV.wire.len = pl.body_len;
            Ike.sa_first_proposal(ike_work);
            if (IkeV.ok)
            {
                Serial.printf("  (SA proposal %u, %u transforms)", (unsigned)IkeV.proposal.proposal_num,
                              (unsigned)IkeV.proposal.num_transforms);
            }
        }
        else if (pl.type == IKE_PL_KE)
        {
            IkeV.wire.msg = pl.body;
            IkeV.wire.len = pl.body_len;
            Ike.ke_parse(ike_work);
            if (IkeV.ok)
            {
                Serial.printf("  (KE group %u, %u bytes)", (unsigned)IkeV.ke_ref.dh_group,
                              (unsigned)IkeV.ke_ref.ke_len);
            }
        }
        else if (pl.type == IKE_PL_NOTIFY)
        {
            IkeV.wire.msg = pl.body;
            IkeV.wire.len = pl.body_len;
            Ike.notify_parse(ike_work);
            if (IkeV.ok)
            {
                Serial.printf("  (NOTIFY type %u)", (unsigned)IkeV.notify_ref.notify_type);
            }
        }
        Serial.println(); // the sub-parses reuse IkeV.wire; the walk keeps its place in `it`
    }
}

// UDP receive callback (lwIP thread): the gateway's reply to our IKE_SA_INIT.
static void on_ike_reply(const uint8_t *data, size_t len, const struct protocore_udp_peer *peer, void *ctx)
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
    UdpListenerV.port = PROTOCORE_IKEV2_PORT;
    UdpListenerV.bind.handler = on_ike_reply;
    UdpListenerV.bind.handler_ctx = nullptr;
    UdpListener.listen(protocore_udp_listener_span());
    if (!UdpListenerV.ok)
    {
        Serial.println("[ike] could not bind UDP 500");
        return;
    }
    UdpListenerV.port = PROTOCORE_IKEV2_PORT;
    UdpListenerV.send_args.dst = &g_gateway;
    UdpListenerV.send_args.dst_port = PROTOCORE_IKEV2_PORT;
    UdpListenerV.send_args.data = msg;
    UdpListenerV.send_args.len = n;
    UdpListener.sendto(protocore_udp_listener_span());
    if (!UdpListenerV.ok)
    {
        Serial.println("[ike] send failed");
        return;
    }
    Serial.printf("[ike] sent to %s:%u - a reply prints from the UDP callback\n", GATEWAY_IP, PROTOCORE_IKEV2_PORT);
}

void setup()
{
    IpV.args.text = GATEWAY_IP;
    IpV.args.out = &g_gateway;
    Ip.parse(ike_work); // the tag becomes an address once, here

    Serial.begin(115200);
    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32; // library egress IP (network byte order), no Arduino WiFi
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
