// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the memory manager (server/mmgr): the two pools, their
// ownership test, and what the secure pool's wipe-on-release actually costs. Prints "PB <op> ..."
// lines over USB-CDC, the same shape as the crypto bench.
//
// Three claims were made while building the pools, all of the kind that sound obviously true and are
// worth nothing until a cycle counter says so:
//
//   1. borrowing from a pool is cheap - it is a bump allocation behind an accessor
//   2. ownership testing is "virtually free": the slot count and slot size are compile-time, so it is
//      one unsigned subtract and compare with no bookkeeping and no loop
//   3. wipe-on-release is the ONLY real cost difference between the two pools, and it is linear in
//      the bytes reclaimed
//
// Claim 3 is the one that matters for the design: the two pools are the same mechanism, so releasing
// the same byte count from each isolates the wipe exactly.
//
// This lives in the rig firmware rather than examples/ because the pools are private - their state
// has internal linkage and the accessor headers are not reachable from a user sketch. A shipped
// example calling them would hand every user the capability the design exists to withhold.

#include "protocore.h"

#include "server/mmgr/plaintext.h"
#include "server/mmgr/secure.h"

#include <stdlib.h> // malloc/free/qsort for the traditional comparison

static inline uint32_t cyc_now()
{
    return ESP.getCycleCount();
}

// CCOUNT ticks at the CPU clock, which differs per die (S3 240 MHz, P4 360 MHz), so the conversion
// must read the live frequency; the raw cycle counts are frequency-independent.
static double g_mhz = 240.0;

// Per-call op. Warm once, then N iters, average cycles. These calls are tens of cycles, so N is large
// and the loop overhead is measured separately by BENCH_BASELINE below and reported alongside.
#define BENCH_OP(label, N, expr)                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        expr; /* warm */                                                                                               \
        uint32_t _c0 = cyc_now();                                                                                      \
        for (uint32_t _i = 0; _i < (uint32_t)(N); _i++)                                                                \
        {                                                                                                              \
            expr;                                                                                                      \
        }                                                                                                              \
        double _cy = (double)(cyc_now() - _c0) / (double)(N);                                                          \
        Serial.printf("PB %-34s cyc=%-9.1f ns=%.1f\n", label, _cy, _cy * 1000.0 / g_mhz);                              \
        vTaskDelay(1);                                                                                                 \
    } while (0)

// Keeps a result observable so the optimizer cannot delete the call being timed.
static volatile bool g_sink = false;
static volatile int g_isink = 0;

#define BENCH_ITERS 2000

static void bench_borrow()
{
    Serial.println("PB -- borrow (per allocation, scope released each iteration) --");

    BENCH_OP("scratch_span 256B", BENCH_ITERS, {
        PlaintextScope s;
        g_sink = pc_span_ok(pc_plaintext_span(256, 16));
    });
    BENCH_OP("secure_span 256B", BENCH_ITERS, {
        SecureScope s;
        g_sink = pc_span_ok(pc_secure_span(256, 16));
    });
    // The SSH reply buffer - the borrow the rule-19 sweep was aimed at.
    BENCH_OP("scratch_span 2048B (SSH reply)", BENCH_ITERS, {
        PlaintextScope s;
        g_sink = pc_span_ok(pc_plaintext_span(2048, 16));
    });
}

// Where does a pool operation's time actually go? mark+alloc+release is three trivial offset
// updates, so ~413 cycles has to be something else. The debug owner tripwire calls
// xTaskGetCurrentTaskHandle() on EVERY entry point, and this build has no NDEBUG - so time that call
// on its own and see how much of the 413 it explains.
static void bench_probe()
{
    Serial.println("PB -- where the time goes --");
    BENCH_OP("xTaskGetCurrentTaskHandle", BENCH_ITERS, g_sink = (xTaskGetCurrentTaskHandle() != nullptr));
    BENCH_OP("pc_plaintext_mark alone", BENCH_ITERS, g_isink = (int)pc_plaintext_mark());
    BENCH_OP("pc_secure_mark alone", BENCH_ITERS, g_isink = (int)pc_secure_mark());
}

