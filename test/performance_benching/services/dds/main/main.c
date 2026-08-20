// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the DDS / RTPS framing codec (services/iot/dds):
// Rtps.header() and Rtps.submessage() build the 20-octet RTPS message header and the
// submessage TLV (id/flags/octetsToNextHeader, either endianness), and Rtps.parse() validates
// a header and walks a message's submessages via callback - all pure (no sockets, no multicast, no
// heap). Worked example for performance_benching/device/<service>/: a pure protocol codec with no hardware involved
// (contrast with performance_benching/device/ads1115, a peripheral driver where the bus transaction is stubbed), so
// every call here exercises the real production code path. The per-submessage payload codecs (CDR,
// discovery SPDP/SEDP, ACKNACK/HEARTBEAT bodies) that layer on top of this framing are out of scope.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/dds -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/iot/dds/dds.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t dds_work[16]; // the borrow an entry takes; Rtps never reads it

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

void dbench_run(void)
{
    static uint8_t hdr_out[24];
    static uint8_t sm_out[16];
    static uint8_t sm_body[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};

    // A full RTPS message: header + INFO_TS (8B, LE) + DATA (4B, LE) - mirrors test_parse_message.
    static uint8_t msg[64];
    Rtps.hdr.guid_prefix = GUID;
    Rtps.hdr.vendor_id = VENDOR;
    Rtps.out.buf = msg;
    Rtps.out.cap = sizeof(msg);
    Rtps.header(dds_work);
    size_t msg_len = Rtps.n;

    uint8_t ts_body[8] = {0};
    Rtps.sub.submessage_id = RTPS_SM_INFO_TS;
    Rtps.sub.flags = RTPS_FLAG_ENDIAN;
    Rtps.sub.contents = ts_body;
    Rtps.sub.contents_len = 8;
    Rtps.out.buf = msg + msg_len;
    Rtps.out.cap = sizeof(msg) - msg_len;
    Rtps.submessage(dds_work);
    msg_len += Rtps.n;

    uint8_t data_body[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    Rtps.sub.submessage_id = RTPS_SM_DATA;
    Rtps.sub.flags = RTPS_FLAG_ENDIAN;
    Rtps.sub.contents = data_body;
    Rtps.sub.contents_len = 4;
    Rtps.out.buf = msg + msg_len;
    Rtps.out.cap = sizeof(msg) - msg_len;
    Rtps.submessage(dds_work);
    msg_len += Rtps.n;

    for (;;)
    {
        DBENCH_BANNER("dds");
        volatile size_t sink = 0;

        Rtps.hdr.guid_prefix = GUID;
        Rtps.hdr.vendor_id = VENDOR;
        Rtps.out.buf = hdr_out;
        Rtps.out.cap = sizeof(hdr_out);
        DBENCH_OP("Rtps.header", 100000, Rtps.header(dds_work); sink += Rtps.n);

        Rtps.sub.submessage_id = RTPS_SM_INFO_TS;
        Rtps.sub.flags = RTPS_FLAG_ENDIAN;
        Rtps.sub.contents = sm_body;
        Rtps.sub.contents_len = sizeof(sm_body);
        Rtps.out.buf = sm_out;
        Rtps.out.cap = sizeof(sm_out);
        DBENCH_OP("Rtps.submessage LE", 100000, Rtps.submessage(dds_work); sink += Rtps.n);

        Rtps.sub.submessage_id = RTPS_SM_DATA;
        Rtps.sub.flags = 0x00;
        DBENCH_OP("Rtps.submessage BE", 100000, Rtps.submessage(dds_work); sink += Rtps.n);

        Rtps.msg.msg = msg;
        Rtps.msg.len = msg_len;
        Rtps.sink.on_submessage = count_submessage;
        Rtps.sink.arg = NULL;
        DBENCH_BULK("Rtps.parse", 50000, msg_len, Rtps.parse(dds_work); sink += Rtps.ok ? 1 : 0);

        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("dds")
