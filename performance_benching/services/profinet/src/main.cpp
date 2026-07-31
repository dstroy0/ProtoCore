// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the PROFINET DCP frame codec (services/fieldbus/profinet): the
// 10-octet DCP header build + parse, a single DCP option/suboption block build (with the odd->even
// pad path exercised via a 3-byte NameOfStation), and walking a multi-block DCP body. All four are
// pure (no heap, no sockets), so every call here exercises the real production code path - a pure
// protocol codec like performance_benching/device/modbus (contrast with performance_benching/device/ads1115, where the
// bus half is stubbed). The raw-L2 transmit (ethertype 0x8892, services/fieldbus/rawl2) is the device step and is
// deliberately out of scope: this rig has no NIC, and pc_pn_dcp_walk's callback is a tiny no-op
// sink so nothing here touches hardware or a transport.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/profinet -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/profinet/profinet.h"
#include <Arduino.h>

// No-op block sink for pc_pn_dcp_walk: counts into a volatile so the walk is not optimized away.
// Mirrors the host test's `collect` callback but without the bookkeeping (nothing here touches the
// wire or any transport - the walk is pure CPU work).
static volatile uint32_t g_walk_sink = 0;
static void walk_sink(uint8_t option, uint8_t suboption, const uint8_t *value, size_t value_len, void *arg)
{
    (void)value;
    (void)arg;
    g_walk_sink += (uint32_t)option + (uint32_t)suboption + (uint32_t)value_len;
}

static void profinet_bench_task(void *)
{
    // A two-block DCP body to walk, built once with the real block builder (spec-conformant, straight
    // from test/test_profinet: DEVICE/NameOfStation "plc" (odd -> even-padded) + IP/IPParameter "ABCD").
    static uint8_t body[64];
    size_t body_len = 0;
    body_len += pc_pn_dcp_block(Pn::PN_DCP_OPT_DEVICE, Pn::PN_DCP_SUB_DEV_NAME_OF_STATION, (const uint8_t *)"plc", 3,
                                body + body_len, sizeof(body) - body_len);
    body_len += pc_pn_dcp_block(Pn::PN_DCP_OPT_IP, Pn::PN_DCP_SUB_IP_PARAM, (const uint8_t *)"ABCD", 4, body + body_len,
                                sizeof(body) - body_len);

    // A known-good Identify-request header (from test/test_profinet test_header_roundtrip) to parse.
    static const uint8_t ident_hdr[] = {0xFE, 0xFE, 0x05, 0x00, 0x11, 0x22, 0x33, 0x44, 0x00, 0x08};

    static uint8_t hdr_out[16];
    static uint8_t blk_out[16];

    for (;;)
    {
        Serial.printf("DB ==== profinet device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        PnDcpHeader h;

        DBENCH_OP("pc_pn_dcp_header (Identify)", 200000,
                  sink += pc_pn_dcp_header(Pn::PN_FRAMEID_DCP_IDENT_REQ, Pn::PN_DCP_SERVICE_IDENTIFY,
                                           Pn::PN_DCP_TYPE_REQUEST, 0x11223344, 8, hdr_out, sizeof(hdr_out)));
        DBENCH_OP("pc_pn_dcp_block (NameOfStation)", 200000,
                  sink += pc_pn_dcp_block(Pn::PN_DCP_OPT_DEVICE, Pn::PN_DCP_SUB_DEV_NAME_OF_STATION,
                                          (const uint8_t *)"plc", 3, blk_out, sizeof(blk_out)));
        DBENCH_OP("pc_pn_dcp_parse_header", 200000,
                  sink += (size_t)pc_pn_dcp_parse_header(ident_hdr, sizeof(ident_hdr), &h));
        DBENCH_OP("pc_pn_dcp_walk (2 blocks)", 100000,
                  sink += (size_t)pc_pn_dcp_walk(body, body_len, walk_sink, nullptr));

        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: profinet device microbench");
    xTaskCreatePinnedToCore(profinet_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
