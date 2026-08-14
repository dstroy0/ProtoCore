// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the shared SPSC ring primitive and its three views (mmgr/ring.h).
//
// No standards body publishes a ring; the expectations here are PROPERTIES, plus one piece of
// arithmetic derived in the comment that carries it.
//
// test_wrap_is_the_modulo_a_power_of_two_capacity_defines is the load-bearing case. Every view in
// this file indexes through PROTOCORE_RING_WRAP, and the mask is only the remainder when the
// capacity is a power of two: for cap == 2^k, i & (cap-1) keeps the low k bits of i, which is
// exactly i - cap*floor(i/cap). The test states that identity against C's own % over three full
// laps, so the day a capacity stops being a power of two the index math fails here.

#include "mmgr/ring.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

#define CAP 16u
#define SEGS 4u
#define SEG_SIZE 8u

static uint8_t g_buf[CAP];
static uint8_t g_segs[SEGS * SEG_SIZE];

// A power-of-two capacity makes the wrap a mask, and the mask is the remainder. Three laps, so a
// value above one lap is covered as well as one inside it.
void test_wrap_is_the_modulo_a_power_of_two_capacity_defines(void)
{
    static const size_t POW2[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 1024};
    for (size_t k = 0; k < sizeof(POW2) / sizeof(POW2[0]); k++)
    {
        const size_t cap = POW2[k];
        TEST_ASSERT_TRUE(PROTOCORE_RING_POW2(cap));
        for (size_t i = 0; i < 3u * cap; i++)
        {
            TEST_ASSERT_EQUAL_size_t(i % cap, PROTOCORE_RING_WRAP(i, cap));
        }
    }
    static const size_t NOT_POW2[] = {3, 5, 6, 7, 9, 12, 100, 1000};
    for (size_t k = 0; k < sizeof(NOT_POW2) / sizeof(NOT_POW2[0]); k++)
    {
        TEST_ASSERT_FALSE(PROTOCORE_RING_POW2(NOT_POW2[k]));
    }
}

// One slot is reserved so a full ring is distinguishable from an empty one: available and free
// always sum to cap-1, whatever the indices sit at, including after a wrap.
void test_available_and_free_partition_the_ring(void)
{
    for (size_t t = 0; t < CAP; t++)
    {
        for (size_t used = 0; used < CAP; used++)
        {
            _Atomic size_t tail = t;
            _Atomic size_t head = PROTOCORE_RING_WRAP(t + used, CAP);
            const size_t avail = protocore_ring_available(&head, &tail, CAP);
            const size_t room = protocore_ring_free(&head, &tail, CAP);
            TEST_ASSERT_EQUAL_size_t(used, avail);
            TEST_ASSERT_EQUAL_size_t(CAP - 1u, avail + room);
        }
    }
    // Never more than cap-1 bytes can be written, so a producer cannot make head meet tail.
    _Atomic size_t head = 0;
    _Atomic size_t tail = 0;
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ring_available(&head, &tail, CAP));
    TEST_ASSERT_EQUAL_size_t(CAP - 1u, protocore_ring_free(&head, &tail, CAP));
}

// The consumer sees bytes in the order the producer wrote them, and an empty ring says so rather
// than handing back a stale byte.
void test_read_byte_pops_in_fifo_order_and_reports_empty(void)
{
    _Atomic size_t head = 0;
    _Atomic size_t tail = 0;
    uint8_t out = 0xFF;

    TEST_ASSERT_FALSE(protocore_ring_read_byte(g_buf, CAP, &head, &tail, &out));
    TEST_ASSERT_EQUAL_HEX8(0xFF, out); // untouched on the empty ring

    static const uint8_t MSG[5] = {0x11, 0x22, 0x33, 0x44, 0x55};
    head = protocore_ring_write_span(g_buf, CAP, head, MSG, sizeof MSG);
    for (size_t i = 0; i < sizeof MSG; i++)
    {
        TEST_ASSERT_TRUE(protocore_ring_read_byte(g_buf, CAP, &head, &tail, &out));
        TEST_ASSERT_EQUAL_HEX8(MSG[i], out);
    }
    TEST_ASSERT_FALSE(protocore_ring_read_byte(g_buf, CAP, &head, &tail, &out));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ring_available(&head, &tail, CAP));
}

