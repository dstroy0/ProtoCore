// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the CC1101 driver (services/radio/cc1101) against a mock chip emulating the SPI header
// protocol: config-register read/write, command strobes, status registers (VERSION/RSSI/RXBYTES),
// and the TX/RX FIFO. init / send / tx_done / set_rx / recv / RSSI decode are verified without a radio.

#include "services/radio/cc1101/cc1101.h"
#include <string.h>
#include <unity.h>

struct MockCC
{
    uint8_t reg[0x30]; // config registers 0x00-0x2E
    uint8_t version;
    uint8_t rssi_raw;
    uint8_t state; // 0 idle, 1 rx, 2 tx
    uint8_t txfifo[66];
    uint8_t txlen;
    uint8_t rxfifo[70];
    uint8_t rxcount; // bytes available in the RX FIFO
    uint8_t rxread;  // read cursor
    uint8_t last_strobe;
};
static MockCC g;

static void mock_spi(const uint8_t *tx, uint8_t *rx, uint8_t len, void *)
{
    uint8_t hdr = tx[0];
    bool read = (hdr & 0x80) != 0;
    bool burst = (hdr & 0x40) != 0;
    uint8_t addr = hdr & 0x3F;
    rx[0] = (uint8_t)((g.state << 4) | (g.rxcount & 0x0F)); // chip status byte

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
        if (read && burst) // status register read
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
        else // command strobe (single write)
        {
            g.last_strobe = addr;
            if (addr == 0x34)
            {
                g.state = 1; // SRX
            }
            else if (addr == 0x35)
            {
                g.state = 2; // STX
            }
            else if (addr == 0x36)
            {
                g.state = 0; // SIDLE
            }
            else if (addr == 0x3A)
            {
                g.rxcount = 0; // SFRX
            }
            else if (addr == 0x3B)
            {
                g.txlen = 0; // SFTX
            }
        }
    }
    else if (addr == 0x3F) // FIFO
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

static pc_cc1101_bus g_bus = {mock_spi, nullptr};
static pc_cc1101_bus g_bus_no_spi = {nullptr, nullptr}; // a bus whose transfer callback is missing

// A minimal SmartRF-style register table (just a couple of entries for the test).
static const pc_cc1101_reg REGS[] = {{0x00, 0x29}, {0x08, 0x05}};

static pc_cc1101_config default_cfg()
{
    pc_cc1101_config c = {};
    c.regs = REGS;
    c.nregs = 2;
    c.channel = 20;
    return c;
}

void setUp()
{
    memset(&g, 0, sizeof(g));
    g.version = 0x14; // typical CC1101 VERSION
}
void tearDown()
{
}

void test_init_configures_and_detects(void)
{
    pc_cc1101_config c = default_cfg();
    TEST_ASSERT_TRUE(pc_cc1101_init(&g_bus, &c));
    TEST_ASSERT_EQUAL_HEX8(0x30, g.last_strobe); // SRES was the only strobe issued (reset)
    TEST_ASSERT_EQUAL_HEX8(0x29, g.reg[0x00]);   // register table applied
    TEST_ASSERT_EQUAL_HEX8(0x05, g.reg[0x08]);
    TEST_ASSERT_EQUAL_UINT8(20, g.reg[0x0A]); // CHANNR set
}

void test_init_fails_when_absent(void)
{
    g.version = 0x00; // floating bus
    pc_cc1101_config c = default_cfg();
    TEST_ASSERT_FALSE(pc_cc1101_init(&g_bus, &c));
    g.version = 0xFF;
    TEST_ASSERT_FALSE(pc_cc1101_init(&g_bus, &c));
}

void test_send_writes_fifo_and_strobes_tx(void)
{
    const uint8_t data[3] = {0xAA, 0xBB, 0xCC};
    TEST_ASSERT_TRUE(pc_cc1101_send(&g_bus, data, 3));
    TEST_ASSERT_EQUAL_UINT8(4, g.txlen);     // length byte + 3 payload
    TEST_ASSERT_EQUAL_UINT8(3, g.txfifo[0]); // leading length
    TEST_ASSERT_EQUAL_MEMORY(data, g.txfifo + 1, 3);
    TEST_ASSERT_EQUAL_HEX8(0x35, g.last_strobe); // STX
    TEST_ASSERT_EQUAL_UINT8(2, g.state);         // TX state
}

