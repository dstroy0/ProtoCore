// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the NMEA 2000 codec (services/timing_position/nmea2000): the marine
// instrumentation network, which is J1939 at the transport layer plus the NMEA-specific Fast
// Packet segmentation for 9..223-octet messages. Everything benched here is pure (no wire bus):
//   - protocore_n2k_fastpacket_num_frames  : frame-count arithmetic for a given payload length
//   - protocore_n2k_build_single           : build a single-frame (<=8 octet) message (thin J1939 wrap)
//   - protocore_n2k_fastpacket_build_frame : build one Fast Packet CAN frame (control octet + data)
//   - protocore_n2k_fastpacket_feed        : full Fast Packet reassembly of a multi-frame message
// This is a pure protocol codec (like services/modbus), so every call exercises the real
// production path. The physical CAN layer is deliberately out of scope: the ESP32 TWAI/MCP2515
// transceiver that would carry these CanFrames onto an NMEA 2000 backbone is never touched here
// (this rig has no transceiver attached), only the deterministic build/reassemble codec is timed.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/services/nmea2000 -p COM7 flash monitor
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/timing_position/nmea2000/nmea2000.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // A single-frame (<=8 octet) NMEA 2000 payload, a PDU2 PGN (from test_single_frame).
    static const uint8_t single_payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    // A 20-octet Fast Packet message spanning 3 frames (from test_fastpacket_roundtrip).
    static uint8_t fp_msg[20];
    for (int i = 0; i < 20; i++)
    {
        fp_msg[i] = (uint8_t)(0x40 + i);
    }
    const uint32_t fp_pgn = 0x01F801; // e.g. position rapid update, a Fast Packet PGN
    const uint8_t fp_sa = 0x15;
    const uint8_t fp_seq = 3;

    // Pre-build the 3 Fast Packet frames once so the feed/reassembly bench times only reassembly.
    const uint8_t fp_frames = protocore_n2k_fastpacket_num_frames(sizeof(fp_msg)); // 3
    static CanFrame fp_built[3];
    for (uint8_t i = 0; i < fp_frames; i++)
    {
        protocore_n2k_fastpacket_build_frame(&fp_built[i], fp_seq, i, 6, fp_pgn, fp_sa, 0xFF, fp_msg, sizeof(fp_msg));
    }

    for (;;)
    {
        DBENCH_BANNER("nmea2000");

        volatile uint32_t sink = 0;
        CanFrame out;
        N2kFastPacketRx rx;

        DBENCH_OP("protocore_n2k_fastpacket_num_frames", 200000, sink += protocore_n2k_fastpacket_num_frames(223));
        DBENCH_OP("protocore_n2k_build_single", 50000,
                  sink += protocore_n2k_build_single(&out, 2, 0x01F200, 0x15, 0xFF, single_payload, 8));
        DBENCH_OP("protocore_n2k_fastpacket_build_frame", 50000,
                  sink += protocore_n2k_fastpacket_build_frame(&out, fp_seq, 1, 6, fp_pgn, fp_sa, 0xFF, fp_msg,
                                                               sizeof(fp_msg)));
        // One op = reset + feed all 3 frames = a complete 20-octet message reassembly.
        DBENCH_OP(
            "protocore_n2k_fastpacket_feed x3 (reassy)", 20000, do {
                protocore_n2k_fastpacket_reset(&rx);
                for (uint8_t _f = 0; _f < fp_frames; _f++)
                {
                    sink += (uint32_t)protocore_n2k_fastpacket_feed(&rx, &fp_built[_f]);
                }
            } while (0));

        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("nmea2000")
