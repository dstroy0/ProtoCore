// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the OCIT-Outstations message codec (services/transportation/ocit): building an
// OCIT object message ([msg-type][object-type:2][instance:2][data-type][value...]), the SET-uint16
// convenience builder, parsing a message back into an OcitMsg, and the typed uint16 value accessor -
// all pure (no sockets, no heap, no OCIT transport). A pure protocol codec with no hardware involved,
// so every call here exercises the real production code path; the OCIT-O BTPPL/TCP transport is
// deliberately out of scope (this rig has no network peer attached).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/ocit -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/transportation/ocit/ocit.h"
#include <Arduino.h>

static void ocit_bench_task(void *)
{
    // SET of a uint32 object value, straight out of test/test_ocit (known-good, spec-conformant):
    //   pc_ocit_build(SET, object_type=0x0102, instance=0x0003, UINT32, value, 4)
    static const uint8_t val32[4] = {0x00, 0x00, 0x12, 0x34};
    // The 10-byte wire message that build produces, reused as the parse input:
    //   [02][01 02][00 03][04][00 00 12 34]
    static const uint8_t wire[] = {0x02, 0x01, 0x02, 0x00, 0x03, 0x04, 0x00, 0x00, 0x12, 0x34};
    static uint8_t out[32];

    for (;;)
    {
        Serial.printf("DB ==== ocit device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sinkn = 0;
        volatile uint16_t sink16 = 0;
        volatile bool sinkb = false;

        DBENCH_OP("pc_ocit_build SET u32", 200000,
                  sinkn += pc_ocit_build(OCIT_MSG_SET, 0x0102, 0x0003, OCIT_TYPE_UINT32, val32, sizeof(val32), out,
                                         sizeof(out)));
        DBENCH_OP("pc_ocit_set_u16", 200000, sinkn += pc_ocit_set_u16(0x00A0, 0x0005, 0xBEEF, out, sizeof(out)));

        OcitMsg m;
        DBENCH_OP("pc_ocit_parse", 200000, sinkb ^= pc_ocit_parse(wire, sizeof(wire), &m));
        // Parse once so the accessor bench reads a valid message.
        pc_ocit_parse(wire, sizeof(wire), &m);
        // Re-tag the parsed message as UINT16 so the accessor returns the value (wire is UINT32).
        m.data_type = OCIT_TYPE_UINT16;
        DBENCH_OP("pc_ocit_value_u16", 200000, sink16 += pc_ocit_value_u16(&m));

        (void)sinkn;
        (void)sink16;
        (void)sinkb;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: ocit device microbench");
    xTaskCreatePinnedToCore(ocit_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
