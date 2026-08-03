// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Concurrency proof for the cross-thread slot fields (pc_atomic state / rx_head /
// rx_tail in tcp.h). Two threads model the real tcpip_thread (producer) and
// worker (consumer) hammering one slot's SPSC ring with the SAME access pattern as
// the production code. It asserts byte-exact, in-order delivery (no tearing); run
// under ThreadSanitizer (env native_tsan) it additionally proves there is no data
// race - the acquire/release discipline in pc_atomic provides the happens-before
// that lets the plain rx_buffer[] writes be read safely on the other core.

#include "network_drivers/transport/tcp.h"
#include <pthread.h>
#include <sched.h>
#include <unity.h>

#define COUNT 200000 // bytes pushed through the ring

static TcpConn g_slot; // one connection slot under test

void setUp(void)
{
}
void tearDown(void)
{
}

// The producer half of the SPSC ring: mirrors lowlevel_recv_cb writing into rx_buffer and
// publishing with rx_head.
static void *ring_producer(void *arg)
{
    (void)arg;
    for (int i = 0; i < COUNT;)
    {
        size_t head = g_slot.rx_head;
        size_t tail = g_slot.rx_tail;
        size_t used = (head + RX_BUF_SIZE - tail) % RX_BUF_SIZE;
        size_t free_space = (RX_BUF_SIZE - 1) - used; // keep one slot (full vs empty)
        if (free_space == 0)
        {
            sched_yield();
            continue;
        }
        g_slot.rx_buffer[head] = (uint8_t)(i & 0xFF);
        g_slot.rx_head = (head + 1) % RX_BUF_SIZE; // release: publishes the byte
        ++i;
    }
    return NULL;
}

// SPSC ring: producer writes a known byte sequence; consumer drains it. Mirrors
// lowlevel_recv_cb (producer) and the session drain loop (consumer).
void test_spsc_ring_no_race(void)
{
    g_slot.rx_head = 0;
    g_slot.rx_tail = 0;
    g_slot.state = CONN_ACTIVE;

    pthread_t producer;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&producer, NULL, ring_producer, NULL));

    proto_bool ok = PROTO_TRUE;
    int recv = 0;
    while (recv < COUNT)
    {
        if (g_slot.rx_tail != g_slot.rx_head) // acquire: observes the byte
        {
            uint8_t b = g_slot.rx_buffer[g_slot.rx_tail];
            g_slot.rx_tail = (g_slot.rx_tail + 1) % RX_BUF_SIZE;
            if (b != (uint8_t)(recv & 0xFF))
            {
                ok = PROTO_FALSE;
            }
            ++recv;
        }
        else
        {
            sched_yield();
        }
    }
    pthread_join(producer, NULL);

    TEST_ASSERT_TRUE_MESSAGE(ok, "SPSC ring delivered corrupted/out-of-order bytes");
    TEST_ASSERT_EQUAL_INT(COUNT, recv);
}

// The writer half of the state handoff: the tcpip close/error path flipping the slot state.
static void *state_flipper(void *arg)
{
    (void)arg;
    for (int i = 0; i < COUNT; ++i)
    {
        g_slot.state = CONN_FREE;
        g_slot.state = CONN_ACTIVE;
    }
    g_slot.state = CONN_FREE;
    return NULL;
}

// State handoff: one thread flips the slot state (the tcpip close/error path),
// another observes it (the worker's ConnState::CONN_ACTIVE guard). Pure atomic visibility -
// proves the state field is race-free across the boundary.
void test_state_handoff_no_race(void)
{
    g_slot.state = CONN_ACTIVE;
    volatile int observed_free = 0;

    pthread_t flipper;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&flipper, NULL, state_flipper, NULL));

    for (int i = 0; i < COUNT; ++i)
    {
        if (g_slot.state == CONN_FREE)
        {
            observed_free++;
        }
    }

    pthread_join(flipper, NULL);
    // The slot ends FREE; the observer must not have crashed/torn (TSan checks the
    // race). observed_free is timing-dependent, so only assert the final state.
    (void)observed_free;
    TEST_ASSERT_EQUAL_INT(CONN_FREE, (ConnState)g_slot.state);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_spsc_ring_no_race);
    RUN_TEST(test_state_handoff_no_race);
    return UNITY_END();
}
