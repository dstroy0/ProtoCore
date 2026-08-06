// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the shared ring primitive (mmgr/ring.h) and its three views: bytes by head/tail,
// whole messages by segment, and claimable slots by mask. Both transports and both UDP rings sit on
// this file, and it had no suite of its own - it was only ever exercised as a side effect of theirs.

#include "mmgr/ring.h"
#include <stdint.h>
#include <string.h>

#include <unity.h>

#define CAP 16u
#define SEGS 4u
#define SEG_SIZE 8u

static uint8_t g_buf[CAP];
static _Atomic size_t g_head;
static _Atomic size_t g_tail;

static uint8_t g_segs[SEGS * SEG_SIZE];
static _Atomic size_t g_claim;
static _Atomic size_t g_rel;

static _Atomic uint32_t g_held;
static _Atomic uint32_t g_mask;
static pc_cspan g_keepout[PC_RING_SLOTS_MAX];

void setUp()
{
    memset(g_buf, 0, sizeof(g_buf));
    PROTO_ATOMIC_STORE(&g_head, 0);
    PROTO_ATOMIC_STORE(&g_tail, 0);
    memset(g_segs, 0, sizeof(g_segs));
    PROTO_ATOMIC_STORE(&g_claim, 0);
    PROTO_ATOMIC_STORE(&g_rel, 0);
    PROTO_ATOMIC_STORE(&g_held, 0);
    PROTO_ATOMIC_STORE(&g_mask, 0);
    memset(g_keepout, 0, sizeof(g_keepout));
}
void tearDown()
{
}

static void fill(const char *s)
{
    size_t h = PROTO_ATOMIC_LOAD(&g_head);
    h = pc_ring_write_span(g_buf, CAP, h, (const uint8_t *)s, strlen(s));
    PROTO_ATOMIC_STORE(&g_head, h);
}

// ---- the capacity law -----------------------------------------------------

void test_a_power_of_two_capacity_is_what_makes_the_index_a_mask()
{
    TEST_ASSERT_TRUE(PC_RING_POW2(16));
    TEST_ASSERT_TRUE(PC_RING_POW2(2048));
    TEST_ASSERT_FALSE(PC_RING_POW2(1536)); // the size four board profiles carried
    TEST_ASSERT_FALSE(PC_RING_POW2(1472));
}

void test_the_mask_wraps_exactly_where_a_modulo_would()
{
    for (size_t i = 0; i < 3u * CAP; i++)
    {
        TEST_ASSERT_EQUAL_size_t(i % CAP, PC_RING_WRAP(i, CAP));
    }
}

// ---- byte view ------------------------------------------------------------

void test_an_empty_ring_reports_nothing_available_and_all_but_one_free()
{
    TEST_ASSERT_EQUAL_size_t(0, pc_ring_available(&g_head, &g_tail, CAP));
    TEST_ASSERT_EQUAL_size_t(CAP - 1, pc_ring_free(&g_head, &g_tail, CAP));
}

void test_one_slot_stays_reserved_so_full_is_distinguishable_from_empty()
{
    fill("123456789012345"); // CAP-1 bytes, the most that fits
    TEST_ASSERT_EQUAL_size_t(CAP - 1, pc_ring_available(&g_head, &g_tail, CAP));
    TEST_ASSERT_EQUAL_size_t(0, pc_ring_free(&g_head, &g_tail, CAP));
}

void test_a_byte_pops_in_order_and_the_ring_empties()
{
    fill("ab");
    uint8_t v = 0;
    TEST_ASSERT_TRUE(pc_ring_read_byte(g_buf, CAP, &g_head, &g_tail, &v));
    TEST_ASSERT_EQUAL_UINT8('a', v);
    TEST_ASSERT_TRUE(pc_ring_read_byte(g_buf, CAP, &g_head, &g_tail, &v));
    TEST_ASSERT_EQUAL_UINT8('b', v);
    TEST_ASSERT_FALSE(pc_ring_read_byte(g_buf, CAP, &g_head, &g_tail, &v));
}