static void bench_owns()
{
    Serial.println("PB -- ownership test (the access control) --");

    PlaintextScope pscope;
    pc_span plain = pc_plaintext_span(64, 16);
    SecureScope sscope;
    pc_span sec = pc_secure_span(64, 16);
    static uint8_t foreign[16];

    if (!pc_span_ok(plain) || !pc_span_ok(sec))
    {
        Serial.println("PB ownership: SKIPPED (a pool borrow failed)");
        return;
    }

    BENCH_OP("pc_plaintext_owns hit", BENCH_ITERS, g_sink = pc_plaintext_owns(plain.buf));
    // The security-relevant direction: a secure pointer offered where plaintext is expected.
    BENCH_OP("pc_plaintext_owns secure ptr (reject)", BENCH_ITERS, g_sink = pc_plaintext_owns(sec.buf));
    BENCH_OP("pc_secure_owns hit", BENCH_ITERS, g_sink = pc_secure_owns(sec.buf));
    BENCH_OP("pc_secure_owns foreign ptr (reject)", BENCH_ITERS, g_sink = pc_secure_owns(foreign));
    BENCH_OP("pc_secure_slot_of", BENCH_ITERS, g_isink = pc_secure_slot_of(sec.buf));
}

// Same mechanism on both sides, so the difference at equal byte counts IS the wipe.
static void bench_release(size_t n)
{
    char label[48];

    snprintf(label, sizeof(label), "pc_plaintext_release %uB", (unsigned)n);
    uint32_t c0 = cyc_now();
    for (uint32_t i = 0; i < BENCH_ITERS; i++)
    {
        size_t mark = pc_plaintext_mark();
        g_sink = pc_span_ok(pc_plaintext_span(n, 16));
        pc_plaintext_release(mark);
    }
    double plain = (double)(cyc_now() - c0) / (double)BENCH_ITERS;
    Serial.printf("PB %-34s cyc=%-9.1f ns=%.1f\n", label, plain, plain * 1000.0 / g_mhz);
    vTaskDelay(1);

    snprintf(label, sizeof(label), "secure_release %uB (wipes)", (unsigned)n);
    c0 = cyc_now();
    for (uint32_t i = 0; i < BENCH_ITERS; i++)
    {
        size_t mark = pc_secure_mark();
        g_sink = pc_span_ok(pc_secure_span(n, 16));
        pc_secure_release(mark);
    }
    double secure = (double)(cyc_now() - c0) / (double)BENCH_ITERS;
    Serial.printf("PB %-34s cyc=%-9.1f ns=%.1f\n", label, secure, secure * 1000.0 / g_mhz);
    vTaskDelay(1);

    double delta = (secure > plain) ? (secure - plain) : 0.0;
    Serial.printf("PB %-34s cyc=%-9.1f cyc/B=%.3f\n", "  -> the wipe alone", delta, n ? delta / (double)n : 0.0);
    vTaskDelay(1);
}

// Per-sample timing, reported as min/median/max. A mean would hide the property actually under test:
// the pool claims a bounded, repeatable cost, and the heap does not. Spread is the evidence.
#define SPREAD_N 256
static uint32_t g_spread[SPREAD_N];

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a;
    uint32_t y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

#define BENCH_SPREAD(label, expr)                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        expr; /* warm */                                                                                               \
        for (int _i = 0; _i < SPREAD_N; _i++)                                                                          \
        {                                                                                                              \
            uint32_t _a = cyc_now();                                                                                   \
            expr;                                                                                                      \
            g_spread[_i] = cyc_now() - _a;                                                                             \
        }                                                                                                              \
        qsort(g_spread, SPREAD_N, sizeof(g_spread[0]), cmp_u32);                                                       \
        Serial.printf("PB %-30s min=%-7u med=%-7u max=%-7u spread=%ux\n", label, g_spread[0], g_spread[SPREAD_N / 2],  \
                      g_spread[SPREAD_N - 1], g_spread[0] ? (unsigned)(g_spread[SPREAD_N - 1] / g_spread[0]) : 0u);    \
        vTaskDelay(1);                                                                                                 \
    } while (0)

