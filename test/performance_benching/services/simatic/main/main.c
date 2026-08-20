// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Siemens 3964R codec (services/fieldbus/simatic): the BCC checksum
// and the DLE-stuffed block build/parse (the pure framing under the stateful 3964R engine). The
// stateful RX/TX engine (Simatic.send_3964r/rx_byte/tick) drives a UART and is out of scope; only the
// deterministic per-block codec is benched.
//
// Build/flash:  idf.py -C test/performance_benching/simatic -t upload --upload-port COM7
#include "device_bench.h"
#include "services/fieldbus/simatic/simatic.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    static const uint8_t data[32] = {0x10, 0x02, 0x03, 0x10, 0x03, 0x11, 0x22, 0x33, 0x44, 0x55, 0x10,
                                     0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x01,
                                     0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B};
    static uint8_t block[80];
    SimaticV.build_block_3964r_args.buf = block;
    SimaticV.build_block_3964r_args.cap = sizeof(block);
    SimaticV.build_block_3964r_args.data = data;
    SimaticV.build_block_3964r_args.len = sizeof(data);
    SimaticV.build_block_3964r_args.with_bcc = true;
    Simatic.build_block_3964r(protocore_simatic_span());
    size_t blen = SimaticV.n;

    for (;;)
    {
        DBENCH_BANNER("simatic");
        volatile size_t sink = 0;
        SimaticV.bcc_3964r_args.data = data;
        SimaticV.bcc_3964r_args.len = sizeof(data);
        DBENCH_BULK("Simatic.bcc_3964r", 200000, sizeof(data),
                    sink += (Simatic.bcc_3964r(protocore_simatic_span()), SimaticV.value));
        static uint8_t out[80];
        SimaticV.build_block_3964r_args.buf = out;
        SimaticV.build_block_3964r_args.cap = sizeof(out);
        SimaticV.build_block_3964r_args.data = data;
        SimaticV.build_block_3964r_args.len = sizeof(data);
        SimaticV.build_block_3964r_args.with_bcc = true;
        DBENCH_OP("Simatic.build_block_3964r (BCC)", 200000,
                  sink += (Simatic.build_block_3964r(protocore_simatic_span()), SimaticV.n));
        DBENCH_OP("Simatic.parse_block_3964r", 200000, {
            uint8_t p[64];
            size_t plen = 0;
            SimaticV.parse_block_3964r_args.buf = block;
            SimaticV.parse_block_3964r_args.len = blen;
            SimaticV.parse_block_3964r_args.with_bcc = true;
            SimaticV.parse_block_3964r_args.out = p;
            SimaticV.parse_block_3964r_args.out_cap = sizeof(p);
            SimaticV.parse_block_3964r_args.out_len = &plen;
            Simatic.parse_block_3964r(protocore_simatic_span());
            sink += SimaticV.ok ? plen : 0;
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("simatic")