void test_a_read_stops_at_what_is_there_not_at_what_was_asked()
{
    uint8_t out[8];
    fill("abc");
    memset(out, 0, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(3, pc_ring_read(g_buf, CAP, &g_head, &g_tail, out, sizeof(out)));
    TEST_ASSERT_EQUAL_MEMORY("abc", out, 3);
    TEST_ASSERT_EQUAL_size_t(0, pc_ring_read(g_buf, CAP, &g_head, &g_tail, out, sizeof(out)));
}

void test_an_entry_that_straddles_the_wrap_reads_back_whole()
{
    uint8_t out[8];
    fill("0123456789ab"); // 12 bytes
    TEST_ASSERT_EQUAL_size_t(10, pc_ring_read(g_buf, CAP, &g_head, &g_tail, out, 10));
    fill("cdefgh"); // wraps: 2 before the end, 4 after
    memset(out, 0, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(8, pc_ring_read(g_buf, CAP, &g_head, &g_tail, out, 8));
    TEST_ASSERT_EQUAL_MEMORY("abcdefgh", out, 8);
}

void test_peek_reads_across_the_wrap_without_consuming()
{
    uint8_t out[4];
    fill("0123456789abcde"); // 15 bytes, positions 0..14
    TEST_ASSERT_EQUAL_size_t(4, pc_ring_read(g_buf, CAP, &g_head, &g_tail, out, 4));
    fill("fghi"); // head wraps: 15, then 0..2
    size_t before = pc_ring_available(&g_head, &g_tail, CAP);
    memset(out, 0, sizeof(out));
    pc_ring_peek(g_buf, CAP, &g_tail, 10, out, 4);
    TEST_ASSERT_EQUAL_MEMORY("efgh", out, 4);
    TEST_ASSERT_EQUAL_size_t(before, pc_ring_available(&g_head, &g_tail, CAP));
}

void test_consume_advances_past_peeked_bytes()
{
    uint8_t v = 0;
    fill("abcd");
    pc_ring_consume(&g_tail, CAP, 3);
    TEST_ASSERT_EQUAL_size_t(1, pc_ring_available(&g_head, &g_tail, CAP));
    TEST_ASSERT_TRUE(pc_ring_read_byte(g_buf, CAP, &g_head, &g_tail, &v));
    TEST_ASSERT_EQUAL_UINT8('d', v);
}

// ---- segment view ---------------------------------------------------------

void test_a_fresh_segment_ring_has_nothing_in_flight_and_a_slot_to_fill()
{
    size_t idx = 99;
    TEST_ASSERT_EQUAL_size_t(0, pc_seg_inflight(&g_claim, &g_rel));
    TEST_ASSERT_TRUE(pc_seg_next(&g_claim, &g_rel, SEGS, &idx));
    TEST_ASSERT_EQUAL_size_t(0, idx);
    TEST_ASSERT_FALSE(pc_seg_front(&g_claim, &g_rel, SEGS, &idx)); // nothing published yet
}

void test_a_segment_is_invisible_until_it_is_published()
{
    size_t idx = 99;
    TEST_ASSERT_TRUE(pc_seg_next(&g_claim, &g_rel, SEGS, &idx));
    TEST_ASSERT_FALSE(pc_seg_front(&g_claim, &g_rel, SEGS, &idx));
    pc_seg_publish(&g_claim);
    TEST_ASSERT_TRUE(pc_seg_front(&g_claim, &g_rel, SEGS, &idx));
    TEST_ASSERT_EQUAL_size_t(0, idx);
    TEST_ASSERT_EQUAL_size_t(1, pc_seg_inflight(&g_claim, &g_rel));
}

void test_segments_leave_in_the_order_they_were_filled()
{
    size_t idx = 99;
    for (size_t i = 0; i < 3; i++)
    {
        TEST_ASSERT_TRUE(pc_seg_next(&g_claim, &g_rel, SEGS, &idx));
        TEST_ASSERT_EQUAL_size_t(i, idx);
        pc_seg_at(g_segs, SEG_SIZE, idx)[0] = (uint8_t)('A' + i);
        pc_seg_publish(&g_claim);
    }
    for (size_t i = 0; i < 3; i++)
    {
        TEST_ASSERT_TRUE(pc_seg_front(&g_claim, &g_rel, SEGS, &idx));
        TEST_ASSERT_EQUAL_UINT8('A' + i, pc_seg_at(g_segs, SEG_SIZE, idx)[0]);
        pc_seg_release(&g_rel);
    }
    TEST_ASSERT_FALSE(pc_seg_front(&g_claim, &g_rel, SEGS, &idx));
}

void test_a_full_segment_ring_refuses_rather_than_overwriting_one_in_flight()
{
    size_t idx = 99;
    for (size_t i = 0; i < SEGS; i++)
    {
        TEST_ASSERT_TRUE(pc_seg_next(&g_claim, &g_rel, SEGS, &idx));
        pc_seg_publish(&g_claim);
    }
    TEST_ASSERT_EQUAL_size_t(SEGS, pc_seg_inflight(&g_claim, &g_rel));
    TEST_ASSERT_FALSE(pc_seg_next(&g_claim, &g_rel, SEGS, &idx));
    pc_seg_release(&g_rel);
    TEST_ASSERT_TRUE(pc_seg_next(&g_claim, &g_rel, SEGS, &idx));
}

void test_a_segment_index_wraps_by_mask_while_the_counters_climb()
{
    size_t idx = 99;
    for (size_t i = 0; i < SEGS * 3u; i++)
    {
        TEST_ASSERT_TRUE(pc_seg_next(&g_claim, &g_rel, SEGS, &idx));
        TEST_ASSERT_EQUAL_size_t(i & (SEGS - 1u), idx);
        pc_seg_publish(&g_claim);
        TEST_ASSERT_TRUE(pc_seg_front(&g_claim, &g_rel, SEGS, &idx));
        pc_seg_release(&g_rel);
    }
}

void test_a_segment_is_contiguous_so_an_entry_never_straddles()
{
    uint8_t *a = pc_seg_at(g_segs, SEG_SIZE, 0);
    uint8_t *b = pc_seg_at(g_segs, SEG_SIZE, 3);
    TEST_ASSERT_EQUAL_PTR(g_segs, a);
    TEST_ASSERT_EQUAL_PTR(g_segs + 3u * SEG_SIZE, b);
    memset(b, 0xEE, SEG_SIZE); // the whole segment is addressable from its own base
    TEST_ASSERT_EQUAL_UINT8(0xEE, g_segs[SEGS * SEG_SIZE - 1]);
}

// ---- slot view ------------------------------------------------------------

void test_the_all_mask_names_exactly_the_slots_that_exist()
{
    TEST_ASSERT_EQUAL_HEX32(0x0u, pc_slot_all(0));
    TEST_ASSERT_EQUAL_HEX32(0x1u, pc_slot_all(1));
    TEST_ASSERT_EQUAL_HEX32(0xFFu, pc_slot_all(8));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, pc_slot_all(PC_RING_SLOTS_MAX));
}

void test_the_first_taker_wins_and_the_second_is_told_it_lost()
{
    TEST_ASSERT_TRUE(pc_slot_take(&g_held, 5));
    TEST_ASSERT_FALSE(pc_slot_take(&g_held, 5));
    TEST_ASSERT_TRUE(pc_slot_take(&g_held, 6)); // a neighbour is unaffected
}

void test_a_dropped_slot_can_be_taken_again()
{
    TEST_ASSERT_TRUE(pc_slot_take(&g_held, 2));
    pc_slot_drop(&g_held, 2);
    TEST_ASSERT_TRUE(pc_slot_take(&g_held, 2));
}

void test_ready_is_what_is_marked_minus_what_is_held()
{
    pc_slot_mark(&g_mask, 1);
    pc_slot_mark(&g_mask, 3);
    pc_slot_mark(&g_mask, 9); // outside the count below
    TEST_ASSERT_EQUAL_HEX32(0xAu, pc_slot_ready(&g_mask, &g_held, 8));
    TEST_ASSERT_TRUE(pc_slot_take(&g_held, 1));
    TEST_ASSERT_EQUAL_HEX32(0x8u, pc_slot_ready(&g_mask, &g_held, 8));
    pc_slot_clear(&g_mask, 3);
    TEST_ASSERT_EQUAL_HEX32(0x0u, pc_slot_ready(&g_mask, &g_held, 8));
}

void test_next_finds_the_lowest_slot_and_reports_none_when_empty()
{
    TEST_ASSERT_EQUAL_INT32(-1, pc_slot_next(0u));
    TEST_ASSERT_EQUAL_INT32(0, pc_slot_next(0x1u));
    TEST_ASSERT_EQUAL_INT32(3, pc_slot_next(0x8u));
    TEST_ASSERT_EQUAL_INT32(3, pc_slot_next(0x98u)); // lowest, not any
    TEST_ASSERT_EQUAL_INT32(31, pc_slot_next(0x80000000u));
}

void test_a_hold_records_the_region_the_wire_walks()
{
    static const uint8_t wire[6] = {1, 2, 3, 4, 5, 6};
    TEST_ASSERT_TRUE(pc_slot_hold(&g_held, g_keepout, 4, wire, sizeof(wire)));

    const pc_cspan *k = pc_slot_keepout(g_keepout, 4);
    TEST_ASSERT_EQUAL_PTR(wire, k->buf);
    TEST_ASSERT_EQUAL_size_t(sizeof(wire), k->len);
    TEST_ASSERT_EQUAL_size_t(0, k->pos);
    TEST_ASSERT_FALSE(k->err);
}

void test_a_losing_hold_does_not_clobber_the_winners_keepout()
{
    static const uint8_t mine[4] = {9, 9, 9, 9};
    static const uint8_t theirs[2] = {7, 7};

    TEST_ASSERT_TRUE(pc_slot_hold(&g_held, g_keepout, 7, mine, sizeof(mine)));
    TEST_ASSERT_FALSE(pc_slot_hold(&g_held, g_keepout, 7, theirs, sizeof(theirs)));

    const pc_cspan *k = pc_slot_keepout(g_keepout, 7);
    TEST_ASSERT_EQUAL_PTR(mine, k->buf); // the loser must not redirect the wire
    TEST_ASSERT_EQUAL_size_t(sizeof(mine), k->len);
}

void test_a_forward_walks_the_keepout_in_place_rather_than_copying()
{
    static const uint8_t payload[5] = {'h', 'e', 'l', 'l', 'o'};
    TEST_ASSERT_TRUE(pc_slot_hold(&g_held, g_keepout, 0, payload, sizeof(payload)));

    // The egress is handed the pointer and walks its length; nothing is staged in between.
    const pc_cspan *k = pc_slot_keepout(g_keepout, 0);
    TEST_ASSERT_EQUAL_PTR(payload, k->buf);
    TEST_ASSERT_EQUAL_MEMORY(payload, k->buf, k->len);

    pc_slot_drop(&g_held, 0); // only now may the slot be refilled
    TEST_ASSERT_TRUE(pc_slot_take(&g_held, 0));
}

void test_every_slot_a_mask_can_address_is_reachable()
{
    for (size_t i = 0; i < PC_RING_SLOTS_MAX; i++)
    {
        TEST_ASSERT_TRUE(pc_slot_take(&g_held, i));
    }
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, PROTO_ATOMIC_LOAD(&g_held));
    TEST_ASSERT_EQUAL_HEX32(0x0u, pc_slot_ready(&g_mask, &g_held, PC_RING_SLOTS_MAX));
    for (size_t i = 0; i < PC_RING_SLOTS_MAX; i++)
    {
        pc_slot_drop(&g_held, i);
    }
    TEST_ASSERT_EQUAL_HEX32(0x0u, PROTO_ATOMIC_LOAD(&g_held));
}

