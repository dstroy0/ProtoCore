// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/radio/nrf24/nrf24.h"
#include <string.h>

#include <unity.h>

static uint8_t nrf24_work[16]; // the borrow an entry takes; Nrf24 never reads it

typedef struct
{
    uint8_t reg[32];
    uint8_t rx_addr_p0[5];
    uint8_t tx_addr[5];
    uint8_t rx_payload[32];
    uint8_t tx_payload[32];
    uint8_t tx_len;
    proto_bool ce;
    proto_bool present;
} MockNrf;
static MockNrf g;

static void mock_spi(const uint8_t *tx, uint8_t *rx, uint8_t len, void *)
{
    uint8_t c = tx[0];
    rx[0] = g.reg[0x07];
    if (c <= 0x1F)
    {
        uint8_t reg = c & 0x1F;
        for (uint8_t i = 1; i < len; i++)
        {
            rx[i] = g.present ? g.reg[(reg + i - 1) & 0x1F] : 0x00;
        }
    }
    else if ((c & 0xE0) == 0x20)
    {
        uint8_t reg = c & 0x1F;
        uint8_t n = (uint8_t)(len - 1);
        if (reg == 0x0A && n == 5)
        {
            memcpy(g.rx_addr_p0, tx + 1, 5);
        }
        else if (reg == 0x10 && n == 5)
        {
            memcpy(g.tx_addr, tx + 1, 5);
        }
        else if (reg == 0x07)
        {
            g.reg[0x07] &= (uint8_t)~tx[1];
        }
        else
        {
            for (uint8_t i = 1; i < len; i++)
            {
                g.reg[(reg + i - 1) & 0x1F] = tx[i];
            }
        }
    }
    else if (c == 0xA0)
    {
        g.tx_len = (uint8_t)(len - 1);
        for (uint8_t i = 1; i < len; i++)
        {
            g.tx_payload[i - 1] = tx[i];
        }
    }
    else if (c == 0x61)
    {
        for (uint8_t i = 1; i < len; i++)
        {
            rx[i] = g.rx_payload[i - 1];
        }
    }
}
static void mock_ce(proto_bool level, void *)
{
    g.ce = level;
}
static nrf_bus g_bus = {mock_spi, mock_ce, NULL};

static const uint8_t ADDR[5] = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};

static nrf_config default_cfg()
{
    nrf_config c = {0};
    c.address = ADDR;
    c.channel = 76;
    c.data_rate = 0;
    c.tx_power = 3;
    return c;
}

void setUp()
{
    memset(&g, 0, sizeof(g));
    g.present = PROTO_TRUE;
}
void tearDown()
{
}

void test_init_configures_and_powers_up()
{
    nrf_config c = default_cfg();
    Nrf24.init_args.bus = &g_bus;
    Nrf24.init_args.cfg = &c;
    Nrf24.init(nrf24_work);
    TEST_ASSERT_TRUE(Nrf24.ok);
    TEST_ASSERT_EQUAL_HEX8(0x0E, g.reg[0x00]);
    TEST_ASSERT_EQUAL_UINT8(76, g.reg[0x05]);
    TEST_ASSERT_EQUAL_HEX8(0x03, g.reg[0x03]);
    TEST_ASSERT_EQUAL_UINT8(8, g.reg[0x11]);
    TEST_ASSERT_EQUAL_MEMORY(ADDR, g.rx_addr_p0, 5);
    TEST_ASSERT_EQUAL_MEMORY(ADDR, g.tx_addr, 5);
    TEST_ASSERT_FALSE(g.ce);
}

void test_init_fails_when_absent()
{
    g.present = PROTO_FALSE;
    nrf_config c = default_cfg();
    Nrf24.init_args.bus = &g_bus;
    Nrf24.init_args.cfg = &c;
    Nrf24.init(nrf24_work);
    TEST_ASSERT_FALSE(Nrf24.ok);
}

void test_send_pads_to_width_and_keys_tx()
{
    nrf_config c = default_cfg();
    Nrf24.init_args.bus = &g_bus;
    Nrf24.init_args.cfg = &c;
    Nrf24.init(nrf24_work);
    const uint8_t data[3] = {0xAB, 0xCD, 0xEF};
    Nrf24.send_args.bus = &g_bus;
    Nrf24.send_args.data = data;
    Nrf24.send_args.len = 3;
    Nrf24.send(nrf24_work);
    TEST_ASSERT_TRUE(Nrf24.ok);
    TEST_ASSERT_EQUAL_UINT8(8, g.tx_len);
    TEST_ASSERT_EQUAL_MEMORY(data, g.tx_payload, 3);
    TEST_ASSERT_EQUAL_UINT8(0, g.tx_payload[3]);
    TEST_ASSERT_EQUAL_HEX8(0x0E, g.reg[0x00]);
    TEST_ASSERT_TRUE(g.ce);
}

