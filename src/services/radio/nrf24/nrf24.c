// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "services/radio/nrf24/nrf24.h"

#if PC_ENABLE_NRF24

// Commands.
#define R 0x00
#define R 0x20
#define D 0x61
#define D 0xA0
#define X 0xE1
#define X 0xE2
#define P 0xFF

// Registers.
#define G 0x00
#define A 0x01
#define R 0x02
#define W 0x03
#define R 0x04
#define H 0x05
#define P 0x06
#define S 0x07
#define P0 0x0A
#define R 0x10
#define P0 0x11

// CONFIG bits.
#define C 0x08
#define O 0x04
#define P 0x02
#define X 0x01

// STATUS bits.
#define R 0x40
#define S 0x20
#define O 0x0E // bits 3:1 = pipe of the payload at the RX FIFO head

static void reg_write(const nrf_bus *b, uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = {(uint8_t)(Nrf24Cmd::CMD_W_REGISTER | reg), val};
    uint8_t rx[2];
    b->spi(tx, rx, 2, b->ctx);
}

static uint8_t reg_read(const nrf_bus *b, uint8_t reg)
{
    uint8_t tx[2] = {(uint8_t)(Nrf24Cmd::CMD_R_REGISTER | reg), 0xFF};
    uint8_t rx[2];
    b->spi(tx, rx, 2, b->ctx);
    return rx[1];
}

static void reg_write_buf(const nrf_bus *b, uint8_t reg, const uint8_t *buf, uint8_t n)
{
    uint8_t tx[6];
    uint8_t rx[6];
    tx[0] = (uint8_t)(Nrf24Cmd::CMD_W_REGISTER | reg);
    for (uint8_t i = 0; i < n; i++)
    {
        tx[1 + i] = buf[i];
    }
    b->spi(tx, rx, (uint8_t)(n + 1), b->ctx);
}

