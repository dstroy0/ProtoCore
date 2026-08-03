// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the nRF24L01+ driver (services/radio/nrf24) against a mock chip that emulates
// the SPI command protocol: STATUS shifts out on the first byte, W_REGISTER / R_REGISTER,
// W_TX_PAYLOAD / R_RX_PAYLOAD, and STATUS write-1-to-clear. init / send / tx-done / set-rx
// / recv are verified without a radio (the RF link needs the module).
//
// The env sizes PC_NRF24_PAYLOAD = 8.

#include "services/radio/nrf24/nrf24.h"
#include <string.h>
#include <unity.h>

typedef struct
{
    uint8_t reg[32];
    uint8_t rx_addr_p0[5]; // 5-byte registers have their own internal storage on the chip
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
    rx[0] = g.reg[0x07]; // STATUS is shifted out on the command byte
    if (c <= 0x1F)       // R_REGISTER (0x00 | reg)
    {
        uint8_t reg = c & 0x1F;
        for (uint8_t i = 1; i < len; i++)
        {
            rx[i] = g.present ? g.reg[(reg + i - 1) & 0x1F] : 0x00;
        }
    }
    else if ((c & 0xE0) == 0x20) // W_REGISTER (0x20 | reg)
    {
        uint8_t reg = c & 0x1F;
        uint8_t n = (uint8_t)(len - 1);
        if (reg == 0x0A && n == 5) // RX_ADDR_P0: 5-byte burst, own storage
        {
            memcpy(g.rx_addr_p0, tx + 1, 5);
        }
        else if (reg == 0x10 && n == 5) // TX_ADDR: 5-byte burst, own storage
        {
            memcpy(g.tx_addr, tx + 1, 5);
        }
        else if (reg == 0x07) // STATUS: write-1-to-clear
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
    else if (c == 0xA0) // W_TX_PAYLOAD
    {
        g.tx_len = (uint8_t)(len - 1);
        for (uint8_t i = 1; i < len; i++)
        {
            g.tx_payload[i - 1] = tx[i];
        }
    }
    else if (c == 0x61) // R_RX_PAYLOAD
    {
        for (uint8_t i = 1; i < len; i++)
        {
            rx[i] = g.rx_payload[i - 1];
        }
    }
    // 0xE1 FLUSH_TX / 0xE2 FLUSH_RX / 0xFF NOP: STATUS only, already in rx[0]
}
static void mock_ce(proto_bool level, void *)
{
    g.ce = level;
}
static nrf_bus g_bus = {mock_spi, mock_ce, NULL};

static const uint8_t ADDR[5] = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};

static nrf_config default_cfg()
{
    nrf_config c = {};
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
    TEST_ASSERT_TRUE(pc_nrf24_init(&g_bus, &c));
    TEST_ASSERT_EQUAL_HEX8(0x0E, g.reg[0x00]);       // CONFIG = EN_CRC|CRCO|PWR_UP (PRX bit clear)
    TEST_ASSERT_EQUAL_UINT8(76, g.reg[0x05]);        // RF_CH
    TEST_ASSERT_EQUAL_HEX8(0x03, g.reg[0x03]);       // SETUP_AW = 5-byte
    TEST_ASSERT_EQUAL_UINT8(8, g.reg[0x11]);         // RX_PW_P0 = PC_NRF24_PAYLOAD
    TEST_ASSERT_EQUAL_MEMORY(ADDR, g.rx_addr_p0, 5); // RX_ADDR_P0
    TEST_ASSERT_EQUAL_MEMORY(ADDR, g.tx_addr, 5);    // TX_ADDR
    TEST_ASSERT_FALSE(g.ce);                         // CE stays low until set_rx / send
}

void test_init_fails_when_absent()
{
    g.present = PROTO_FALSE; // reads float -> the RF_CH read-back will not match
    nrf_config c = default_cfg();
    TEST_ASSERT_FALSE(pc_nrf24_init(&g_bus, &c));
}

