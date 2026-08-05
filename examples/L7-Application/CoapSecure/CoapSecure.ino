// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file CoapSecure.ino
 * @brief CoAP over DTLS 1.3 (CoAPs, RFC 7252 §9) on UDP/5684 - the same resources as CoAP, secured.
 *
 * Exposes the resources through a DTLS 1.3 server (coaps://), dispatched against the request Uri-Path:
 *   GET  /info  -> application/json  (uptime + free heap)
 *   GET  /led   -> text/plain        ("0" or "1")
 *   PUT  /led   -> drive the on-board LED from the request payload ("0"/"1")
 *   GET  /hello -> text/plain        (a constant greeting)
 *
 * coaps_server owns a small pool of DTLS connections keyed by peer address, binds UDP/5684 through the
 * transport layer, and (once a peer's handshake completes) hands each decrypted CoAP request to the
 * same pc_coap_server_process() the plaintext server uses - so the resource table is registered exactly
 * once with pc_coap_server_add_resource(). pc_coaps_server_poll() drives the handshakes, the DTLS
 * retransmission timer, and idle-connection reaping; call it every loop iteration.
 *
 * The profile is TLS_AES_128_GCM_SHA256 + X25519 + an Ed25519 server certificate. The sketch ships a
 * *throwaway demo* certificate + key below - regenerate your own before using this for anything real:
 *   openssl genpkey -algorithm ed25519 -out ed.key
 *   openssl pkey -in ed.key -outform DER | tail -c 32 > seed.bin        # raw 32-byte Ed25519 seed
 *   openssl req -x509 -new -key ed.key -subj '/CN=my-device' -days 3650 -out cert.pem
 *   openssl x509 -in cert.pem -outform DER -out cert.der                # DER leaf certificate
 * then paste cert.der / seed.bin (e.g. `xxd -i`) over the arrays below.
 *
 * Flash, open Serial @ 115200 for the IP, then from a host with a DTLS 1.3 CoAP client:
 *   coap-client -m get coaps://<ip>/info      # libcoap built with DTLS 1.3 (accept the demo cert)
 *   aiocoap-client coaps://<ip>/hello         # aiocoap with a DTLS backend
 * The verified reference client is the wolfSSL harness in test/servers/pc_dtls_wolfssl.
 *
 * NOTE: optional services are gated by compile flags the *library* sources must also see. The
 * `#define`s below document intent, but for PlatformIO enable them for the whole build, e.g.:
 *     build_flags = -DPC_ENABLE_COAP=1 -DPC_ENABLE_DTLS=1
 * (Arduino IDE: already set for you in build_opt.h beside this sketch, so it builds as-is.) A define in
 * the sketch alone does not reach the separately-compiled library .cpp.
 */

#define PC_ENABLE_COAP 1
#define PC_ENABLE_DTLS 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/iot/coap/coap.h"
#include "services/iot/coap/coaps_server.h"
#include <esp_random.h> // esp_fill_random() - the ESP32 hardware CSPRNG

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

// --- throwaway demo Ed25519 leaf certificate (DER) + its raw 32-byte seed. REGENERATE for real use;
// see the openssl commands in the file header. ---
static const uint8_t COAPS_CERT_DER[322] = {
    0x30, 0x82, 0x01, 0x3e, 0x30, 0x81, 0xf1, 0xa0, 0x03, 0x02, 0x01, 0x02, 0x02, 0x14, 0x6e, 0x8e, 0x6a, 0xea, 0x1d,
    0x61, 0x38, 0xdf, 0x3e, 0xe4, 0x02, 0x24, 0x84, 0xb5, 0x03, 0x43, 0x9f, 0xa3, 0x43, 0x9d, 0x30, 0x05, 0x06, 0x03,
    0x2b, 0x65, 0x70, 0x30, 0x15, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0c, 0x0a, 0x63, 0x6f, 0x61,
    0x70, 0x73, 0x2d, 0x64, 0x65, 0x6d, 0x6f, 0x30, 0x1e, 0x17, 0x0d, 0x32, 0x36, 0x30, 0x37, 0x31, 0x36, 0x30, 0x31,
    0x34, 0x37, 0x33, 0x31, 0x5a, 0x17, 0x0d, 0x33, 0x36, 0x30, 0x37, 0x31, 0x33, 0x30, 0x31, 0x34, 0x37, 0x33, 0x31,
    0x5a, 0x30, 0x15, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0c, 0x0a, 0x63, 0x6f, 0x61, 0x70, 0x73,
    0x2d, 0x64, 0x65, 0x6d, 0x6f, 0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00, 0xd9, 0xd5,
    0xee, 0xa7, 0xe4, 0x46, 0xdf, 0xa8, 0x01, 0x72, 0x25, 0x73, 0xab, 0xf5, 0x89, 0x66, 0x4a, 0xd3, 0x42, 0x16, 0xb7,
    0x97, 0x1c, 0x93, 0x26, 0xfc, 0x03, 0x47, 0x5d, 0x20, 0x39, 0xe0, 0xa3, 0x53, 0x30, 0x51, 0x30, 0x1d, 0x06, 0x03,
    0x55, 0x1d, 0x0e, 0x04, 0x16, 0x04, 0x14, 0x0e, 0x62, 0x41, 0x8c, 0x2f, 0xdd, 0xaa, 0xdc, 0x18, 0xf4, 0xa2, 0xbf,
    0xf3, 0xd8, 0xa2, 0xcb, 0x23, 0xd7, 0x0d, 0xda, 0x30, 0x1f, 0x06, 0x03, 0x55, 0x1d, 0x23, 0x04, 0x18, 0x30, 0x16,
    0x80, 0x14, 0x0e, 0x62, 0x41, 0x8c, 0x2f, 0xdd, 0xaa, 0xdc, 0x18, 0xf4, 0xa2, 0xbf, 0xf3, 0xd8, 0xa2, 0xcb, 0x23,
    0xd7, 0x0d, 0xda, 0x30, 0x0f, 0x06, 0x03, 0x55, 0x1d, 0x13, 0x01, 0x01, 0xff, 0x04, 0x05, 0x30, 0x03, 0x01, 0x01,
    0xff, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x41, 0x00, 0xe6, 0x72, 0xa4, 0x0b, 0x53, 0xe2, 0x8d, 0x27,
    0xd4, 0x63, 0x13, 0x23, 0xa0, 0xd5, 0xb2, 0xe6, 0x3c, 0x93, 0x2c, 0x8e, 0xab, 0x74, 0x0e, 0x73, 0x41, 0x6a, 0xe4,
    0x90, 0xab, 0xec, 0x9f, 0x11, 0x84, 0x0b, 0x81, 0x62, 0x07, 0x8b, 0x3e, 0x45, 0xd1, 0xcf, 0x09, 0xe4, 0xa0, 0x39,
    0x9d, 0xbf, 0x01, 0x5e, 0xe0, 0x13, 0x24, 0x05, 0x66, 0x25, 0x2c, 0x4a, 0xee, 0x0a, 0x71, 0x85, 0xed, 0x0e,
};
static const uint8_t COAPS_ED25519_SEED[32] = {
    0x5b, 0x94, 0x8d, 0xdc, 0x1b, 0xd1, 0x98, 0x9a, 0x96, 0x18, 0x08, 0x14, 0x8e, 0xe5, 0xda, 0xf9,
    0x3f, 0x40, 0x41, 0x31, 0x43, 0x53, 0x58, 0x58, 0x49, 0xaf, 0x03, 0x2b, 0x33, 0xef, 0x6d, 0x6a,
};

static int g_led_state = 0;

// The DTLS server's per-handshake randomness (X25519 ephemeral + ServerHello random): the hardware CSPRNG.
static void coaps_rng(uint8_t *out, size_t len)
{
    esp_fill_random(out, len);
}

// GET /info -> a small JSON document with uptime and free heap.
static void coap_info(const CoapRequest *req, CoapResponse *resp)
{
    (void)req;
    int n = snprintf((char *)resp->payload, resp->payload_cap, "{\"uptime_ms\":%lu,\"free_heap\":%u}",
                     (unsigned long)millis(), (unsigned)ESP.getFreeHeap());
    if (n < 0)
    {
        n = 0;
    }
    resp->payload_len = (size_t)n;
    resp->content_format = COAP_CF_JSON;
    resp->code = (uint8_t)COAP_RSP_CONTENT;
}

// GET/PUT /led -> read or drive the on-board LED.
static void coap_led(const CoapRequest *req, CoapResponse *resp)
{
    if (req->method == COAP_PUT)
    {
        g_led_state = (req->payload_len && req->payload[0] != '0') ? 1 : 0;
        digitalWrite(LED_BUILTIN, g_led_state ? HIGH : LOW);
        resp->code = (uint8_t)COAP_RSP_CHANGED;
        resp->payload_len = 0;
        return;
    }
    resp->payload[0] = g_led_state ? '1' : '0';
    resp->payload_len = 1;
    resp->content_format = COAP_CF_TEXT;
    resp->code = (uint8_t)COAP_RSP_CONTENT;
}

// GET /hello -> a constant greeting.
static void coap_hello(const CoapRequest *req, CoapResponse *resp)
{
    (void)req;
    static const char msg[] = "hello from a deterministic CoAPs server";
    size_t n = sizeof(msg) - 1;
    if (n > resp->payload_cap)
    {
        n = resp->payload_cap;
    }
    memcpy(resp->payload, msg, n);
    resp->payload_len = n;
    resp->content_format = COAP_CF_TEXT;
    resp->code = (uint8_t)COAP_RSP_CONTENT;
}

void setup()
{
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);

    init_wifi_physical(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!wifi_ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Build the resource table once (shared by every transport), then start the DTLS front-end on 5684.
    pc_coap_server_reset();
    pc_coap_server_add_resource("/info", COAP_ALLOW_GET, coap_info);
    pc_coap_server_add_resource("/led", COAP_ALLOW_GET | COAP_ALLOW_PUT, coap_led);
    pc_coap_server_add_resource("/hello", COAP_ALLOW_GET, coap_hello);

    CoapsServerConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.cert_der = COAPS_CERT_DER;
    cfg.cert_len = sizeof(COAPS_CERT_DER);
    memcpy(cfg.ed25519_seed, COAPS_ED25519_SEED, sizeof cfg.ed25519_seed);
    esp_fill_random(cfg.cookie_key, sizeof cfg.cookie_key); // fresh HelloRetryRequest cookie secret per boot
    cfg.rng = coaps_rng;
    if (pc_coaps_server_begin(PC_COAPS_PORT, &cfg))
    {
        Serial.println("CoAPs (DTLS 1.3) server listening on UDP/5684 (try: coap-client -m get coaps://<ip>/info)");
    }
    else
    {
        Serial.println("pc_coaps_server_begin() failed (UDP bind)");
    }

    int32_t result = begin_http(80, NULL);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
    }
}

void loop()
{
    pc_coaps_server_poll(); // drive DTLS handshakes, the retransmission timer, and idle-connection reaping
    handle();        // the TCP server (CoAPs itself runs off lwIP UDP callbacks + this poll)
}
