// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the ICCP / TASE.2 (IEC 60870-6) Data_Value codec
// (services/energy/iccp): pc_iccp_state_q() builds the StateQ indication-point BER structure (a 2-bit
// discrete state + quality-flags byte, optional 4-octet timestamp) and pc_iccp_real_q() builds the
// RealQ structure (a scaled signed INTEGER in milli-units + quality, optional timestamp). Both are
// pure, zero-heap, no-stdlib BER blob builders - the byte-for-byte production code path, benched
// with the exact spec-conformant inputs from test/test_iccp. This is a pure protocol codec: there
// is no hardware, socket, or MMS transport involved, so the TASE.2 bilateral table and the MMS Read
// wrapper (pc_mms_read_response) are deliberately out of scope here - only the deterministic
// CPU-side data-value encoder is timed.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/iccp -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/energy/iccp/iccp.h"
#include <Arduino.h>

static void iccp_bench_task(void *)
{
    // 4-octet TASE.2 TimeStamp (big-endian seconds since 1970), matching test_state_q_with_time.
    static const uint8_t time4[4] = {0x66, 0x00, 0x00, 0x00};
    static uint8_t out[32];

    for (;;)
    {
        Serial.printf("DB ==== iccp device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;

        // StateQ without timestamp: A2 { 85 01 <state<<6 | quality> } - the minimal indication point.
        DBENCH_OP("pc_iccp_state_q no-time", 200000,
                  sink += pc_iccp_state_q(Iccp::ICCP_STATE_ON, Iccp::ICCP_QUAL_VALID, nullptr, out, sizeof(out)));
        // StateQ with the optional 4-octet TimeStamp TLV appended.
        DBENCH_OP("pc_iccp_state_q +time", 200000,
                  sink += pc_iccp_state_q(Iccp::ICCP_STATE_OFF, Iccp::ICCP_QUAL_SUSPECT, time4, out, sizeof(out)));
        // RealQ, small positive value: exercises the minimal-length signed-INTEGER content path.
        DBENCH_OP("pc_iccp_real_q +12345", 200000,
                  sink += pc_iccp_real_q(12345, Iccp::ICCP_QUAL_VALID, nullptr, out, sizeof(out)));
        // RealQ, negative value + timestamp: two's-complement trimming plus the time TLV (worst case).
        DBENCH_OP("pc_iccp_real_q -256 +time", 200000,
                  sink += pc_iccp_real_q(-256, Iccp::ICCP_QUAL_SUSPECT, time4, out, sizeof(out)));

        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: iccp device microbench");
    xTaskCreatePinnedToCore(iccp_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
