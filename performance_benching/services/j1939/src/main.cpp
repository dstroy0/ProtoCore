// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SAE J1939 codec (services/fieldbus/j1939): the pure CPU-side
// packing/parsing that a J1939-over-CAN bridge runs on every frame. Benched here:
//   - pc_j1939_encode_id / pc_j1939_decode_id : the 29-bit extended-id <-> {priority,PGN,SA,DA}
//     bit pack + unpack (PDU2 broadcast form) that every transmit/receive touches;
//   - pc_j1939_build_message : compose a single-frame (<=8 octet) message (id encode + 0xFF pad +
//     payload copy);
//   - pc_j1939_build_request : build a Request-PGN frame (the common "give me PGN X" query);
//   - pc_j1939_build_bam_cm : build the BAM Transport-Protocol announce for a multi-packet message;
//   - a full BAM reassembly (pc_j1939_tp_feed over the announce + every TP.DT packet), reported as
//     throughput over the reassembled message - the reassembler is the hottest receive path.
// All of the above are pure and deterministic. The physical CAN layer (the ESP32 TWAI peripheral or
// an MCP2515 over SPI) is deliberately out of scope: this codec never touches the wire - the app
// hands it a received CanFrame or transmits one a builder filled in - so there is nothing hardware
// to stub, exactly like the host test test/test_j1939/.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/j1939 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/j1939/j1939.h"
#include <Arduino.h>

// Pre-built frames for the Transport-Protocol reassembly bench: built once (below), then fed to the
// reassembler many times so the timed loop measures only decode + validate + memcpy, not frame build.
static CanFrame g_tp_cm;
static CanFrame g_tp_dt[3];
static uint8_t g_tp_packets;
static J1939TpRx g_tp_rx;

// Full BAM reassembly of a 16-octet message: reset the context, feed the announce, then feed every
// TP.DT data packet in order. Returns the final reassembler verdict (J1939_TP_COMPLETE on success).
static J1939TpResult j1939_tp_reassemble(void)
{
    pc_j1939_tp_reset(&g_tp_rx);
    pc_j1939_tp_feed(&g_tp_rx, &g_tp_cm);
    J1939TpResult r = J1939TpResult::J1939_TP_IGNORED;
    for (uint8_t s = 0; s < g_tp_packets; s++)
        r = pc_j1939_tp_feed(&g_tp_rx, &g_tp_dt[s]);
    return r;
}

static void j1939_bench_task(void *)
{
    // Realistic sample data lifted from test/test_j1939/: a PDU2 (broadcast, PF>=240) engine-ish PGN,
    // a source address, and an 8-octet payload.
    const uint8_t priority = 6;
    const uint32_t pgn = 0x00FEEE; // PDU2 broadcast PGN (PF = 0xFE)
    const uint8_t sa = 0x21;
    const uint8_t da = J1939_ADDR_GLOBAL;
    static const uint8_t payload8[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};

    // A pre-encoded id to feed the decode bench (the encode step is timed separately below).
    uint32_t enc_id = 0;
    pc_j1939_encode_id(&enc_id, priority, pgn, sa, da);

    // Build the TP reassembly fixtures once: a 16-octet BAM message split into 3 TP.DT packets.
    static uint8_t tp_msg[16];
    for (int i = 0; i < 16; i++)
        tp_msg[i] = (uint8_t)(0xA0 + i);
    const uint32_t tp_pgn = 0x00FECA; // DM1-style broadcast PGN
    pc_j1939_build_bam_cm(&g_tp_cm, sa, tp_pgn, 16);
    g_tp_packets = pc_j1939_tp_num_packets(16); // 3
    for (uint8_t seq = 1; seq <= g_tp_packets; seq++)
    {
        uint16_t off = (uint16_t)((seq - 1) * J1939_TP_DT_LEN);
        uint8_t len = (uint8_t)((16 - off) < J1939_TP_DT_LEN ? (16 - off) : J1939_TP_DT_LEN);
        pc_j1939_build_tp_dt(&g_tp_dt[seq - 1], sa, J1939_ADDR_GLOBAL, seq, tp_msg + off, len);
    }

    static CanFrame frame;
    J1939Id decoded;

    for (;;)
    {
        Serial.printf("DB ==== j1939 device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;

        DBENCH_OP("pc_j1939_encode_id (PDU2)", 200000, {
            uint32_t _id = 0;
            sink += pc_j1939_encode_id(&_id, priority, pgn, sa, da) ? _id : 0;
        });
        DBENCH_OP("pc_j1939_decode_id", 200000, sink += pc_j1939_decode_id(enc_id, &decoded) ? decoded.pgn : 0);
        DBENCH_OP("pc_j1939_build_message x8", 100000,
                  sink += pc_j1939_build_message(&frame, priority, pgn, sa, da, payload8, 8) ? frame.id : 0);
        DBENCH_OP("pc_j1939_build_request", 100000,
                  sink += pc_j1939_build_request(&frame, sa, 0x00, 0x00FEEC) ? frame.id : 0);
        DBENCH_OP("pc_j1939_build_bam_cm", 100000,
                  sink += pc_j1939_build_bam_cm(&frame, sa, tp_pgn, 16) ? frame.id : 0);
        DBENCH_BULK("j1939 TP reassemble 16B", 50000, 16, sink += (uint32_t)j1939_tp_reassemble());

        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: j1939 device microbench");
    xTaskCreatePinnedToCore(j1939_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
