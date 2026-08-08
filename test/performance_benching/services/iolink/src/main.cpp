// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the IO-Link (SDCI, IEC 61131-9) data-link message codec
// (services/fieldbus/iolink): the M-sequence Control octet (MC) build/decode, the CKT / CKS check-octet
// builders, and the SDCI message checksum (seed 0x52, XOR every octet, then the 8->6-bit
// compression of IO-Link spec v1.1.4 Annex A.1.6) with its finalize-in-place and verify helpers.
// This is a pure protocol codec - like performance_benching/device/modbus, every call here exercises the real
// production code path. The physical wire (a UART at 4.8/38.4/230.4 kbit/s through an IO-Link
// transceiver such as a MAX14819 / L6360) is deliberately out of scope: no rig peripheral is
// touched, only the deterministic CPU-side octet math is benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/iolink -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/iolink/iolink.h"
#include <Arduino.h>

static void iolink_bench_task(void *)
{
    // A realistic master message: MC (write, Process channel, addr 0x01) + two process-data octets
    // + a type-2 CKT with its checksum field still zero (finalize fills it). check_idx = 3.
    // Layout and known-good octet values are taken straight from test/test_iolink/test_iolink.cpp.
    static uint8_t frame[4] = {0x00,  // MC, filled below
                               0xAB,  // process data
                               0xCD,  // process data
                               0x00}; // CKT (type 2, checksum 0), filled below
    frame[0] = pc_iol_mc(false, IOL_CH_PROCESS, 0x01);
    frame[3] = pc_iol_ckt(IOL_MSEQ_TYPE_2, 0);
    static const size_t check_idx = 3;

    // A finalized copy so pc_iol_verify runs against a valid checksum on every iteration.
    static uint8_t vframe[4];
    memcpy(vframe, frame, sizeof(vframe));
    pc_iol_finalize(vframe, sizeof(vframe), check_idx);

    for (;;)
    {
        Serial.printf("DB ==== iolink device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint8_t sink8 = 0;
        volatile bool sinkb = false;

        // Control-octet builders: cheap bit-packing, large N.
        DBENCH_OP("pc_iol_mc build", 200000, sink8 += pc_iol_mc(true, IOL_CH_PAGE, 0x10));
        DBENCH_OP("pc_iol_ckt build", 200000, sink8 += pc_iol_ckt(IOL_MSEQ_TYPE_1, 0x15));
        DBENCH_OP("pc_iol_cks build", 200000, sink8 += pc_iol_cks(true, false, 0x0A));

        // SDCI checksum over the 4-octet frame (seed 0x52 + 8->6 compression); bulk reports MB/s.
        DBENCH_BULK("pc_iol_checksum6 x4B", 100000, sizeof(frame), sink8 += pc_iol_checksum6(frame, sizeof(frame)));

        // Finalize-in-place (zero checksum field, recompute, OR in) - idempotent, so re-running
        // on the same buffer stays valid.
        DBENCH_OP("pc_iol_finalize x4B", 100000, sink8 += pc_iol_finalize(frame, sizeof(frame), check_idx));

        // Verify a known-good frame (recompute + compare the 6-bit field).
        DBENCH_OP("pc_iol_verify x4B", 100000, sinkb = pc_iol_verify(vframe, sizeof(vframe), check_idx));

        (void)sink8;
        (void)sinkb;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: iolink device microbench");
    xTaskCreatePinnedToCore(iolink_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
