// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the raw L2 Ethernet codec (services/fieldbus/rawl2): the Ethernet II
// and 802.1Q VLAN frame builders, the frame parser, and the IEEE 802.3 FCS (CRC-32). All pure
// (no NIC / no DMA) - this is the per-frame CPU cost of framing a captured or forwarded L2 frame.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/rawl2 -t upload --upload-port COM7
#include "device_bench.h"
#include "services/fieldbus/rawl2/rawl2.h"
#include <Arduino.h>

static void rawl2_bench_task(void *)
{
    static const uint8_t DST[6] = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E};
    static const uint8_t SRC[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    static uint8_t payload[64];
    for (int i = 0; i < (int)sizeof(payload); i++)
    {
        payload[i] = (uint8_t)(i * 7 + 1);
    }
    static uint8_t frame[128], vframe[128];
    size_t flen = pc_eth_build(DST, SRC, 0x88B8, payload, sizeof(payload), frame, sizeof(frame));
    pc_eth_build_vlan(DST, SRC, 5, false, 100, 0x0800, payload, sizeof(payload), vframe, sizeof(vframe));

    for (;;)
    {
        Serial.printf("DB ==== rawl2 device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        static uint8_t out[128];
        DBENCH_OP("pc_eth_build (64B payload)", 200000,
                  sink += pc_eth_build(DST, SRC, 0x88B8, payload, sizeof(payload), out, sizeof(out)));
        DBENCH_OP("pc_eth_build_vlan (64B)", 200000,
                  sink +=
                  pc_eth_build_vlan(DST, SRC, 5, false, 100, 0x0800, payload, sizeof(payload), out, sizeof(out)));
        EthFrame ef;
        DBENCH_OP("pc_eth_parse", 200000, sink += pc_eth_parse(frame, flen, &ef));
        DBENCH_OP("pc_eth_parse (vlan)", 200000, sink += pc_eth_parse(vframe, flen + 4, &ef));
        DBENCH_BULK("pc_eth_fcs (CRC-32)", 50000, flen, sink += pc_eth_fcs(frame, flen));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: rawl2 device microbench");
    xTaskCreatePinnedToCore(rawl2_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
