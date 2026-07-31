// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the BACnet/IP BVLC + NPDU codec (services/fieldbus/bacnet):
// pc_bvlc_build()/pc_bvlc_parse() frame and slice the Annex J BVLL envelope (type + function +
// big-endian length), and pc_npdu_build()/pc_npdu_parse() frame and slice the Clause 6 NPDU
// header (version + NPCI control, with optional DNET/DADR/hop-count addressing) around an APDU -
// all pure (no sockets, no heap). Worked example for performance_benching/device/<service>/: a pure protocol codec
// with no hardware involved, so every call here exercises the real production code path (contrast
// with performance_benching/device/ads1115, a peripheral driver where the bus transaction itself is stubbed).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/bacnet -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/bacnet/bacnet.h"
#include <Arduino.h>

static void bacnet_bench_task(void *)
{
    // Sample bytes lifted straight from test/test_bacnet/test_bacnet.cpp (known-good, spec-conformant).
    static const uint8_t npdu_local[] = {0x01, 0x00, 0xAA}; // version, control(0), 1-byte NSDU tail
    static const uint8_t apdu_local[] = {0x41, 0x42, 0x43};
    static const uint8_t dadr_routed[] = {0x0A};
    static const uint8_t apdu_routed[] = {0x10};
    // A full dest+source NPDU frame (parser exercises both address slices + hop count skip).
    static const uint8_t npdu_full[] = {
        0x01, 0x28,             // version, control: dest + source present
        0x00, 0x05, 0x01, 0x0A, // DNET 5, DLEN 1, DADR 0A
        0x00, 0x03, 0x01, 0x0B, // SNET 3, SLEN 1, SADR 0B
        0xFF,                   // hop count
        0x30, 0x31              // apdu
    };

    static uint8_t bvlc_buf[32];
    static uint8_t npdu_buf[32];

    for (;;)
    {
        Serial.printf("DB ==== bacnet device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sinkz = 0;
        volatile bool sinkb = false;

        DBENCH_OP("pc_bvlc_build", 50000,
                  sinkz += pc_bvlc_build(bvlc_buf, sizeof(bvlc_buf), BVLC_FUNC_ORIGINAL_UNICAST, npdu_local,
                                         sizeof(npdu_local)));

        {
            uint8_t func;
            const uint8_t *p;
            size_t plen;
            DBENCH_OP("pc_bvlc_parse", 50000, sinkb = pc_bvlc_parse(bvlc_buf, sizeof(bvlc_buf), &func, &p, &plen));
        }

        DBENCH_OP("pc_npdu_build local", 50000,
                  sinkz += pc_npdu_build(npdu_buf, sizeof(npdu_buf), false, NPDU_PRIO_NORMAL, false, 0, nullptr, 0, 0,
                                         apdu_local, sizeof(apdu_local)));

        DBENCH_OP("pc_npdu_build routed", 50000,
                  sinkz += pc_npdu_build(npdu_buf, sizeof(npdu_buf), true, NPDU_PRIO_NORMAL, true, 0x0005, dadr_routed,
                                         sizeof(dadr_routed), 0xFF, apdu_routed, sizeof(apdu_routed)));

        {
            NpduInfo info;
            DBENCH_OP("pc_npdu_parse dest+src", 50000, sinkb = pc_npdu_parse(npdu_full, sizeof(npdu_full), &info));
        }

        (void)sinkz;
        (void)sinkb;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: bacnet device microbench");
    xTaskCreatePinnedToCore(bacnet_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