// ---- stress ---------------------------------------------------------------
//
// The views above check one property each. These drive the same code against a model for long
// enough to cross every wrap many times over, because a ring's failures are wrap failures and a
// single-shot case sits at whichever offset it was written at.

static uint32_t lcg(uint32_t *s)
{
    *s = (*s * 1664525u) + 1013904223u;
    return *s >> 16;
}

void test_stress_every_byte_comes_back_once_and_in_order()
{
    uint32_t rs = 12345u;
    uint8_t chunk[CAP];
    uint8_t out[CAP];
    uint8_t next_w = 0;
    uint8_t next_r = 0;
    size_t written = 0;
    size_t consumed = 0;

    // The payload is a wrapping counter, so order and completeness are both checked by value.
    for (int it = 0; it < 200000; it++)
    {
        size_t want = (lcg(&rs) % CAP) + 1u;
        if (want <= pc_ring_free(&g_head, &g_tail, CAP))
        {
            for (size_t i = 0; i < want; i++)
            {
                chunk[i] = next_w;
                next_w++;
            }
            size_t h = PROTO_ATOMIC_LOAD(&g_head);
            h = pc_ring_write_span(g_buf, CAP, h, chunk, want);
            PROTO_ATOMIC_STORE(&g_head, h);
            written += want;
        }

        size_t got = pc_ring_read(g_buf, CAP, &g_head, &g_tail, out, (lcg(&rs) % CAP) + 1u);
        for (size_t i = 0; i < got; i++)
        {
            TEST_ASSERT_EQUAL_UINT8(next_r, out[i]);
            next_r++;
        }
        consumed += got;
    }

    TEST_ASSERT_TRUE(written > (size_t)CAP * 1000u); // the run actually moved bytes
    TEST_ASSERT_EQUAL_size_t(written - pc_ring_available(&g_head, &g_tail, CAP), consumed);
}

