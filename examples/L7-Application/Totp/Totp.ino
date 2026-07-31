// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Totp.ino
 * @brief TOTP two-factor auth (RFC 6238) (PC_ENABLE_TOTP).
 *
 * Decodes a base32 shared secret (the kind Google Authenticator / Authy import),
 * computes the current 6-digit code, and verifies a submitted one within a +/-1
 * step window. Use it as a second factor on a protected route.
 *   GET /totp              -> the current code (demo only; never expose in prod)
 *   GET /totp/verify?code=NNNNNN -> {"ok":true|false}
 *
 * For codes that match a real authenticator app, sync the clock to real time
 * (NTP); this example uses a fixed RTC base so it is self-contained.
 *
 * NOTE: enable it for the whole build. In platformio.ini:
 *     build_flags = -DPC_ENABLE_TOTP=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_TOTP 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/security/totp/totp.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// The shared secret, base32 (share this with the authenticator app at enrolment).
static const char *SECRET_B32 = "JBSWY3DPEHPK3PXP";
static uint8_t g_secret[32];
static size_t g_secret_len = 0;

PC server;

static uint64_t now_unix()
{
    // Self-contained demo clock; replace with real (NTP) time for app-matching codes.
    return 1700000000ull + millis() / 1000;
}

void setup()
{
    Serial.begin(115200);
    init_wifi_physical(SSID, PASSWORD);
    while (!wifi_ready())
    {
        delay(250);
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    int n = pc_base32_decode(SECRET_B32, g_secret, sizeof(g_secret));
    g_secret_len = (n > 0) ? (size_t)n : 0;

    server.on("/totp", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) {
        uint32_t code = pc_totp(g_secret, g_secret_len, now_unix(), 30, 6);
        char b[16];
        snprintf(b, sizeof(b), "%06u", code); // zero-pad to 6 digits
        server.send(id, 200, "text/plain", b);
    });
    server.on("/totp/verify", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *code_s = http_get_query(req, "code");
        uint32_t code = code_s ? (uint32_t)strtoul(code_s, nullptr, 10) : 0;
        bool ok = pc_totp_verify(g_secret, g_secret_len, now_unix(), code, 30, 6, 1);
        server.send(id, 200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });
    server.begin(80);
}

void loop()
{
    server.handle();
}
