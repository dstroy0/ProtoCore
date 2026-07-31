// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the multi-interface link-manager policy (services/system/link_manager):
// a caller-owned table of interfaces (kind + priority + up/down) with a deterministic "best link that
// is up" selection (pc_link_select), initial-egress compute (pc_link_init), and up/down state change
// with escalation/failover + change detection (pc_link_set). All three are pure integer table scans -
// no heap, no stdlib, no PHY bring-up, no netif reconfigure (those belong to the app and the stack;
// this only decides which interface should be active). So every call here exercises the real
// production code path, exactly like the modbus pure-codec worked example - nothing is stubbed.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/link_manager -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/system/link_manager/link_manager.h"
#include <Arduino.h>

static void link_manager_bench_task(void *)
{
    // Realistic 3-interface table straight out of test/test_link_manager: wired Eth (prio 20),
    // WiFi STA (prio 10), softAP (prio 5) - the priority order that drives escalate-to-Eth /
    // fail-over-to-WiFi.
    static LinkIface ifaces[3] = {
        {LinkKind::LINK_KIND_ETH, 20, true},
        {LinkKind::LINK_KIND_WIFI_STA, 10, true},
        {LinkKind::LINK_KIND_WIFI_AP, 5, true},
    };
    static LinkManager m;
    pc_link_init(&m, ifaces, 3);

    for (;;)
    {
        Serial.printf("DB ==== link_manager device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile int sink = 0;
        volatile bool bsink = false;
        int from = 0, to = 0;

        // Best-link-up selection: full priority scan over the 3-interface table (all up).
        DBENCH_OP("pc_link_select all-up", 200000, sink += pc_link_select(&m));

        // Initial-egress compute over caller storage: seeds active via one pc_link_select scan.
        DBENCH_OP("pc_link_init recompute", 200000, pc_link_init(&m, ifaces, 3));

        // State change + recompute + change detection (idx 0 held up -> escalation path, full rescan).
        DBENCH_OP("pc_link_set escalate", 100000, bsink = pc_link_set(&m, 0, true, &from, &to));

        // Fail-over path: drop the top-priority link, rescan picks the next best up.
        DBENCH_OP("pc_link_set failover", 100000, bsink = pc_link_set(&m, 0, false, &from, &to));

        (void)sink;
        (void)bsink;
        (void)from;
        (void)to;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: link_manager device microbench");
    xTaskCreatePinnedToCore(link_manager_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
