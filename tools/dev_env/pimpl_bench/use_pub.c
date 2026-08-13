// The control consumer: same loop, but the accessors are static inline from the header.
#include "pub.h"

PubConn pub_pool[RT_SLOTS];

// noinline for the same reason as hot_opq: keep it measurable under LTO.
__attribute__((noinline)) size_t hot_pub(uint8_t slot, int n)
{
    size_t sum = 0;
    uint8_t b;
    for (int i = 0; i < n; i++)
    {
        if (pub_available(slot) == 0)
        {
            break;
        }
        if (pub_read_byte(slot, &b))
        {
            sum += b;
        }
    }
    return sum;
}

void pub_produce(uint8_t slot, const uint8_t *src, size_t n)
{
    PubConn *c = &pub_pool[slot];
    size_t h = atomic_load_explicit(&c->head, memory_order_acquire);
    for (size_t i = 0; i < n; i++)
    {
        c->buf[h] = src[i];
        h = (h + 1u) & (RT_CAP - 1u);
    }
    atomic_store_explicit(&c->head, h, memory_order_release);
}
