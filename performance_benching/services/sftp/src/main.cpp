// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SFTP v3 wire codec (network_drivers/application/sftp): the zero-heap reader
// (u32 / string) and writer (u32 / string / finish) used to parse SSH_FXP requests and build
// responses. Pure; the SSH channel is elsewhere.
//
// Build/flash:  pio run -d performance_benching/device/sftp -t upload --upload-port COM7
#include "device_bench.h"
#include "network_drivers/application/sftp/sftp.h"
#include <Arduino.h>

static void sftp_bench_task(void *)
{
    // A representative PC_SSH_FXP_OPEN-ish payload: id(u32) + filename(string) + pflags(u32).
    static const uint8_t payload[] = {0x00, 0x00, 0x00, 0x2A, // id = 42
                                      0x00, 0x00, 0x00, 0x08, // string len = 8
                                      '/',  'l',  'o',  'g',  '.', 't', 'x', 't', 0x00, 0x00, 0x00, 0x01}; // pflags

    for (;;)
    {
        Serial.printf("DB ==== sftp device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        DBENCH_OP("pc_sftp reader (u32+string+u32)", 200000, {
            SftpReader r;
            pc_sftp_rd_init(&r, payload, sizeof(payload));
            uint32_t id = pc_sftp_rd_u32(&r);
            const uint8_t *nm;
            uint32_t nl;
            pc_sftp_rd_string(&r, &nm, &nl);
            sink += id + nl;
        });
        static uint8_t out[64];
        DBENCH_OP("pc_sftp writer (u32+string+finish)", 200000, {
            SftpWriter w;
            pc_sftp_wr_init(&w, out, sizeof(out));
            pc_sftp_wr_u32(&w, 42);
            pc_sftp_wr_string(&w, "/log.txt", 8);
            sink += pc_sftp_wr_finish(&w);
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
    Serial.println("\nDB boot: sftp device microbench");
    xTaskCreatePinnedToCore(sftp_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
