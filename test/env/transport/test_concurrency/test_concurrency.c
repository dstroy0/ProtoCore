// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/transport/tcp/common/common.h"
#include "network_drivers/transport/tcp/tcp.h"
#include "rx_feed.h"
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
        if (rx_vacant(&g_slot) == 0u)
        {
            sched_yield();
            continue;
        }
        rx_put(&g_slot, (uint8_t)(i & 0xFF));
        ++i;
    }
    return NULL;
}

void test_spsc_ring_no_race(void)
{
    rx_reset(&g_slot);
    g_slot.state = CONN_ACTIVE;

    pthread_t producer;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&producer, NULL, ring_producer, NULL));

    proto_bool ok = PROTO_TRUE;
    int recv = 0;
    while (recv < COUNT)
    {
        if (rx_used(&g_slot) != 0u)
        {
            uint8_t b = rx_take(&g_slot);
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