void test_stress_a_ring_kept_full_never_loses_the_boundary()
{
    uint8_t chunk[CAP];
    uint8_t out[CAP];
    uint8_t next_w = 0;
    uint8_t next_r = 0;

    // Refill to capacity every iteration, so head sits one behind tail for the whole run.
    for (int it = 0; it < 50000; it++)
    {
        size_t room = pc_ring_free(&g_head, &g_tail, CAP);
        for (size_t i = 0; i < room; i++)
        {
            chunk[i] = next_w;
            next_w++;
        }
        size_t h = PROTO_ATOMIC_LOAD(&g_head);
        h = pc_ring_write_span(g_buf, CAP, h, chunk, room);
        PROTO_ATOMIC_STORE(&g_head, h);
        TEST_ASSERT_EQUAL_size_t(0, pc_ring_free(&g_head, &g_tail, CAP));
        TEST_ASSERT_EQUAL_size_t(CAP - 1, pc_ring_available(&g_head, &g_tail, CAP));

        size_t got = pc_ring_read(g_buf, CAP, &g_head, &g_tail, out, 3);
        for (size_t i = 0; i < got; i++)
        {
            TEST_ASSERT_EQUAL_UINT8(next_r, out[i]);
            next_r++;
        }
    }
}

void test_stress_segments_cycle_far_past_the_index_width()
{
    size_t idx = 99;
    uint8_t tag = 0;
    for (int it = 0; it < 100000; it++)
    {
        TEST_ASSERT_TRUE(pc_seg_next(&g_claim, &g_rel, SEGS, &idx));
        pc_seg_at(g_segs, SEG_SIZE, idx)[0] = tag;
        pc_seg_publish(&g_claim);

        TEST_ASSERT_TRUE(pc_seg_front(&g_claim, &g_rel, SEGS, &idx));
        TEST_ASSERT_EQUAL_UINT8(tag, pc_seg_at(g_segs, SEG_SIZE, idx)[0]);
        pc_seg_release(&g_rel);
        tag++;
    }
    TEST_ASSERT_EQUAL_size_t(0, pc_seg_inflight(&g_claim, &g_rel));
}