void test_send_rejects_oversize()
{
    const uint8_t big[9] = {0};
    Nrf24.send_args.bus = &g_bus;
    Nrf24.send_args.data = big;
    Nrf24.send_args.len = 9;
    Nrf24.send(nrf24_work);
    TEST_ASSERT_FALSE(Nrf24.ok);
}

void test_tx_done_flag()
{
    Nrf24.tx_done_args.bus = &g_bus;
    Nrf24.tx_done(nrf24_work);
    TEST_ASSERT_FALSE(Nrf24.ok);
    g.reg[0x07] = 0x20;
    Nrf24.tx_done_args.bus = &g_bus;
    Nrf24.tx_done(nrf24_work);
    TEST_ASSERT_TRUE(Nrf24.ok);
    Nrf24.tx_done_args.bus = &g_bus;
    Nrf24.tx_done(nrf24_work);
    TEST_ASSERT_FALSE(Nrf24.ok);
}

void test_set_rx_enters_prx()
{
    Nrf24.set_rx_args.bus = &g_bus;
    Nrf24.set_rx(nrf24_work);
    TEST_ASSERT_EQUAL_HEX8(0x0F, g.reg[0x00]);
    TEST_ASSERT_TRUE(g.ce);
}

void test_recv_reads_payload_and_pipe()
{
    for (int i = 0; i < 8; i++)
    {
        g.rx_payload[i] = (uint8_t)(0x20 + i);
    }
    g.reg[0x07] = 0x40 | (2 << 1);
    uint8_t buf[16], pipe = 0xFF;
    Nrf24.recv_args.bus = &g_bus;
    Nrf24.recv_args.buf = buf;
    Nrf24.recv_args.cap = sizeof(buf);
    Nrf24.recv_args.pipe = &pipe;
    Nrf24.recv(nrf24_work);
    int n = Nrf24.n;
    TEST_ASSERT_EQUAL_INT(8, n);
    TEST_ASSERT_EQUAL_UINT8(2, pipe);
    TEST_ASSERT_EQUAL_MEMORY(g.rx_payload, buf, 8);
    TEST_ASSERT_EQUAL_HEX8(0x00, g.reg[0x07] & 0x40);
}

void test_recv_no_packet()
{
    uint8_t buf[16];
    Nrf24.recv_args.bus = &g_bus;
    Nrf24.recv_args.buf = buf;
    Nrf24.recv_args.cap = sizeof(buf);
    Nrf24.recv_args.pipe = NULL;
    Nrf24.recv(nrf24_work);
    TEST_ASSERT_EQUAL_INT(-1, Nrf24.n);
}

void test_recv_fifo_empty_pipe()
{
    g.reg[0x07] = 0x40 | (7 << 1);
    uint8_t buf[16];
    Nrf24.recv_args.bus = &g_bus;
    Nrf24.recv_args.buf = buf;
    Nrf24.recv_args.cap = sizeof(buf);
    Nrf24.recv_args.pipe = NULL;
    Nrf24.recv(nrf24_work);
    TEST_ASSERT_EQUAL_INT(-1, Nrf24.n);
    TEST_ASSERT_EQUAL_HEX8(0x00, g.reg[0x07] & 0x40);
}

void test_recv_truncates_to_cap()
{
    for (int i = 0; i < 8; i++)
    {
        g.rx_payload[i] = (uint8_t)(0x50 + i);
    }
    g.reg[0x07] = 0x40;
    uint8_t buf[4], pipe = 0xFF;
    Nrf24.recv_args.bus = &g_bus;
    Nrf24.recv_args.buf = buf;
    Nrf24.recv_args.cap = sizeof(buf);
    Nrf24.recv_args.pipe = &pipe;
    Nrf24.recv(nrf24_work);
    int n = Nrf24.n;
    TEST_ASSERT_EQUAL_INT(4, n);
    TEST_ASSERT_EQUAL_UINT8(0, pipe);
    TEST_ASSERT_EQUAL_MEMORY(g.rx_payload, buf, 4);
}

