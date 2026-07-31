// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// P4 HTTPS test-rig sketch: the wired-Ethernet twin of rig_s3_tls / main_tls.cpp, built for the
// ESP32-P4 (Waveshare P4-POE-ETH, no radio) with arduino-cli (see build_p4_tls.sh + this dir's README).
// Same HTTPS/443 + /bench/tls ECC decomposition, so the TLS handshake wall-clock + the ECDHE/ECDSA CCOUNT
// sweep are directly comparable to the S3 - the point being the P4's HW P-256 (mbedTLS ecc_alt) vs the S3's
// software curves. Findings: docs/FEATURE_PERFORMANCE.md "Device-CPU breakdown + the ESP32-P4 (HW ECC)".
#include "network_drivers/physical/physical.h"
#include "protocore.h"
#ifdef PC_TLS_HS_BENCH
#include "network_drivers/tls/tls.h" // pc_tls_hs_bench (handshake device-CPU vs wall probe)
#endif
#include <Arduino.h>
#include <esp_system.h> // esp_fill_random
#include <mbedtls/ecdh.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>

PC server;

// Throwaway ECDSA P-256 cert/key (CN=esp32-protocore, self-signed) - the SAME public test material as the
// S3 rig so the two boards present an identical handshake. NEVER a real credential.
static const char TLS_CERT_PEM[] = R"PEM(-----BEGIN CERTIFICATE-----
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
static const char TLS_KEY_PEM[] = R"PEM(-----BEGIN EC PRIVATE KEY-----
MHcCAQEEIECeRrZswSjVISz/EEkAK02Jf39SRWTPRBOcbqIhSolQoAoGCCqGSM49
AwEHoUQDQgAEHGSfVhJFHrMMzWyOZu/wrFPpz1RfblpT3pMVSSjJx7boWMWgbZvO
0XcPxYpeUESKivKVY2FqLlPx2meJe6i7aw==
-----END EC PRIVATE KEY-----
)PEM";

static void h_root(uint8_t id, HttpReq *)
{
    server.send(id, 200, "text/plain", "pc-p4-tls-rig");
}

static int bench_rng(void *c, unsigned char *out, size_t len)
{
    (void)c;
    esp_fill_random(out, len);
    return 0;
}

// One ECDHE cost on a curve = gen ephemeral public (fixed-base d*G) + compute shared (variable-base
// d*PeerPub). Returns us for each via the out-params; peer keypair is generated untimed.
static void bench_ecdhe(mbedtls_ecp_group_id id, uint32_t *gen_us, uint32_t *shared_us)
{
    mbedtls_ecp_group grp;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_group_load(&grp, id);
    mbedtls_mpi d, d_peer, z;
    mbedtls_ecp_point Q, Q_peer;
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&d_peer);
    mbedtls_mpi_init(&z);
    mbedtls_ecp_point_init(&Q);
    mbedtls_ecp_point_init(&Q_peer);
    mbedtls_ecdh_gen_public(&grp, &d_peer, &Q_peer, bench_rng, nullptr); // untimed "peer"
    uint32_t t0 = ESP.getCycleCount();
    mbedtls_ecdh_gen_public(&grp, &d, &Q, bench_rng, nullptr);
    *gen_us = (ESP.getCycleCount() - t0) / (ESP.getCpuFreqMHz());
    t0 = ESP.getCycleCount();
    mbedtls_ecdh_compute_shared(&grp, &z, &Q_peer, &d, bench_rng, nullptr);
    *shared_us = (ESP.getCycleCount() - t0) / (ESP.getCpuFreqMHz());
    mbedtls_ecp_point_free(&Q);
    mbedtls_ecp_point_free(&Q_peer);
    mbedtls_mpi_free(&d);
    mbedtls_mpi_free(&d_peer);
    mbedtls_mpi_free(&z);
    mbedtls_ecp_group_free(&grp);
}

