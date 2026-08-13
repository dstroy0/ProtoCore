// The consumer, in a DIFFERENT translation unit from the accessor bodies - which is the whole
// question. This models the request byte path: drain the ring a byte at a time.
#include "opq.h"

// noinline so the loop survives LTO as its own symbol. Without this, LTO folds hot_opq into main
// and the disassembly has nothing to measure - which silently reads as "0 calls" whether the
// accessors were inlined or not.
__attribute__((noinline)) size_t hot_opq(uint8_t slot, int n)
{
    size_t sum = 0;
    uint8_t b;
    for (int i = 0; i < n; i++)
    {
        if (rt_available(slot) == 0)
        {
            break;
        }
        if (rt_read_byte(slot, &b))
        {
            sum += b;
        }
    }
    return sum;
}
