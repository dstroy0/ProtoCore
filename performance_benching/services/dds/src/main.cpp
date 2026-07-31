// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the DDS / RTPS framing codec (services/iot/dds):
// pc_rtps_header() and pc_rtps_submessage() build the 20-octet RTPS message header and the
// submessage TLV (id/flags/octetsToNextHeader, either endianness), and pc_rtps_parse() validates
// a header and walks a message's submessages via callback - all pure (no sockets, no multicast, no
// heap). Worked example for performance_benching/device/<service>/: a pure protocol codec with no hardware involved
// (contrast with performance_benching/device/ads1115, a peripheral driver where the bus transaction is stubbed), so
// every call here exercises the real production code path. The per-submessage payload codecs (CDR,
// discovery SPDP/SEDP, ACKNACK/HEARTBEAT bodies) that layer on top of this framing are out of scope.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/dds -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/iot/dds/dds.h"
#include <Arduino.h>

// Same fixtures as test/test_dds/test_dds.cpp (known-good, spec-conformant).
static const uint8_t GUID[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
static const uint8_t VENDOR[2] = {0x01, 0x03};

static volatile uint32_t g_submessages_seen = 0;
static void count_submessage(uint8_t id, uint8_t flags, const uint8_t *body, size_t body_len, void *arg)
{
    (void)id;
    (void)flags;
    (void)body;
    (void)body_len;
    (void)arg;
    g_submessages_seen++;
}

static void dds_bench_task(void *)
{
    static uint8_t hdr_out[24];
    static uint8_t sm_out[16];
    static uint8_t sm_body[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};

    // A full RTPS message: header + INFO_TS (8B, LE) + DATA (4B, LE) - mirrors test_parse_message.
    static uint8_t msg[64];
    size_t msg_len = pc_rtps_header(GUID, VENDOR, msg, sizeof(msg));
    uint8_t ts_body[8] = {0};
    msg_len += pc_rtps_submessage(Rtps::RTPS_SM_INFO_TS, Rtps::RTPS_FLAG_ENDIAN, ts_body, 8, msg + msg_len,
                                  sizeof(msg) - msg_len);
    uint8_t data_body[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    msg_len += pc_rtps_submessage(Rtps::RTPS_SM_DATA, Rtps::RTPS_FLAG_ENDIAN, data_body, 4, msg + msg_len,
                                  sizeof(msg) - msg_len);

    for (;;)
    {
        Serial.printf("DB ==== dds device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        DBENCH_OP("pc_rtps_header", 100000, sink += pc_rtps_header(GUID, VENDOR, hdr_out, sizeof(hdr_out)));
        DBENCH_OP("pc_rtps_submessage LE", 100000,
                  sink += pc_rtps_submessage(Rtps::RTPS_SM_INFO_TS, Rtps::RTPS_FLAG_ENDIAN, sm_body, sizeof(sm_body),
                                             sm_out, sizeof(sm_out)));
        DBENCH_OP("pc_rtps_submessage BE", 100000,
                  sink +=
                  pc_rtps_submessage(Rtps::RTPS_SM_DATA, 0x00, sm_body, sizeof(sm_body), sm_out, sizeof(sm_out)));
        DBENCH_BULK("pc_rtps_parse", 50000, msg_len,
                    sink += pc_rtps_parse(msg, msg_len, count_submessage, nullptr) ? 1 : 0);
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: dds device microbench");
    xTaskCreatePinnedToCore(dds_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