void test_send_rejects_bad_len(void)
{
    const uint8_t d[1] = {0};
    TEST_ASSERT_FALSE(pc_cc1101_send(&g_bus, d, 0));
    uint8_t big[64] = {0};
    TEST_ASSERT_FALSE(pc_cc1101_send(&g_bus, big, 64));
}

void test_tx_done(void)
{
    g.state = 2; // TX in progress
    TEST_ASSERT_FALSE(pc_cc1101_tx_done(&g_bus));
    g.state = 0; // returned to IDLE
    TEST_ASSERT_TRUE(pc_cc1101_tx_done(&g_bus));
}

void test_set_rx(void)
{
    pc_cc1101_set_rx(&g_bus);
    TEST_ASSERT_EQUAL_HEX8(0x34, g.last_strobe); // last strobe is SRX
    TEST_ASSERT_EQUAL_UINT8(1, g.state);         // RX
}

void test_recv_reads_packet_and_rssi(void)
{
    // FIFO: [len=3][A][B][C][rssi_raw][lqi]; RXBYTES = 6.
    uint8_t payload[3] = {0x11, 0x22, 0x33};
    g.rxfifo[0] = 3;
    memcpy(g.rxfifo + 1, payload, 3);
    g.rxfifo[4] = 0x50; // raw RSSI 0x50 = 80 -> 80/2 - 74 = -34 dBm
    g.rxfifo[5] = 0x80; // LQI/CRC
    g.rxcount = 6;
    g.rxread = 0;
    uint8_t buf[16];
    int16_t rssi = 0;
    int n = pc_cc1101_recv(&g_bus, buf, sizeof(buf), &rssi);
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_MEMORY(payload, buf, 3);
    TEST_ASSERT_EQUAL_INT16(-34, rssi);
}

void test_recv_empty(void)
{
    g.rxcount = 0;
    uint8_t buf[16];
    TEST_ASSERT_EQUAL_INT(-1, pc_cc1101_recv(&g_bus, buf, sizeof(buf), nullptr));
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
    int n = pc_cc1101_recv(&g_bus, buf, sizeof(buf), nullptr);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_HEX8(0x60, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x61, buf[1]);
}

void test_rssi_decode(void)
{
    // TI formula: raw>=128 -> (raw-256)/2-74 ; else raw/2-74.
    TEST_ASSERT_EQUAL_INT16(-34, pc_cc1101_rssi_dbm(0x50));  // 80/2-74
    TEST_ASSERT_EQUAL_INT16(-74, pc_cc1101_rssi_dbm(0x00));  // 0/2-74
    TEST_ASSERT_EQUAL_INT16(-138, pc_cc1101_rssi_dbm(0x80)); // (128-256)/2-74 = -64-74
}

void test_send_guard_subconditions()
{
    uint8_t data[8] = {0};
    TEST_ASSERT_FALSE(pc_cc1101_send(nullptr, data, 8));   // null bus
    TEST_ASSERT_FALSE(pc_cc1101_send(&g_bus, nullptr, 8)); // null data
    TEST_ASSERT_FALSE(pc_cc1101_send(&g_bus, data, 0));    // zero len
    TEST_ASSERT_FALSE(pc_cc1101_send(&g_bus, data, 64));   // len > 63
    TEST_ASSERT_TRUE(pc_cc1101_send(&g_bus, data, 8));     // valid FIFO burst
}

// init guards each argument: a null bus, a bus with no transfer callback, and a null config.
void test_init_null_args(void)
{
    pc_cc1101_config c = default_cfg();
    TEST_ASSERT_FALSE(pc_cc1101_init(nullptr, &c));
    TEST_ASSERT_FALSE(pc_cc1101_init(&g_bus_no_spi, &c));
    TEST_ASSERT_FALSE(pc_cc1101_init(&g_bus, nullptr));
}