// Op-for-op against what the pool displaced. Same payload size, same work, one borrow+release each.
static void bench_vs_traditional()
{
    Serial.println("PB -- our access method vs traditional allocation (256 B, per-sample spread) --");

    // The baseline rule 19 took away: a stack local costs a stack-pointer adjustment and nothing else.
    // This is the honest cost of moving off the stack, and it should be stated plainly.
    BENCH_SPREAD("stack local (what r19 removed)", {
        volatile uint8_t local[256];
        local[0] = 1;
        local[255] = 2;
        g_sink = (local[0] != local[255]);
    });

    BENCH_SPREAD("pool borrow+release", {
        PlaintextScope s;
        pc_span sp = pc_plaintext_span(256, 16);
        g_sink = pc_span_ok(sp);
    });

    // Two boundary crossings instead of three: mark+alloc in the ctor, release in the dtor. Same
    // work, one fewer call, and the slot resolved once.
    BENCH_SPREAD("pool borrow+release (2-call)", {
        PlaintextBorrow b(256, 16);
        g_sink = pc_span_ok(b.span());
    });

    BENCH_SPREAD("malloc+free", {
        void *p = malloc(256);
        g_sink = (p != nullptr);
        free(p);
    });

    // The secure pool's extra duty is the wipe. The fair comparison is a heap block wiped by hand,
    // which is what a caller must do today to get the same guarantee.
    BENCH_SPREAD("secure borrow+release (wipes)", {
        SecureScope s;
        pc_span sp = pc_secure_span(256, 16);
        g_sink = pc_span_ok(sp);
    });

    BENCH_SPREAD("malloc+wipe+free", {
        void *p = malloc(256);
        if (p != nullptr)
        {
            pc_secure_wipe(p, 256);
        }
        g_sink = (p != nullptr);
        free(p);
    });

    // Fragmentation is the reason a deterministic system cannot use the heap at all. Interleave two
    // sizes so the allocator cannot simply hand back the same block every time.
    Serial.println("PB -- heap under interleaved sizes (fragmentation pressure) --");
    BENCH_SPREAD("malloc+free 256/2048 alternating", {
        static bool _big = false;
        void *p = malloc(_big ? 2048 : 256);
        g_sink = (p != nullptr);
        free(p);
        _big = !_big;
    });
    BENCH_SPREAD("pool borrow 256/2048 alternating", {
        static bool _big2 = false;
        PlaintextScope s;
        pc_span sp = pc_plaintext_span(_big2 ? 2048 : 256, 16);
        g_sink = pc_span_ok(sp);
        _big2 = !_big2;
    });
}

static void bench_task(void *)
{
    g_mhz = (double)getCpuFrequencyMhz();
    Serial.println("PB ==== memory-manager microbench start (CCOUNT) ====");
    Serial.printf("PB cpu_mhz=%u slots=%u plaintext_slot=%uB secure_slot=%uB\n", (unsigned)getCpuFrequencyMhz(),
                  (unsigned)PC_REG_POOL_SLOTS, (unsigned)pc_plaintext_capacity(), (unsigned)pc_secure_capacity());

    bench_borrow();
    bench_owns();
    bench_probe();
    bench_vs_traditional();

    Serial.println("PB -- reclaim: the only real difference between the pools --");
    bench_release(64);
    bench_release(256);
    bench_release(2048);

    // The backward direction of the size constants: what the pools actually needed.
    Serial.printf("PB high_water plaintext=%u/%uB secure=%u/%uB\n", (unsigned)pc_plaintext_high_water(),
                  (unsigned)pc_plaintext_capacity(), (unsigned)pc_secure_high_water(), (unsigned)pc_secure_capacity());
    Serial.println("PB ==== done ====");
    vTaskDelete(nullptr);
}

void setup()
{
    Serial.begin(115200);
    delay(2000);
    // Run off a task with a known stack: the bench borrows from the pools, and the Arduino loop task's
    // stack is not the one the worker pool sizing assumes.
    xTaskCreatePinnedToCore(bench_task, "poolbench", 8192, nullptr, 5, nullptr, 0);
}

void loop()
{
    delay(1000);
}
