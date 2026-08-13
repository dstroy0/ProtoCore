// CONTROL: the shape transport/tcp/common.h has today.
// The slot struct is complete in the header and the ring accessors are static inline, so a caller
// in any TU compiles the ring math straight into its own code.
#ifndef PUB_H
#define PUB_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#define RT_CAP 1024u
#define RT_SLOTS 8u

typedef struct
{
    uint8_t id;
    _Atomic size_t head;
    _Atomic size_t tail;
    uint8_t buf[RT_CAP];
} PubConn;

extern PubConn pub_pool[RT_SLOTS];

static inline size_t pub_available(uint8_t slot)
{
    const PubConn *c = &pub_pool[slot];
    size_t h = atomic_load_explicit(&c->head, memory_order_acquire);
    size_t t = atomic_load_explicit(&c->tail, memory_order_acquire);
    return (h - t) & (RT_CAP - 1u);
}

static inline int pub_read_byte(uint8_t slot, uint8_t *out)
{
    PubConn *c = &pub_pool[slot];
    size_t t = atomic_load_explicit(&c->tail, memory_order_acquire);
    if (t == atomic_load_explicit(&c->head, memory_order_acquire))
    {
        return 0;
    }
    *out = c->buf[t];
    atomic_store_explicit(&c->tail, (t + 1u) & (RT_CAP - 1u), memory_order_release);
    return 1;
}

void pub_produce(uint8_t slot, const uint8_t *src, size_t n);

#endif
