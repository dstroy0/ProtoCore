// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file nrf24.c
 * @brief nRF24L01+ driver - implementation.
 *
 * The nRF24L01+ command protocol (Nordic datasheet): every SPI transaction is a command
 * byte followed by data, and the STATUS register is shifted out on the first byte. Register
 * access is R_REGISTER (0x00 | reg) / W_REGISTER (0x20 | reg); payloads use R_RX_PAYLOAD /
 * W_TX_PAYLOAD. Host-testable with a mock; the RF link needs the module.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_NRF24

#include "services/radio/nrf24/nrf24.h"

PROTOCORE_BEGIN_DECLS

// Commands.
#define CMD_R_REGISTER 0x00
#define CMD_W_REGISTER 0x20
#define CMD_R_RX_PAYLOAD 0x61
#define CMD_W_TX_PAYLOAD 0xA0
#define CMD_FLUSH_TX 0xE1
#define CMD_FLUSH_RX 0xE2
#define CMD_NOP 0xFF

// Registers.
#define REG_CONFIG 0x00
#define REG_EN_AA 0x01
#define REG_EN_RXADDR 0x02
#define REG_SETUP_AW 0x03
#define REG_SETUP_RETR 0x04
#define REG_RF_CH 0x05
#define REG_RF_SETUP 0x06
#define REG_STATUS 0x07
#define REG_RX_ADDR_P0 0x0A
#define REG_TX_ADDR 0x10
#define REG_RX_PW_P0 0x11

// CONFIG bits.
#define CFG_EN_CRC 0x08
#define CFG_CRCO 0x04
#define CFG_PWR_UP 0x02
#define CFG_PRIM_RX 0x01

// STATUS bits.
#define ST_RX_DR 0x40
#define ST_TX_DS 0x20
#define ST_RX_P_NO 0x0E // bits 3:1 = pipe of the payload at the RX FIFO head

static void reg_write(const nrf_bus *b, uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = {(uint8_t)(CMD_W_REGISTER | reg), val};
    uint8_t rx[2];
    b->spi(tx, rx, 2, b->ctx);
}

static uint8_t reg_read(const nrf_bus *b, uint8_t reg)
{
    uint8_t tx[2] = {(uint8_t)(CMD_R_REGISTER | reg), 0xFF};
    uint8_t rx[2];
    b->spi(tx, rx, 2, b->ctx);
    return rx[1];
}

static void reg_write_buf(const nrf_bus *b, uint8_t reg, const uint8_t *buf, uint8_t n)
{
    uint8_t tx[6];
    uint8_t rx[6];
    tx[0] = (uint8_t)(CMD_W_REGISTER | reg);
    for (uint8_t i = 0; i < n; i++)
    {
        tx[1 + i] = buf[i];
    }
    b->spi(tx, rx, (uint8_t)(n + 1), b->ctx);
}

static uint8_t status(const nrf_bus *b)
{
    uint8_t tx[1] = {CMD_NOP};
    uint8_t rx[1];
    b->spi(tx, rx, 1, b->ctx);
    return rx[0];
}

static void cmd(const nrf_bus *b, uint8_t c)
{
    uint8_t tx[1] = {c};
    uint8_t rx[1];
    b->spi(tx, rx, 1, b->ctx);
}

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void nrf24_init(uint8_t *restrict work)
{
    (void)work;
    const nrf_bus *bus = Nrf24.init_args.bus;
    const nrf_config *cfg = Nrf24.init_args.cfg;

    if (!bus || !bus->spi || !bus->ce || !cfg || !cfg->address)
    {
        Nrf24.ok = PROTO_FALSE;
        return;
    }
    bus->ce(PROTO_FALSE, bus->ctx);

    reg_write(bus, REG_CONFIG, CFG_EN_CRC | CFG_CRCO); // power down, 16-bit CRC
    reg_write(bus, REG_RF_CH, cfg->channel);
    if (reg_read(bus, REG_RF_CH) != cfg->channel)
    {
        Nrf24.ok = PROTO_FALSE; // written value did not read back -> no chip on the bus
        return;
    }

    reg_write(bus, REG_SETUP_AW, 0x03);   // 5-byte addresses
    reg_write(bus, REG_EN_RXADDR, 0x01);  // enable pipe 0
    reg_write(bus, REG_EN_AA, 0x00);      // raw mode (no auto-ack)
    reg_write(bus, REG_SETUP_RETR, 0x00); // no auto-retransmit
    uint8_t dr = 0x00;                    // 1 Mbps
    if (cfg->data_rate == 1)
    {
        dr = 0x08; // 2 Mbps
    }
    else if (cfg->data_rate == 2)
    {
        dr = 0x20; // 250 kbps
    }
    reg_write(bus, REG_RF_SETUP, (uint8_t)(dr | ((cfg->tx_power & 0x03) << 1)));
    reg_write(bus, REG_RX_PW_P0, PROTOCORE_NRF24_PAYLOAD);
    reg_write_buf(bus, REG_RX_ADDR_P0, cfg->address, 5);
    reg_write_buf(bus, REG_TX_ADDR, cfg->address, 5);

    cmd(bus, CMD_FLUSH_RX);
    cmd(bus, CMD_FLUSH_TX);
    reg_write(bus, REG_STATUS, ST_RX_DR | ST_TX_DS | 0x10); // clear all flags

    reg_write(bus, REG_CONFIG,
              CFG_EN_CRC | CFG_CRCO | CFG_PWR_UP); // power up (standby)
    Nrf24.ok = PROTO_TRUE;
}

