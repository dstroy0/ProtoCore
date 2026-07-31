// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the CIP message codec (services/fieldbus/cip): the EPATH
// logical-segment builder (8-bit and 16-bit segment forms), the Get_Attribute_Single /
// Set_Attribute_Single request builders, and the response parser (service / status /
// additional-status / data) - pure, no heap, no sockets. This is the CIP message that rides
// inside an EtherNet/IP Unconnected Data item (services/fieldbus/enip); the ENIP/socket transport is
// out of scope here, exactly as services/fieldbus/modbus's TCP transport is out of scope for
// performance_benching/device/modbus, the worked example for a pure protocol codec with no hardware involved.
// Sample bytes are taken straight from test/test_cip/test_cip.cpp (already known-good,
// Wireshark-verified).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/cip -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/cip/cip.h"
#include <Arduino.h>

static void cip_bench_task(void *)
{
    // class 1 / instance 1 / attribute 7 -> all 8-bit logical segments.
    static uint8_t epath8[16];
    // class 0x0100 (>0xFF) / instance 1, no attribute -> 16-bit class segment + 8-bit instance segment.
    static uint8_t epath16[16];
    static uint8_t req[16];
    const uint8_t set_epath[] = {0x20, 0x01, 0x24, 0x01, 0x30, 0x07};
    const uint8_t set_data[] = {0xAB, 0xCD};

    // Get_Attribute_Single reply: service|0x80, reserved, status OK, no addl status, 4B data ("Acme").
    static const uint8_t resp_ok[] = {0x8E, 0x00, 0x00, 0x00, 'A', 'c', 'm', 'e'};
    // Reply with 1 word of additional status ahead of the data.
    static const uint8_t resp_addl[] = {0x8E, 0x00, 0x1F, 0x01, 0xAA, 0xBB, 0x12, 0x34};

    for (;;)
    {
        Serial.printf("DB ==== cip device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        CipResponse r;

        DBENCH_OP("pc_cip_build_epath 8bit", 100000,
                  sink += pc_cip_build_epath(epath8, sizeof(epath8), 0x01, 0x01, 0x07, true));
        DBENCH_OP("pc_cip_build_epath 16bit", 100000,
                  sink += pc_cip_build_epath(epath16, sizeof(epath16), 0x0100, 0x01, 0, false));
        DBENCH_OP("pc_cip_build_get_attr_single", 100000,
                  sink += pc_cip_build_get_attr_single(req, sizeof(req), 0x01, 0x01, 0x07));
        DBENCH_OP("pc_cip_build_request (SET)", 100000,
                  sink += pc_cip_build_request(req, sizeof(req), CIP_SC_SET_ATTR_SINGLE, set_epath, sizeof(set_epath),
                                               set_data, sizeof(set_data)));
        DBENCH_OP("pc_cip_parse_response ok", 100000,
                  sink += pc_cip_parse_response(resp_ok, sizeof(resp_ok), &r) ? 1 : 0);
        DBENCH_OP("pc_cip_parse_response addl", 100000,
                  sink += pc_cip_parse_response(resp_addl, sizeof(resp_addl), &r) ? 1 : 0);
        (void)sink;

        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: cip device microbench");
    xTaskCreatePinnedToCore(cip_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
