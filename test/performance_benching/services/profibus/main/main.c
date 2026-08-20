// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the PROFIBUS-DP FDL telegram codec (services/fieldbus/profibus):
// building/validating the SD1 (no-data) and SD2 (variable-data) telegrams a DP master exchanges with
// its slaves, plus the underlying arithmetic-sum FCS. All four benched calls (Profibus.fcs,
// Profibus.build_sd1, Profibus.build_sd2, Profibus.parse) are pure - zero heap, no stdlib, no I/O - so each
// exercises the real production code path (like performance_benching/device/modbus, a pure codec; contrast with
// performance_benching/device/ads1115, a peripheral driver where the bus transaction is stubbed). The RS-485 UART
// timing + the DP-V0 cyclic state machine are the physical "device step" and are deliberately out of
// scope here: this rig drives no PROFIBUS transceiver, only the deterministic CPU-side telegram math.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/profibus -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/profibus/profibus.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t profibus_work[16]; // the borrow an entry takes; Profibus never reads it

void dbench_run(void)
{
    // SD2 process-data payload (spec-conformant vector shape from test/test_profibus).
    static const uint8_t data3[3] = {0xAA, 0xBB, 0xCC};
    // FCS body: DA + SA + FC (matches test_fcs: 0x03 + 0x02 + 0x49 -> 0x4E).
    static const uint8_t fcs_body[3] = {0x03, 0x02, PB_FC_REQUEST_FDL_STATUS};
    static uint8_t out[16];

    // Prebuilt telegrams for the parse bench (known-good, so Profibus.parse takes the accept path).
    static uint8_t sd1_frame[8];
    static uint8_t sd2_frame[16];
    Profibus.build_sd1_args.da = 0x03;
    Profibus.build_sd1_args.sa = 0x02;
    Profibus.build_sd1_args.fc = PB_FC_REQUEST_FDL_STATUS;
    Profibus.build_sd1_args.out = sd1_frame;
    Profibus.build_sd1_args.cap = sizeof(sd1_frame);
    Profibus.build_sd1(profibus_work);
    size_t sd1_len = Profibus.n;
    Profibus.build_sd2_args.da = 0x05;
    Profibus.build_sd2_args.sa = 0x02;
    Profibus.build_sd2_args.fc = PB_FC_SRD_HIGH;
    Profibus.build_sd2_args.data = data3;
    Profibus.build_sd2_args.data_len = sizeof(data3);
    Profibus.build_sd2_args.out = sd2_frame;
    Profibus.build_sd2_args.cap = sizeof(sd2_frame);
    Profibus.build_sd2(profibus_work);
    size_t sd2_len = Profibus.n;

    for (;;)
    {
        DBENCH_BANNER("profibus");
        volatile size_t sink = 0;
        volatile uint8_t sink8 = 0;
        PbTelegram tg;

        Profibus.fcs_args.bytes = fcs_body;
        Profibus.fcs_args.len = sizeof(fcs_body);
        DBENCH_OP("Profibus.fcs (DA+SA+FC)", 100000, sink8 += (Profibus.fcs(profibus_work), Profibus.value));
        Profibus.build_sd1_args.da = 0x03;
        Profibus.build_sd1_args.sa = 0x02;
        Profibus.build_sd1_args.fc = PB_FC_REQUEST_FDL_STATUS;
        Profibus.build_sd1_args.out = out;
        Profibus.build_sd1_args.cap = sizeof(out);
        DBENCH_OP("Profibus.build_sd1", 100000,
                  sink += (Profibus.build_sd1(profibus_work), Profibus.n));
        Profibus.build_sd2_args.da = 0x05;
        Profibus.build_sd2_args.sa = 0x02;
        Profibus.build_sd2_args.fc = PB_FC_SRD_HIGH;
        Profibus.build_sd2_args.data = data3;
        Profibus.build_sd2_args.data_len = sizeof(data3);
        Profibus.build_sd2_args.out = out;
        Profibus.build_sd2_args.cap = sizeof(out);
        DBENCH_OP("Profibus.build_sd2 (3B data)", 50000,
                  sink += (Profibus.build_sd2(profibus_work), Profibus.n));
        Profibus.parse_args.frame = sd1_frame;
        Profibus.parse_args.len = sd1_len;
        Profibus.parse_args.out = &tg;
        DBENCH_OP("Profibus.parse SD1", 100000, sink += (Profibus.parse(profibus_work), Profibus.ok) ? 1 : 0);
        Profibus.parse_args.frame = sd2_frame;
        Profibus.parse_args.len = sd2_len;
        Profibus.parse_args.out = &tg;
        DBENCH_OP("Profibus.parse SD2 (3B data)", 100000,
                  sink += (Profibus.parse(profibus_work), Profibus.ok) ? 1 : 0);

        (void)sink;
        (void)sink8;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("profibus")
