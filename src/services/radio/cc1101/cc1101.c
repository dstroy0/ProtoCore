// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cc1101.c
 * @brief CC1101 sub-GHz radio driver (see cc1101.h).
 */

#include "services/radio/cc1101/cc1101.h"

#if PC_ENABLE_CC1101

// SPI header bits.
static const uint8_t READ = 0x80;
static const uint8_t BURST = 0x40;

// Register / strobe / FIFO addresses.
static const uint8_t REG_CHANNR = 0x0A;
static const uint8_t STROBE_SRES = 0x30; ///< reset chip.
static const uint8_t STROBE_SRX = 0x34;  ///< enable RX.
static const uint8_t STROBE_STX = 0x35;  ///< enable TX.
static const uint8_t STROBE_SIDLE = 0x36;
static const uint8_t STROBE_SFRX = 0x3A; ///< flush RX FIFO.
static const uint8_t STROBE_SFTX = 0x3B; ///< flush TX FIFO.
static const uint8_t STAT_VERSION = 0x31;
static const uint8_t STAT_RXBYTES = 0x3B;
static const uint8_t FIFO = 0x3F;

// The chip status byte's state field (bits 6-4).
static const uint8_t STATE_IDLE = 0;

static void write_reg(const pc_cc1101_bus *b, uint8_t addr, uint8_t val)
{
    uint8_t tx[2] = {addr, val}; // header = address (write, single), then value
    uint8_t rx[2] = {0, 0};
    b->spi(tx, rx, 2, b->ctx);
}

static uint8_t read_reg(const pc_cc1101_bus *b, uint8_t addr, proto_bool status)
{
    // Status registers (0x30-0x3D) require the burst bit to distinguish them from strobes.
    uint8_t hdr = (uint8_t)(addr | READ | (status ? BURST : 0));
    uint8_t tx[2] = {hdr, 0};
    uint8_t rx[2] = {0, 0};
    b->spi(tx, rx, 2, b->ctx);
    return rx[1];
}

static void strobe(const pc_cc1101_bus *b, uint8_t cmd)
{
    uint8_t tx[1] = {cmd};
    uint8_t rx[1] = {0};
    b->spi(tx, rx, 1, b->ctx);
}

static uint8_t status_byte(const pc_cc1101_bus *b)
{
    uint8_t tx[1] = {(uint8_t)(0x3D | READ | BURST)}; // SNOP as a read returns the status byte
    uint8_t rx[1] = {0};
    b->spi(tx, rx, 1, b->ctx);
    return rx[0];
}

int16_t pc_cc1101_rssi_dbm(uint8_t raw)
{
    // TI CC1101 datasheet: dBm = (raw >= 128 ? (raw - 256) : raw) / 2 - 74.
    int16_t r = raw >= 128 ? (int16_t)raw - 256 : (int16_t)raw;
    return (int16_t)(r / 2 - 74);
}

proto_bool pc_cc1101_init(const pc_cc1101_bus *bus, const pc_cc1101_config *cfg)
{
    if (!bus || !bus->spi || !cfg)
    {
        return PROTO_FALSE;
    }
    strobe(bus, STROBE_SRES);
    for (size_t i = 0; i < cfg->nregs && cfg->regs; i++)
    {
        write_reg(bus, cfg->regs[i].addr, cfg->regs[i].value);
    }
    write_reg(bus, REG_CHANNR, cfg->channel);
    uint8_t ver = read_reg(bus, STAT_VERSION, PROTO_TRUE);
    return ver != 0x00 && ver != 0xFF; // a floating bus reads all-0 or all-1
}

proto_bool pc_cc1101_send(const pc_cc1101_bus *bus, const uint8_t *data, uint8_t len)
{
    if (!bus || !bus->spi || !data || len == 0 || len > 63)
    {
        return PROTO_FALSE;
    }
    strobe(bus, STROBE_SIDLE);
    strobe(bus, STROBE_SFTX);
    // Burst-write the FIFO: header, length byte, payload.
    uint8_t tx[65];
    uint8_t rx[65];
    tx[0] = (uint8_t)(FIFO | BURST);
    tx[1] = len;
    for (uint8_t i = 0; i < len; i++)
    {
        tx[2 + i] = data[i];
    }
    bus->spi(tx, rx, (uint8_t)(2 + len), bus->ctx);
    strobe(bus, STROBE_STX);
    return PROTO_TRUE;
}

proto_bool pc_cc1101_tx_done(const pc_cc1101_bus *bus)
{
    if (!bus || !bus->spi)
    {
        return PROTO_FALSE;
    }
    uint8_t st = (uint8_t)((status_byte(bus) >> 4) & 0x07);
    return st == STATE_IDLE;
}

void pc_cc1101_set_rx(const pc_cc1101_bus *bus)
{
    if (!bus || !bus->spi)
    {
        return;
    }
    strobe(bus, STROBE_SIDLE);
    strobe(bus, STROBE_SFRX);
    strobe(bus, STROBE_SRX);
}

int pc_cc1101_recv(const pc_cc1101_bus *bus, uint8_t *buf, uint8_t cap, int16_t *rssi_dbm)
{
    if (!bus || !bus->spi || !buf)
    {
        return -1;
    }
    uint8_t rxbytes = (uint8_t)(read_reg(bus, STAT_RXBYTES, PROTO_TRUE) & 0x7F); // low 7 bits = count
    if (rxbytes == 0)
    {
        return -1;
    }
    uint8_t len = read_reg(bus, FIFO, PROTO_FALSE); // variable-length: leading length byte
    if (len == 0 || len > 63)
    {
        strobe(bus, STROBE_SFRX); // corrupt length: flush and bail
        return -1;
    }
    // Burst-read payload + 2 appended status bytes (RSSI, LQI/CRC).
    uint8_t tx[66];
    uint8_t rx[66];
    uint8_t n = (uint8_t)(len + 2);
    tx[0] = (uint8_t)(FIFO | READ | BURST);
    for (uint8_t i = 0; i < n; i++)
    {
        tx[1 + i] = 0;
    }
    bus->spi(tx, rx, (uint8_t)(1 + n), bus->ctx);
    if (rssi_dbm)
    {
        *rssi_dbm = pc_cc1101_rssi_dbm(rx[1 + len]); // first appended status byte is raw RSSI
    }
    uint8_t out = len < cap ? len : cap;
    for (uint8_t i = 0; i < out; i++)
    {
        buf[i] = rx[1 + i];
    }
    return out;
}

#endif // PC_ENABLE_CC1101
