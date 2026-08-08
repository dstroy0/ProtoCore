// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Bluetooth ATT codec + GATT bridge (services/radio/ble_gatt):
// building the common ATT PDUs (read/write/notify/error), parsing a PDU back out, and serializing a
// GATT characteristic table as JSON for the web stack - all pure (no heap, no radio). Worked pattern
// mirrors performance_benching/device/modbus (a pure protocol codec, no hardware involved): the ESP32's BLE radio is
// on-chip and owned by NimBLE/Bluedroid, but nothing in services/radio/ble_gatt touches it - this bench
// exercises the real production ATT/GATT byte-shuffling code path directly, no stubbing needed.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/ble_gatt -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/radio/ble_gatt/ble_gatt.h"
#include <Arduino.h>

static void ble_gatt_bench_task(void *)
{
    // Sample data lifted straight from test/test_ble_gatt/test_ble_gatt.cpp (known-good).
    static const uint8_t wr_val[3] = {0xDE, 0xAD, 0xBE};
    static const uint8_t write_req_pdu[] = {AttOp::ATT_OP_WRITE_REQ, 0x31, 0x00, 0x01, 0x02};
    static const GattChar chars[2] = {
        {0x0025, 0x2A37, (uint8_t)(GattProp::GATT_PROP_READ | GattProp::GATT_PROP_NOTIFY)}, // Heart Rate Measurement
        {0x0031, 0x2A6E, GattProp::GATT_PROP_READ}};                                        // Temperature

    static uint8_t buf[32];
    static char json[160];

    for (;;)
    {
        Serial.printf("DB ==== ble_gatt device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        AttPdu p;

        DBENCH_OP("att_read_req", 100000, sink += att_read_req(0x0025, buf, sizeof(buf)));
        DBENCH_OP("att_write_req", 100000, sink += att_write_req(0x0031, wr_val, sizeof(wr_val), buf, sizeof(buf)));
        DBENCH_OP("att_notify", 100000, sink += att_notify(0x0031, wr_val, sizeof(wr_val), buf, sizeof(buf)));
        DBENCH_OP("att_error_rsp", 100000,
                  sink += att_error_rsp(AttOp::ATT_OP_READ_REQ, 0x0025, 0x0A, buf, sizeof(buf)));
        DBENCH_OP("att_parse", 100000, sink += (size_t)att_parse(write_req_pdu, sizeof(write_req_pdu), &p));
        DBENCH_OP("pc_gatt_char_json", 50000, sink += pc_gatt_char_json(chars, 2, json, sizeof(json)));

        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: ble_gatt device microbench");
    xTaskCreatePinnedToCore(ble_gatt_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