// A bulk read takes min(maxn, available) and leaves the tail exactly that far on, so the bytes it
// did not take are still there for the next call.
void test_read_takes_what_is_asked_for_and_advances_the_tail(void)
{
    _Atomic size_t head = 0;
    _Atomic size_t tail = 0;
    static const uint8_t MSG[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t got[16];

    head = protocore_ring_write_span(g_buf, CAP, head, MSG, sizeof MSG);

    memset(got, 0xA5, sizeof got);
    TEST_ASSERT_EQUAL_size_t(3u, protocore_ring_read(g_buf, CAP, &head, &tail, got, 3));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MSG, got, 3);
    TEST_ASSERT_EQUAL_HEX8(0xA5, got[3]); // nothing written past what was asked for
    TEST_ASSERT_EQUAL_size_t(5u, protocore_ring_available(&head, &tail, CAP));

    // Asking for more than is there yields what is there, and no more.
    memset(got, 0xA5, sizeof got);
    TEST_ASSERT_EQUAL_size_t(5u, protocore_ring_read(g_buf, CAP, &head, &tail, got, sizeof got));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&MSG[3], got, 5);
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ring_available(&head, &tail, CAP));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ring_read(g_buf, CAP, &head, &tail, got, sizeof got));
}

// Peek reads ahead of the tail without moving it, so the same bytes come back twice; consume is the
// separate step that drops them.
void test_peek_does_not_consume_and_consume_advances(void)
{
    _Atomic size_t head = 0;
    _Atomic size_t tail = 0;
    static const uint8_t MSG[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
    uint8_t got[6];

    head = protocore_ring_write_span(g_buf, CAP, head, MSG, sizeof MSG);

    protocore_ring_peek(g_buf, CAP, &tail, 0, got, sizeof MSG);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MSG, got, sizeof MSG);
    TEST_ASSERT_EQUAL_size_t(sizeof MSG, protocore_ring_available(&head, &tail, CAP));

    // The same peek again, and one from an offset: a header read then the body behind it.
    protocore_ring_peek(g_buf, CAP, &tail, 0, got, sizeof MSG);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MSG, got, sizeof MSG);
    protocore_ring_peek(g_buf, CAP, &tail, 4, got, 2);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&MSG[4], got, 2);

    protocore_ring_consume(&tail, CAP, 4);
    TEST_ASSERT_EQUAL_size_t(2u, protocore_ring_available(&head, &tail, CAP));
    protocore_ring_peek(g_buf, CAP, &tail, 0, got, 2);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&MSG[4], got, 2);
}

// A span that crosses the buffer end lands in two pieces and reads back as one, and a peek across
// the same seam returns it in order. This is the case a mask index exists for.
void test_a_span_across_the_wrap_reads_back_whole(void)
{
    static const uint8_t MSG[10] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9};
    uint8_t got[10];

    // Start the fill 12 bytes in, so 4 bytes land before the end and 6 after the wrap.
    for (size_t start = 0; start < CAP; start++)
    {
        _Atomic size_t head = start;
        _Atomic size_t tail = start;
        memset(g_buf, 0, sizeof g_buf);

        TEST_ASSERT_TRUE(protocore_ring_free(&head, &tail, CAP) >= sizeof MSG);
        head = protocore_ring_write_span(g_buf, CAP, head, MSG, sizeof MSG);
        TEST_ASSERT_EQUAL_size_t(PROTOCORE_RING_WRAP(start + sizeof MSG, CAP), (size_t)head);
        TEST_ASSERT_EQUAL_size_t(sizeof MSG, protocore_ring_available(&head, &tail, CAP));

        memset(got, 0, sizeof got);
        protocore_ring_peek(g_buf, CAP, &tail, 0, got, sizeof MSG);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(MSG, got, sizeof MSG);

        memset(got, 0, sizeof got);
        TEST_ASSERT_EQUAL_size_t(sizeof MSG, protocore_ring_read(g_buf, CAP, &head, &tail, got, sizeof got));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(MSG, got, sizeof MSG);
    }
}

