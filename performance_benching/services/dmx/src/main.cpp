// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the DMX512 + RDM (ANSI E1.20) codec (services/peripherals/dmx):
// assembling a DMX512 packet body ([start code][channel slots]) and reading a slot back out of
// it, plus building/parsing a full RDM management packet (48-bit UIDs, 16-bit additive checksum)
// - all pure (no UART, no RS-485 transceiver, no heap). Worked example for performance_benching/device/<service>/:
// a pure protocol codec with no hardware involved (contrast with performance_benching/device/ads1115, a peripheral
// driver where the bus transaction itself is stubbed) - the break + RS-485 direction toggling are
// the application's job and are out of scope here.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/dmx -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/peripherals/dmx/dmx.h"
#include <Arduino.h>

static void dmx_bench_task(void *)
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
    get_p.dest_uid = pc_rdm_uid(0x4444, 0x00000001);
    get_p.src_uid = pc_rdm_uid(0x7A70, 0x000000AA);
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
    set_p.dest_uid = pc_rdm_uid(0x4444, 0x00000001);
    set_p.src_uid = pc_rdm_uid(0x7A70, 0x000000AA);
    set_p.tn = 9;
    set_p.port_id = 1;
    set_p.cc = RDM_CC_SET;
    set_p.pid = RDM_PID_DMX_START_ADDRESS;
    static const uint8_t start_addr[2] = {0x00, 0x64}; // start address 100, big-endian

    static uint8_t rdm_get_buf[64];
    static uint8_t rdm_set_buf[64];
    size_t rdm_get_len = pc_rdm_build(rdm_get_buf, sizeof(rdm_get_buf), &get_p, nullptr, 0);
    size_t rdm_set_len = pc_rdm_build(rdm_set_buf, sizeof(rdm_set_buf), &set_p, start_addr, 2);

    static uint8_t rdm_build_scratch[64];
    RdmPacket parsed;

    for (;;)
    {
        Serial.printf("DB ==== dmx device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        volatile uint8_t sink8 = 0;
        volatile uint16_t sink16 = 0;
        volatile uint64_t sink64 = 0;
        volatile bool sinkb = false;

        DBENCH_BULK("pc_dmx_build (512ch)", 20000, sizeof(dmx_frame),
                    sink += pc_dmx_build(dmx_frame, sizeof(dmx_frame), DMX_SC_DIMMER, channels, DMX_MAX_CHANNELS));
        DBENCH_OP("pc_dmx_get_channel", 100000, sink8 += pc_dmx_get_channel(dmx_frame, sizeof(dmx_frame), 256));
        DBENCH_OP("pc_rdm_uid", 200000, sink64 += pc_rdm_uid(0x4444, 0x00000001));
        DBENCH_BULK("pc_rdm_checksum", 50000, rdm_set_len, sink16 += pc_rdm_checksum(rdm_set_buf, rdm_set_len));
        DBENCH_OP("pc_rdm_build (GET, pdl 0)", 50000,
                  sink += pc_rdm_build(rdm_build_scratch, sizeof(rdm_build_scratch), &get_p, nullptr, 0));
        DBENCH_OP("pc_rdm_build (SET, pdl 2)", 50000,
                  sink += pc_rdm_build(rdm_build_scratch, sizeof(rdm_build_scratch), &set_p, start_addr, 2));
        DBENCH_OP("pc_rdm_parse (GET, pdl 0)", 50000, sinkb = pc_rdm_parse(rdm_get_buf, rdm_get_len, &parsed, nullptr));
        DBENCH_OP("pc_rdm_parse (SET, pdl 2)", 50000, sinkb = pc_rdm_parse(rdm_set_buf, rdm_set_len, &parsed, nullptr));

        (void)sink;
        (void)sink8;
        (void)sink16;
        (void)sink64;
        (void)sinkb;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: dmx device microbench");
    xTaskCreatePinnedToCore(dmx_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
