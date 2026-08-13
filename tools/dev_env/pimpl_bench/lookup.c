// Bitmask walk versus linear equality scan, for the pcb->slot lookup.
//
// The question: to find which pool slot holds a given control block, is it faster to compare every
// slot's pointer, or to walk only the occupied slots via a ctz over the free bitmap?
//
// Both answer the same thing. The scan does N compares regardless of how many slots are live; the
// mask does one ctz plus one compare per LIVE slot. So the crossover is occupancy, and that is what
// this sweeps: full, half, and one-live pools, each for a hit and for a miss.
//
// Reported as the MINIMUM over trials - the run least disturbed by scheduling.

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef SLOTS
#define SLOTS 8
#endif

#define ITERS 200000
#define TRIALS 9

typedef struct
{
    void *pcb;
    uint8_t pad[24]; // keep the stride realistic; the real slot is not just a pointer
} Slot;

static Slot pool[SLOTS];
static _Atomic uint32_t freemask;

static double now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static inline uint32_t slot_all(unsigned n)
{
    return (n >= 32u) ? 0xFFFFFFFFu : ((1u << n) - 1u);
}

__attribute__((noinline)) static int scan_lookup(const void *pcb)
{
    for (int i = 0; i < SLOTS; i++)
    {
        if (pool[i].pcb == pcb)
        {
            return i;
        }
    }
    return -1;
}

__attribute__((noinline)) static int mask_lookup(const void *pcb)
{
    uint32_t m = ~atomic_load_explicit(&freemask, memory_order_acquire) & slot_all(SLOTS);
    while (m != 0u)
    {
        int i = __builtin_ctz(m);
        if (pool[i].pcb == pcb)
        {
            return i;
        }
        m &= ~(1u << i);
    }
    return -1;
}

// Same walk without the atomic load, to price the acquire separately.
__attribute__((noinline)) static int mask_lookup_plain(const void *pcb, uint32_t occupied)
{
    uint32_t m = occupied;
    while (m != 0u)
    {
        int i = __builtin_ctz(m);
        if (pool[i].pcb == pcb)
        {
            return i;
        }
        m &= ~(1u << i);
    }
    return -1;
}

static void occupy(int live)
{
    uint32_t f = slot_all(SLOTS);
    memset(pool, 0, sizeof(pool));
    for (int i = 0; i < live; i++)
    {
        pool[i].pcb = (void *)(uintptr_t)(0x1000 + i);
        f &= ~(1u << i); // not free
    }
    atomic_store_explicit(&freemask, f, memory_order_release);
}

static double timeit(int (*fn)(const void *), const void *target)
{
    double best = 1e30;
    volatile int sink = 0;
    for (int t = 0; t < TRIALS; t++)
    {
        double t0 = now_ns();
        for (int i = 0; i < ITERS; i++)
        {
            sink += fn(target);
        }
        double dt = now_ns() - t0;
        if (dt < best)
        {
            best = dt;
        }
    }
    (void)sink;
    return best / ITERS;
}

static double timeit_plain(const void *target, uint32_t occupied)
{
    double best = 1e30;
    volatile int sink = 0;
    for (int t = 0; t < TRIALS; t++)
    {
        double t0 = now_ns();
        for (int i = 0; i < ITERS; i++)
        {
            sink += mask_lookup_plain(target, occupied);
        }
        double dt = now_ns() - t0;
        if (dt < best)
        {
            best = dt;
        }
    }
    (void)sink;
    return best / ITERS;
}

int main(void)
{
    const int levels[] = {SLOTS, SLOTS / 2, 1};
    printf("slots=%d\n", SLOTS);
    printf("%-6s %-5s | %-10s %-10s %-10s | %s\n", "live", "case", "scan ns", "mask ns", "mask-noat", "winner");
    for (unsigned L = 0; L < sizeof(levels) / sizeof(levels[0]); L++)
    {
        int live = levels[L] > 0 ? levels[L] : 1;
        occupy(live);
        uint32_t occupied = ~atomic_load_explicit(&freemask, memory_order_acquire) & slot_all(SLOTS);

        // hit: the LAST live slot, so both do their full work before answering
        const void *hit = (const void *)(uintptr_t)(0x1000 + live - 1);
        const void *miss = (const void *)(uintptr_t)0xDEAD;

        double s_hit = timeit(scan_lookup, hit), m_hit = timeit(mask_lookup, hit);
        double p_hit = timeit_plain(hit, occupied);
        double s_mis = timeit(scan_lookup, miss), m_mis = timeit(mask_lookup, miss);
        double p_mis = timeit_plain(miss, occupied);

        printf("%-6d %-5s | %-10.3f %-10.3f %-10.3f | %s\n", live, "hit", s_hit, m_hit, p_hit,
               m_hit < s_hit ? "mask" : "scan");
        printf("%-6d %-5s | %-10.3f %-10.3f %-10.3f | %s\n", live, "miss", s_mis, m_mis, p_mis,
               m_mis < s_mis ? "mask" : "scan");
    }
    return 0;
}
