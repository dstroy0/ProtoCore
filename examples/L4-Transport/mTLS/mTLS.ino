// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mTLS.ino
 * @brief Mutual TLS - require and verify a client certificate (mTLS).
 *
 * With PC_ENABLE_MTLS the HTTPS server demands a client certificate during the
 * TLS handshake and verifies it against a configured trust-anchor CA. A client
 * that presents no certificate, or one not signed by the CA, is rejected before
 * any HTTP is exchanged - strong transport-level client authentication with no
 * passwords. Inside a handler, tls_client_subject() returns the verified peer's
 * certificate subject DN so you can identify or authorize the caller.
 *
 * Flash, open Serial @ 115200 for the IP, then test with the DEMO client cert
 * below (extract the two PEM blocks into client.crt / client.key):
 *   curl -k --cert client.crt --key client.key https://<ip>/whoami     # 200, your DN
 *   curl -k https://<ip>/whoami                                        # handshake fails
 *
 * WARNING: the certificates and PRIVATE KEYS below are throwaway demo material
 * committed for the example - the keys are public. NEVER use them in production;
 * generate your own CA / server / client pairs and keep the keys secret.
 *
 * NOTE: optional features are gated by a compile flag the *library* sources must
 * also see. The defines below document intent, but for PlatformIO enable them
 * for the whole build, e.g. in platformio.ini:
 *     build_flags = -DPC_ENABLE_TLS=1 -DPC_ENABLE_MTLS=1
 * (Arduino IDE: they are already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_TLS 1
#define PC_ENABLE_MTLS 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// --- DEMO server identity (ECDSA P-256), presented to the client. ----------
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

// --- DEMO trust-anchor CA: client certs must chain to this. -----------------
static const char CA_CERT_PEM[] = R"PEM(-----BEGIN CERTIFICATE-----
MIIBhDCCASugAwIBAgIUA6/2t+cW3keoDgK2gD9WdUOTf2AwCgYIKoZIzj0EAwIw
GDEWMBQGA1UEAwwNRGV0V1MgRGVtbyBDQTAeFw0yNjA2MjUxNzMyMDlaFw0zNjA2
MjIxNzMyMDlaMBgxFjAUBgNVBAMMDURldFdTIERlbW8gQ0EwWTATBgcqhkjOPQIB
BggqhkjOPQMBBwNCAAS5zckoHZfkZxpIgosprmmz0jKANLkgaRy4tPpr1mD7ibJR
XXyXZgUxRLS4LFJ7MIEN/NXMmdPuiZSIjJ2i0ibIo1MwUTAdBgNVHQ4EFgQUDZGy
03JnIjGaHfZY5Doe/4bchqkwHwYDVR0jBBgwFoAUDZGy03JnIjGaHfZY5Doe/4bc
hqkwDwYDVR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAgNHADBEAiB+w6mqTKEkOKiB
VznPbF5MrYYNuzAxI7mGxUjNYZo0BQIgTCm/61EIZ+Uh0YHfWckGye1uIYrIhB6w
5qjCmKqAvLo=
-----END CERTIFICATE-----
)PEM";

// --- DEMO client cert + key (chains to the CA above). Save these two blocks to
//     client.crt / client.key and pass them to curl --cert/--key:
//
// -----BEGIN CERTIFICATE-----
// MIIBpDCCAUugAwIBAgIUGy5nzIkUKHk5TGe5a7cC4P2SkkkwCgYIKoZIzj0EAwIw
// GDEWMBQGA1UEAwwNRGV0V1MgRGVtbyBDQTAeFw0yNjA2MjUxNzMyMjFaFw0zNjA2
// MjIxNzMyMjFaMBwxGjAYBgNVBAMMEWFsaWNlQGV4YW1wbGUuY29tMFkwEwYHKoZI
// zj0CAQYIKoZIzj0DAQcDQgAEBS7DcoB4YMfvkUGyci+2Qw7UzUvlaf1gpUsGrA02
// KVRAJpHk64EKkSxkgU2KH8U1ISdalRs60QCF3GU9Ggy0FKNvMG0wCQYDVR0TBAIw
// ADALBgNVHQ8EBAMCB4AwEwYDVR0lBAwwCgYIKwYBBQUHAwIwHQYDVR0OBBYEFDs3
// WexIxJrXjnEcqR2ld4SvvzbSMB8GA1UdIwQYMBaAFA2RstNyZyIxmh32WOQ6Hv+G
// 3IapMAoGCCqGSM49BAMCA0cAMEQCIDghdyE+NhdMM0amvYHFQ/n9fB6ML3whVAy/
// f4k92wugAiA24vhv3J5+E8LCTBlvCkTc0Xog9RvsMeUfTQJJWCKEyQ==
// -----END CERTIFICATE-----
// -----BEGIN EC PRIVATE KEY-----
// MHcCAQEEIE7G9svkRpRad5n5PGqVWlBDBYkelK/F6O29CsHkBVv5oAoGCCqGSM49
// AwEHoUQDQgAEBS7DcoB4YMfvkUGyci+2Qw7UzUvlaf1gpUsGrA02KVRAJpHk64EK
// kSxkgU2KH8U1ISdalRs60QCF3GU9Ggy0FA==
// -----END EC PRIVATE KEY-----

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

    // Identify the verified client to the handler.
    on_http("/whoami", HTTP_GET, [](uint8_t id, HttpReq *) {
        char subject[PC_MTLS_SUBJECT_MAX];
        if (tls_client_subject(id, subject, sizeof(subject)) > 0)
        {
            send_text(id, 200, "text/plain", subject);
        }
        else
        {
            send_text(id, 200, "text/plain", "(no client certificate)");
        }
    });

    // Load the server identity, then require a CA-signed client certificate.
    if (!tls_cert((const uint8_t *)SERVER_CERT_PEM, sizeof(SERVER_CERT_PEM), (const uint8_t *)SERVER_KEY_PEM,
                         sizeof(SERVER_KEY_PEM)))
    {
        Serial.println("TLS cert/key load failed");
        return;
    }
    if (!tls_require_client_cert((const uint8_t *)CA_CERT_PEM, sizeof(CA_CERT_PEM)))
    {
        Serial.println("client CA load failed");
        return;
    }
    listen_tls(443);

    int32_t result = begin();
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
    }
    else
    {
        Serial.println("mTLS server on :443 (curl -k --cert client.crt --key client.key https://<ip>/whoami)");
    }
}

void loop()
{
    handle();
}
