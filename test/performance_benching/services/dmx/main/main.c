// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the DMX512 + RDM (ANSI E1.20) codec (server/peripherals/dmx):
// assembling a DMX512 packet body ([start code][channel slots]) and reading a slot back out of
// it, plus building/parsing a full RDM management packet (48-bit UIDs, 16-bit additive checksum)
// - all pure (no UART, no RS-485 transceiver, no heap). Worked example for performance_benching/device/<service>/:
// a pure protocol codec with no hardware involved (contrast with performance_benching/device/ads1115, a peripheral
// driver where the bus transaction itself is stubbed) - the break + RS-485 direction toggling are
// the application's job and are out of scope here.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/dmx -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/peripherals/dmx/dmx.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t dmx_work[16]; // the borrow an entry takes; Dmx never reads it

void dbench_run(void)
{
    // A full 512-channel DMX512 universe (dimmer data, start code 0x00).
    static uint8_t channels[DMX_MAX_CHANNELS];
    for (uint16_t i = 0; i < DMX_MAX_CHANNELS; i++)
    {
        channels[i] = (uint8_t)i;
    }
    static uint8_t dmx_frame[1 + DMX_MAX_CHANNELS];

    // Build a GET DEVICE_INFO RDM packet (no parameter data) - see test_rdm_get_roundtrip().
    RdmPacket get_p;
    memset(&get_p, 0, sizeof(get_p));
    DmxV.rdm_uid_args.manufacturer = 0x4444;
    DmxV.rdm_uid_args.device = 0x00000001;
    Dmx.rdm_uid(dmx_work);
    get_p.dest_uid = DmxV.uid;
    DmxV.rdm_uid_args.manufacturer = 0x7A70;
    DmxV.rdm_uid_args.device = 0x000000AA;
    Dmx.rdm_uid(dmx_work);
    get_p.src_uid = DmxV.uid;
    get_p.tn = 5;
    get_p.port_id = 1;
    get_p.msg_count = 0;
    get_p.sub_device = 0;
    get_p.cc = RDM_CC_GET;
    get_p.pid = RDM_PID_DEVICE_INFO;
    get_p.pdl = 0;

    // Build a SET DMX_START_ADDRESS RDM packet with 2 octets of parameter data - see
    // test_rdm_set_with_data().
    RdmPacket set_p;
    memset(&set_p, 0, sizeof(set_p));
    DmxV.rdm_uid_args.manufacturer = 0x4444;
    DmxV.rdm_uid_args.device = 0x00000001;
    Dmx.rdm_uid(dmx_work);
    set_p.dest_uid = DmxV.uid;
    DmxV.rdm_uid_args.manufacturer = 0x7A70;
    DmxV.rdm_uid_args.device = 0x000000AA;
    Dmx.rdm_uid(dmx_work);
    set_p.src_uid = DmxV.uid;
    set_p.tn = 9;
    set_p.port_id = 1;
    set_p.cc = RDM_CC_SET;
    set_p.pid = RDM_PID_DMX_START_ADDRESS;
    static const uint8_t start_addr[2] = {0x00, 0x64}; // start address 100, big-endian

    static uint8_t rdm_get_buf[64];
    static uint8_t rdm_set_buf[64];
    DmxV.rdm_build_args.buf = rdm_get_buf;
    DmxV.rdm_build_args.cap = sizeof(rdm_get_buf);
    DmxV.rdm_build_args.p = &get_p;
    DmxV.rdm_build_args.pdata = NULL;
    DmxV.rdm_build_args.pdl = 0;
    Dmx.rdm_build(dmx_work);
    size_t rdm_get_len = DmxV.n;
    DmxV.rdm_build_args.buf = rdm_set_buf;
    DmxV.rdm_build_args.cap = sizeof(rdm_set_buf);
    DmxV.rdm_build_args.p = &set_p;
    DmxV.rdm_build_args.pdata = start_addr;
    DmxV.rdm_build_args.pdl = 2;
    Dmx.rdm_build(dmx_work);
    size_t rdm_set_len = DmxV.n;

    static uint8_t rdm_build_scratch[64];
    RdmPacket parsed;

    for (;;)
    {
        DBENCH_BANNER("dmx");
        volatile size_t sink = 0;
        volatile uint8_t sink8 = 0;
        volatile uint16_t sink16 = 0;
        volatile uint64_t sink64 = 0;
        volatile bool sinkb = false;

        DmxV.build_args.buf = dmx_frame;
        DmxV.build_args.cap = sizeof(dmx_frame);
        DmxV.build_args.start_code = DMX_SC_DIMMER;
        DmxV.build_args.channels = channels;
        DmxV.build_args.n = DMX_MAX_CHANNELS;
        // Every entry call stays inside its DBENCH_OP / DBENCH_BULK so the timed loop measures the
        // codec, not the read that follows it. Args that do not vary are staged once, above.
        DBENCH_BULK("Dmx.build (512ch)", 20000, sizeof(dmx_frame), (Dmx.build(dmx_work), sink += DmxV.n));

        DmxV.get_channel_args.buf = dmx_frame;
        DmxV.get_channel_args.len = sizeof(dmx_frame);
        DmxV.get_channel_args.ch = 256;
        DBENCH_OP("Dmx.get_channel", 100000, (Dmx.get_channel(dmx_work), sink8 += DmxV.u8));

        DmxV.rdm_uid_args.manufacturer = 0x4444;
        DmxV.rdm_uid_args.device = 0x00000001;
        DBENCH_OP("Dmx.rdm_uid", 200000, (Dmx.rdm_uid(dmx_work), sink64 += DmxV.uid));

        DmxV.rdm_checksum_args.buf = rdm_set_buf;
        DmxV.rdm_checksum_args.len = rdm_set_len;
        DBENCH_BULK("Dmx.rdm_checksum", 50000, rdm_set_len, (Dmx.rdm_checksum(dmx_work), sink16 += DmxV.checksum));

        DmxV.rdm_build_args.buf = rdm_build_scratch;
        DmxV.rdm_build_args.cap = sizeof(rdm_build_scratch);
        DmxV.rdm_build_args.p = &get_p;
        DmxV.rdm_build_args.pdata = NULL;
        DmxV.rdm_build_args.pdl = 0;
        DBENCH_OP("Dmx.rdm_build (GET, pdl 0)", 50000, (Dmx.rdm_build(dmx_work), sink += DmxV.n));
        DmxV.rdm_build_args.p = &set_p;
        DmxV.rdm_build_args.pdata = start_addr;
        DmxV.rdm_build_args.pdl = 2;
        DBENCH_OP("Dmx.rdm_build (SET, pdl 2)", 50000, (Dmx.rdm_build(dmx_work), sink += DmxV.n));

        DmxV.rdm_parse_args.buf = rdm_get_buf;
        DmxV.rdm_parse_args.len = rdm_get_len;
        DmxV.rdm_parse_args.out = &parsed;
        DmxV.rdm_parse_args.consumed = NULL;
        DBENCH_OP("Dmx.rdm_parse (GET, pdl 0)", 50000, (Dmx.rdm_parse(dmx_work), sinkb = DmxV.ok));
        DmxV.rdm_parse_args.buf = rdm_set_buf;
        DmxV.rdm_parse_args.len = rdm_set_len;
        DBENCH_OP("Dmx.rdm_parse (SET, pdl 2)", 50000, (Dmx.rdm_parse(dmx_work), sinkb = DmxV.ok));

        (void)sink;
        (void)sink8;
        (void)sink16;
        (void)sink64;
        (void)sinkb;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("dmx")
