// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
//   idf.py -C test/performance_benching/ble_gatt -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/radio/ble_gatt/ble_gatt.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t ble_gatt_work[16]; // the borrow an entry takes; BleGatt never reads it

void dbench_run(void)
{
    // Sample data lifted straight from test/test_ble_gatt/test_ble_gatt.cpp (known-good).
    static const uint8_t wr_val[3] = {0xDE, 0xAD, 0xBE};
    static const uint8_t write_req_pdu[] = {ATT_OP_WRITE_REQ, 0x31, 0x00, 0x01, 0x02};
    static const GattChar chars[2] = {
        {0x0025, 0x2A37, (uint8_t)(GATT_PROP_READ | GATT_PROP_NOTIFY)}, // Heart Rate Measurement
        {0x0031, 0x2A6E, GATT_PROP_READ}};                              // Temperature

    static uint8_t buf[32];
    static char json[160];

    for (;;)
    {
        DBENCH_BANNER("ble_gatt");
        volatile size_t sink = 0;
        AttPdu p;

        DBENCH_OP("att_read_req", 100000, sink += att_read_req(0x0025, buf, sizeof(buf)));
        DBENCH_OP("att_write_req", 100000, sink += att_write_req(0x0031, wr_val, sizeof(wr_val), buf, sizeof(buf)));
        DBENCH_OP("att_notify", 100000, sink += att_notify(0x0031, wr_val, sizeof(wr_val), buf, sizeof(buf)));
        DBENCH_OP("att_error_rsp", 100000, sink += att_error_rsp(ATT_OP_READ_REQ, 0x0025, 0x0A, buf, sizeof(buf)));
        DBENCH_OP("att_parse", 100000, sink += (size_t)att_parse(write_req_pdu, sizeof(write_req_pdu), &p));
        DBENCH_OP("protocore_gatt_char_json", 50000, sink += protocore_gatt_char_json(chars, 2, json, sizeof(json)));

        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("ble_gatt")
