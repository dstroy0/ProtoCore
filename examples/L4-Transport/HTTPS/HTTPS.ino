// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file HTTPS.ino
 * @brief HTTPS via the deterministic static-pool mbedTLS engine (PC_ENABLE_TLS).
 *
 * Serves the same routes over TLS on port 443. mbedTLS is pointed at a fixed BSS
 * arena (PC_TLS_ARENA_SIZE) through a custom allocator, so there is no heap
 * use and the determinism guarantee holds. The RNG is the ESP32 hardware CSPRNG.
 *
 * NOTE: optional services are gated by a compile flag the *library* sources must
 * also see, so for PlatformIO enable it for the whole build, e.g.:
 *     build_flags = -DPC_ENABLE_TLS=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.) The `#define`
 * below alone does not reach the separately-compiled library .cpp.
 *
 * Flash, open Serial @ 115200 for the IP, then (the cert is self-signed, so -k):
 *   curl -k https://<ip>/
 *   openssl s_client -connect <ip>:443        # inspect the handshake
 *
 * WARNING: the certificate and PRIVATE KEY below are a throwaway self-signed
 * pair committed for the demo - the key is public. NEVER use them in production;
 * generate your own and keep the key secret.
 */

#define PC_ENABLE_TLS 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// Test-only self-signed ECDSA (P-256) server certificate + key. DEMO ONLY.
static const char CERT_PEM[] = R"PEM(-----BEGIN CERTIFICATE-----
MIIBgjCCASegAwIBAgIUG3IKbVV5bjxjp4KYseqV+rhoZBwwCgYIKoZIzj0EAwIw
FjEUMBIGA1UEAwwLZXNwMzItZGV0d3MwHhcNMjYwNjIzMjMzMTU1WhcNMzYwNjIw
MjMzMTU1WjAWMRQwEgYDVQQDDAtlc3AzMi1kZXR3czBZMBMGByqGSM49AgEGCCqG
SM49AwEHA0IABBxkn1YSRR6zDM1sjmbv8KxT6c9UX25aU96TFUkoyce26FjFoG2b
ztF3D8WKXlBEiorylWNhai5T8dpniXuou2ujUzBRMB0GA1UdDgQWBBQW3pb8dDtr
15Ul1QyLl2WF/cVQ5DAfBgNVHSMEGDAWgBQW3pb8dDtr15Ul1QyLl2WF/cVQ5DAP
BgNVHRMBAf8EBTADAQH/MAoGCCqGSM49BAMCA0kAMEYCIQC1Mj9PLsbiu0zY+haX
IaYPYb8erMPadAi+h71aG2JCpwIhAJ/mMzrtrTT4GJ0x+Ijpm8Mc0kU3KR9sNipX
wxUQ6Sfd
-----END CERTIFICATE-----
)PEM";

static const char KEY_PEM[] = R"PEM(-----BEGIN EC PRIVATE KEY-----
MHcCAQEEIECeRrZswSjVISz/EEkAK02Jf39SRWTPRBOcbqIhSolQoAoGCCqGSM49
AwEHoUQDQgAEHGSfVhJFHrMMzWyOZu/wrFPpz1RfblpT3pMVSSjJx7boWMWgbZvO
0XcPxYpeUESKivKVY2FqLlPx2meJe6i7aw==
-----END EC PRIVATE KEY-----
)PEM";


void handle_root(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    send_text(slot_id, 200, "text/plain", "hello over TLS (deterministic, static-pool mbedTLS)\n");
}

void handle_status(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    char body[96];
    snprintf(body, sizeof(body), "{\"tls\":true,\"uptime_ms\":%lu,\"free_heap\":%u}", millis(), ESP.getFreeHeap());
    send_text(slot_id, 200, "application/json", body);
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

    on_http("/", HTTP_GET, handle_root);
    on_http("/status", HTTP_GET, handle_status);

    // Load cert/key into the static-pool TLS engine and listen on 443.
    int32_t result =
        begin_tls(443, (const uint8_t *)CERT_PEM, sizeof(CERT_PEM), (const uint8_t *)KEY_PEM, sizeof(KEY_PEM));
    if (result < 0)
    {
        Serial.printf("begin_tls() failed (error %d) - check the cert/key and arena size\n", result);
        return;
    }
    Serial.println("HTTPS server started on port 443  (curl -k https://<ip>/)");
}

void loop()
{
    handle();
}
