// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "mmgr/dma/dma.h"
#include "protocore_dma_host.h"
#include <string.h>

#include <unity.h>

#define EV_MAX 64
#define EV_DATA_MAX 256

typedef struct
{
    protocore_dma_dir dir;
    uint8_t channel;
    uint16_t len;
    uint16_t seq;
    const void *ptr;
    uint8_t data[EV_DATA_MAX];
    size_t data_len;
} Ev;

static Ev g_ev[EV_MAX];
static size_t g_ev_n;

static void on_ev(const protocore_dma_event *ev, void *ctx)
{
    (void)ctx;
    Ev e;
    e.dir = ev->dir;
    e.channel = ev->channel;
    e.len = ev->len;
    e.seq = ev->seq;
    e.ptr = ev->data;
    e.data_len = 0;
    if (ev->dir == PROTOCORE_DMA_RX && ev->data)
    {
        e.data_len = ev->len < EV_DATA_MAX ? ev->len : EV_DATA_MAX;
        memcpy(e.data, ev->data, e.data_len);
    }
    if (g_ev_n < EV_MAX)
    {
        g_ev[g_ev_n++] = e;
    }
}

static proto_bool open_ch(uint8_t ch, proto_bool loop)
{
    protocore_dma_config c = {0};
    c.channel = ch;
    c.periph = PROTOCORE_DMA_UART;
    c.loopback = loop;
    c.on_complete = on_ev;
    c.ctx = NULL;
    return protocore_dma_open(&c);
}

static uint8_t g_rx[EV_MAX * EV_DATA_MAX];
static size_t rx_concat(void)
{
    size_t n = 0;
    for (size_t i = 0; i < g_ev_n; i++)
    {
        if (g_ev[i].dir == PROTOCORE_DMA_RX && n + g_ev[i].data_len <= sizeof(g_rx))
        {
            memcpy(g_rx + n, g_ev[i].data, g_ev[i].data_len);
            n += g_ev[i].data_len;
        }
    }
    return n;
}

static size_t count_dir(protocore_dma_dir dir)
{
    size_t n = 0;
    for (size_t i = 0; i < g_ev_n; i++)
    {
        if (g_ev[i].dir == dir)
        {
            n++;
        }
    }
    return n;
}

void setUp()
{
    g_ev_n = 0;
    protocore_dma_close(0);
    protocore_dma_close(1);
}
void tearDown()
{
    protocore_dma_close(0);
    protocore_dma_close(1);
}

void test_open_validates()
{
    TEST_ASSERT_FALSE(protocore_dma_open(NULL));
    protocore_dma_config c = {0};
    c.channel = 0;
    c.on_complete = NULL;
    TEST_ASSERT_FALSE(protocore_dma_open(&c));
    c.on_complete = on_ev;
    c.channel = PROTOCORE_DMA_CHANNELS;
    TEST_ASSERT_FALSE(protocore_dma_open(&c));
    TEST_ASSERT_TRUE(open_ch(0, PROTO_FALSE));
    TEST_ASSERT_FALSE(open_ch(0, PROTO_FALSE));
}

void test_ingress_emits_rx_event()
{
    TEST_ASSERT_TRUE(open_ch(0, PROTO_FALSE));
    const uint8_t msg[] = {'h', 'e', 'l', 'l', 'o'};
    TEST_ASSERT_TRUE(protocore_dma_host_feed(0, msg, sizeof(msg)));
    TEST_ASSERT_EQUAL_size_t(0, g_ev_n);
    protocore_dma_poll();
    TEST_ASSERT_EQUAL_size_t(1, count_dir(PROTOCORE_DMA_RX));
    TEST_ASSERT_EQUAL_UINT16(5, g_ev[0].len);
    TEST_ASSERT_EQUAL_UINT8(0, g_ev[0].channel);
    TEST_ASSERT_EQUAL_MEMORY(msg, g_ev[0].data, sizeof(msg));
}