// A segment being filled is invisible to the consumer until it is published, so a half-written
// message is never sent.
void test_a_segment_is_invisible_until_it_is_published(void)
{
    _Atomic size_t claim = 0;
    _Atomic size_t rel = 0;
    size_t idx = (size_t)-1;

    TEST_ASSERT_FALSE(protocore_seg_front(&claim, &rel, SEGS, &idx));
    TEST_ASSERT_TRUE(protocore_seg_next(&claim, &rel, SEGS, &idx));
    TEST_ASSERT_EQUAL_size_t(0u, idx);

    memset(protocore_seg_at(g_segs, SEG_SIZE, idx), 0x5A, SEG_SIZE);
    TEST_ASSERT_FALSE(protocore_seg_front(&claim, &rel, SEGS, &idx)); // filled, not yet published
    TEST_ASSERT_EQUAL_size_t(0u, protocore_seg_inflight(&claim, &rel));

    protocore_seg_publish(&claim);
    TEST_ASSERT_EQUAL_size_t(1u, protocore_seg_inflight(&claim, &rel));
    TEST_ASSERT_TRUE(protocore_seg_front(&claim, &rel, SEGS, &idx));
    TEST_ASSERT_EQUAL_size_t(0u, idx);
    TEST_ASSERT_EQUAL_HEX8(0x5A, protocore_seg_at(g_segs, SEG_SIZE, idx)[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5A, protocore_seg_at(g_segs, SEG_SIZE, idx)[SEG_SIZE - 1]);
}

// Segments come back in the order they were published, and a producer with every segment in flight
// is refused rather than handed one the wire is still reading.
void test_segments_release_in_order_and_a_full_ring_refuses(void)
{
    _Atomic size_t claim = 0;
    _Atomic size_t rel = 0;
    size_t idx = 0;

    for (size_t i = 0; i < SEGS; i++)
    {
        TEST_ASSERT_TRUE(protocore_seg_next(&claim, &rel, SEGS, &idx));
        TEST_ASSERT_EQUAL_size_t(i, idx);
        memset(protocore_seg_at(g_segs, SEG_SIZE, idx), (int)(0x10 + i), SEG_SIZE);
        protocore_seg_publish(&claim);
    }
    TEST_ASSERT_EQUAL_size_t(SEGS, protocore_seg_inflight(&claim, &rel));
    TEST_ASSERT_FALSE(protocore_seg_next(&claim, &rel, SEGS, &idx)); // every segment in flight

    for (size_t i = 0; i < SEGS; i++)
    {
        TEST_ASSERT_TRUE(protocore_seg_front(&claim, &rel, SEGS, &idx));
        TEST_ASSERT_EQUAL_size_t(i, idx);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(0x10 + i), protocore_seg_at(g_segs, SEG_SIZE, idx)[0]);
        protocore_seg_release(&rel);
    }
    TEST_ASSERT_FALSE(protocore_seg_front(&claim, &rel, SEGS, &idx));

    // The indices keep counting; the segment they name is the count masked by nsegs, so the ring
    // reuses slot 0 on the next lap.
    TEST_ASSERT_TRUE(protocore_seg_next(&claim, &rel, SEGS, &idx));
    TEST_ASSERT_EQUAL_size_t(0u, idx);
}

// Exactly one caller takes a slot. The loser is told so and moves on rather than sharing it, and a
// drop makes the slot available again.
void test_slot_take_is_won_by_exactly_one_caller(void)
{
    _Atomic uint32_t held = 0;
    TEST_ASSERT_TRUE(protocore_slot_take(&held, 3));
    TEST_ASSERT_FALSE(protocore_slot_take(&held, 3));
    TEST_ASSERT_TRUE(protocore_slot_take(&held, 4)); // a different slot is unaffected
    protocore_slot_drop(&held, 3);
    TEST_ASSERT_TRUE(protocore_slot_take(&held, 3));
    TEST_ASSERT_FALSE(protocore_slot_take(&held, 4));
}

