// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SERCOS III codec (services/fieldbus/sercos): the IDN build/parse
// (S/P param-set + data block) and the telegram build/parse. Pure; no fieldbus link.
//
// Build/flash:  idf.py -C test/performance_benching/sercos -t upload --upload-port COM7
#include "device_bench.h"
#include "services/fieldbus/sercos/sercos.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t sercos_work[16]; // the borrow an entry takes; Sercos never reads it

void dbench_run(void)
{
    static const uint8_t data[16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                                     0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00};
    static uint8_t frame[64];
    Sercos.build_args.type = 0x02;
    Sercos.build_args.phase = 4;
    Sercos.build_args.cycle = 1000;
    Sercos.build_args.data = data;
    Sercos.build_args.data_len = sizeof(data);
    Sercos.build_args.out = frame;
    Sercos.build_args.cap = sizeof(frame);
    Sercos.build(sercos_work);
    size_t flen = Sercos.n;

    for (;;)
    {
        DBENCH_BANNER("sercos");
        volatile size_t sink = 0;
        Sercos.idn_args.is_product = false;
        Sercos.idn_args.param_set = 0;
        Sercos.idn_args.data_block = 100;
        DBENCH_OP("Sercos.idn", 200000, sink += (Sercos.idn(sercos_work), Sercos.value));
        bool prod;
        uint8_t ps;
        uint16_t db;
        DBENCH_OP("Sercos.idn_parse", 200000, {
            Sercos.idn_parse_args.idn = 0x0064;
            Sercos.idn_parse_args.is_product = &prod;
            Sercos.idn_parse_args.param_set = &ps;
            Sercos.idn_parse_args.data_block = &db;
            Sercos.idn_parse(sercos_work);
            sink += db;
        });
        static uint8_t out[64];
        Sercos.build_args.type = 0x02;
        Sercos.build_args.phase = 4;
        Sercos.build_args.cycle = 1000;
        Sercos.build_args.data = data;
        Sercos.build_args.data_len = sizeof(data);
        Sercos.build_args.out = out;
        Sercos.build_args.cap = sizeof(out);
        DBENCH_OP("Sercos.build", 200000,
                  sink += (Sercos.build(sercos_work), Sercos.n));
        SercosTelegram tg;
        Sercos.parse_args.frame = frame;
        Sercos.parse_args.len = flen;
        Sercos.parse_args.out = &tg;
        DBENCH_OP("Sercos.parse", 200000, sink += (Sercos.parse(sercos_work), Sercos.ok));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("sercos")