void test_send_pads_to_width_and_keys_tx()
{
    nrf_config c = default_cfg();
    pc_nrf24_init(&g_bus, &c);
    const uint8_t data[3] = {0xAB, 0xCD, 0xEF};
    TEST_ASSERT_TRUE(pc_nrf24_send(&g_bus, data, 3));
    TEST_ASSERT_EQUAL_UINT8(8, g.tx_len); // padded to the static width
    TEST_ASSERT_EQUAL_MEMORY(data, g.tx_payload, 3);
    TEST_ASSERT_EQUAL_UINT8(0, g.tx_payload[3]); // zero pad
    TEST_ASSERT_EQUAL_HEX8(0x0E, g.reg[0x00]);   // CONFIG PRIM_RX = 0 (PTX)
    TEST_ASSERT_TRUE(g.ce);                      // CE keyed high
}

void test_send_rejects_oversize()
{
    const uint8_t big[9] = {0};
    TEST_ASSERT_FALSE(pc_nrf24_send(&g_bus, big, 9)); // > PC_NRF24_PAYLOAD (8)
}

void test_tx_done_flag()
{
    TEST_ASSERT_FALSE(pc_nrf24_tx_done(&g_bus));
    g.reg[0x07] = 0x20; // STATUS TX_DS
    TEST_ASSERT_TRUE(pc_nrf24_tx_done(&g_bus));
    TEST_ASSERT_FALSE(pc_nrf24_tx_done(&g_bus)); // cleared (write-1-to-clear)
}

void test_set_rx_enters_prx()
{
    pc_nrf24_set_rx(&g_bus);
    TEST_ASSERT_EQUAL_HEX8(0x0F, g.reg[0x00]); // CONFIG PRIM_RX = 1
    TEST_ASSERT_TRUE(g.ce);
}

void test_recv_reads_payload_and_pipe()
{
    for (int i = 0; i < 8; i++)
    {
        g.rx_payload[i] = (uint8_t)(0x20 + i);
    }
    g.reg[0x07] = 0x40 | (2 << 1); // STATUS RX_DR, pipe 2
    uint8_t buf[16], pipe = 0xFF;
    int n = pc_nrf24_recv(&g_bus, buf, sizeof(buf), &pipe);
    TEST_ASSERT_EQUAL_INT(8, n);
    TEST_ASSERT_EQUAL_UINT8(2, pipe);
    TEST_ASSERT_EQUAL_MEMORY(g.rx_payload, buf, 8);
    TEST_ASSERT_EQUAL_HEX8(0x00, g.reg[0x07] & 0x40); // RX_DR cleared
}

void test_recv_no_packet()
{
    uint8_t buf[16];
    TEST_ASSERT_EQUAL_INT(-1, pc_nrf24_recv(&g_bus, buf, sizeof(buf), NULL));
}

void test_recv_fifo_empty_pipe()
{
    g.reg[0x07] = 0x40 | (7 << 1); // RX_DR set but pipe 7 = FIFO empty
    uint8_t buf[16];
    TEST_ASSERT_EQUAL_INT(-1, pc_nrf24_recv(&g_bus, buf, sizeof(buf), NULL));
    TEST_ASSERT_EQUAL_HEX8(0x00, g.reg[0x07] & 0x40); // cleared
}

void test_recv_truncates_to_cap()
{
    for (int i = 0; i < 8; i++)
    {
        g.rx_payload[i] = (uint8_t)(0x50 + i);
    }
    g.reg[0x07] = 0x40; // RX_DR, pipe 0
    uint8_t buf[4], pipe = 0xFF;
    int n = pc_nrf24_recv(&g_bus, buf, sizeof(buf), &pipe);
    TEST_ASSERT_EQUAL_INT(4, n);
    TEST_ASSERT_EQUAL_UINT8(0, pipe);
    TEST_ASSERT_EQUAL_MEMORY(g.rx_payload, buf, 4);
}

