// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/radio/cc1101/cc1101.h"
#include <string.h>

#include <unity.h>

static uint8_t cc1101_work[16]; // the borrow an entry takes; Cc1101 never reads it

typedef struct
{
    uint8_t reg[0x30];
    uint8_t version;
    uint8_t rssi_raw;
    uint8_t state;
    uint8_t txfifo[66];
    uint8_t txlen;
    uint8_t rxfifo[70];
    uint8_t rxcount;
    uint8_t rxread;
    uint8_t last_strobe;
} MockCC;
static MockCC g;

static void mock_spi(const uint8_t *tx, uint8_t *rx, uint8_t len, void *)
{
    uint8_t hdr = tx[0];
    proto_bool read = (hdr & 0x80) != 0;
    proto_bool burst = (hdr & 0x40) != 0;
    uint8_t addr = hdr & 0x3F;
    rx[0] = (uint8_t)((g.state << 4) | (g.rxcount & 0x0F));

    if (addr <= 0x2E)
    {
        if (read)
        {
            for (uint8_t i = 1; i < len; i++)
            {
                rx[i] = g.reg[(addr + (burst ? i - 1 : 0)) & 0x2F];
            }
        }
        else
        {
            for (uint8_t i = 1; i < len; i++)
            {
                g.reg[(addr + (burst ? i - 1 : 0)) & 0x2F] = tx[i];
            }
        }
    }
    else if (addr >= 0x30 && addr <= 0x3D)
    {
        if (read && burst)
        {
            uint8_t v = 0;
            if (addr == 0x31)
            {
                v = g.version;
            }
            else if (addr == 0x34)
            {
                v = g.rssi_raw;
            }
            else if (addr == 0x3B)
            {
                v = g.rxcount;
            }
            if (len > 1)
            {
                rx[1] = v;
            }
        }
        else
        {
            g.last_strobe = addr;
            if (addr == 0x34)
            {
                g.state = 1;
            }
            else if (addr == 0x35)
            {
                g.state = 2;
            }
            else if (addr == 0x36)
            {
                g.state = 0;
            }
            else if (addr == 0x3A)
            {
                g.rxcount = 0;
            }
            else if (addr == 0x3B)
            {
                g.txlen = 0;
            }
        }
    }
    else if (addr == 0x3F)
    {
        if (read)
        {
            for (uint8_t i = 1; i < len; i++)
            {
                rx[i] = g.rxfifo[g.rxread++];
            }
        }
        else
        {
            g.txlen = (uint8_t)(len - 1);
            for (uint8_t i = 1; i < len; i++)
            {
                g.txfifo[i - 1] = tx[i];
            }
        }
    }
}

static protocore_cc1101_bus g_bus = {mock_spi, NULL};
static protocore_cc1101_bus g_bus_no_spi = {NULL, NULL};

static const protocore_cc1101_reg REGS[] = {{0x00, 0x29}, {0x08, 0x05}};

static protocore_cc1101_config default_cfg()
{
    protocore_cc1101_config c = {0};
    c.regs = REGS;
    c.nregs = 2;
    c.channel = 20;
    return c;
}

void setUp()
{
    memset(&g, 0, sizeof(g));
    g.version = 0x14;
}
void tearDown()
{
}

void test_init_configures_and_detects(void)
{
    protocore_cc1101_config c = default_cfg();
    Cc1101.init_args.bus = &g_bus;
    Cc1101.init_args.cfg = &c;
    Cc1101.init(cc1101_work);
    TEST_ASSERT_TRUE(Cc1101.ok);
    TEST_ASSERT_EQUAL_HEX8(0x30, g.last_strobe);
    TEST_ASSERT_EQUAL_HEX8(0x29, g.reg[0x00]);
    TEST_ASSERT_EQUAL_HEX8(0x05, g.reg[0x08]);
    TEST_ASSERT_EQUAL_UINT8(20, g.reg[0x0A]);
}

