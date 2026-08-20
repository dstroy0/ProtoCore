// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Beckhoff ADS / AMS codec (services/fieldbus/ads): the AMS/TCP +
// AMS-header request builders (little-endian, target-before-source addressing, cmd id + state
// flags + cbData + invoke id) and the response parsers (AMS-header validation, Read/ReadWrite
// payload, DeviceNotification stamp/sample walk). Pure codec, no sockets - the caller owns the TCP
// connection (`protocore_client_*`) and the AMS route registration, both out of scope here. Worked
// example for performance_benching/device/<service>/: a pure protocol codec with no hardware involved (contrast
// with performance_benching/device/ads1115, a peripheral driver where the bus transaction itself is stubbed).
//
// Sample bytes (request frame, Read response, DeviceNotification payload) are copied verbatim from
// test/test_ads/test_ads.cpp - already known-good, spec-conformant fixtures.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/ads -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/ads/ads.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t ads_work[16]; // the borrow an entry takes; Ads never reads it

// protocore_ads_parse_notification requires a real callback (it fails closed on a null one); this just
// counts samples so the walk itself - not the callback body - is what gets timed.
static volatile uint32_t g_notif_samples = 0;
static void on_sample(uint32_t /*handle*/, const uint8_t * /*sample*/, uint32_t /*sample_len*/, uint64_t /*ts*/,
                      void * /*user*/)
{
    g_notif_samples++;
}

// A representative AMS route: PLC at 5.18.30.40.1.1 port 851 (first TC3 runtime), local client at
// 192.168.1.100.1.1 port 32768, invoke id 42 - same fixture as test_ads.cpp's make_req().
static AdsRequest make_req()
{
    AdsRequest r;
    const uint8_t tgt[ADS_NET_ID_LEN] = {5, 18, 30, 40, 1, 1};
    const uint8_t src[ADS_NET_ID_LEN] = {192, 168, 1, 100, 1, 1};
    memcpy(r.target.net_id, tgt, ADS_NET_ID_LEN);
    r.target.port = 851;
    memcpy(r.source.net_id, src, ADS_NET_ID_LEN);
    r.source.port = 32768;
    r.invoke_id = 42;
    return r;
}

void dbench_run(void)
{
    AdsRequest r = make_req();
    static uint8_t buf[128];
    const uint8_t val[] = {0x01, 0x00, 0x00, 0x00};
    const char *name = "MAIN.counter";
    size_t name_len = strlen(name);

    // ADS Read response (Read of 4 octets): AMS/TCP + AMS header + Result(0) + Length(4) + Data.
    static const uint8_t read_resp[] = {
        0x00, 0x00, 0x2C, 0x00, 0x00, 0x00,             // AMS/TCP: len 44
        0xC0, 0xA8, 0x01, 0x64, 0x01, 0x01, 0x00, 0x80, // target (now the client)
        0x05, 0x12, 0x1E, 0x28, 0x01, 0x01, 0x53, 0x03, // source (the PLC)
        0x02, 0x00,                                     // cmd 2 (Read)
        0x05, 0x00,                                     // state flags 0x0005 (reply)
        0x0C, 0x00, 0x00, 0x00,                         // cbData 12
        0x00, 0x00, 0x00, 0x00,                         // AMS error 0
        0x2A, 0x00, 0x00, 0x00,                         // invoke id 42 (echoed)
        0x00, 0x00, 0x00, 0x00,                         // result 0
        0x04, 0x00, 0x00, 0x00,                         // length 4
        0xDE, 0xAD, 0xBE, 0xEF                          // data
    };

    // DeviceNotification (cmd 8) payload: one stamp, one sample (handle 0x1234, 2 octets).
    static const uint8_t notif_payload[] = {
        0x1A, 0x00, 0x00, 0x00,                         // Length = 26 (octets after this field)
        0x01, 0x00, 0x00, 0x00,                         // Stamps = 1
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Timestamp = 1
        0x01, 0x00, 0x00, 0x00,                         // Samples = 1
        0x34, 0x12, 0x00, 0x00,                         // NotificationHandle = 0x1234
        0x02, 0x00, 0x00, 0x00,                         // SampleSize = 2
        0x11, 0x22                                      // data
    };

    for (;;)
    {
        DBENCH_BANNER("ads");
        volatile size_t sink = 0;
        volatile bool sinkb = false;

        AdsV.build_read_args.buf = buf;
        AdsV.build_read_args.cap = sizeof(buf);
        AdsV.build_read_args.r = &r;
        AdsV.build_read_args.index_group = ADS_IGRP_PLC_RW_M;
        AdsV.build_read_args.index_offset = 0;
        AdsV.build_read_args.read_len = 4;
        DBENCH_OP("Ads.build_read", 100000, sink += (AdsV.build_read(ads_work), AdsV.n));
        AdsV.build_write_args.buf = buf;
        AdsV.build_write_args.cap = sizeof(buf);
        AdsV.build_write_args.r = &r;
        AdsV.build_write_args.index_group = ADS_IGRP_PLC_RW_M;
        AdsV.build_write_args.index_offset = 8;
        AdsV.build_write_args.data = val;
        AdsV.build_write_args.len = sizeof(val);
        DBENCH_OP("Ads.build_write", 100000, sink += (AdsV.build_write(ads_work), AdsV.n));
        AdsV.build_read_write_args.buf = buf;
        AdsV.build_read_write_args.cap = sizeof(buf);
        AdsV.build_read_write_args.r = &r;
        AdsV.build_read_write_args.index_group = ADS_IGRP_SYM_HND_BY_NAME;
        AdsV.build_read_write_args.index_offset = 0;
        AdsV.build_read_write_args.read_len = 4;
        AdsV.build_read_write_args.write_data = (const uint8_t *)name;
        AdsV.build_read_write_args.write_len = (uint32_t)name_len;
        DBENCH_OP("Ads.build_read_write", 50000, sink += (AdsV.build_read_write(ads_work), AdsV.n));

        AdsAmsHeader h;
        AdsV.parse_ams_header_args.buf = read_resp;
        AdsV.parse_ams_header_args.len = sizeof(read_resp);
        AdsV.parse_ams_header_args.out = &h;
        DBENCH_BULK("Ads.parse_ams_header", 100000, sizeof(read_resp),
                    sinkb = (AdsV.parse_ams_header(ads_work), AdsV.ok));

        AdsV.parse_ams_header_args.buf = read_resp;
        AdsV.parse_ams_header_args.len = sizeof(read_resp);
        AdsV.parse_ams_header_args.out = &h;
        AdsV.parse_ams_header(ads_work);
        (void)AdsV.ok; // refresh h.data for the parse below
        AdsReadResult rr;
        AdsV.parse_read_args.data = h.data;
        AdsV.parse_read_args.data_len = h.data_len;
        AdsV.parse_read_args.out = &rr;
        DBENCH_OP("Ads.parse_read", 100000, sinkb = (AdsV.parse_read(ads_work), AdsV.ok));

        AdsV.parse_notification_args.data = notif_payload;
        AdsV.parse_notification_args.data_len = sizeof(notif_payload);
        AdsV.parse_notification_args.on_sample = on_sample;
        AdsV.parse_notification_args.user = NULL;
        DBENCH_BULK("Ads.parse_notification", 50000, sizeof(notif_payload),
                    sinkb = (AdsV.parse_notification(ads_work), AdsV.ok));

        (void)sink;
        (void)sinkb;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("ads")
