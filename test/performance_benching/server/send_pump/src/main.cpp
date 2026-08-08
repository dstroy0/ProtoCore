// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the chunked send-pump framing (src/server/response.cpp
// chunk_send_pump): the per-chunk "<hexlen>\r\n...\r\n" HTTP/1.1 transfer-coding framing. Compares the
// old snprintf("%x\r\n") size line against the hand-rolled pc_hex_u32 (shared_primitives/hex.h) that
// replaced it, to size the win on the ESP32 (where newlib snprintf is heavy). Pure - the ChunkSource
// that fills the body is measured elsewhere. Build/flash: pio run -d performance_benching/server/send_pump -t upload
#include "device_bench.h"
#include "shared_primitives/hex.h"
#include <Arduino.h>
#include <stdio.h>

static const size_t RESERVE = 8;
static const size_t CHUNK = 1440; // CHUNK_BUF_SIZE (one TCP MSS)
static uint8_t framed[RESERVE + CHUNK + 2];

// The old framing: snprintf the size line just ahead of the body, then the trailing CRLF.
static inline size_t frame_snprintf(uint8_t *body, size_t n)
{
    char sz[8];
    int sn = snprintf(sz, sizeof(sz), "%x\r\n", (unsigned)n);
    uint8_t *start = body - sn;
    memcpy(start, sz, (size_t)sn);
    body[n] = '\r';
    body[n + 1] = '\n';
    return (size_t)sn + n + 2;
}

// The new framing: hand-written hex size line (pc_hex_u32) + trailing CRLF.
static inline size_t frame_hex_u32(uint8_t *body, size_t n)
{
    char digits[8];
    size_t nd = pc_hex_u32((uint32_t)n, digits);
    uint8_t *start = body - (nd + 2);
    memcpy(start, digits, nd);
    start[nd] = '\r';
    start[nd + 1] = '\n';
    body[n] = '\r';
    body[n + 1] = '\n';
    return (nd + 2) + n + 2;
}

static void send_pump_bench_task(void *)
{
    uint8_t *body = framed + RESERVE;
    memset(body, 'x', CHUNK);
    const size_t BODY = 64 * 1024;
    const size_t NCHUNKS = (BODY + CHUNK - 1) / CHUNK;
    for (;;)
    {
        Serial.printf("DB ==== send_pump device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        DBENCH_OP("frame snprintf (1440B)", 200000, { sink += frame_snprintf(body, CHUNK); });
        DBENCH_OP("frame pc_hex_u32 (1440B)", 200000, { sink += frame_hex_u32(body, CHUNK); });
        DBENCH_BULK("pump 64KiB snprintf", 2000, BODY, {
            for (size_t c = 0; c < NCHUNKS; c++)
            {
                sink += frame_snprintf(body, CHUNK);
            }
        });
        DBENCH_BULK("pump 64KiB pc_hex_u32", 2000, BODY, {
            for (size_t c = 0; c < NCHUNKS; c++)
            {
                sink += frame_hex_u32(body, CHUNK);
            }
        });
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: send_pump device microbench");
    xTaskCreatePinnedToCore(send_pump_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}
void loop()
{
    delay(1000);
}
