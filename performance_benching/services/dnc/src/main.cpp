// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the CNC RS-232 DNC drip-feed codec (services/machine_tool/dnc):
// EIA RS-244 <-> ISO/ASCII tape-code character translation (odd-parity EIA table), ISO even
// parity, G-code block framing (pc_dnc_encode_block, both tape codes), the byte-at-a-time
// block decoder (pc_dnc_decode_feed), and the full drip-feed engine (dnc_stream) that frames a
// whole program with the '%' start/end marker and paces it against reverse-channel XON/XOFF -
// pure (no heap) and, aside from dnc_stream's send/recv seam, no I/O at all. Worked example for
// performance_benching/device/<service>/: a pure protocol codec with no hardware involved, so every call here
// exercises the real production code path (contrast with performance_benching/device/ads1115, a peripheral
// driver where the bus transaction itself is stubbed). dnc_stream still needs a transport seam
// (DncSendFn/DncRecvFn) to call at all; here it is wired to a tiny stub - send just counts bytes,
// recv always reports "nothing available" (never asserts XOFF) - the same no-op-stub approach
// test/test_dnc_stream/test_dnc_stream.cpp uses for its mock controller, never a real UART/socket
// transaction (this rig has no RS-232/Ethernet DNC controller attached). Sample lines are copied
// from test/test_dnc/test_dnc.cpp (test_roundtrip_program, test_encode_block_eia) and
// test/test_dnc_stream/test_dnc_stream.cpp (test_iso_roundtrip); out of scope: dnc_stream's
// higher-level callers (there are none in this library - it is the top of the DNC stack).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/dnc -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/machine_tool/dnc/dnc.h"
#include "services/machine_tool/dnc/dnc_stream.h"
#include <Arduino.h>

// Transport seam stub for dnc_stream: send just counts bytes (never touches a real UART/socket),
// recv always reports "no reverse-channel bytes available" (i.e. the controller never asserts
// XOFF), so a full drip-feed run always completes in one straight pass - the same shape as
// test_dnc_stream.cpp's mock_send/mock_recv with every scripted failure/XOFF path disabled.
struct DncBenchCtx
{
    size_t bytes;
};

static int dnc_bench_send(void *ctx, const uint8_t *data, size_t len)
{
    (void)data;
    DncBenchCtx *c = (DncBenchCtx *)ctx;
    c->bytes += len;
    return (int)len;
}

static int dnc_bench_recv(void *ctx, uint8_t *buf, size_t cap)
{
    (void)ctx;
    (void)buf;
    (void)cap;
    return 0; // reverse channel idle - stub only, no real transport
}

static DncStreamResult dnc_bench_run_stream(const DncCfg *cfg, const char *prog, size_t len)
{
    DncBenchCtx ctx = {0};
    return dnc_stream(cfg, prog, len, dnc_bench_send, dnc_bench_recv, &ctx);
}

// Full byte-at-a-time decode of one pre-encoded block, mirroring test_dnc.cpp's decode_all().
static DncDecoder g_dec;
static int dnc_bench_decode_full(DncCode code, const uint8_t *buf, size_t len)
{
    pc_dnc_decode_init(&g_dec, code);
    int lines = 0;
    for (size_t i = 0; i < len; i++)
    {
        if (pc_dnc_decode_feed(&g_dec, buf[i]) == DncEvent::DNC_EV_LINE)
        {
            lines++;
        }
    }
    return lines;
}

static void dnc_bench_task(void *)
{
    DncCfg iso_cfg = {DncCode::DNC_CODE_ISO, false, false, 0};
    DncCfg eia_cfg = {DncCode::DNC_CODE_EIA, false, false, 0};

    // "G1 Z-1. F100" (test_roundtrip_program) framed as one ISO block (LF end-of-block).
    static const char iso_line[] = "G1 Z-1. F100";
    static uint8_t iso_block[32];
    size_t iso_block_len = pc_dnc_encode_block(&iso_cfg, iso_line, strlen(iso_line), iso_block, sizeof(iso_block));

    // "G01" (test_encode_block_eia) framed as one EIA block (0x80 end-of-block).
    static const char eia_line[] = "G01";
    static uint8_t eia_block[32];
    size_t eia_block_len = pc_dnc_encode_block(&eia_cfg, eia_line, strlen(eia_line), eia_block, sizeof(eia_block));

    // A 3-line ISO program (test_iso_roundtrip) drip-fed whole through dnc_stream.
    static const char prog[] = "N10 G0 X1 Y2\nN20 G1 X3 F100\nM30";
    size_t prog_len = strlen(prog);

    for (;;)
    {
        Serial.printf("DB ==== dnc device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile uint8_t sink8 = 0;
        volatile size_t sink = 0;
        volatile int sinki = 0;

        DBENCH_OP("pc_dnc_iso_to_eia", 200000, sink8 += pc_dnc_iso_to_eia('W'));

        DBENCH_OP("pc_dnc_eia_to_iso", 200000, sink8 += (uint8_t)pc_dnc_eia_to_iso(0x67));

        DBENCH_BULK("pc_dnc_encode_block (ISO)", 50000, iso_block_len,
                    sink += pc_dnc_encode_block(&iso_cfg, iso_line, strlen(iso_line), iso_block, sizeof(iso_block)));

        DBENCH_BULK("pc_dnc_encode_block (EIA)", 50000, eia_block_len,
                    sink += pc_dnc_encode_block(&eia_cfg, eia_line, strlen(eia_line), eia_block, sizeof(eia_block)));

        DBENCH_BULK("pc_dnc_decode_feed (ISO block)", 50000, iso_block_len,
                    sinki += dnc_bench_decode_full(DncCode::DNC_CODE_ISO, iso_block, iso_block_len));

        DBENCH_BULK("dnc_stream (ISO, 3 lines)", 5000, prog_len,
                    sinki += (int)dnc_bench_run_stream(&iso_cfg, prog, prog_len));

        (void)sink8;
        (void)sink;
        (void)sinki;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: dnc device microbench");
    xTaskCreatePinnedToCore(dnc_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