// A hold records the region the wire is still reading. The loser of a hold must not overwrite it:
// that region is the pointer the egress walks, so clobbering it redirects the transmit.
void test_a_losing_hold_cannot_redirect_the_keepout(void)
{
    _Atomic uint32_t held = 0;
    protocore_cspan keepout[PROTOCORE_RING_SLOTS_MAX];
    static const uint8_t WINNER[4] = {1, 2, 3, 4};
    static const uint8_t LOSER[4] = {9, 9, 9, 9};

    memset(keepout, 0, sizeof keepout);
    TEST_ASSERT_TRUE(protocore_slot_hold(&held, keepout, 2, WINNER, sizeof WINNER));
    TEST_ASSERT_FALSE(protocore_slot_hold(&held, keepout, 2, LOSER, sizeof LOSER));

    const protocore_cspan *k = protocore_slot_keepout(keepout, 2);
    TEST_ASSERT_EQUAL_PTR(WINNER, k->buf);
    TEST_ASSERT_EQUAL_size_t(sizeof WINNER, k->len);
    TEST_ASSERT_EQUAL_size_t(0u, k->pos);
    TEST_ASSERT_FALSE(k->err);
}

// Ready is what is marked, minus what is held, minus everything past the pool's own count.
void test_ready_is_marked_minus_held_within_the_count(void)
{
    _Atomic uint32_t mask = 0;
    _Atomic uint32_t held = 0;

    protocore_slot_mark(&mask, 0);
    protocore_slot_mark(&mask, 1);
    protocore_slot_mark(&mask, 5); // past the count used below
    TEST_ASSERT_EQUAL_HEX32(0x03u, protocore_slot_ready(&mask, &held, 4));

    TEST_ASSERT_TRUE(protocore_slot_take(&held, 0));
    TEST_ASSERT_EQUAL_HEX32(0x02u, protocore_slot_ready(&mask, &held, 4));

    protocore_slot_drop(&held, 0);
    protocore_slot_clear(&mask, 1);
    TEST_ASSERT_EQUAL_HEX32(0x01u, protocore_slot_ready(&mask, &held, 4));

    // With the count raised to reach it, slot 5 is ready too.
    TEST_ASSERT_EQUAL_HEX32(0x21u, protocore_slot_ready(&mask, &held, 8));
}

// The next slot to service is the lowest one set, and an empty mask names none.
void test_slot_next_is_the_lowest_set_and_minus_one_when_empty(void)
{
    TEST_ASSERT_EQUAL_INT32(-1, protocore_slot_next(0u));
    for (int i = 0; i < 32; i++)
    {
        TEST_ASSERT_EQUAL_INT32(i, protocore_slot_next(1u << i));
        // With every higher bit set as well, the answer is still the lowest.
        TEST_ASSERT_EQUAL_INT32(i, protocore_slot_next(0xFFFFFFFFu << i));
    }
    TEST_ASSERT_EQUAL_INT32(2, protocore_slot_next(0x84u));
}

// A slot index the mask cannot address names nothing, so it reads as held and is never handed out.
void test_an_out_of_range_slot_names_nothing(void)
{
    _Atomic uint32_t held = 0;
    TEST_ASSERT_EQUAL_HEX32(0u, protocore_slot_bit(PROTOCORE_RING_SLOTS_MAX));
    TEST_ASSERT_EQUAL_HEX32(0u, protocore_slot_bit(PROTOCORE_RING_SLOTS_MAX + 100u));
    TEST_ASSERT_EQUAL_HEX32(0x80000000u, protocore_slot_bit(PROTOCORE_RING_SLOTS_MAX - 1u));
    TEST_ASSERT_FALSE(protocore_slot_take(&held, PROTOCORE_RING_SLOTS_MAX));
    TEST_ASSERT_EQUAL_HEX32(0u, (uint32_t)held); // and nothing was set trying

    // The count-to-mask side of the same bound: a count at or past the width is every slot.
    TEST_ASSERT_EQUAL_HEX32(0x0Fu, protocore_slot_all(4));
    TEST_ASSERT_EQUAL_HEX32(0x7FFFFFFFu, protocore_slot_all(PROTOCORE_RING_SLOTS_MAX - 1u));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, protocore_slot_all(PROTOCORE_RING_SLOTS_MAX));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, protocore_slot_all(PROTOCORE_RING_SLOTS_MAX + 1u));
    TEST_ASSERT_EQUAL_HEX32(0u, protocore_slot_all(0));
}
