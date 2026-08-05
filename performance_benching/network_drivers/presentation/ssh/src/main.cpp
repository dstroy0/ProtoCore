// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for representative SSH crypto primitives (network_drivers/
// crypto): SHA-256 and the ChaCha20 stream cipher (bulk). The full crypto suite is
// exercised in depth by penetration_testing/rig_firmware/main_cryptobench; this is the performance_benching/
// counterpart. Build/flash: pio run -d performance_benching/network_drivers/presentation/ssh -t upload
#include "crypto/cipher/chacha20.h"
#include "crypto/hash/sha256.h"
#include "device_bench.h"
#include <Arduino.h>

static void ssh_bench_task(void *)
{
    static uint8_t buf[1024];
    memset(buf, 0xA5, sizeof(buf));
    static const uint8_t key[PC_CHACHA20_KEY_LEN] = {0};
    static const uint8_t iv[8] = {0};
    for (;;)
    {
        Serial.printf("DB ==== ssh device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;
        uint8_t digest[PC_SHA256_DIGEST_LEN];
        DBENCH_BULK("pc_sha256 (1 KiB)", 2000, 1024, {
            pc_sha256(buf, 1024, digest);
            sink += digest[0];
        });
        DBENCH_BULK("pc_chacha20 (1 KiB)", 1000, 1024, {
            pc_chacha20_xor(key, iv, 1, buf, buf, 1024);
            sink += buf[0];
        });
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: ssh device microbench");
    xTaskCreatePinnedToCore(ssh_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}
void loop()
{
    delay(1000);
}
