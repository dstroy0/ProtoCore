// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Modbus TCP slave codec (services/fieldbus/modbus):
// pc_modbus_process_adu() takes a complete ADU (MBAP header + PDU), dispatches the function code
// against the data model, and builds the response - pure (no sockets, no heap). Worked example for
// performance_benching/device/<service>/: a pure protocol codec with no hardware involved, so every call here
// exercises the real production code path (contrast with performance_benching/device/ads1115, a peripheral driver
// where the bus transaction itself is stubbed).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/modbus -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/modbus/modbus.h"
#include <Arduino.h>

static void modbus_bench_task(void *)
{
    pc_modbus_server_init();
    for (int i = 0; i < 16; i++)
        pc_modbus_set_holding_reg((uint16_t)i, (uint16_t)(0x1000 + i));

    // Read Holding Registers (FC 0x03), 8 regs from addr 0: MBAP(txn,proto,len,unit) + PDU(fc,addr,qty).
    static const uint8_t rd8[] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x00, 0x00, 0x08};
    // Write Multiple Registers (FC 0x10), 2 regs from addr 0.
    static const uint8_t wr2[] = {0x00, 0x02, 0x00, 0x00, 0x00, 0x0B, 0x01, 0x10, 0x00,
                                  0x00, 0x00, 0x02, 0x04, 0xAB, 0xCD, 0xEF, 0x01};
    static uint8_t resp[260];

    for (;;)
    {
        Serial.printf("DB ==== modbus device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        DBENCH_OP("pc_modbus_process_adu read x8 (FC3)", 20000,
                  sink += pc_modbus_process_adu(rd8, sizeof(rd8), resp, sizeof(resp)));
        DBENCH_OP("pc_modbus_process_adu write x2 (FC16)", 20000,
                  sink += pc_modbus_process_adu(wr2, sizeof(wr2), resp, sizeof(resp)));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: modbus device microbench");
    xTaskCreatePinnedToCore(modbus_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