void test_init_fails_when_absent(void)
{
    g.version = 0x00;
    protocore_cc1101_config c = default_cfg();
    Cc1101.init_args.bus = &g_bus;
    Cc1101.init_args.cfg = &c;
    Cc1101.init(cc1101_work);
    TEST_ASSERT_FALSE(Cc1101.ok);
    g.version = 0xFF;
    Cc1101.init_args.bus = &g_bus;
    Cc1101.init_args.cfg = &c;
    Cc1101.init(cc1101_work);
    TEST_ASSERT_FALSE(Cc1101.ok);
}

void test_send_writes_fifo_and_strobes_tx(void)
{
    const uint8_t data[3] = {0xAA, 0xBB, 0xCC};
    Cc1101.send_args.bus = &g_bus;
    Cc1101.send_args.data = data;
    Cc1101.send_args.len = 3;
    Cc1101.send(cc1101_work);
    TEST_ASSERT_TRUE(Cc1101.ok);
    TEST_ASSERT_EQUAL_UINT8(4, g.txlen);
    TEST_ASSERT_EQUAL_UINT8(3, g.txfifo[0]);
    TEST_ASSERT_EQUAL_MEMORY(data, g.txfifo + 1, 3);
    TEST_ASSERT_EQUAL_HEX8(0x35, g.last_strobe);
    TEST_ASSERT_EQUAL_UINT8(2, g.state);
}

void test_send_rejects_bad_len(void)
{
    const uint8_t d[1] = {0};
    Cc1101.send_args.bus = &g_bus;
    Cc1101.send_args.data = d;
    Cc1101.send_args.len = 0;
    Cc1101.send(cc1101_work);
    TEST_ASSERT_FALSE(Cc1101.ok);
    uint8_t big[64] = {0};
    Cc1101.send_args.bus = &g_bus;
    Cc1101.send_args.data = big;
    Cc1101.send_args.len = 64;
    Cc1101.send(cc1101_work);
    TEST_ASSERT_FALSE(Cc1101.ok);
}

void test_tx_done(void)
{
    g.state = 2;
    Cc1101.tx_done_args.bus = &g_bus;
    Cc1101.tx_done(cc1101_work);
    TEST_ASSERT_FALSE(Cc1101.ok);
    g.state = 0;
    Cc1101.tx_done_args.bus = &g_bus;
    Cc1101.tx_done(cc1101_work);
    TEST_ASSERT_TRUE(Cc1101.ok);
}

void test_set_rx(void)
{
    Cc1101.set_rx_args.bus = &g_bus;
    Cc1101.set_rx(cc1101_work);
    TEST_ASSERT_EQUAL_HEX8(0x34, g.last_strobe);
    TEST_ASSERT_EQUAL_UINT8(1, g.state);
}

void test_recv_reads_packet_and_rssi(void)
{

    uint8_t payload[3] = {0x11, 0x22, 0x33};
    g.rxfifo[0] = 3;
    memcpy(g.rxfifo + 1, payload, 3);
    g.rxfifo[4] = 0x50;
    g.rxfifo[5] = 0x80;
    g.rxcount = 6;
    g.rxread = 0;
    uint8_t buf[16];
    int16_t rssi = 0;
    Cc1101.recv_args.bus = &g_bus;
    Cc1101.recv_args.buf = buf;
    Cc1101.recv_args.cap = sizeof(buf);
    Cc1101.recv_args.rssi_dbm = &rssi;
    Cc1101.recv(cc1101_work);
    int n = Cc1101.n;
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_MEMORY(payload, buf, 3);
    TEST_ASSERT_EQUAL_INT16(-34, rssi);
}

