// Does sizeof(Internal) being a power of two change the index math in `&s_internal[slot]`?
//
// The claim under test: a non-power-of-two stride forces a multiply on every accessor, so Internal
// should be padded. The counter-claim: natural alignment padding already rounds the struct up, so
// the pad is usually a no-op.
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#define SLOTS 8u
#define CAP 1024u

struct Storage
{
    uint8_t buf[CAP];
};

// Natural layout: 1 (+7 pad) + 8 + 8 + 8. Whatever that comes to is what it comes to.
struct Nat
{
    uint8_t id;
    _Atomic size_t head;
    _Atomic size_t tail;
    struct Storage *store;
};

// One extra word past the natural size, to push it off a power of two.
struct Odd
{
    uint8_t id;
    _Atomic size_t head;
    _Atomic size_t tail;
    struct Storage *store;
    uint64_t extra;
};

// The same, explicitly padded back up to the next power of two.
struct Padded
{
    uint8_t id;
    _Atomic size_t head;
    _Atomic size_t tail;
    struct Storage *store;
    uint64_t extra;
    uint8_t _pad[64 - 40]; // the fields above total 40, not 48 - counted by hand, then measured
};

_Static_assert(sizeof(struct Nat) == 32, "Nat is expected to land on 32 naturally");
_Static_assert(sizeof(struct Odd) == 40, "Odd is expected to land on 40");
_Static_assert(sizeof(struct Padded) == 64, "Padded is expected to land on 64");

static struct Nat s_nat[SLOTS];
static struct Odd s_odd[SLOTS];
static struct Padded s_pad[SLOTS];

__attribute__((noinline)) size_t nat_avail(uint8_t slot)
{
    const struct Nat *restrict c = &s_nat[slot];
    size_t h = atomic_load_explicit(&c->head, memory_order_acquire);
    size_t t = atomic_load_explicit(&c->tail, memory_order_acquire);
    return (h - t) & (CAP - 1u);
}

__attribute__((noinline)) size_t odd_avail(uint8_t slot)
{
    const struct Odd *restrict c = &s_odd[slot];
    size_t h = atomic_load_explicit(&c->head, memory_order_acquire);
    size_t t = atomic_load_explicit(&c->tail, memory_order_acquire);
    return (h - t) & (CAP - 1u);
}

__attribute__((noinline)) size_t pad_avail(uint8_t slot)
{
    const struct Padded *restrict c = &s_pad[slot];
    size_t h = atomic_load_explicit(&c->head, memory_order_acquire);
    size_t t = atomic_load_explicit(&c->tail, memory_order_acquire);
    return (h - t) & (CAP - 1u);
}