void test_buffer_fills_then_partial_flush()
{
    TEST_ASSERT_TRUE(open_ch(0, PROTO_FALSE));
    uint8_t msg[PROTOCORE_DMA_BUF_SIZE + 3];
    for (size_t i = 0; i < sizeof(msg); i++)
    {
        msg[i] = (uint8_t)i;
    }
    TEST_ASSERT_TRUE(protocore_dma_host_feed(0, msg, sizeof(msg)));

    protocore_dma_poll();
    protocore_dma_poll();

    TEST_ASSERT_EQUAL_size_t(2, count_dir(PROTOCORE_DMA_RX));
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_DMA_BUF_SIZE, g_ev[0].len);
    TEST_ASSERT_EQUAL_UINT16(3, g_ev[1].len);
    size_t got_n = rx_concat();
    TEST_ASSERT_EQUAL_size_t(sizeof(msg), got_n);
    TEST_ASSERT_EQUAL_MEMORY(msg, g_rx, sizeof(msg));
}

void test_ping_pong_flips_buffer()
{
    TEST_ASSERT_TRUE(open_ch(0, PROTO_FALSE));
    uint8_t msg[PROTOCORE_DMA_BUF_SIZE * 2];
    for (size_t i = 0; i < sizeof(msg); i++)
    {
        msg[i] = (uint8_t)(0x40 + i);
    }
    TEST_ASSERT_TRUE(protocore_dma_host_feed(0, msg, sizeof(msg)));
    protocore_dma_poll();
    protocore_dma_poll();
    TEST_ASSERT_EQUAL_size_t(2, count_dir(PROTOCORE_DMA_RX));

    TEST_ASSERT_NOT_EQUAL(g_ev[0].ptr, g_ev[1].ptr);
    TEST_ASSERT_EQUAL_UINT16(0, g_ev[0].seq);
    TEST_ASSERT_EQUAL_UINT16(1, g_ev[1].seq);
    size_t got_n = rx_concat();
    TEST_ASSERT_EQUAL_MEMORY(msg, g_rx, sizeof(msg));
}

void test_egress_captures_tx()
{
    TEST_ASSERT_TRUE(open_ch(0, PROTO_FALSE));
    const uint8_t out[] = {'a', 'b', 'c', 'd'};
    TEST_ASSERT_TRUE(protocore_dma_tx_submit(0, out, sizeof(out)));
    protocore_dma_poll();
    TEST_ASSERT_EQUAL_size_t(1, count_dir(PROTOCORE_DMA_TX));
    TEST_ASSERT_EQUAL_size_t(0, count_dir(PROTOCORE_DMA_RX));
    TEST_ASSERT_EQUAL_UINT16(4, g_ev[0].len);
    TEST_ASSERT_NULL(g_ev[0].ptr);

    uint8_t cap[16];
    uint16_t n = protocore_dma_host_capture(0, cap, sizeof(cap));
    TEST_ASSERT_EQUAL_UINT16(4, n);
    TEST_ASSERT_EQUAL_MEMORY(out, cap, 4);
}

void test_tx_one_in_flight_fail_closed()
{
    TEST_ASSERT_TRUE(open_ch(0, PROTO_FALSE));
    const uint8_t a[] = {1, 2, 3};
    const uint8_t b[] = {4, 5};
    TEST_ASSERT_TRUE(protocore_dma_tx_submit(0, a, sizeof(a)));
    TEST_ASSERT_FALSE(protocore_dma_tx_submit(0, b, sizeof(b)));
    protocore_dma_poll();
    TEST_ASSERT_TRUE(protocore_dma_tx_submit(0, b, sizeof(b)));
    protocore_dma_poll();
    TEST_ASSERT_EQUAL_size_t(2, count_dir(PROTOCORE_DMA_TX));
}

void test_tx_rejects_bad_len()
{
    TEST_ASSERT_TRUE(open_ch(0, PROTO_FALSE));
    const uint8_t x[1] = {9};
    TEST_ASSERT_FALSE(protocore_dma_tx_submit(0, x, 0));
    uint8_t big[PROTOCORE_DMA_BUF_SIZE + 1] = {0};
    TEST_ASSERT_FALSE(protocore_dma_tx_submit(0, big, PROTOCORE_DMA_BUF_SIZE + 1));
    TEST_ASSERT_FALSE(protocore_dma_tx_submit(0, NULL, 4));
}

