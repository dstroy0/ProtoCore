// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the PN532 NFC frame codec (server/peripherals/pn532): the NXP PN532
// "normal information frame" (00 | 00 FF | LEN | LCS | TFI | PData | DCS | 00) build + parse, its
// two running checksums (LCS over LEN, DCS over TFI+PData), and the 6-byte ACK detect/build - all
// pure (protocore_pn532_build_frame / parse_frame / is_ack / build_ack carry no I2C/SPI/HSU of their own;
// the caller moves the bytes over the bus). Like performance_benching/device/modbus, this is a pure protocol codec
// with no hardware involved, so every call here exercises the real production code path - the actual
// PN532 reader on I2C/SPI/UART is out of scope everywhere on this peripheral-less rig. Sample frames
// are the documented GetFirmwareVersion command + response KATs copied verbatim from
// test/test_pn532/test_pn532.cpp (already known-good, spec-conformant).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/pn532 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/peripherals/pn532/pn532.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t pn532_work[16]; // the borrow an entry takes; Pn532 never reads it

void dbench_run(void)
{
    // Host -> PN532 GetFirmwareVersion (command 0x02); the documented frame is 00 00 FF 02 FE D4 02 2A 00.
    static const uint8_t cmd_gfv[1] = {0x02};
    // PN532 -> host GetFirmwareVersion response: 00 00 FF 06 FA D5 03 32 01 06 07 E8 00.
    static const uint8_t resp_gfv[13] = {0x00, 0x00, 0xFF, 0x06, 0xFA, 0xD5, 0x03, 0x32, 0x01, 0x06, 0x07, 0xE8, 0x00};
    // The 6-byte ACK frame (00 00 FF 00 FF 00).
    static const uint8_t ack[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
    static uint8_t out[32];

    for (;;)
    {
        DBENCH_BANNER("pn532");
        volatile size_t sink = 0;
        volatile int isink = 0;
        volatile bool bsink = false;

        // Build the GetFirmwareVersion command frame (LEN/LCS + TFI + DCS + postamble). The entry
        // call stays inside DBENCH_OP so the timed loop measures the framing, not the read.
        Pn532.build_frame_args.tfi = PN532_TFI_HOST;
        Pn532.build_frame_args.data = cmd_gfv;
        Pn532.build_frame_args.len = 1;
        Pn532.build_frame_args.out = out;
        Pn532.build_frame_args.cap = sizeof(out);
        DBENCH_OP("Pn532.build_frame gfv", 200000, (Pn532.build_frame(pn532_work), sink += Pn532.len));
        // Frame + verify the GetFirmwareVersion response (LCS + DCS checks over 5 PData bytes).
        DBENCH_OP("protocore_pn532_parse_frame gfv resp", 200000, {
            uint8_t tfi = 0;
            const uint8_t *pd = NULL;
            uint8_t pdlen = 0;
            Pn532.parse_frame_args.raw = resp_gfv;
            Pn532.parse_frame_args.len = sizeof(resp_gfv);
            Pn532.parse_frame_args.tfi = &tfi;
            Pn532.parse_frame_args.pdata = &pd;
            Pn532.parse_frame_args.pdata_len = &pdlen;
            Pn532.parse_frame(pn532_work);
            isink += Pn532.n;
        });
        // ACK detect (6-byte compare) and ACK build.
        Pn532.is_ack_args.raw = ack;
        Pn532.is_ack_args.len = sizeof(ack);
        DBENCH_OP("Pn532.is_ack", 200000, (Pn532.is_ack(pn532_work), bsink ^= Pn532.ok));
        Pn532.build_ack_args.out = out;
        Pn532.build_ack_args.cap = sizeof(out);
        DBENCH_OP("Pn532.build_ack", 200000, (Pn532.build_ack(pn532_work), sink += Pn532.len));

        (void)sink;
        (void)isink;
        (void)bsink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("pn532")
