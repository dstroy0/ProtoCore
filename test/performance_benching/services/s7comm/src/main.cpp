// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Siemens S7comm codec (services/fieldbus/s7comm): pc_s7_build_setup
// (negotiate PDU size, once per connection), pc_s7_build_read_request (frame an N-item Read Var job -
// the PLC-poll transmit op) and pc_s7_parse_header (validate protocol id / ROSCTR / lengths and slice
// param+data - the receive op). Pure; the ISO-on-TCP socket is out of scope.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/s7comm -t upload --upload-port COM7
#include "device_bench.h"
#include "services/fieldbus/s7comm/s7comm.h"
#include <Arduino.h>

static void s7comm_bench_task(void *)
{
    // A 3-item Read Var job (two DB reads + a flag bit) - a realistic PLC poll (from test/host bench).
    static const S7ReadItem items[3] = {
        {S7_AREA_DB, 1, 0, S7_TS_BYTE, 16},
        {S7_AREA_DB, 2, 4, S7_TS_WORD, 8},
        {S7_AREA_FLAGS, 0, 0, S7_TS_BIT, 1},
    };
    static uint8_t req[256];
    size_t req_len = pc_s7_build_read_request(req, sizeof(req), 0x0002, items, 3);

    for (;;)
    {
        Serial.printf("DB ==== s7comm device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        static uint8_t buf[256];
        DBENCH_OP("pc_s7_build_setup", 200000, sink += pc_s7_build_setup(buf, sizeof(buf), 0x0001, 1, 1, 480));
        DBENCH_OP("pc_s7_build_read_request x3", 200000,
                  sink += pc_s7_build_read_request(buf, sizeof(buf), 0x0002, items, 3));
        S7Header h;
        DBENCH_OP("pc_s7_parse_header", 200000, sink += pc_s7_parse_header(req, req_len, &h));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: s7comm device microbench");
    xTaskCreatePinnedToCore(s7comm_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
