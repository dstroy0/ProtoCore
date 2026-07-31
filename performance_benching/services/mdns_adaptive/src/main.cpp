// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the adaptive mDNS beacon scheduler (services/net/mdns_adaptive):
// the pure scheduling decisions that pick *when* to re-announce. Benched here -
//   - pc_mdns_refresh_interval()      : the TTL/2 continuous-refresher cadence (overflow-guarded math),
//   - pc_mdns_beacon_adapt()          : the RF-contention backoff/recovery step (double on crowded air,
//                                        halve back toward base when quiet, clamped to [base, ceiling]),
//   - pc_mdns_beacon_due()            : the wrap-safe announce-due check run every tick,
//   - pc_mdns_beacon_presleep_due()   : the auto-sleep beacon (announce now if the record would lapse
//                                        while the radio is off), and
//   - pc_mdns_contention_sample()     : the frame-counter -> per-window contention value (wrap-safe
//                                        delta, saturated to uint16), also run every tick.
// All of the above are pure, wrap-safe integer math with no heap and no stdlib, so every call exercises
// the real production code path. Deliberately OUT OF SCOPE: the device binding (pc_mdns_adaptive_begin/
// tick/end) - it drives promiscuous 802.11 capture and re-applies mDNS TXT records over the radio, needs
// PC_ENABLE_MDNS + PC_ENABLE_PROMISC, and this rig has no associated station to sniff, so it is never
// compiled or benched (contrast performance_benching/device/modbus, a pure codec fully in scope).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/mdns_adaptive -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a capture
// opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/net/mdns_adaptive/mdns_adaptive.h"
#include <Arduino.h>

static void mdns_adaptive_bench_task(void *)
{
    // Beacon armed with the same realistic values the host test uses: 120 s TTL -> 60 s base cadence,
    // an 8 min backoff ceiling, back off at >= 5 contenders/window.
    MdnsBeacon b;
    pc_mdns_beacon_init(&b, 60000, 480000, 5);
    // A contention-sampling window and its scratch out-param.
    MdnsContentionWindow w;
    uint16_t c = 0;

    for (;;)
    {
        Serial.printf("DB ==== mdns_adaptive device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink32 = 0;
        volatile uint32_t sinkb = 0;
        volatile uint16_t sink16 = 0;

        // TTL/2 refresher cadence: the *1000/2 math with its 64-bit overflow guard.
        DBENCH_OP("pc_mdns_refresh_interval", 200000, sink32 += pc_mdns_refresh_interval(120));
        // RF-contention backoff/recovery step (mutates cur_ms in place; settles at the ceiling clamp).
        DBENCH_OP("pc_mdns_beacon_adapt", 200000, sink32 += pc_mdns_beacon_adapt(&b, 8));
        // Wrap-safe announce-due check (evaluated every tick in production).
        DBENCH_OP("pc_mdns_beacon_due", 200000, sinkb += pc_mdns_beacon_due(&b, 1000, 1000 + 60000));
        // Auto-sleep beacon: would the record lapse mid-sleep? (64-bit sum, no overflow).
        DBENCH_OP("pc_mdns_beacon_presleep_due", 200000, sinkb += pc_mdns_beacon_presleep_due(&b, 0, 10000, 60000));
        // Frame-counter -> per-window contention (re-arm the window each call so the full wrap-safe
        // delta + uint16 saturation path is measured, not the every-tick "window not up yet" early-out).
        DBENCH_OP("pc_mdns_contention_sample", 200000,
                  sink16 += (pc_mdns_contention_init(&w, 1000, 0, 0), pc_mdns_contention_sample(&w, 500, 100000, &c)));

        (void)sink32;
        (void)sinkb;
        (void)sink16;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: mdns_adaptive device microbench");
    xTaskCreatePinnedToCore(mdns_adaptive_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
