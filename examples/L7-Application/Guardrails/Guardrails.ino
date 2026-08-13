// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Guardrails.ino
 * @brief Runtime heap/stack guardrails (PROTOCORE_ENABLE_GUARDRAILS).
 *
 * Installs a breach callback and checks the guardrails once a second: free heap,
 * heap low-water, largest free block (fragmentation), and this task's remaining
 * stack. When any crosses its PROTOCORE_GUARDRAIL_* floor the callback fires so the
 * app can shed load / drop to a safe state / reboot before exhaustion bites. The
 * live snapshot is also served as JSON at GET /health.
 *
 * NOTE: enable it for the whole build (a .ino #define does not reach the
 * separately compiled library). In platformio.ini:
 *     build_flags = -DPROTOCORE_ENABLE_GUARDRAILS=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PROTOCORE_ENABLE_GUARDRAILS 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/security/guardrails/guardrails.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


static void on_breach(uint8_t breaches, const protocore_health *h)
{
    Serial.printf("[guardrail] breach=0x%02x heap=%u frag=%u stack=%u\n", breaches, (unsigned)h->free_heap,
                  (unsigned)h->largest_free_block, (unsigned)h->stack_free);
    // Real app: shed load, drop to a safe state, or ESP.restart().
}

void setup()
{
    Serial.begin(115200);
    Physical.wifi->init(SSID, PASSWORD);
    while (!Physical.wifi->ready())
    {
        delay(250);
    }
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    protocore_guardrails_begin(on_breach);

    on_http("/health", HTTP_GET, [](uint8_t id, HttpReq *) {
        protocore_health h;
        protocore_guardrails_sample(&h);
        char buf[128];
        protocore_health_json(&h, buf, sizeof(buf));
        send_text(id, 200, "application/json", buf);
    });
    begin_http(80, NULL);
}

void loop()
{
    static uint32_t last = 0;
    if (millis() - last >= 1000)
    {
        last = millis();
        protocore_guardrails_check(); // fires on_breach() if any floor is crossed
    }
}
