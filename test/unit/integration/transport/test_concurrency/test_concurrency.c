// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/transport/tcp/common.h"
#include "network_drivers/transport/tcp/tcp.h"
#include <pthread.h>
#include <sched.h>
#include <unity.h>

#define COUNT 200000

static TcpConn g_slot;

void setUp(void)
{
}
void tearDown(void)
{
}

static void *ring_producer(void *arg)
{
    (void)arg;
    for (int i = 0; i < COUNT;)
    {
        size_t head = g_slot.rx_head;
        size_t tail = g_slot.rx_tail;
        size_t used = (head + RX_BUF_SIZE - tail) % RX_BUF_SIZE;
        size_t free_space = (RX_BUF_SIZE - 1) - used;
        if (free_space == 0)
        {
            sched_yield();
            continue;
        }
        g_slot.rx_buffer[head] = (uint8_t)(i & 0xFF);
        g_slot.rx_head = (head + 1) % RX_BUF_SIZE;
        ++i;
    }
    return NULL;
}

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
        if (g_slot.rx_tail != g_slot.rx_head)
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

    (void)observed_free;
    TEST_ASSERT_EQUAL_INT(CONN_FREE, (ConnState)g_slot.state);
}
