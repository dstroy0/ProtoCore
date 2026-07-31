// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the TLS policy helpers (services/security/tls_policy): the version
// negotiation, the cipher-suite selection (server-pinned order over the client's offered list), and
// the AEAD classifier. Pure decision logic; no handshake.
//
// Build/flash:  pio run -d performance_benching/device/tls_policy -t upload --upload-port COM7
#include "device_bench.h"
#include "services/security/tls_policy/tls_policy.h"
#include <Arduino.h>

static void tls_policy_bench_task(void *)
{
    // TLS 1.3 + 1.2 AEAD suites (client offered, server pinned).
    static const uint16_t offered[] = {0x1301, 0x1302, 0xC02F, 0xC030, 0x009C};
    static const uint16_t pinned[] = {0x1302, 0x1301, 0xC030};

    for (;;)
    {
        Serial.printf("DB ==== tls_policy device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;
        DBENCH_OP("pc_tls_negotiate_version", 200000, sink += pc_tls_negotiate_version(0x0304, 0x0303, 0x0304));
        DBENCH_OP("pc_tls_select_cipher", 200000, sink += pc_tls_select_cipher(offered, 5, pinned, 3));
        DBENCH_OP("pc_tls_is_aead", 200000, sink += pc_tls_is_aead(0x1301));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: tls_policy device microbench");
    xTaskCreatePinnedToCore(tls_policy_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
