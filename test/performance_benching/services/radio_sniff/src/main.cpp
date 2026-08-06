// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the receive-only radio sniffer capture codec
// (services/radio/radio_sniff): turning frames pulled off the air into a wired-Wireshark .pcap. Three pure
// pieces are benched - pc_radiosniff_i2f32() (int dBm -> IEEE-754 float32 bit pattern for the TAP
// RSSI TLV, a hand-rolled highest-set-bit encode), pc_radiosniff_global() (the 24-byte pcap global
// header with DLT_IEEE802_15_4_TAP=283), and pc_radiosniff_tap_record() (one capture record: pcap
// record header + 20-byte 802.15.4 TAP pseudo-header carrying the RSSI/channel TLVs + the raw MAC
// frame copied through). All three are pure byte-framing (no heap, no stdlib) - the radio drivers
// (CC1101 / LoRa / the Thread 802.15.4 RCP) own the actual receive off the air, so no SPI/radio I/O
// happens here and none is stubbed; only the deterministic CPU-side framing is measured. Sample
// frames and RSSI/channel values are lifted straight from test/test_radio_sniff (known-good,
// spec-conformant), plus one larger realistic MAC frame to show record throughput (MB/s).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/radio_sniff -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/radio/radio_sniff/radio_sniff.h"
#include <Arduino.h>

static void radio_sniff_bench_task(void *)
{
    // A tiny fake 802.15.4 MAC frame, verbatim from test/test_radio_sniff (test_tap_record).
    static const uint8_t frame_small[5] = {0x41, 0x88, 0x00, 0xAB, 0xCD};
    // A larger, realistic 802.15.4 data frame (FCF + seq + PAN/addr + payload + FCS-ish tail) so the
    // record build measures a representative frame-copy, not just the fixed 36-byte header overhead.
    static const uint8_t frame_big[64] = {0x41, 0x88, 0x2A, 0xCD, 0xAB, 0xFF, 0xFF, 0x02, 0x00, 0x7B, 0x3D, 0x00, 0x00,
                                          0x00, 0x0C, 0x4B, 0x12, 0x00, 0x2A, 0x03, 0x00, 0x00, 0x00, 0x00, 0x11, 0x22,
                                          0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
                                          0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0, 0xC0,
                                          0xD0, 0xE0, 0xF0, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0x9C, 0x71};
    static uint8_t rec[256];

    for (;;)
    {
        Serial.printf("DB ==== radio_sniff device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink32 = 0;
        volatile size_t sink = 0;

        // int dBm -> float32 bit pattern (the RSSI TLV payload encode).
        DBENCH_OP("pc_radiosniff_i2f32", 200000, sink32 += pc_radiosniff_i2f32(-55));
        // 24-byte pcap global header (DLT 802.15.4 TAP).
        DBENCH_OP("pc_radiosniff_global", 200000, sink += pc_radiosniff_global(rec, sizeof(rec)));
        // Full capture record for a tiny 5-byte frame (record hdr + TAP + frame = 41 bytes).
        DBENCH_OP("pc_radiosniff_tap_record 5B", 100000,
                  sink += pc_radiosniff_tap_record(rec, sizeof(rec), frame_small, sizeof(frame_small), -55, 11, 0x1234,
                                                   0x5678));
        // Full capture record for a realistic 64-byte frame - report throughput over the frame bytes.
        DBENCH_BULK("pc_radiosniff_tap_record 64B", 100000, sizeof(frame_big),
                    sink +=
                    pc_radiosniff_tap_record(rec, sizeof(rec), frame_big, sizeof(frame_big), -72, 15, 0x1234, 0x5678));
        (void)sink32;
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: radio_sniff device microbench");
    xTaskCreatePinnedToCore(radio_sniff_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
