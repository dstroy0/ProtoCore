// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the TPKT (RFC 1006) + COTP X.224 class-0 frame codec
// (services/fieldbus/cotp): the TPKT envelope build/parse, the COTP Data TPDU builder, the Connection
// Request builder (plain, and with S7-style src/dst TSAP parameters appended), and the COTP
// parser - all pure (no sockets, no heap). This is the ISO-on-TCP foundation under S7comm /
// IEC 61850 MMS. Worked example for performance_benching/device/<service>/: a pure protocol codec with no
// hardware involved, so every call here exercises the real production code path (contrast
// with performance_benching/device/ads1115, a peripheral driver where the bus transaction itself is stubbed).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/cotp -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/cotp/cotp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t cotp_work[16]; // the borrow an entry takes; Cotp never reads it

void dbench_run(void)
{
    // TPKT: wrap a 3-octet payload (mirrors test_tpkt_bytes).
    static const uint8_t tpkt_payload[] = {0xAA, 0xBB, 0xCC};
    static uint8_t tpkt_buf[16];

    // COTP Data TPDU: 3-octet user data, EOT set (mirrors test_cotp_dt_bytes).
    static const uint8_t dt_data[] = {0x41, 0x42, 0x43}; // "ABC"
    static uint8_t dt_buf[16];
    CotpV.build_dt_args.buf = dt_buf;
    CotpV.build_dt_args.cap = sizeof(dt_buf);
    CotpV.build_dt_args.data = dt_data;
    CotpV.build_dt_args.data_len = sizeof(dt_data);
    CotpV.build_dt_args.eot = true;
    Cotp.build_dt(cotp_work);
    size_t dt_len = CotpV.n;

    // COTP Connection Request: plain (mirrors test_cotp_cr_bytes).
    static uint8_t cr_buf[32];

    // COTP Connection Request with S7-style src/dst TSAP parameters appended (mirrors
    // test_cotp_cr_with_tsaps).
    static const uint8_t tsaps[] = {0xC1, 0x02, 0x01, 0x00, 0xC2, 0x02, 0x01, 0x02};
    static uint8_t cr_tsap_buf[32];

    for (;;)
    {
        DBENCH_BANNER("cotp");
        volatile size_t sink = 0;

        CotpV.tpkt_build_args.buf = tpkt_buf;
        CotpV.tpkt_build_args.cap = sizeof(tpkt_buf);
        CotpV.tpkt_build_args.payload = tpkt_payload;
        CotpV.tpkt_build_args.payload_len = sizeof(tpkt_payload);
        DBENCH_OP("Cotp.tpkt_build", 100000, sink += (Cotp.tpkt_build(cotp_work), CotpV.n));

        {
            const uint8_t *payload;
            size_t payload_len, consumed;
            const size_t tpkt_len = TPKT_HEADER_SIZE + sizeof(tpkt_payload); // 4 + 3 = 7
            CotpV.tpkt_parse_args.buf = tpkt_buf;
            CotpV.tpkt_parse_args.len = tpkt_len;
            CotpV.tpkt_parse_args.payload = &payload;
            CotpV.tpkt_parse_args.payload_len = &payload_len;
            CotpV.tpkt_parse_args.consumed = &consumed;
            DBENCH_OP("Cotp.tpkt_parse", 100000, sink += (Cotp.tpkt_parse(cotp_work), CotpV.ok));
        }

        CotpV.build_dt_args.buf = dt_buf;
        CotpV.build_dt_args.cap = sizeof(dt_buf);
        CotpV.build_dt_args.data = dt_data;
        CotpV.build_dt_args.data_len = sizeof(dt_data);
        CotpV.build_dt_args.eot = true;
        DBENCH_OP("Cotp.build_dt", 100000, sink += (Cotp.build_dt(cotp_work), CotpV.n));

        CotpV.build_cr_args.buf = cr_buf;
        CotpV.build_cr_args.cap = sizeof(cr_buf);
        CotpV.build_cr_args.src_ref = 0x0001;
        CotpV.build_cr_args.tpdu_size_code = 0x0A;
        CotpV.build_cr_args.extra_params = NULL;
        CotpV.build_cr_args.extra_len = 0;
        DBENCH_OP("Cotp.build_cr", 100000, sink += (Cotp.build_cr(cotp_work), CotpV.n));

        CotpV.build_cr_args.buf = cr_tsap_buf;
        CotpV.build_cr_args.cap = sizeof(cr_tsap_buf);
        CotpV.build_cr_args.src_ref = 0x0002;
        CotpV.build_cr_args.tpdu_size_code = 0x0A;
        CotpV.build_cr_args.extra_params = tsaps;
        CotpV.build_cr_args.extra_len = sizeof(tsaps);
        DBENCH_OP("Cotp.build_cr+tsaps", 100000, sink += (Cotp.build_cr(cotp_work), CotpV.n));

        {
            CotpHeader h;
            CotpV.parse_args.buf = dt_buf;
            CotpV.parse_args.len = dt_len;
            CotpV.parse_args.out = &h;
            DBENCH_OP("Cotp.parse (DT)", 100000, sink += (Cotp.parse(cotp_work), CotpV.ok));
        }

        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("cotp")
