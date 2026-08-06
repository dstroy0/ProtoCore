// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Wi-Fi promiscuous capture helpers (services/radio/promisc):
// wifi_frame_parse() decodes an 802.11 MAC header (type/subtype + the to/from-DS src/dst/bssid
// layout, QoS, sequence), and the shared libpcap framing (pc_pcap_record_header /
// pc_pcap_global_header) wraps a captured frame for a wired collector - all pure, no radio. The
// esp_wifi promiscuous bring-up (pc_promisc_begin/_end) is real-hardware and out of scope here;
// only the deterministic per-frame CPU path (the hot path the rx callback runs) is benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/promisc -t upload --upload-port COM7
#include "device_bench.h"
#include "services/radio/promisc/promisc.h"
#include <Arduino.h>

// Build a 3-address 802.11 MAC header (24 bytes): fc0, fc1, a1, a2, a3, seq. (from test/test_promisc)
static void hdr3(uint8_t *f, uint8_t fc0, uint8_t fc1, const uint8_t *a1, const uint8_t *a2, const uint8_t *a3,
                 uint16_t seq)
{
    memset(f, 0, 24);
    f[0] = fc0;
    f[1] = fc1;
    memcpy(f + 4, a1, 6);
    memcpy(f + 10, a2, 6);
    memcpy(f + 16, a3, 6);
    uint16_t sc = (uint16_t)(seq << 4);
    f[22] = (uint8_t)sc;
    f[23] = (uint8_t)(sc >> 8);
}

static void promisc_bench_task(void *)
{
    static const uint8_t AP[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    static const uint8_t CLI[6] = {0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb};
    static const uint8_t SRC[6] = {0xde, 0xad, 0xbe, 0xef, 0x00, 0x01};
    static const uint8_t BCAST[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    static uint8_t beacon[64], data_fromds[64], qos[64];
    hdr3(beacon, 0x80, 0x00, BCAST, AP, AP, 1);     // Mgmt/Beacon, no DS bits
    hdr3(data_fromds, 0x08, 0x02, CLI, AP, SRC, 7); // Data, from-DS
    hdr3(qos, 0x88, 0x01, AP, CLI, AP, 3);          // QoS Data, to-DS

    static uint8_t pcaprec[32], pcapglob[32];

    for (;;)
    {
        Serial.printf("DB ==== promisc device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;
        WifiFrameInfo fi;
        DBENCH_OP("wifi_frame_parse beacon(mgmt)", 200000, sink += wifi_frame_parse(beacon, sizeof(beacon), &fi));
        DBENCH_OP("wifi_frame_parse data(from-ds)", 200000,
                  sink += wifi_frame_parse(data_fromds, sizeof(data_fromds), &fi));
        DBENCH_OP("wifi_frame_parse qos data", 200000, sink += wifi_frame_parse(qos, sizeof(qos), &fi));
        DBENCH_OP("pc_pcap_record_header", 200000,
                  sink += pc_pcap_record_header(pcaprec, sizeof(pcaprec), 1720700000u, 123456u, 128u, 128u));
        DBENCH_OP("pc_pcap_global_header", 200000,
                  sink += pc_pcap_global_header(pcapglob, sizeof(pcapglob), PC_DLT_IEEE802_11));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: promisc device microbench");
    xTaskCreatePinnedToCore(promisc_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