static void nrf24_send(uint8_t *restrict work)
{
    (void)work;
    const nrf_bus *bus = Nrf24.send_args.bus;
    const uint8_t *data = Nrf24.send_args.data;
    uint8_t len = Nrf24.send_args.len;

    if (!bus || !data || len == 0 || len > PROTOCORE_NRF24_PAYLOAD)
    {
        Nrf24.ok = PROTO_FALSE;
        return;
    }
    bus->ce(PROTO_FALSE, bus->ctx);
    reg_write(bus, REG_CONFIG,
              CFG_EN_CRC | CFG_CRCO | CFG_PWR_UP); // PRIM_RX = 0 -> PTX

    uint8_t tx[1 + PROTOCORE_NRF24_PAYLOAD];
    uint8_t rx[1 + PROTOCORE_NRF24_PAYLOAD];
    tx[0] = CMD_W_TX_PAYLOAD;
    for (uint8_t i = 0; i < PROTOCORE_NRF24_PAYLOAD; i++)
    {
        tx[1 + i] = (i < len) ? data[i] : 0x00; // zero-pad to the static width
    }
    bus->spi(tx, rx, (uint8_t)(PROTOCORE_NRF24_PAYLOAD + 1), bus->ctx);

    bus->ce(PROTO_TRUE, bus->ctx); // key the transmit
    Nrf24.ok = PROTO_TRUE;
}

static void nrf24_tx_done(uint8_t *restrict work)
{
    (void)work;
    const nrf_bus *bus = Nrf24.tx_done_args.bus;

    if (!bus)
    {
        Nrf24.ok = PROTO_FALSE;
        return;
    }
    if (status(bus) & ST_TX_DS)
    {
        reg_write(bus, REG_STATUS, ST_TX_DS); // write-1-to-clear
        Nrf24.ok = PROTO_TRUE;
        return;
    }
    Nrf24.ok = PROTO_FALSE;
}

static void nrf24_set_rx(uint8_t *restrict work)
{
    (void)work;
    const nrf_bus *bus = Nrf24.set_rx_args.bus;

    if (!bus)
    {
        return;
    }
    reg_write(bus, REG_CONFIG,
              CFG_EN_CRC | CFG_CRCO | CFG_PWR_UP | CFG_PRIM_RX); // PRX
    bus->ce(PROTO_TRUE, bus->ctx);
}

static void nrf24_recv(uint8_t *restrict work)
{
    (void)work;
    const nrf_bus *bus = Nrf24.recv_args.bus;
    uint8_t *buf = Nrf24.recv_args.buf;
    uint8_t cap = Nrf24.recv_args.cap;
    uint8_t *pipe = Nrf24.recv_args.pipe;

    if (!bus || !buf)
    {
        Nrf24.n = -1;
        return;
    }
    uint8_t st = status(bus);
    if (!(st & ST_RX_DR))
    {
        Nrf24.n = -1; // nothing received
        return;
    }
    uint8_t p = (uint8_t)((st & ST_RX_P_NO) >> 1);
    if (p > 5) // 0x07 = RX FIFO empty
    {
        reg_write(bus, REG_STATUS, ST_RX_DR);
        Nrf24.n = -1;
        return;
    }
    uint8_t tx[1 + PROTOCORE_NRF24_PAYLOAD];
    uint8_t rx[1 + PROTOCORE_NRF24_PAYLOAD];
    tx[0] = CMD_R_RX_PAYLOAD;
    for (uint8_t i = 0; i < PROTOCORE_NRF24_PAYLOAD; i++)
    {
        tx[1 + i] = 0xFF;
    }
    bus->spi(tx, rx, (uint8_t)(PROTOCORE_NRF24_PAYLOAD + 1), bus->ctx);

    uint8_t n = (PROTOCORE_NRF24_PAYLOAD < cap) ? PROTOCORE_NRF24_PAYLOAD : cap;
    for (uint8_t i = 0; i < n; i++)
    {
        buf[i] = rx[1 + i];
    }
    if (pipe)
    {
        *pipe = p;
    }
    reg_write(bus, REG_STATUS, ST_RX_DR); // clear
    Nrf24.n = (int)n;
}

Nrf24Ns Nrf24 = {
    .init = nrf24_init, .send = nrf24_send, .tx_done = nrf24_tx_done, .set_rx = nrf24_set_rx, .recv = nrf24_recv};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_NRF24