// One ECDSA-P256 signature (the server's per-handshake signature over the key exchange). Returns us.
static uint32_t bench_ecdsa_sign_p256()
{
    mbedtls_ecp_group grp;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
    mbedtls_mpi d, r, s;
    mbedtls_ecp_point Q;
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    mbedtls_ecp_point_init(&Q);
    mbedtls_ecp_gen_keypair(&grp, &d, &Q, bench_rng, nullptr);
    uint8_t hash[32];
    esp_fill_random(hash, 32);
    uint32_t t0 = ESP.getCycleCount();
    mbedtls_ecdsa_sign(&grp, &r, &s, &d, hash, sizeof(hash), bench_rng, nullptr);
    uint32_t us = (ESP.getCycleCount() - t0) / (ESP.getCpuFreqMHz());
    mbedtls_ecp_point_free(&Q);
    mbedtls_mpi_free(&d);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    mbedtls_ecp_group_free(&grp);
    return us;
}

// GET /bench/tls - decompose the handshake ECC cost (us/op) so we can see the dominant scalar mult.
static void h_bench_tls(uint8_t id, HttpReq *)
{
    uint32_t x_gen = 0, x_sh = 0, p_gen = 0, p_sh = 0, s5_gen = 0, s5_sh = 0;
    bench_ecdhe(MBEDTLS_ECP_DP_CURVE25519, &x_gen, &x_sh);
    bench_ecdhe(MBEDTLS_ECP_DP_SECP256R1, &p_gen, &p_sh);
    bench_ecdhe(MBEDTLS_ECP_DP_SECP521R1, &s5_gen, &s5_sh);
    uint32_t sign_us = bench_ecdsa_sign_p256();
    char b[320];
    snprintf(b, sizeof(b),
             "{\"cpu_mhz\":%u,\"ecdsa_sign_p256_us\":%lu,"
             "\"ecdhe_x25519\":{\"gen_us\":%lu,\"shared_us\":%lu},"
             "\"ecdhe_p256\":{\"gen_us\":%lu,\"shared_us\":%lu},"
             "\"ecdhe_p521\":{\"gen_us\":%lu,\"shared_us\":%lu}}",
             (unsigned)ESP.getCpuFreqMHz(), (unsigned long)sign_us, (unsigned long)x_gen, (unsigned long)x_sh,
             (unsigned long)p_gen, (unsigned long)p_sh, (unsigned long)s5_gen, (unsigned long)s5_sh);
    server.send(id, 200, "application/json", b);
}

static void h_health(uint8_t id, HttpReq *)
{
    char b[64];
    snprintf(b, sizeof(b), "{\"heap\":%u}", (unsigned)ESP.getFreeHeap());
    server.send(id, 200, "application/json", b);
}

static void h_status(uint8_t id, HttpReq *)
{
    char b[96];
    snprintf(b, sizeof(b), "{\"tls\":true,\"uptime_ms\":%lu,\"heap\":%u}", (unsigned long)millis(),
             (unsigned)ESP.getFreeHeap());
    server.send(id, 200, "application/json", b);
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    // The P4-POE-ETH has no radio - bring up the wired PHY instead of WiFi.
    init_eth_physical();
    uint32_t t0 = millis();
    while (!eth_ready() && millis() - t0 < 30000)
        delay(200);
    uint32_t ip = pc_net_egress_ip(); // network byte order
    Serial.printf("RIG_IP=%u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    server.set_cors("*");
    server.on("/", HttpMethod::HTTP_GET, h_root);
    server.on("/health", HttpMethod::HTTP_GET, h_health);
    server.on("/status", HttpMethod::HTTP_GET, h_status);
    server.on("/diag", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.diag(id); });
    server.on("/bench/tls", HttpMethod::HTTP_GET, h_bench_tls);

    server.tls_cert((const uint8_t *)TLS_CERT_PEM, sizeof(TLS_CERT_PEM), (const uint8_t *)TLS_KEY_PEM,
                    sizeof(TLS_KEY_PEM));
    int32_t trc = server.listen_tls(443);
    Serial.printf("TLS=%s\n", trc >= 0 ? "tcp/443" : "listen-failed");
    int32_t rc = server.begin(80);
    Serial.printf("BEGIN=%ld\n", (long)rc);
}

void loop()
{
    server.handle();
#ifdef PC_TLS_HS_BENCH
    static unsigned seen_hs = 0;
    if (pc_tls_hs_bench.count != seen_hs)
    {
        seen_hs = pc_tls_hs_bench.count;
        Serial.printf("TLSHSBENCH #%u  cpu_us=%lld  wall_us=%lld  (cpu=device-compute, wall=incl-network)\n", seen_hs,
                      pc_tls_hs_bench.last_cpu_us, pc_tls_hs_bench.last_wall_us);
    }
#endif
}