static uint8_t status(const nrf_bus *b)
{
    uint8_t tx[1] = {Nrf24Cmd::CMD_NOP};
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

proto_bool pc_nrf24_init(const nrf_bus *bus, const nrf_config *cfg)
{
    if (!bus || !bus->spi || !bus->ce || !cfg || !cfg->address)
    {
        return PROTO_FALSE;
    }
    bus->ce(PROTO_FALSE, bus->ctx);

    reg_write(bus, Nrf24Reg::REG_CONFIG, Nrf24Cfg::CFG_EN_CRC | Nrf24Cfg::CFG_CRCO); // power down, 16-bit CRC
    reg_write(bus, Nrf24Reg::REG_RF_CH, cfg->channel);
    if (reg_read(bus, Nrf24Reg::REG_RF_CH) != cfg->channel)
    {
        return PROTO_FALSE; // written value did not read back -> no chip on the bus
    }

    reg_write(bus, Nrf24Reg::REG_SETUP_AW, 0x03);   // 5-byte addresses
    reg_write(bus, Nrf24Reg::REG_EN_RXADDR, 0x01);  // enable pipe 0
    reg_write(bus, Nrf24Reg::REG_EN_AA, 0x00);      // raw mode (no auto-ack)
    reg_write(bus, Nrf24Reg::REG_SETUP_RETR, 0x00); // no auto-retransmit
    uint8_t dr = 0x00;                              // 1 Mbps
    if (cfg->data_rate == 1)
    {
        dr = 0x08; // 2 Mbps
    }
    else if (cfg->data_rate == 2)
    {
        dr = 0x20; // 250 kbps
    }
    reg_write(bus, Nrf24Reg::REG_RF_SETUP, (uint8_t)(dr | ((cfg->tx_power & 0x03) << 1)));
    reg_write(bus, Nrf24Reg::REG_RX_PW_P0, PC_NRF24_PAYLOAD);
    reg_write_buf(bus, Nrf24Reg::REG_RX_ADDR_P0, cfg->address, 5);
    reg_write_buf(bus, Nrf24Reg::REG_TX_ADDR, cfg->address, 5);

    cmd(bus, Nrf24Cmd::CMD_FLUSH_RX);
    cmd(bus, Nrf24Cmd::CMD_FLUSH_TX);
    reg_write(bus, Nrf24Reg::REG_STATUS, Nrf24Status::ST_RX_DR | Nrf24Status::ST_TX_DS | 0x10); // clear all flags

    reg_write(bus, Nrf24Reg::REG_CONFIG,
              Nrf24Cfg::CFG_EN_CRC | Nrf24Cfg::CFG_CRCO | Nrf24Cfg::CFG_PWR_UP); // power up (standby)
    return PROTO_TRUE;
}

proto_bool pc_nrf24_send(const nrf_bus *bus, const uint8_t *data, uint8_t len)
{
    if (!bus || !data || len == 0 || len > PC_NRF24_PAYLOAD)
    {
        return PROTO_FALSE;
    }
    bus->ce(PROTO_FALSE, bus->ctx);
    reg_write(bus, Nrf24Reg::REG_CONFIG,
              Nrf24Cfg::CFG_EN_CRC | Nrf24Cfg::CFG_CRCO | Nrf24Cfg::CFG_PWR_UP); // PRIM_RX = 0 -> PTX

    uint8_t tx[1 + PC_NRF24_PAYLOAD];
    uint8_t rx[1 + PC_NRF24_PAYLOAD];
    tx[0] = Nrf24Cmd::CMD_W_TX_PAYLOAD;
    for (uint8_t i = 0; i < PC_NRF24_PAYLOAD; i++)
    {
        tx[1 + i] = (i < len) ? data[i] : 0x00; // zero-pad to the static width
    }
    bus->spi(tx, rx, (uint8_t)(PC_NRF24_PAYLOAD + 1), bus->ctx);

    bus->ce(PROTO_TRUE, bus->ctx); // key the transmit
    return PROTO_TRUE;
}

proto_bool pc_nrf24_tx_done(const nrf_bus *bus)
{
    if (!bus)
    {
        return PROTO_FALSE;
    }
    if (status(bus) & Nrf24Status::ST_TX_DS)
    {
        reg_write(bus, Nrf24Reg::REG_STATUS, Nrf24Status::ST_TX_DS); // write-1-to-clear
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

void pc_nrf24_set_rx(const nrf_bus *bus)
{
    if (!bus)
    {
        return;
    }
    reg_write(bus, Nrf24Reg::REG_CONFIG,
              Nrf24Cfg::CFG_EN_CRC | Nrf24Cfg::CFG_CRCO | Nrf24Cfg::CFG_PWR_UP | Nrf24Cfg::CFG_PRIM_RX); // PRX
    bus->ce(PROTO_TRUE, bus->ctx);
}

int pc_nrf24_recv(const nrf_bus *bus, uint8_t *buf, uint8_t cap, uint8_t *pipe)
{
    if (!bus || !buf)
    {
        return -1;
    }
    uint8_t st = status(bus);
    if (!(st & Nrf24Status::ST_RX_DR))
    {
        return -1; // nothing received
    }
    uint8_t p = (uint8_t)((st & Nrf24Status::ST_RX_P_NO) >> 1);
    if (p > 5) // 0x07 = RX FIFO empty
    {
        reg_write(bus, Nrf24Reg::REG_STATUS, Nrf24Status::ST_RX_DR);
        return -1;
    }
    uint8_t tx[1 + PC_NRF24_PAYLOAD];
    uint8_t rx[1 + PC_NRF24_PAYLOAD];
    tx[0] = Nrf24Cmd::CMD_R_RX_PAYLOAD;
    for (uint8_t i = 0; i < PC_NRF24_PAYLOAD; i++)
    {
        tx[1 + i] = 0xFF;
    }
    bus->spi(tx, rx, (uint8_t)(PC_NRF24_PAYLOAD + 1), bus->ctx);

    uint8_t n = (PC_NRF24_PAYLOAD < cap) ? PC_NRF24_PAYLOAD : cap;
    for (uint8_t i = 0; i < n; i++)
    {
        buf[i] = rx[1 + i];
    }
    if (pipe)
    {
        *pipe = p;
    }
    reg_write(bus, Nrf24Reg::REG_STATUS, Nrf24Status::ST_RX_DR); // clear
    return (int)n;
}

#endif // PC_ENABLE_NRF24