void test_loopback_round_trip()
{
    TEST_ASSERT_TRUE(open_ch(0, PROTO_TRUE));
    const uint8_t ping[] = {'P', 'I', 'N', 'G'};
    TEST_ASSERT_TRUE(protocore_dma_tx_submit(0, ping, sizeof(ping)));
    protocore_dma_poll();
    TEST_ASSERT_EQUAL_size_t(1, count_dir(PROTOCORE_DMA_TX));
    TEST_ASSERT_EQUAL_size_t(1, count_dir(PROTOCORE_DMA_RX));
    size_t got_n = rx_concat();
    TEST_ASSERT_EQUAL_size_t(sizeof(ping), got_n);
    TEST_ASSERT_EQUAL_MEMORY(ping, g_rx, sizeof(ping));
}

void test_feed_fail_closed_when_full()
{
    TEST_ASSERT_TRUE(open_ch(0, PROTO_FALSE));

    uint8_t big[PROTOCORE_DMA_HOST_STAGE] = {0};
    TEST_ASSERT_FALSE(protocore_dma_host_feed(0, big, sizeof(big)));
    uint8_t ok[PROTOCORE_DMA_HOST_STAGE - 1u] = {0};
    TEST_ASSERT_TRUE(protocore_dma_host_feed(0, ok, sizeof(ok)));
}

void test_closed_channel_is_inert()
{
    const uint8_t x[] = {1, 2, 3};
    TEST_ASSERT_FALSE(protocore_dma_host_feed(0, x, sizeof(x)));
    TEST_ASSERT_FALSE(protocore_dma_tx_submit(0, x, sizeof(x)));
    protocore_dma_poll();
    TEST_ASSERT_EQUAL_size_t(0, g_ev_n);
    TEST_ASSERT_TRUE(open_ch(0, PROTO_FALSE));
    protocore_dma_close(0);
    TEST_ASSERT_FALSE(protocore_dma_host_feed(0, x, sizeof(x)));
}

void test_two_channels_independent()
{
    TEST_ASSERT_TRUE(open_ch(0, PROTO_FALSE));
    TEST_ASSERT_TRUE(open_ch(1, PROTO_FALSE));
    const uint8_t a[] = {0xA0, 0xA1};
    const uint8_t b[] = {0xB0, 0xB1, 0xB2};
    TEST_ASSERT_TRUE(protocore_dma_host_feed(0, a, sizeof(a)));
    TEST_ASSERT_TRUE(protocore_dma_host_feed(1, b, sizeof(b)));
    protocore_dma_poll();
    size_t ch0 = 0, ch1 = 0;
    for (size_t i = 0; i < g_ev_n; i++)
    {
        if (g_ev[i].dir != PROTOCORE_DMA_RX)
        {
            continue;
        }
        if (g_ev[i].channel == 0)
        {
            ch0++;
            TEST_ASSERT_EQUAL_MEMORY(a, g_ev[i].data, sizeof(a));
        }
        else if (g_ev[i].channel == 1)
        {
            ch1++;
            TEST_ASSERT_EQUAL_MEMORY(b, g_ev[i].data, sizeof(b));
        }
    }
    TEST_ASSERT_EQUAL_size_t(1, ch0);
    TEST_ASSERT_EQUAL_size_t(1, ch1);
}

void test_channel_guard_subconditions()
{
    protocore_dma_close(255);
    protocore_dma_close(0);
    uint8_t b[4] = {0};
    TEST_ASSERT_FALSE(protocore_dma_host_feed(0, b, sizeof(b)));
    TEST_ASSERT_EQUAL_UINT16(0, protocore_dma_host_capture(0, b, 4));
    TEST_ASSERT_EQUAL_UINT16(0, protocore_dma_host_capture(255, b, 4));

    TEST_ASSERT_FALSE(protocore_dma_host_feed(PROTOCORE_DMA_CHANNELS, b, sizeof(b)));
    TEST_ASSERT_FALSE(protocore_dma_host_feed(0, NULL, sizeof(b)));
    TEST_ASSERT_FALSE(protocore_dma_tx_submit(PROTOCORE_DMA_CHANNELS, b, sizeof(b)));
    TEST_ASSERT_EQUAL_UINT16(0, protocore_dma_host_capture(0, NULL, 4));
}

