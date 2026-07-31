// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the PROFIBUS-DP FDL telegram codec (services/fieldbus/profibus):
// building/validating the SD1 (no-data) and SD2 (variable-data) telegrams a DP master exchanges with
// its slaves, plus the underlying arithmetic-sum FCS. All four benched calls (pc_pb_fcs,
// pc_pb_build_sd1, pc_pb_build_sd2, pc_pb_parse) are pure - zero heap, no stdlib, no I/O - so each
// exercises the real production code path (like performance_benching/device/modbus, a pure codec; contrast with
// performance_benching/device/ads1115, a peripheral driver where the bus transaction is stubbed). The RS-485 UART
// timing + the DP-V0 cyclic state machine are the physical "device step" and are deliberately out of
// scope here: this rig drives no PROFIBUS transceiver, only the deterministic CPU-side telegram math.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/profibus -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/profibus/profibus.h"
#include <Arduino.h>

static void profibus_bench_task(void *)
{
    // SD2 process-data payload (spec-conformant vector shape from test/test_profibus).
    static const uint8_t data3[3] = {0xAA, 0xBB, 0xCC};
    // FCS body: DA + SA + FC (matches test_fcs: 0x03 + 0x02 + 0x49 -> 0x4E).
    static const uint8_t fcs_body[3] = {0x03, 0x02, Profibus::PB_FC_REQUEST_FDL_STATUS};
    static uint8_t out[16];

    // Prebuilt telegrams for the parse bench (known-good, so pc_pb_parse takes the accept path).
    static uint8_t sd1_frame[8];
    static uint8_t sd2_frame[16];
    size_t sd1_len = pc_pb_build_sd1(0x03, 0x02, Profibus::PB_FC_REQUEST_FDL_STATUS, sd1_frame, sizeof(sd1_frame));
    size_t sd2_len =
        pc_pb_build_sd2(0x05, 0x02, Profibus::PB_FC_SRD_HIGH, data3, sizeof(data3), sd2_frame, sizeof(sd2_frame));

    for (;;)
    {
        Serial.printf("DB ==== profibus device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        volatile uint8_t sink8 = 0;
        PbTelegram tg;

        DBENCH_OP("pc_pb_fcs (DA+SA+FC)", 100000, sink8 += pc_pb_fcs(fcs_body, sizeof(fcs_body)));
        DBENCH_OP("pc_pb_build_sd1", 100000,
                  sink += pc_pb_build_sd1(0x03, 0x02, Profibus::PB_FC_REQUEST_FDL_STATUS, out, sizeof(out)));
        DBENCH_OP("pc_pb_build_sd2 (3B data)", 50000,
                  sink +=
                  pc_pb_build_sd2(0x05, 0x02, Profibus::PB_FC_SRD_HIGH, data3, sizeof(data3), out, sizeof(out)));
        DBENCH_OP("pc_pb_parse SD1", 100000, sink += pc_pb_parse(sd1_frame, sd1_len, &tg) ? 1 : 0);
        DBENCH_OP("pc_pb_parse SD2 (3B data)", 100000, sink += pc_pb_parse(sd2_frame, sd2_len, &tg) ? 1 : 0);

        (void)sink;
        (void)sink8;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: profibus device microbench");
    xTaskCreatePinnedToCore(profibus_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
