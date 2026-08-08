// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file TlsResumption.ino
 * @brief HTTPS with TLS session resumption (RFC 5077 tickets, PC_ENABLE_TLS_RESUMPTION).
 *
 * Same HTTPS server as example 22, but with session tickets enabled: a returning
 * client completes an abbreviated handshake (no certificate or full key exchange),
 * which is dramatically cheaper than the full ECDHE/RSA handshake on a constrained
 * device. Resumption is stateless - the session lives in the client's ticket,
 * sealed with a server-held key - so nothing grows per session (the zero-heap
 * guarantee holds). There is no runtime API: it is a build-time option.
 *
 * Verify resumption with OpenSSL (look for "Reused" on the later connections):
 *     openssl s_client -connect <ip>:443 -tls1_2 -reconnect
 *
 * NOTE: optional services are gated by a compile flag the *library* sources must
 * also see; for PlatformIO enable it for the whole build, e.g.:
 *     build_flags = -DPC_ENABLE_TLS=1 -DPC_ENABLE_TLS_RESUMPTION=1
 * (Arduino IDE: they are already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 *
 * WARNING: the certificate and PRIVATE KEY below are a throwaway self-signed pair
 * committed for the demo - the key is public. NEVER use them in production.
 */

#define PC_ENABLE_TLS 1
#define PC_ENABLE_TLS_RESUMPTION 1

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


void setup()
{
    Serial.begin(115200);

    Physical.wifi->init(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!Physical.wifi->ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    on_http("/", HTTP_GET,
              [](uint8_t id, HttpReq *) { send_text(id, 200, "text/plain", "hello over resumable TLS\n"); });

    int32_t r =
        begin_tls(443, (const uint8_t *)CERT_PEM, sizeof(CERT_PEM), (const uint8_t *)KEY_PEM, sizeof(KEY_PEM), NULL);
    if (r < 0)
    {
        Serial.printf("begin_tls() failed (%d)\n", (int)r);
        return;
    }
    Serial.println("HTTPS + session resumption on :443  (openssl s_client -reconnect to see 'Reused')");
}

void loop()
{
    handle();
}
