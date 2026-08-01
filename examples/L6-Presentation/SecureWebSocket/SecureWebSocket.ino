// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file SecureWebSocket.ino
 * @brief Secure WebSocket (wss://) and Server-Sent Events over TLS.
 *
 * WebSocket and SSE both run over the deterministic TLS engine: the wss://
 * handshake and every frame are encrypted, and SSE events are pushed over the
 * same TLS record layer. It is transparent to handler code - you register the
 * same on_ws() / on_sse() routes; enabling PC_ENABLE_TLS and serving on a TLS
 * listener is all it takes. (wss receive decrypts records straight into the
 * frame parser; SSE is send-only.)
 *
 * Flash, open Serial @ 115200 for the IP, then (self-signed cert -> skip verify):
 *   wss://<ip>/ws       e.g. a browser console:  new WebSocket("wss://<ip>/ws")
 *   curl -k -N https://<ip>/events                # live SSE stream over TLS
 *
 * WARNING: the certificate and PRIVATE KEY below are a throwaway pair committed
 * for the demo - the key is public. NEVER use them in production.
 *
 * NOTE: optional features are gated by a compile flag the *library* sources must
 * also see; for PlatformIO enable them for the whole build, e.g.:
 *     build_flags = -DPC_ENABLE_TLS=1
 * (WebSocket and SSE are on by default.)
 */

#define PC_ENABLE_TLS 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/presentation/http/websocket/websocket.h" // ws_pool[] for the echo payload

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// Throwaway self-signed-style ECDSA P-256 server cert + key. DEMO ONLY.
static const char SERVER_CERT_PEM[] = R"PEM(-----BEGIN CERTIFICATE-----
MIIBsjCCAVigAwIBAgIUGy5nzIkUKHk5TGe5a7cC4P2SkkgwCgYIKoZIzj0EAwIw
GDEWMBQGA1UEAwwNRGV0V1MgRGVtbyBDQTAeFw0yNjA2MjUxNzMyMjFaFw0zNjA2
MjIxNzMyMjFaMBcxFTATBgNVBAMMDGRldHdzLWRldmljZTBZMBMGByqGSM49AgEG
CCqGSM49AwEHA0IABOpv7lCVnYJU264F7KMKz/6gVooJMVsDv7Ic31eW72Se0RVC
idvsF/HoGKndxUC99hy8jkVrh1Bz3U5VLZVpezijgYAwfjAPBgNVHREECDAGhwTA
qAFVMAkGA1UdEwQCMAAwCwYDVR0PBAQDAgWgMBMGA1UdJQQMMAoGCCsGAQUFBwMB
MB0GA1UdDgQWBBQZ3SxOD9+Fm9cAnPXOIJjpaD6WlTAfBgNVHSMEGDAWgBQNkbLT
cmciMZod9ljkOh7/htyGqTAKBggqhkjOPQQDAgNIADBFAiArX32WETSfyFfNPD8y
16wPsr9hDn+jLhI8dgdaDV32owIhAJY1UanHCO+zY3gKd6SFzIJX7N6Swan6Zkl/
rSZTNBUm
-----END CERTIFICATE-----
)PEM";

static const char SERVER_KEY_PEM[] = R"PEM(-----BEGIN EC PRIVATE KEY-----
MHcCAQEEIP/URhdgBALhCZx9ab9ONj+XD4XFWQw9ctcoH0pGQnEioAoGCCqGSM49
AwEHoUQDQgAE6m/uUJWdglTbrgXsowrP/qBWigkxWwO/shzfV5bvZJ7RFUKJ2+wX
8egYqd3FQL32HLyORWuHUHPdTlUtlWl7OA==
-----END EC PRIVATE KEY-----
)PEM";

void ws_connect(uint8_t ws_id)
{
    ws_send_text(ws_id, "secure channel up - type something");
}

void ws_message(uint8_t ws_id)
{
    char out[WS_FRAME_SIZE + 8];
    snprintf(out, sizeof(out), "echo: %s", (const char *)ws_pool[ws_id].buf);
    ws_send_text(ws_id, out);
}

void ws_close(uint8_t ws_id)
{
    (void)ws_id;
}

void pc_sse_connect(uint8_t pc_sse_id)
{
    pc_sse_send(pc_sse_id, "subscribed", "tick");
}

void setup()
{
    Serial.begin(115200);

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

    on_ws("/ws", ws_connect, ws_message, ws_close);
    on_sse("/events", pc_sse_connect);

    int32_t result = begin_tls(443, (const uint8_t *)SERVER_CERT_PEM, sizeof(SERVER_CERT_PEM),
                                      (const uint8_t *)SERVER_KEY_PEM, sizeof(SERVER_KEY_PEM));
    if (result < 0)
    {
        Serial.printf("begin_tls() failed (error %d)\n", result);
    }
    else
    {
        Serial.println("wss:// + TLS SSE server on :443");
    }
}

void loop()
{
    handle();

    // Push an SSE counter once a second (encrypted over TLS to subscribers).
    static uint32_t last = 0;
    static uint32_t n = 0;
    if (millis() - last >= 1000)
    {
        last = millis();
        char buf[24];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)n++);
        pc_sse_broadcast("/events", buf, "tick");
    }
}