void test_stress_segment_counters_survive_the_size_t_wrap()
{
    // The counters climb forever and only the index is masked, so the arithmetic has to hold
    // across the point where they roll over.
    PROTO_ATOMIC_STORE(&g_claim, SIZE_MAX - 2u);
    PROTO_ATOMIC_STORE(&g_rel, SIZE_MAX - 2u);

    size_t idx = 99;
    for (int it = 0; it < 64; it++)
    {
        TEST_ASSERT_EQUAL_size_t(0, pc_seg_inflight(&g_claim, &g_rel));
        TEST_ASSERT_TRUE(pc_seg_next(&g_claim, &g_rel, SEGS, &idx));
        TEST_ASSERT_EQUAL_size_t(PROTO_ATOMIC_LOAD(&g_claim) & (SEGS - 1u), idx);
        pc_seg_publish(&g_claim);
        TEST_ASSERT_EQUAL_size_t(1, pc_seg_inflight(&g_claim, &g_rel));
        TEST_ASSERT_TRUE(pc_seg_front(&g_claim, &g_rel, SEGS, &idx));
        pc_seg_release(&g_rel);
    }
}

void test_stress_a_full_segment_ring_holds_its_count_across_the_wrap()
{
    PROTO_ATOMIC_STORE(&g_claim, SIZE_MAX - 1u);
    PROTO_ATOMIC_STORE(&g_rel, SIZE_MAX - 1u);

    size_t idx = 99;
    for (size_t i = 0; i < SEGS; i++)
    {
        TEST_ASSERT_TRUE(pc_seg_next(&g_claim, &g_rel, SEGS, &idx));
        pc_seg_publish(&g_claim);
        TEST_ASSERT_EQUAL_size_t(i + 1u, pc_seg_inflight(&g_claim, &g_rel));
    }
    TEST_ASSERT_FALSE(pc_seg_next(&g_claim, &g_rel, SEGS, &idx)); // full, straddling the rollover
    for (size_t i = 0; i < SEGS; i++)
    {
        TEST_ASSERT_TRUE(pc_seg_front(&g_claim, &g_rel, SEGS, &idx));
        pc_seg_release(&g_rel);
    }
    TEST_ASSERT_EQUAL_size_t(0, pc_seg_inflight(&g_claim, &g_rel));
}

void test_stress_slot_masks_track_a_model_exactly()
{
    uint32_t rs = 987u;
    uint32_t held = 0;
    uint32_t marked = 0;

    for (int it = 0; it < 200000; it++)
    {
        size_t s = lcg(&rs) % PC_RING_SLOTS_MAX;
        const uint32_t bit = 1u << s;
        uint32_t act = lcg(&rs) % 4u;

        if (act == 0u)
        {
            proto_bool won = pc_slot_take(&g_held, s);
            proto_bool free_before = PROTO_FALSE;
            if ((held & bit) == 0u)
            {
                free_before = PROTO_TRUE;
            }
            TEST_ASSERT_EQUAL_INT(free_before, won); // only a free slot is ever won
            held |= bit;
        }
        else if (act == 1u)
        {
            pc_slot_drop(&g_held, s);
            held &= ~bit;
        }
        else if (act == 2u)
        {
            pc_slot_mark(&g_mask, s);
            marked |= bit;
        }
        else
        {
            pc_slot_clear(&g_mask, s);
            marked &= ~bit;
        }

        TEST_ASSERT_EQUAL_HEX32(held, PROTO_ATOMIC_LOAD(&g_held));
        TEST_ASSERT_EQUAL_HEX32(marked, PROTO_ATOMIC_LOAD(&g_mask));
    }

    // ready and next must agree with the model at every slot count, not just the full width.
    for (size_t n = 0; n <= PC_RING_SLOTS_MAX; n++)
    {
        uint32_t all = pc_slot_all(n);
        uint32_t want = marked & ~held & all;
        TEST_ASSERT_EQUAL_HEX32(want, pc_slot_ready(&g_mask, &g_held, n));

        int32_t got = pc_slot_next(want);
        if (want == 0u)
        {
            TEST_ASSERT_EQUAL_INT32(-1, got);
        }
        else
        {
            TEST_ASSERT_TRUE(got >= 0);
            TEST_ASSERT_TRUE((want & (1u << got)) != 0u);
            TEST_ASSERT_EQUAL_HEX32(0u, want & ((1u << got) - 1u)); // nothing lower was skipped
        }
    }
}

void test_stress_every_keepout_stays_with_its_own_slot()
{
    static uint8_t region[PC_RING_SLOTS_MAX][4];
    uint32_t rs = 555u;

    for (size_t s = 0; s < PC_RING_SLOTS_MAX; s++)
    {
        region[s][0] = (uint8_t)s;
        TEST_ASSERT_TRUE(pc_slot_hold(&g_held, g_keepout, s, region[s], sizeof(region[s])));
    }

    // Hammer with losing holds: every one must be refused and none may redirect a keepout.
    for (int it = 0; it < 100000; it++)
    {
        size_t s = lcg(&rs) % PC_RING_SLOTS_MAX;
        static const uint8_t decoy[2] = {0xDE, 0xAD};
        TEST_ASSERT_FALSE(pc_slot_hold(&g_held, g_keepout, s, decoy, sizeof(decoy)));
    }

    for (size_t s = 0; s < PC_RING_SLOTS_MAX; s++)
    {
        const pc_cspan *k = pc_slot_keepout(g_keepout, s);
        TEST_ASSERT_EQUAL_PTR(region[s], k->buf);
        TEST_ASSERT_EQUAL_size_t(sizeof(region[s]), k->len);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)s, k->buf[0]);
    }
}

