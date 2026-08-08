// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the NTCIP object layer (services/transportation/ntcip): pc_ntcip_oid() copies
// an NTCIP 1202/1203 object root (the arcs under 1.3.6.1.4.1.1206.4.2) and appends an instance/row
// index to produce a full object OID for the shipped SNMP agent. This is pure OID data - zero heap, no
// SNMP transport, no sockets - so every call here exercises the real production code path, like the
// performance_benching/device/modbus worked example (a pure protocol codec). The const OID arc arrays themselves are
// just static data, so there is nothing else to bench: only the OID builder does work at runtime, and
// there is no hardware or transport dependency to stub.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/ntcip -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/transportation/ntcip/ntcip.h"
#include <Arduino.h>

static void ntcip_bench_task(void *)
{
    static uint32_t out[24];

    for (;;)
    {
        Serial.printf("DB ==== ntcip device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;

        // Scalar object (.0 instance): 1202 maxPhases root (12 arcs) + instance index.
        DBENCH_OP("pc_ntcip_oid 1202 scalar", 200000,
                  sink +=
                  pc_ntcip_oid(NTCIP_1202_MAX_PHASES, NTCIP_1202_MAX_PHASES_LEN, 0, out, sizeof(out) / sizeof(out[0])));
        // Table column (row index): 1202 phaseMinimumGreen root (13 arcs) + row 4.
        DBENCH_OP("pc_ntcip_oid 1202 phase.4", 200000,
                  sink += pc_ntcip_oid(NTCIP_1202_PHASE_MIN_GREEN, NTCIP_1202_PHASE_MIN_GREEN_LEN, 4, out,
                                       sizeof(out) / sizeof(out[0])));
        // Longest root: 1203 dmsMessageMultiString table column (14 arcs) + row 1.
        DBENCH_OP("pc_ntcip_oid 1203 dms.1", 200000,
                  sink += pc_ntcip_oid(NTCIP_1203_DMS_MESSAGE_MULTI, NTCIP_1203_DMS_MESSAGE_MULTI_LEN, 1, out,
                                       sizeof(out) / sizeof(out[0])));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: ntcip device microbench");
    xTaskCreatePinnedToCore(ntcip_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
