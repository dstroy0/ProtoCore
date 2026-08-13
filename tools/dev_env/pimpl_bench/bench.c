// What the opaque cross-TU call actually costs, in time rather than in instruction counts.
//
// sweep.sh answers WHETHER the call survives. This answers what it is worth: the same ring
// accessors reached the two ways, timed, so the encapsulation is traded against a number.
//
// Two measurements:
//   avail  - available() in a tight loop. A pure read, no state change, so it isolates exactly the
//            thing that changes shape: a load-and-mask inline versus a call to the same.
//   drain  - refill the ring and read it out a byte at a time. Both variants pay the identical
//            refill cost, so the delta is still the accessor.
//
// Reported as the MINIMUM over trials, not the mean: the minimum is the run least disturbed by
// scheduling, and this is a comparison, not a throughput claim.

#include "opq.h"
#include "pub.h"
#include <stdio.h>
#include <time.h>

#define ITERS 200000
#define TRIALS 9
#define FILL 512

static double now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

// Returned, never discarded, so the loops cannot be optimized away.
__attribute__((noinline)) static size_t avail_opq(int iters)
{
    size_t acc = 0;
    for (int i = 0; i < iters; i++)
    {
        acc += rt_available(0);
    }
    return acc;
}

__attribute__((noinline)) static size_t avail_pub(int iters)
{
    size_t acc = 0;
    for (int i = 0; i < iters; i++)
    {
        acc += pub_available(0);
    }
    return acc;
}

__attribute__((noinline)) static size_t drain_opq(const uint8_t *src, int rounds)
{
    size_t acc = 0;
    uint8_t b;
    for (int r = 0; r < rounds; r++)
    {
        rt_produce(0, src, FILL);
        while (rt_available(0) != 0)
        {
            if (rt_read_byte(0, &b))
            {
                acc += b;
            }
        }
    }
    return acc;
}

__attribute__((noinline)) static size_t drain_pub(const uint8_t *src, int rounds)
{
    size_t acc = 0;
    uint8_t b;
    for (int r = 0; r < rounds; r++)
    {
        pub_produce(0, src, FILL);
        while (pub_available(0) != 0)
        {
            if (pub_read_byte(0, &b))
            {
                acc += b;
            }
        }
    }
    return acc;
}

int main(void)
{
    uint8_t src[FILL];
    for (int i = 0; i < FILL; i++)
    {
        src[i] = (uint8_t)i;
    }

    // Leave both rings holding the same bytes so available() reads the same value each way.
    rt_produce(0, src, FILL);
    pub_produce(0, src, FILL);

    size_t sink = 0;
    double best_ao = 1e30, best_ap = 1e30, best_do = 1e30, best_dp = 1e30;

    sink += avail_opq(ITERS); // warm
    sink += avail_pub(ITERS);

    for (int t = 0; t < TRIALS; t++)
    {
        double t0 = now_ns();
        sink += avail_opq(ITERS);
        double t1 = now_ns();
        sink += avail_pub(ITERS);
        double t2 = now_ns();
        if (t1 - t0 < best_ao)
        {
            best_ao = t1 - t0;
        }
        if (t2 - t1 < best_ap)
        {
            best_ap = t2 - t1;
        }
    }

    const int rounds = ITERS / FILL;
    for (int t = 0; t < TRIALS; t++)
    {
        double t0 = now_ns();
        sink += drain_opq(src, rounds);
        double t1 = now_ns();
        sink += drain_pub(src, rounds);
        double t2 = now_ns();
        if (t1 - t0 < best_do)
        {
            best_do = t1 - t0;
        }
        if (t2 - t1 < best_dp)
        {
            best_dp = t2 - t1;
        }
    }

    const double bytes = (double)rounds * FILL;
    printf("avail_opaque_ns %.3f avail_inline_ns %.3f drain_opaque_ns %.3f drain_inline_ns %.3f sink %zu\n",
           best_ao / ITERS, best_ap / ITERS, best_do / bytes, best_dp / bytes, sink);
    return 0;
}