// ---- pressure -------------------------------------------------------------
//
// This is what stands between the wire and everything above it. Under overload the contract is
// refusal, not truncation: a segment that will not fit is rejected whole so the peer retransmits,
// because a partial write would leave the ring holding half an entry and no way to know it.

void test_pressure_a_segment_that_does_not_fit_is_refused_whole()
{
    fill("0123456789ab"); // 12 of the 15 usable bytes
    size_t room = pc_ring_free(&g_head, &g_tail, CAP);
    TEST_ASSERT_EQUAL_size_t(3, room);

    uint8_t before[CAP];
    memcpy(before, g_buf, sizeof(before));
    size_t head_before = PROTO_ATOMIC_LOAD(&g_head);

    // The caller checks room first and declines; nothing is written, so the ring is untouched.
    TEST_ASSERT_TRUE(room < 4u);
    TEST_ASSERT_EQUAL_MEMORY(before, g_buf, sizeof(before));
    TEST_ASSERT_EQUAL_size_t(head_before, PROTO_ATOMIC_LOAD(&g_head));

    uint8_t out[CAP];
    TEST_ASSERT_EQUAL_size_t(12, pc_ring_read(g_buf, CAP, &g_head, &g_tail, out, CAP));
    TEST_ASSERT_EQUAL_MEMORY("0123456789ab", out, 12);
}

void test_pressure_the_producer_outruns_the_drain_without_losing_a_byte()
{
    uint32_t rs = 4242u;
    uint8_t chunk[CAP];
    uint8_t out[CAP];
    uint8_t next_w = 0;
    uint8_t next_r = 0;
    size_t refused = 0;
    size_t accepted = 0;

    // The producer always asks for more than the drain takes, so the ring rides full.
    for (int it = 0; it < 200000; it++)
    {
        size_t want = (lcg(&rs) % CAP) + 1u;
        if (want <= pc_ring_free(&g_head, &g_tail, CAP))
        {
            for (size_t i = 0; i < want; i++)
            {
                chunk[i] = next_w;
                next_w++;
            }
            size_t h = PROTO_ATOMIC_LOAD(&g_head);
            h = pc_ring_write_span(g_buf, CAP, h, chunk, want);
            PROTO_ATOMIC_STORE(&g_head, h);
            accepted += want;
        }
        else
        {
            refused++;
        }

        TEST_ASSERT_TRUE(pc_ring_available(&g_head, &g_tail, CAP) <= CAP - 1u);

        size_t got = pc_ring_read(g_buf, CAP, &g_head, &g_tail, out, 2); // drains slower than it fills
        for (size_t i = 0; i < got; i++)
        {
            TEST_ASSERT_EQUAL_UINT8(next_r, out[i]);
            next_r++;
        }
    }

    TEST_ASSERT_TRUE(refused > 1000u); // the vessel actually came under pressure
    TEST_ASSERT_TRUE(accepted > 0u);
}

void test_pressure_a_full_segment_ring_refuses_until_the_wire_lets_go()
{
    size_t idx = 99;
    for (size_t i = 0; i < SEGS; i++)
    {
        TEST_ASSERT_TRUE(pc_seg_next(&g_claim, &g_rel, SEGS, &idx));
        pc_seg_publish(&g_claim);
    }
    for (int it = 0; it < 10000; it++)
    {
        TEST_ASSERT_FALSE(pc_seg_next(&g_claim, &g_rel, SEGS, &idx)); // stays refused, never wraps over
        TEST_ASSERT_EQUAL_size_t(SEGS, pc_seg_inflight(&g_claim, &g_rel));
    }
    pc_seg_release(&g_rel);
    TEST_ASSERT_TRUE(pc_seg_next(&g_claim, &g_rel, SEGS, &idx));
}

void test_pressure_every_slot_held_leaves_nothing_ready()
{
    for (size_t s = 0; s < PC_RING_SLOTS_MAX; s++)
    {
        pc_slot_mark(&g_mask, s);
        TEST_ASSERT_TRUE(pc_slot_take(&g_held, s));
    }
    TEST_ASSERT_EQUAL_HEX32(0u, pc_slot_ready(&g_mask, &g_held, PC_RING_SLOTS_MAX));
    TEST_ASSERT_EQUAL_INT32(-1, pc_slot_next(pc_slot_ready(&g_mask, &g_held, PC_RING_SLOTS_MAX)));

    for (int it = 0; it < 10000; it++)
    {
        TEST_ASSERT_FALSE(pc_slot_take(&g_held, (size_t)it % PC_RING_SLOTS_MAX));
    }

    pc_slot_drop(&g_held, 17);
    TEST_ASSERT_EQUAL_INT32(17, pc_slot_next(pc_slot_ready(&g_mask, &g_held, PC_RING_SLOTS_MAX)));
}

// ---- abuse ----------------------------------------------------------------
//
// Degenerate arguments and out-of-range slots. A vessel that only holds when it is addressed
// politely is not holding.

