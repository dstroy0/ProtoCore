// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the IEC 61850 GOOSE publisher codec (services/energy/goose): the
// BER-encoded IECGoosePdu builder (pc_goose_pdu - the `61 { 80 gocbRef .. AB allData }` structure
// with the minimal-length positive-INTEGER leading-zero rule) and the full Ethernet-frame wrap
// (pc_goose_frame - dst/src/0x88B8 + 8-octet GOOSE header + the PDU). Both are pure builders: zero
// heap, no sockets, no stdlib. GOOSE is publish-only here so there is no parser/subscriber to bench,
// and the actual raw-L2 transmit (esp_eth_transmit) is a device transport step that is deliberately
// OUT OF SCOPE on this rig - only the deterministic CPU-side codec is timed. A pure protocol codec
// with no hardware involved (like performance_benching/device/modbus), so every call exercises the real production
// path with no stubbing required. Sample data is a protection-trip control block copied verbatim
// from the known-good performance_benching/bench_goose.cpp / test/test_goose.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/goose -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/energy/goose/goose.h"
#include <Arduino.h>

static void goose_bench_task(void *)
{
    // A protection-trip GOOSE control block (two boolean dataset entries in allData: 83 01 00, 83 01 01).
    static const uint8_t utc[8] = {0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a};
    static const uint8_t all_data[] = {0x83, 0x01, 0x00, 0x83, 0x01, 0x01};
    pc_goose g = {};
    g.gocb_ref = "IED1LD0/LLN0$GO$gcb01";
    g.time_allowed_to_live = 2000;
    g.dat_set = "IED1LD0/LLN0$DataSet1";
    g.go_id = "IED1_GOOSE";
    g.t = utc;
    g.st_num = 1;
    g.sq_num = 0;
    g.simulation = false;
    g.conf_rev = 1;
    g.nds_com = false;
    g.num_entries = 2;
    g.all_data = all_data;
    g.all_data_len = sizeof(all_data);

    // IEC 61850 GOOSE multicast dst / an arbitrary IED src for the frame wrap.
    static const uint8_t dst[6] = {0x01, 0x0c, 0xcd, 0x01, 0x00, 0x01};
    static const uint8_t src[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

    static uint8_t pdu[256];
    static uint8_t frame[300];
    size_t pdu_len = pc_goose_pdu(&g, pdu, sizeof(pdu));
    size_t frame_len = pc_goose_frame(dst, src, 0x0001, &g, frame, sizeof(frame));

    for (;;)
    {
        Serial.printf("DB ==== goose device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        // pc_goose_pdu: BER-encode the IECGoosePdu (11 control fields + the allData blob) - the publish core.
        DBENCH_OP("pc_goose_pdu build", 50000, sink += pc_goose_pdu(&g, pdu, sizeof(pdu)));
        // Same, reported as throughput over the produced PDU bytes (encode MB/s).
        DBENCH_BULK("pc_goose_pdu bulk", 50000, pdu_len, sink += pc_goose_pdu(&g, pdu, sizeof(pdu)));
        // pc_goose_frame: wrap the PDU in the Ethernet + 8-octet GOOSE header (the full L2 datagram).
        DBENCH_OP("pc_goose_frame build", 50000, sink += pc_goose_frame(dst, src, 0x0001, &g, frame, sizeof(frame)));
        DBENCH_BULK("pc_goose_frame bulk", 50000, frame_len,
                    sink += pc_goose_frame(dst, src, 0x0001, &g, frame, sizeof(frame)));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: goose device microbench");
    xTaskCreatePinnedToCore(goose_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