void test_recv_empty(void)
{
    g.rxcount = 0;
    uint8_t buf[16];
    Cc1101.recv_args.bus = &g_bus;
    Cc1101.recv_args.buf = buf;
    Cc1101.recv_args.cap = sizeof(buf);
    Cc1101.recv_args.rssi_dbm = NULL;
    Cc1101.recv(cc1101_work);
    TEST_ASSERT_EQUAL_INT(-1, Cc1101.n);
}

void test_recv_truncates(void)
{
    g.rxfifo[0] = 4;
    for (int i = 0; i < 4; i++)
    {
        g.rxfifo[1 + i] = (uint8_t)(0x60 + i);
    }
    g.rxfifo[5] = 0x40;
    g.rxfifo[6] = 0x80;
    g.rxcount = 7;
    g.rxread = 0;
    uint8_t buf[2];
    Cc1101.recv_args.bus = &g_bus;
    Cc1101.recv_args.buf = buf;
    Cc1101.recv_args.cap = sizeof(buf);
    Cc1101.recv_args.rssi_dbm = NULL;
    Cc1101.recv(cc1101_work);
    int n = Cc1101.n;
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_HEX8(0x60, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x61, buf[1]);
}

void test_rssi_decode(void)
{

    Cc1101.rssi_dbm_args.raw = 0x50;
    Cc1101.rssi_dbm(cc1101_work);
    TEST_ASSERT_EQUAL_INT16(-34, Cc1101.value);
    Cc1101.rssi_dbm_args.raw = 0x00;
    Cc1101.rssi_dbm(cc1101_work);
    TEST_ASSERT_EQUAL_INT16(-74, Cc1101.value);
    Cc1101.rssi_dbm_args.raw = 0x80;
    Cc1101.rssi_dbm(cc1101_work);
    TEST_ASSERT_EQUAL_INT16(-138, Cc1101.value);
}

void test_send_guard_subconditions()
{
    uint8_t data[8] = {0};
    Cc1101.send_args.bus = NULL;
    Cc1101.send_args.data = data;
    Cc1101.send_args.len = 8;
    Cc1101.send(cc1101_work);
    TEST_ASSERT_FALSE(Cc1101.ok);
    Cc1101.send_args.bus = &g_bus;
    Cc1101.send_args.data = NULL;
    Cc1101.send_args.len = 8;
    Cc1101.send(cc1101_work);
    TEST_ASSERT_FALSE(Cc1101.ok);
    Cc1101.send_args.bus = &g_bus;
    Cc1101.send_args.data = data;
    Cc1101.send_args.len = 0;
    Cc1101.send(cc1101_work);
    TEST_ASSERT_FALSE(Cc1101.ok);
    Cc1101.send_args.bus = &g_bus;
    Cc1101.send_args.data = data;
    Cc1101.send_args.len = 64;
    Cc1101.send(cc1101_work);
    TEST_ASSERT_FALSE(Cc1101.ok);
    Cc1101.send_args.bus = &g_bus;
    Cc1101.send_args.data = data;
    Cc1101.send_args.len = 8;
    Cc1101.send(cc1101_work);
    TEST_ASSERT_TRUE(Cc1101.ok);
}

void test_init_null_args(void)
{
    protocore_cc1101_config c = default_cfg();
    Cc1101.init_args.bus = NULL;
    Cc1101.init_args.cfg = &c;
    Cc1101.init(cc1101_work);
    TEST_ASSERT_FALSE(Cc1101.ok);
    Cc1101.init_args.bus = &g_bus_no_spi;
    Cc1101.init_args.cfg = &c;
    Cc1101.init(cc1101_work);
    TEST_ASSERT_FALSE(Cc1101.ok);
    Cc1101.init_args.bus = &g_bus;
    Cc1101.init_args.cfg = NULL;
    Cc1101.init(cc1101_work);
    TEST_ASSERT_FALSE(Cc1101.ok);
}