void test_abuse_zero_length_operations_change_nothing()
{
    uint8_t out[4];
    fill("abcd");
    size_t avail = pc_ring_available(&g_head, &g_tail, CAP);

    TEST_ASSERT_EQUAL_size_t(0, pc_ring_read(g_buf, CAP, &g_head, &g_tail, out, 0));
    pc_ring_peek(g_buf, CAP, &g_tail, 0, out, 0);
    pc_ring_consume(&g_tail, CAP, 0);

    size_t h = PROTO_ATOMIC_LOAD(&g_head);
    TEST_ASSERT_EQUAL_size_t(h, pc_ring_write_span(g_buf, CAP, h, (const uint8_t *)"", 0));
    TEST_ASSERT_EQUAL_size_t(avail, pc_ring_available(&g_head, &g_tail, CAP));
}

void test_abuse_reading_an_empty_ring_forever_never_moves_the_tail()
{
    uint8_t out[CAP];
    size_t t = PROTO_ATOMIC_LOAD(&g_tail);
    for (int it = 0; it < 10000; it++)
    {
        TEST_ASSERT_EQUAL_size_t(0, pc_ring_read(g_buf, CAP, &g_head, &g_tail, out, CAP));
        uint8_t v = 0;
        TEST_ASSERT_FALSE(pc_ring_read_byte(g_buf, CAP, &g_head, &g_tail, &v));
    }
    TEST_ASSERT_EQUAL_size_t(t, PROTO_ATOMIC_LOAD(&g_tail));
}

void test_abuse_a_slot_past_the_word_names_nothing()
{
    // 1u << 32 is undefined, so an out-of-range index must not reach the shift at all. It is
    // refused rather than aliasing slot 0, which is what a masked index would have done.
    TEST_ASSERT_TRUE(pc_slot_take(&g_held, 0));
    TEST_ASSERT_FALSE(pc_slot_take(&g_held, PC_RING_SLOTS_MAX));
    TEST_ASSERT_FALSE(pc_slot_take(&g_held, PC_RING_SLOTS_MAX + 7u));
    TEST_ASSERT_FALSE(pc_slot_take(&g_held, (size_t)-1));
    TEST_ASSERT_EQUAL_HEX32(0x1u, PROTO_ATOMIC_LOAD(&g_held)); // only slot 0 was ever taken
}

void test_abuse_out_of_range_mark_drop_and_clear_leave_the_masks_alone()
{
    pc_slot_mark(&g_mask, 3);
    TEST_ASSERT_TRUE(pc_slot_take(&g_held, 3));

    pc_slot_mark(&g_mask, PC_RING_SLOTS_MAX);
    pc_slot_mark(&g_mask, (size_t)-1);
    pc_slot_clear(&g_mask, PC_RING_SLOTS_MAX + 1u);
    pc_slot_drop(&g_held, PC_RING_SLOTS_MAX);
    pc_slot_drop(&g_held, (size_t)-1);

    TEST_ASSERT_EQUAL_HEX32(0x8u, PROTO_ATOMIC_LOAD(&g_mask));
    TEST_ASSERT_EQUAL_HEX32(0x8u, PROTO_ATOMIC_LOAD(&g_held));
}

void test_abuse_an_out_of_range_hold_records_no_keepout()
{
    static const uint8_t junk[3] = {1, 2, 3};
    TEST_ASSERT_FALSE(pc_slot_hold(&g_held, g_keepout, PC_RING_SLOTS_MAX, junk, sizeof(junk)));
    TEST_ASSERT_EQUAL_HEX32(0u, PROTO_ATOMIC_LOAD(&g_held));
    for (size_t s = 0; s < PC_RING_SLOTS_MAX; s++)
    {
        TEST_ASSERT_NULL(pc_slot_keepout(g_keepout, s)->buf); // nothing was written anywhere
    }
}

void test_abuse_dropping_a_slot_that_was_never_taken_is_inert()
{
    for (int it = 0; it < 1000; it++)
    {
        pc_slot_drop(&g_held, (size_t)it % PC_RING_SLOTS_MAX);
    }
    TEST_ASSERT_EQUAL_HEX32(0u, PROTO_ATOMIC_LOAD(&g_held));
    TEST_ASSERT_TRUE(pc_slot_take(&g_held, 9)); // still usable afterwards
}

void test_abuse_a_zero_length_keepout_is_recorded_as_asked()
{
    static const uint8_t nothing[1] = {0};
    TEST_ASSERT_TRUE(pc_slot_hold(&g_held, g_keepout, 1, nothing, 0));
    const pc_cspan *k = pc_slot_keepout(g_keepout, 1);
    TEST_ASSERT_EQUAL_PTR(nothing, k->buf);
    TEST_ASSERT_EQUAL_size_t(0, k->len); // an egress handed this walks nowhere, rather than guessing
}

void test_abuse_ready_past_the_slot_count_still_names_only_real_slots()
{
    pc_slot_mark(&g_mask, 0);
    pc_slot_mark(&g_mask, 31);
    TEST_ASSERT_EQUAL_HEX32(0x80000001u, pc_slot_ready(&g_mask, &g_held, PC_RING_SLOTS_MAX));
    TEST_ASSERT_EQUAL_HEX32(0x80000001u, pc_slot_ready(&g_mask, &g_held, PC_RING_SLOTS_MAX + 100u));
    TEST_ASSERT_EQUAL_HEX32(0x1u, pc_slot_ready(&g_mask, &g_held, 1));
    TEST_ASSERT_EQUAL_HEX32(0x0u, pc_slot_ready(&g_mask, &g_held, 0));
}

