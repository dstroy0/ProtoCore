// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Zigbee ASH codec (services/radio/zigbee): the ASH CRC-16 and
// the byte-stuffed ASH frame encode/decode that carries EZSP to an NCP. Pure; the UART is elsewhere.
//
// Build/flash:  idf.py -C test/performance_benching/zigbee -t upload --upload-port COM7
#include "device_bench.h"
#include "services/radio/zigbee/zigbee.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t zigbee_work[16]; // the borrow an entry takes; Zigbee never reads it

void dbench_run(void)
{
    static const uint8_t payload[16] = {0x00, 0x00, 0x00, 0x02, 0x11, 0x22, 0x33, 0x44,
                                        0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC};
    static uint8_t frame[64];
    ZigbeeV.ash_frame_encode_args.control = 0x25;
    ZigbeeV.ash_frame_encode_args.payload = payload;
    ZigbeeV.ash_frame_encode_args.len = sizeof(payload);
    ZigbeeV.ash_frame_encode_args.out = frame;
    ZigbeeV.ash_frame_encode_args.cap = sizeof(frame);
    Zigbee.ash_frame_encode(zigbee_work);
    uint16_t flen = ZigbeeV.value;

    for (;;)
    {
        DBENCH_BANNER("zigbee");
        volatile uint32_t sink = 0;
        DBENCH_BULK("protocore_ash_crc16 (16B)", 200000, sizeof(payload),
                    sink += protocore_ash_crc16(payload, sizeof(payload)));
        static uint8_t out[64];
        DBENCH_OP("protocore_ash_frame_encode", 200000,
                  sink += protocore_ash_frame_encode(0x25, payload, sizeof(payload), out, sizeof(out)));
        DBENCH_OP("protocore_ash_frame_decode", 200000, {
            uint8_t ctl;
            uint8_t pay[64];
            uint16_t plen = 0;
            ZigbeeV.ash_frame_decode_args.raw = frame;
            ZigbeeV.ash_frame_decode_args.len = flen;
            ZigbeeV.ash_frame_decode_args.control = &ctl;
            ZigbeeV.ash_frame_decode_args.payload = pay;
            ZigbeeV.ash_frame_decode_args.pay_cap = sizeof(pay);
            ZigbeeV.ash_frame_decode_args.pay_len = &plen;
            Zigbee.ash_frame_decode(zigbee_work);
            sink += ZigbeeV.n >= 0 ? plen : 0;
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("zigbee")
