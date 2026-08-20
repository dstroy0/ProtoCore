// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the CAN listen-only capture framing (server/signaling/bus_capture):
// can_to_socketcan() building the 16-byte Linux SocketCAN frame (big-endian can_id, EFF/RTR flags,
// length, data) for a standard data frame, an extended (29-bit) id frame, and an RTR frame, plus
// protocore_pcap_global_header() writing the libpcap global header with the DLT_CAN_SOCKETCAN link type
// (shared/pcap/pcap.h). Every call here is pure (no heap, no bus) - worked example for
// performance_benching/device/<service>/: a pure protocol codec with no hardware involved, so every call exercises
// the real production code path (contrast with performance_benching/device/ads1115, a peripheral driver where the
// bus transaction itself is stubbed). bus_capture_begin()/poll()/end() - the ESP32 TWAI listen-only
// bind - are deliberately out of scope: this rig has no CAN transceiver wired to it, and installing
// a real TWAI driver would be hardware I/O this bench must never perform.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/bus_capture -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/signaling/bus_capture/bus_capture.h"
#include "shared/pcap/pcap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t pcap_work[16]; // the borrow an entry takes; Pcap never reads it

void dbench_run(void)
{
    // Standard data frame, id 0x123, 8 data bytes (test_standard_data_frame in test_bus_capture.cpp).
    static CanFrame std8;
    memset(&std8, 0, sizeof(std8));
    std8.id = 0x123;
    std8.extended = false;
    std8.rtr = false;
    std8.dlc = 8;
    for (int i = 0; i < 8; i++)
    {
        std8.data[i] = (uint8_t)(0x10 + i);
    }

    // Extended (29-bit) J1939-style id, 2 data bytes (test_extended_id_sets_eff).
    static CanFrame ext2;
    memset(&ext2, 0, sizeof(ext2));
    ext2.id = 0x18FEF100;
    ext2.extended = true;
    ext2.dlc = 2;
    ext2.data[0] = 0xAA;
    ext2.data[1] = 0xBB;

    // RTR frame, no data (test_rtr_flag_and_no_data).
    static CanFrame rtr4;
    memset(&rtr4, 0, sizeof(rtr4));
    rtr4.id = 0x7FF;
    rtr4.rtr = true;
    rtr4.dlc = 4;
    rtr4.data[0] = 0xFF;

    static uint8_t out[PROTOCORE_SOCKETCAN_FRAME_LEN];
    static uint8_t pcap_hdr[PROTOCORE_PCAP_GLOBAL_HDR_LEN];

    for (;;)
    {
        DBENCH_BANNER("bus_capture");
        volatile size_t sink = 0;
        BusCaptureV.can_to_socketcan_args.f = &std8;
        BusCaptureV.can_to_socketcan_args.out = out;
        BusCaptureV.can_to_socketcan_args.cap = sizeof(out);
        DBENCH_BULK("can_to_socketcan std8", 100000, PROTOCORE_SOCKETCAN_FRAME_LEN,
                    (BusCapture.can_to_socketcan(protocore_bus_capture_span()), sink += BusCaptureV.n));
        DBENCH_OP("can_to_socketcan ext2", 100000, sink += can_to_socketcan(&ext2, out, sizeof(out)));
        DBENCH_OP("can_to_socketcan rtr4", 100000, sink += can_to_socketcan(&rtr4, out, sizeof(out)));
        PcapV.args.out = pcap_hdr;
        PcapV.args.cap = sizeof(pcap_hdr);
        PcapV.args.linktype = PROTOCORE_DLT_CAN_SOCKETCAN;
        DBENCH_OP("Pcap.global_header can", 100000, Pcap.global_header(pcap_work); sink += PcapV.n);
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("bus_capture")