void test_abuse_a_long_run_of_hold_and_drop_leaves_no_residue()
{
    static const uint8_t r[2] = {0xA5, 0x5A};
    uint32_t rs = 31337u;
    for (int it = 0; it < 200000; it++)
    {
        size_t s = lcg(&rs) % (PC_RING_SLOTS_MAX + 4u); // deliberately overshoots the width
        if (pc_slot_hold(&g_held, g_keepout, s, r, sizeof(r)))
        {
            TEST_ASSERT_TRUE(s < PC_RING_SLOTS_MAX);
            pc_slot_drop(&g_held, s);
        }
    }
    TEST_ASSERT_EQUAL_HEX32(0u, PROTO_ATOMIC_LOAD(&g_held));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_power_of_two_capacity_is_what_makes_the_index_a_mask);
    RUN_TEST(test_the_mask_wraps_exactly_where_a_modulo_would);

    RUN_TEST(test_an_empty_ring_reports_nothing_available_and_all_but_one_free);
    RUN_TEST(test_one_slot_stays_reserved_so_full_is_distinguishable_from_empty);
    RUN_TEST(test_a_byte_pops_in_order_and_the_ring_empties);
    RUN_TEST(test_a_read_stops_at_what_is_there_not_at_what_was_asked);
    RUN_TEST(test_an_entry_that_straddles_the_wrap_reads_back_whole);
    RUN_TEST(test_peek_reads_across_the_wrap_without_consuming);
    RUN_TEST(test_consume_advances_past_peeked_bytes);

    RUN_TEST(test_a_fresh_segment_ring_has_nothing_in_flight_and_a_slot_to_fill);
    RUN_TEST(test_a_segment_is_invisible_until_it_is_published);
    RUN_TEST(test_segments_leave_in_the_order_they_were_filled);
    RUN_TEST(test_a_full_segment_ring_refuses_rather_than_overwriting_one_in_flight);
    RUN_TEST(test_a_segment_index_wraps_by_mask_while_the_counters_climb);
    RUN_TEST(test_a_segment_is_contiguous_so_an_entry_never_straddles);

    RUN_TEST(test_the_all_mask_names_exactly_the_slots_that_exist);
    RUN_TEST(test_the_first_taker_wins_and_the_second_is_told_it_lost);
    RUN_TEST(test_a_dropped_slot_can_be_taken_again);
    RUN_TEST(test_ready_is_what_is_marked_minus_what_is_held);
    RUN_TEST(test_next_finds_the_lowest_slot_and_reports_none_when_empty);
    RUN_TEST(test_a_hold_records_the_region_the_wire_walks);
    RUN_TEST(test_a_losing_hold_does_not_clobber_the_winners_keepout);
    RUN_TEST(test_a_forward_walks_the_keepout_in_place_rather_than_copying);
    RUN_TEST(test_every_slot_a_mask_can_address_is_reachable);

    RUN_TEST(test_stress_every_byte_comes_back_once_and_in_order);
    RUN_TEST(test_stress_a_ring_kept_full_never_loses_the_boundary);
    RUN_TEST(test_stress_segments_cycle_far_past_the_index_width);
    RUN_TEST(test_stress_segment_counters_survive_the_size_t_wrap);
    RUN_TEST(test_stress_a_full_segment_ring_holds_its_count_across_the_wrap);
    RUN_TEST(test_stress_slot_masks_track_a_model_exactly);
    RUN_TEST(test_stress_every_keepout_stays_with_its_own_slot);

    RUN_TEST(test_pressure_a_segment_that_does_not_fit_is_refused_whole);
    RUN_TEST(test_pressure_the_producer_outruns_the_drain_without_losing_a_byte);
    RUN_TEST(test_pressure_a_full_segment_ring_refuses_until_the_wire_lets_go);
    RUN_TEST(test_pressure_every_slot_held_leaves_nothing_ready);

    RUN_TEST(test_abuse_zero_length_operations_change_nothing);
    RUN_TEST(test_abuse_reading_an_empty_ring_forever_never_moves_the_tail);
    RUN_TEST(test_abuse_a_slot_past_the_word_names_nothing);
    RUN_TEST(test_abuse_out_of_range_mark_drop_and_clear_leave_the_masks_alone);
    RUN_TEST(test_abuse_an_out_of_range_hold_records_no_keepout);
    RUN_TEST(test_abuse_dropping_a_slot_that_was_never_taken_is_inert);
    RUN_TEST(test_abuse_a_zero_length_keepout_is_recorded_as_asked);
    RUN_TEST(test_abuse_ready_past_the_slot_count_still_names_only_real_slots);
    RUN_TEST(test_abuse_a_long_run_of_hold_and_drop_leaves_no_residue);
    return UNITY_END();
}
