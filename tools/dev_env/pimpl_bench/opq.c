// The completed types and the accessor bodies. Nothing here is visible to a caller.
#include "opq.h"
#include <stdatomic.h>

// Bulk backing, split from the hot state so a sweep over RtInternal does not stride a ring.
struct RtStorage
{
    uint8_t buf[RT_CAP];
};

struct RtInternal
{
    uint8_t id;
    _Atomic size_t head;
    _Atomic size_t tail;
    struct RtStorage *store;
};

static struct RtStorage s_storage[RT_SLOTS];
static struct RtInternal s_internal[RT_SLOTS];

// Bound once at init, the way protocol.c would bind the pool.
__attribute__((constructor)) static void rt_bind(void)
{
    for (unsigned i = 0; i < RT_SLOTS; i++)
    {
        s_internal[i].id = (uint8_t)i;
        s_internal[i].store = &s_storage[i];
    }
}

size_t rt_available(uint8_t slot)
{
    const struct RtInternal *restrict c = &s_internal[slot];
    size_t h = atomic_load_explicit(&c->head, memory_order_acquire);
    size_t t = atomic_load_explicit(&c->tail, memory_order_acquire);
    return (h - t) & (RT_CAP - 1u);
}

int rt_read_byte(uint8_t slot, uint8_t *out)
{
    struct RtInternal *restrict c = &s_internal[slot];
    size_t t = atomic_load_explicit(&c->tail, memory_order_acquire);
    if (t == atomic_load_explicit(&c->head, memory_order_acquire))
    {
        return 0;
    }
    *out = c->store->buf[t];
    atomic_store_explicit(&c->tail, (t + 1u) & (RT_CAP - 1u), memory_order_release);
    return 1;
}

void rt_produce(uint8_t slot, const uint8_t *src, size_t n)
{
    struct RtInternal *restrict c = &s_internal[slot];
    size_t h = atomic_load_explicit(&c->head, memory_order_acquire);
    for (size_t i = 0; i < n; i++)
    {
        c->store->buf[h] = src[i];
        h = (h + 1u) & (RT_CAP - 1u);
    }
    atomic_store_explicit(&c->head, h, memory_order_release);
}
