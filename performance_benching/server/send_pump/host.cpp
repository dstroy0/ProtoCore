// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the chunked send-pump framing (docs/FEATURE_PERFORMANCE.md section 3):
// the per-chunk HTTP/1.1 transfer-coding framing the pump adds around each body piece
// ("<hexlen>\r\n<body>\r\n", src/server/response.cpp chunk_send_pump). Isolates the framing overhead
// (the ChunkSource that fills the body is measured elsewhere - file I/O in section 1, template render
// in the request path). Compares the current snprintf("%x\r\n") size line against a hand-rolled hex
// writer, since snprintf's format-string parsing dominates such a tiny write. A deterministic host
// ns/op baseline that complements the on-device ESP32-S3 numbers (the host figure is a fast RPi core,
// a relative baseline, not the device cost). Build + run:
//   g++ -O2 -std=c++17 performance_benching/server/send_pump/host.cpp -o /tmp/sp && /tmp/sp

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

using clk = std::chrono::steady_clock;

template <typename F> static double bench_ns(uint64_t iters, F fn)
{
    auto t0 = clk::now();
    for (uint64_t i = 0; i < iters; i++)
        fn();
    auto t1 = clk::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / (double)iters;
}

static void row(const char *op, double ns_per_op, double bytes_per_op)
{
    double mbps = bytes_per_op > 0 ? (bytes_per_op / (ns_per_op * 1e-9)) / 1e6 : 0.0;
    printf("| %-26s | %10.1f | %10.1f |\n", op, ns_per_op, mbps);
}

// The current framing (mirrors response.cpp chunk_send_pump exactly): snprintf the size line just
// before the body, set the trailing CRLF. `body` points CHUNK_HDR_RESERVE into a framing buffer.
static size_t frame_snprintf(uint8_t *framed, uint8_t *body, size_t n)
{
    char sz[8];
    int sn = snprintf(sz, sizeof(sz), "%x\r\n", (unsigned)n);
    uint8_t *start = body - sn;
    memcpy(start, sz, (size_t)sn);
    body[n] = '\r';
    body[n + 1] = '\n';
    (void)framed;
    return (size_t)sn + n + 2;
}

// A hand-rolled hex size line: write the hex digits directly (no format-string parse), then CRLF.
static size_t frame_handrolled(uint8_t *framed, uint8_t *body, size_t n)
{
    char tmp[6];
    int t = 0;
    unsigned v = (unsigned)n;
    if (v == 0)
        tmp[t++] = '0';
    else
        while (v)
        {
            tmp[t++] = "0123456789abcdef"[v & 0xF];
            v >>= 4;
        }
    // Reverse the digits directly ahead of the body, then CRLF, matching the size line layout.
    uint8_t *start = body - (t + 2);
    int len = 0;
    while (t)
        start[len++] = (uint8_t)tmp[--t];
    start[len++] = '\r';
    start[len++] = '\n';
    body[n] = '\r';
    body[n + 1] = '\n';
    (void)framed;
    return (size_t)len + n + 2;
}

int main()
{
    printf("| Operation                  |     ns/op |      MB/s |\n");
    printf("|----------------------------|-----------|-----------|\n");

    static const size_t RESERVE = 8;
    static const size_t CHUNK = 1440; // CHUNK_BUF_SIZE (one TCP MSS)
    static uint8_t framed[RESERVE + CHUNK + 2];
    uint8_t *body = framed + RESERVE;
    memset(body, 'x', CHUNK);

    volatile size_t sink = 0;

    // Per-chunk framing of a full 1440-byte chunk: MB/s = the CPU ceiling for framing at MSS size.
    double ns = bench_ns(2000000, [&] { sink += frame_snprintf(framed, body, CHUNK); });
    row("frame snprintf (1440B)", ns, (double)CHUNK);
    ns = bench_ns(2000000, [&] { sink += frame_handrolled(framed, body, CHUNK); });
    row("frame hand-rolled (1440B)", ns, (double)CHUNK);

    // Pump a 64 KiB body: frame it into 1440-byte chunks (46 chunks). MB/s over the whole body is the
    // CPU-limited pump throughput; the real limit is the network/W5500, far below this.
    const size_t BODY = 64 * 1024;
    const size_t NCHUNKS = (BODY + CHUNK - 1) / CHUNK;
    ns = bench_ns(50000, [&] {
        for (size_t c = 0; c < NCHUNKS; c++)
            sink += frame_snprintf(framed, body, CHUNK);
    });
    row("pump 64KiB snprintf", ns, (double)BODY);
    ns = bench_ns(50000, [&] {
        for (size_t c = 0; c < NCHUNKS; c++)
            sink += frame_handrolled(framed, body, CHUNK);
    });
    row("pump 64KiB hand-rolled", ns, (double)BODY);

    (void)sink;
    return 0;
}
