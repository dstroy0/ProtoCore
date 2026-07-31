// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SMB/NTLM codec (services/file_transfer/smb): the NTLMv2 authentication
// crypto (NT hash, NTOWFv2, the NTLMv2 response) and the NTLMSSP / SMB2 message builders. Pure
// crypto + framing (MD4 / MD5 / HMAC-MD5); the TCP socket is out of scope.
//
// Build/flash:  pio run -d performance_benching/device/smb -t upload --upload-port COM7
#include "device_bench.h"
#include "services/file_transfer/smb/ntlm.h"
#include "services/file_transfer/smb/ntlmssp.h"
#include "services/file_transfer/smb/smb2.h"
#include <Arduino.h>

static void smb_bench_task(void *)
{
    static const uint8_t server_chal[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    static const uint8_t client_chal[8] = {0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10};
    static const uint8_t timestamp[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    static const uint8_t target_info[16] = {0x02, 0x00, 0x0C, 0x00, 'D', 0, 'O', 0, 'M', 0, 'A', 0, 'I', 0, 'N', 0};
    static const uint8_t file_id[16] = {0xAA, 0xBB, 0xCC, 0xDD, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    uint8_t nt_hash[16], owf[16];
    pc_ntlm_nt_hash("Passw0rd!", nt_hash);
    pc_ntlm_ntowfv2(nt_hash, "user", "DOMAIN", owf);

    for (;;)
    {
        Serial.printf("DB ==== smb device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        uint8_t h[16];
        DBENCH_OP("pc_ntlm_nt_hash (MD4)", 100000, {
            pc_ntlm_nt_hash("Passw0rd!", h);
            sink += h[0];
        });
        DBENCH_OP("pc_ntlm_ntowfv2 (HMAC-MD5)", 100000, {
            pc_ntlm_ntowfv2(nt_hash, "user", "DOMAIN", h);
            sink += h[0];
        });
        static uint8_t resp[256];
        uint8_t skey[16];
        DBENCH_OP("pc_ntlm_v2_response", 50000,
                  sink += pc_ntlm_v2_response(owf, server_chal, client_chal, timestamp, target_info,
                                              sizeof(target_info), resp, sizeof(resp), skey));
        static uint8_t buf[256];
        DBENCH_OP("pc_ntlmssp_build_negotiate", 200000,
                  sink += pc_ntlmssp_build_negotiate(buf, sizeof(buf), 0xE2088297u));
        DBENCH_OP("pc_smb2_build_close", 200000,
                  sink += pc_smb2_build_close(buf, sizeof(buf), 5, 0x1122334455667788ull, 0x99, file_id));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: smb device microbench");
    xTaskCreatePinnedToCore(smb_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