void test_data_rate_variants()
{
    nrf_config c = default_cfg();
    c.data_rate = 1;
    Nrf24.init_args.bus = &g_bus;
    Nrf24.init_args.cfg = &c;
    Nrf24.init(nrf24_work);
    TEST_ASSERT_TRUE(Nrf24.ok);
    nrf_config c2 = default_cfg();
    c2.data_rate = 2;
    Nrf24.init_args.bus = &g_bus;
    Nrf24.init_args.cfg = &c2;
    Nrf24.init(nrf24_work);
    TEST_ASSERT_TRUE(Nrf24.ok);
}

void test_init_rejects_null_args()
{
    nrf_config c = default_cfg();
    Nrf24.init_args.bus = NULL;
    Nrf24.init_args.cfg = &c;
    Nrf24.init(nrf24_work);
    TEST_ASSERT_FALSE(Nrf24.ok);

    nrf_bus no_spi = {NULL, mock_ce, NULL};
    Nrf24.init_args.bus = &no_spi;
    Nrf24.init_args.cfg = &c;
    Nrf24.init(nrf24_work);
    TEST_ASSERT_FALSE(Nrf24.ok);

    nrf_bus no_ce = {mock_spi, NULL, NULL};
    Nrf24.init_args.bus = &no_ce;
    Nrf24.init_args.cfg = &c;
    Nrf24.init(nrf24_work);
    TEST_ASSERT_FALSE(Nrf24.ok);

    Nrf24.init_args.bus = &g_bus;
    Nrf24.init_args.cfg = NULL;
    Nrf24.init(nrf24_work);
    TEST_ASSERT_FALSE(Nrf24.ok);

    nrf_config no_addr = default_cfg();
    no_addr.address = NULL;
    Nrf24.init_args.bus = &g_bus;
    Nrf24.init_args.cfg = &no_addr;
    Nrf24.init(nrf24_work);
    TEST_ASSERT_FALSE(Nrf24.ok);
}

void test_send_rejects_null_args_and_zero_len()
{
    const uint8_t data[3] = {0x01, 0x02, 0x03};
    Nrf24.send_args.bus = NULL;
    Nrf24.send_args.data = data;
    Nrf24.send_args.len = 3;
    Nrf24.send(nrf24_work);
    TEST_ASSERT_FALSE(Nrf24.ok);
    Nrf24.send_args.bus = &g_bus;
    Nrf24.send_args.data = NULL;
    Nrf24.send_args.len = 3;
    Nrf24.send(nrf24_work);
    TEST_ASSERT_FALSE(Nrf24.ok);
    Nrf24.send_args.bus = &g_bus;
    Nrf24.send_args.data = data;
    Nrf24.send_args.len = 0;
    Nrf24.send(nrf24_work);
    TEST_ASSERT_FALSE(Nrf24.ok);
}

void test_tx_done_null_bus()
{
    Nrf24.tx_done_args.bus = NULL;
    Nrf24.tx_done(nrf24_work);
    TEST_ASSERT_FALSE(Nrf24.ok);
}

void test_set_rx_null_bus_is_noop()
{
    Nrf24.set_rx_args.bus = NULL;
    Nrf24.set_rx(nrf24_work);
}

void test_recv_rejects_null_args()
{
    uint8_t buf[16];
    Nrf24.recv_args.bus = NULL;
    Nrf24.recv_args.buf = buf;
    Nrf24.recv_args.cap = sizeof(buf);
    Nrf24.recv_args.pipe = NULL;
    Nrf24.recv(nrf24_work);
    TEST_ASSERT_EQUAL_INT(-1, Nrf24.n);
    Nrf24.recv_args.bus = &g_bus;
    Nrf24.recv_args.buf = NULL;
    Nrf24.recv_args.cap = sizeof(buf);
    Nrf24.recv_args.pipe = NULL;
    Nrf24.recv(nrf24_work);
    TEST_ASSERT_EQUAL_INT(-1, Nrf24.n);
}

void test_recv_with_null_pipe_out_ok()
{
    for (int i = 0; i < 8; i++)
    {
        g.rx_payload[i] = (uint8_t)(0x60 + i);
    }
    g.reg[0x07] = 0x40 | (3 << 1);
    uint8_t buf[16];
    Nrf24.recv_args.bus = &g_bus;
    Nrf24.recv_args.buf = buf;
    Nrf24.recv_args.cap = sizeof(buf);
    Nrf24.recv_args.pipe = NULL;
    Nrf24.recv(nrf24_work);
    int n = Nrf24.n;
    TEST_ASSERT_EQUAL_INT(8, n);
    TEST_ASSERT_EQUAL_MEMORY(g.rx_payload, buf, 8);
    TEST_ASSERT_EQUAL_HEX8(0x00, g.reg[0x07] & 0x40);
}