void test_data_rate_variants()
{
    nrf_config c = default_cfg();
    c.data_rate = 1; // 2 Mbps
    TEST_ASSERT_TRUE(pc_nrf24_init(&g_bus, &c));
    nrf_config c2 = default_cfg();
    c2.data_rate = 2; // 250 kbps
    TEST_ASSERT_TRUE(pc_nrf24_init(&g_bus, &c2));
}

void test_init_rejects_null_args()
{
    nrf_config c = default_cfg();
    TEST_ASSERT_FALSE(pc_nrf24_init(NULL, &c)); // null bus

    nrf_bus no_spi = {NULL, mock_ce, NULL};
    TEST_ASSERT_FALSE(pc_nrf24_init(&no_spi, &c)); // null bus->spi

    nrf_bus no_ce = {mock_spi, NULL, NULL};
    TEST_ASSERT_FALSE(pc_nrf24_init(&no_ce, &c)); // null bus->ce

    TEST_ASSERT_FALSE(pc_nrf24_init(&g_bus, NULL)); // null cfg

    nrf_config no_addr = default_cfg();
    no_addr.address = NULL;
    TEST_ASSERT_FALSE(pc_nrf24_init(&g_bus, &no_addr)); // null cfg->address
}

void test_send_rejects_null_args_and_zero_len()
{
    const uint8_t data[3] = {0x01, 0x02, 0x03};
    TEST_ASSERT_FALSE(pc_nrf24_send(NULL, data, 3));   // null bus
    TEST_ASSERT_FALSE(pc_nrf24_send(&g_bus, NULL, 3)); // null data
    TEST_ASSERT_FALSE(pc_nrf24_send(&g_bus, data, 0)); // zero length
}

void test_tx_done_null_bus()
{
    TEST_ASSERT_FALSE(pc_nrf24_tx_done(NULL));
}

void test_set_rx_null_bus_is_noop()
{
    pc_nrf24_set_rx(NULL); // must not crash, must not touch the bus
}

void test_recv_rejects_null_args()
{
    uint8_t buf[16];
    TEST_ASSERT_EQUAL_INT(-1, pc_nrf24_recv(NULL, buf, sizeof(buf), NULL));    // null bus
    TEST_ASSERT_EQUAL_INT(-1, pc_nrf24_recv(&g_bus, NULL, sizeof(buf), NULL)); // null buf
}

void test_recv_with_null_pipe_out_ok()
{
    for (int i = 0; i < 8; i++)
    {
        g.rx_payload[i] = (uint8_t)(0x60 + i);
    }
    g.reg[0x07] = 0x40 | (3 << 1); // STATUS RX_DR, pipe 3
    uint8_t buf[16];
    int n = pc_nrf24_recv(&g_bus, buf, sizeof(buf), NULL); // caller does not want the pipe
    TEST_ASSERT_EQUAL_INT(8, n);
    TEST_ASSERT_EQUAL_MEMORY(g.rx_payload, buf, 8);
    TEST_ASSERT_EQUAL_HEX8(0x00, g.reg[0x07] & 0x40); // RX_DR cleared
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_init_configures_and_powers_up);
    RUN_TEST(test_init_fails_when_absent);
    RUN_TEST(test_send_pads_to_width_and_keys_tx);
    RUN_TEST(test_send_rejects_oversize);
    RUN_TEST(test_tx_done_flag);
    RUN_TEST(test_set_rx_enters_prx);
    RUN_TEST(test_recv_reads_payload_and_pipe);
    RUN_TEST(test_recv_no_packet);
    RUN_TEST(test_recv_fifo_empty_pipe);
    RUN_TEST(test_recv_truncates_to_cap);
    RUN_TEST(test_data_rate_variants);
    RUN_TEST(test_init_rejects_null_args);
    RUN_TEST(test_send_rejects_null_args_and_zero_len);
    RUN_TEST(test_tx_done_null_bus);
    RUN_TEST(test_set_rx_null_bus_is_noop);
    RUN_TEST(test_recv_rejects_null_args);
    RUN_TEST(test_recv_with_null_pipe_out_ok);
    return UNITY_END();
}
