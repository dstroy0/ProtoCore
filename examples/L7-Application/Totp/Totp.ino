// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Totp.ino
 * @brief TOTP two-factor auth (RFC 6238) (PROTOCORE_ENABLE_TOTP).
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
 *     build_flags = -DPROTOCORE_ENABLE_TOTP=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PROTOCORE_ENABLE_TOTP 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "services/security/totp/totp.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// The shared secret, base32 (share this with the authenticator app at enrolment).
static const char *SECRET_B32 = "JBSWY3DPEHPK3PXP";
static uint8_t g_secret[32];
static size_t g_secret_len = 0;


static uint64_t now_unix()
{
    // Self-contained demo clock; replace with real (NTP) time for app-matching codes.
    return 1700000000ull + millis() / 1000;
}

static uint8_t totp_work[16]; // Totp keeps no state in its borrow

// Key, 30 s step from the epoch, 6 digits, at the current time: what every call below shares.
static void totp_setup()
{
    TotpV.k = g_secret;
    TotpV.keylen = g_secret_len;
    TotpV.digit = 6;
    TotpV.step.t0 = 0;
    TotpV.step.x = 30;
    TotpV.step.unix_time = now_unix();
}

void setup()
{
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
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    TotpV.secret.b32 = SECRET_B32;
    TotpV.secret.out = g_secret;
    TotpV.secret.cap = sizeof(g_secret);
    Totp.base32_decode(totp_work);
    int n = TotpV.i32;
    g_secret_len = (n > 0) ? (size_t)n : 0;

    on_http("/totp", HTTP_GET, [](uint8_t id, HttpReq *) {
        totp_setup();
        Totp.totp(totp_work);
        uint32_t code = TotpV.u32;
        char b[16];
        snprintf(b, sizeof(b), "%06u", code); // zero-pad to 6 digits
        send_text(id, 200, "text/plain", b);
    });
    on_http("/totp/verify", HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *code_s = HttpParser.get_query(protocore_http_parser_span(), req, "code");
        uint32_t code = code_s ? (uint32_t)strtoul(code_s, nullptr, 10) : 0;
        totp_setup();
        TotpV.check.otp = code;
        TotpV.check.drift = 1; // accept +/-1 step
        Totp.verify(totp_work);
        bool ok = TotpV.ok;
        send_text(id, 200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });
    begin_http(80, NULL);
}

void loop()
{
    handle();
}