void test_init_no_regs(void)
{
    protocore_cc1101_config c = {0};
    c.regs = NULL;
    c.nregs = 2;
    c.channel = 7;
    Cc1101.init_args.bus = &g_bus;
    Cc1101.init_args.cfg = &c;
    Cc1101.init(cc1101_work);
    TEST_ASSERT_TRUE(Cc1101.ok);
    TEST_ASSERT_EQUAL_UINT8(7, g.reg[0x0A]);
}

void test_tx_done_null_args(void)
{
    Cc1101.tx_done_args.bus = NULL;
    Cc1101.tx_done(cc1101_work);
    TEST_ASSERT_FALSE(Cc1101.ok);
    Cc1101.tx_done_args.bus = &g_bus_no_spi;
    Cc1101.tx_done(cc1101_work);
    TEST_ASSERT_FALSE(Cc1101.ok);
}

void test_set_rx_null_args(void)
{
    g.last_strobe = 0xEE;
    Cc1101.set_rx_args.bus = NULL;
    Cc1101.set_rx(cc1101_work);
    Cc1101.set_rx_args.bus = &g_bus_no_spi;
    Cc1101.set_rx(cc1101_work);
    TEST_ASSERT_EQUAL_HEX8(0xEE, g.last_strobe);
}

void test_recv_null_args(void)
{
    uint8_t buf[16];
    int16_t rssi = 0;
    Cc1101.recv_args.bus = NULL;
    Cc1101.recv_args.buf = buf;
    Cc1101.recv_args.cap = sizeof(buf);
    Cc1101.recv_args.rssi_dbm = &rssi;
    Cc1101.recv(cc1101_work);
    TEST_ASSERT_EQUAL_INT(-1, Cc1101.n);
    Cc1101.recv_args.bus = &g_bus_no_spi;
    Cc1101.recv_args.buf = buf;
    Cc1101.recv_args.cap = sizeof(buf);
    Cc1101.recv_args.rssi_dbm = &rssi;
    Cc1101.recv(cc1101_work);
    TEST_ASSERT_EQUAL_INT(-1, Cc1101.n);
    Cc1101.recv_args.bus = &g_bus;
    Cc1101.recv_args.buf = NULL;
    Cc1101.recv_args.cap = sizeof(buf);
    Cc1101.recv_args.rssi_dbm = &rssi;
    Cc1101.recv(cc1101_work);
    TEST_ASSERT_EQUAL_INT(-1, Cc1101.n);
}

void test_recv_bad_length(void)
{
    uint8_t buf[16];

    g.rxfifo[0] = 0;
    g.rxcount = 1;
    g.rxread = 0;
    g.last_strobe = 0;
    Cc1101.recv_args.bus = &g_bus;
    Cc1101.recv_args.buf = buf;
    Cc1101.recv_args.cap = sizeof(buf);
    Cc1101.recv_args.rssi_dbm = NULL;
    Cc1101.recv(cc1101_work);
    TEST_ASSERT_EQUAL_INT(-1, Cc1101.n);
    TEST_ASSERT_EQUAL_HEX8(0x3A, g.last_strobe);

    g.rxfifo[0] = 64;
    g.rxcount = 5;
    g.rxread = 0;
    g.last_strobe = 0;
    Cc1101.recv_args.bus = &g_bus;
    Cc1101.recv_args.buf = buf;
    Cc1101.recv_args.cap = sizeof(buf);
    Cc1101.recv_args.rssi_dbm = NULL;
    Cc1101.recv(cc1101_work);
    TEST_ASSERT_EQUAL_INT(-1, Cc1101.n);
    TEST_ASSERT_EQUAL_HEX8(0x3A, g.last_strobe);
}

void test_send_null_spi(void)
{
    const uint8_t data[8] = {0};
    Cc1101.send_args.bus = &g_bus_no_spi;
    Cc1101.send_args.data = data;
    Cc1101.send_args.len = 8;
    Cc1101.send(cc1101_work);
    TEST_ASSERT_FALSE(Cc1101.ok);
}