// init with a null register table skips the table write loop but still sets the channel and detects.
void test_init_no_regs(void)
{
    pc_cc1101_config c = {};
    c.regs = nullptr; // no table
    c.nregs = 2;      // nregs non-zero, but regs null -> loop body never runs
    c.channel = 7;
    TEST_ASSERT_TRUE(pc_cc1101_init(&g_bus, &c));
    TEST_ASSERT_EQUAL_UINT8(7, g.reg[0x0A]); // CHANNR still applied
}

// tx_done guards a null bus / missing transfer callback.
void test_tx_done_null_args(void)
{
    TEST_ASSERT_FALSE(pc_cc1101_tx_done(nullptr));
    TEST_ASSERT_FALSE(pc_cc1101_tx_done(&g_bus_no_spi));
}

// set_rx guards a null bus / missing transfer callback (no strobe issued).
void test_set_rx_null_args(void)
{
    g.last_strobe = 0xEE;
    pc_cc1101_set_rx(nullptr);
    pc_cc1101_set_rx(&g_bus_no_spi);
    TEST_ASSERT_EQUAL_HEX8(0xEE, g.last_strobe); // untouched: the guard returned before any SPI
}

// recv guards a null bus, a missing transfer callback, and a null buffer.
void test_recv_null_args(void)
{
    uint8_t buf[16];
    int16_t rssi = 0;
    TEST_ASSERT_EQUAL_INT(-1, pc_cc1101_recv(nullptr, buf, sizeof(buf), &rssi));
    TEST_ASSERT_EQUAL_INT(-1, pc_cc1101_recv(&g_bus_no_spi, buf, sizeof(buf), &rssi));
    TEST_ASSERT_EQUAL_INT(-1, pc_cc1101_recv(&g_bus, nullptr, sizeof(buf), &rssi));
}

// recv flushes the RX FIFO and bails on a corrupt length byte (0 or > 63).
void test_recv_bad_length(void)
{
    uint8_t buf[16];
    // Zero length byte with bytes waiting.
    g.rxfifo[0] = 0;
    g.rxcount = 1;
    g.rxread = 0;
    g.last_strobe = 0;
    TEST_ASSERT_EQUAL_INT(-1, pc_cc1101_recv(&g_bus, buf, sizeof(buf), nullptr));
    TEST_ASSERT_EQUAL_HEX8(0x3A, g.last_strobe); // SFRX flush issued

    // Over-long length byte.
    g.rxfifo[0] = 64;
    g.rxcount = 5;
    g.rxread = 0;
    g.last_strobe = 0;
    TEST_ASSERT_EQUAL_INT(-1, pc_cc1101_recv(&g_bus, buf, sizeof(buf), nullptr));
    TEST_ASSERT_EQUAL_HEX8(0x3A, g.last_strobe);
}

// send guards a bus with no transfer callback (the remaining subconditions are covered elsewhere).
void test_send_null_spi(void)
{
    const uint8_t data[8] = {0};
    TEST_ASSERT_FALSE(pc_cc1101_send(&g_bus_no_spi, data, 8));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_init_configures_and_detects);
    RUN_TEST(test_init_fails_when_absent);
    RUN_TEST(test_send_writes_fifo_and_strobes_tx);
    RUN_TEST(test_send_rejects_bad_len);
    RUN_TEST(test_tx_done);
    RUN_TEST(test_set_rx);
    RUN_TEST(test_recv_reads_packet_and_rssi);
    RUN_TEST(test_recv_empty);
    RUN_TEST(test_recv_truncates);
    RUN_TEST(test_rssi_decode);
    RUN_TEST(test_send_guard_subconditions);
    RUN_TEST(test_init_null_args);
    RUN_TEST(test_init_no_regs);
    RUN_TEST(test_tx_done_null_args);
    RUN_TEST(test_set_rx_null_args);
    RUN_TEST(test_recv_null_args);
    RUN_TEST(test_recv_bad_length);
    RUN_TEST(test_send_null_spi);
    return UNITY_END();
}
