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
#include <thread>
#include <unity.h>

namespace
{
constexpr int kCount = 200000; // bytes pushed through the ring
TcpConn g_slot;                // one connection slot under test
} // namespace

void setUp()
{
}
void tearDown()
{
}

// SPSC ring: producer writes a known byte sequence; consumer drains it. Mirrors
// lowlevel_recv_cb (producer) and the session drain loop (consumer).
void test_spsc_ring_no_race()
{
    g_slot.rx_head = 0;
    g_slot.rx_tail = 0;
    g_slot.state = ConnState::CONN_ACTIVE;

    std::thread producer([] {
        for (int i = 0; i < kCount;)
        {
            size_t head = g_slot.rx_head;
            size_t tail = g_slot.rx_tail;
            size_t used = (head + RX_BUF_SIZE - tail) % RX_BUF_SIZE;
            size_t free_space = (RX_BUF_SIZE - 1) - used; // keep one slot (full vs empty)
            if (free_space == 0)
            {
                std::this_thread::yield();
                continue;
            }
            g_slot.rx_buffer[head] = (uint8_t)(i & 0xFF);
            g_slot.rx_head = (head + 1) % RX_BUF_SIZE; // release: publishes the byte
            ++i;
        }
    });

    bool ok = true;
    int recv = 0;
    while (recv < kCount)
    {
        if (g_slot.rx_tail != g_slot.rx_head) // acquire: observes the byte
        {
            uint8_t b = g_slot.rx_buffer[g_slot.rx_tail];
            g_slot.rx_tail = (g_slot.rx_tail + 1) % RX_BUF_SIZE;
            if (b != (uint8_t)(recv & 0xFF))
            {
                ok = false;
            }
            ++recv;
        }
        else
        {
            std::this_thread::yield();
        }
    }
    producer.join();

    TEST_ASSERT_TRUE_MESSAGE(ok, "SPSC ring delivered corrupted/out-of-order bytes");
    TEST_ASSERT_EQUAL_INT(kCount, recv);
}

// State handoff: one thread flips the slot state (the tcpip close/error path),
// another observes it (the worker's ConnState::CONN_ACTIVE guard). Pure atomic visibility -
// proves the state field is race-free across the boundary.
void test_state_handoff_no_race()
{
    g_slot.state = ConnState::CONN_ACTIVE;
    volatile int observed_free = 0;

    std::thread flipper([] {
        for (int i = 0; i < kCount; ++i)
        {
            g_slot.state = ConnState::CONN_FREE;
            g_slot.state = ConnState::CONN_ACTIVE;
        }
        g_slot.state = ConnState::CONN_FREE;
    });

    for (int i = 0; i < kCount; ++i)
    {
        if (g_slot.state == ConnState::CONN_FREE)
        {
            observed_free++;
        }
    }

    flipper.join();
    // The slot ends FREE; the observer must not have crashed/torn (TSan checks the
    // race). observed_free is timing-dependent, so only assert the final state.
    (void)observed_free;
    TEST_ASSERT_EQUAL_INT(ConnState::CONN_FREE, (ConnState)g_slot.state);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_spsc_ring_no_race);
    RUN_TEST(test_state_handoff_no_race);
    return UNITY_END();
}
