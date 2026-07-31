// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Redis RESP2/RESP3 codec (services/iot/redis_resp):
// pc_resp_encode_command() (the device builds an outbound command) and pc_resp_parse() (the
// device decodes a server reply - the untrusted-input hot op). Pure; no socket.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/redis_resp -t upload --upload-port COM7
#include "device_bench.h"
#include "services/iot/redis_resp/redis_resp.h"
#include <Arduino.h>

static void redis_resp_bench_task(void *)
{
    static const char *argv[] = {"SET", "pc:sensor:temp", "21.4"};
    // A RESP2 array reply of 3 bulk strings, as a client would receive.
    static const uint8_t reply[] = "*3\r\n$2\r\nOK\r\n$5\r\nhello\r\n$3\r\n123\r\n";

    for (;;)
    {
        Serial.printf("DB ==== redis_resp device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        static char cmd[64];
        DBENCH_OP("pc_resp_encode_command (3 args)", 200000,
                  sink += pc_resp_encode_command(cmd, sizeof(cmd), argv, nullptr, 3));
        const size_t rlen = sizeof(reply) - 1;
        DBENCH_OP("pc_resp_parse (walk array)", 200000, {
            size_t off = 0;
            size_t used = 0;
            RespReply r;
            while (off < rlen && pc_resp_parse(reply + off, rlen - off, &r, &used) && used)
            {
                off += used;
                sink += (size_t)r.type;
            }
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
    Serial.println("\nDB boot: redis_resp device microbench");
    xTaskCreatePinnedToCore(redis_resp_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
