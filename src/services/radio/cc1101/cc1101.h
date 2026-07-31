// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cc1101.h
 * @brief CC1101 sub-GHz radio driver (PC_ENABLE_CC1101) - TI 300-928 MHz over SPI.
 *
 * A radio driver plugin for the gateway (PC_ENABLE_GATEWAY): generic ISM-band remotes and sensors
 * (OOK / 2-FSK on 315/433/868/915 MHz) bridged to the web stack. Like the nRF24, the CC1101 speaks an
 * **SPI header protocol** - every transaction begins with a header byte (bit7 = read, bit6 = burst,
 * bits5-0 = address) and returns the **chip status byte** (CHIP_RDYn, the 3-bit state machine value, and
 * the FIFO-bytes-available count). Config registers live at 0x00-0x2E, the 13 command **strobes** at
 * 0x30-0x3D (a single write triggers the command), the read-only **status** registers at 0x30-0x3D read
 * with the burst bit set, and both FIFOs at 0x3F.
 *
 * The huge modem configuration (band, data rate, deviation, sync word) is board/tool-specific, so the
 * caller supplies it as a register table (a TI SmartRF Studio export); the driver resets the chip, writes
 * that table, sets the channel, and verifies the VERSION status register talks back. Packets use variable
 * length mode (a leading length byte) with appended RSSI/LQI status. Bridge received payloads northbound
 * with pc_gateway_uplink. The register/strobe/FIFO protocol is host-testable against a mock; the RF link
 * needs the module.
 */

#ifndef PROTOCORE_CC1101_H
#define PROTOCORE_CC1101_H

#include "protocore_config.h"

#if PC_ENABLE_CC1101

#include <stddef.h>
#include <stdint.h>

/** @brief Full-duplex SPI transfer of @p len bytes (chip-select toggled by the callback). */
typedef void (*pc_cc1101_spi_fn)(const uint8_t *tx, uint8_t *rx, uint8_t len, void *ctx);

/** @brief The bus a driver call uses: your SPI transfer behind it. */
struct pc_cc1101_bus
{
    pc_cc1101_spi_fn spi;
    void *ctx;
};

/** @brief One modem-config register write (address + value). */
struct pc_cc1101_reg
{
    uint8_t addr;
    uint8_t value;
};

/** @brief Radio configuration applied by pc_cc1101_init(). */
struct pc_cc1101_config
{
    const pc_cc1101_reg *regs; ///< SmartRF-exported register settings (may be null for none).
    size_t nregs;
    uint8_t channel; ///< CHANNR (0x0A): channel number on top of the base frequency.
};

/**
 * @brief Reset the CC1101, apply @p cfg, set the channel, and confirm it is present.
 * @return true; false if the VERSION status register reads 0x00 / 0xFF (the bus is not talking).
 */
bool pc_cc1101_init(const pc_cc1101_bus *bus, const pc_cc1101_config *cfg);

/**
 * @brief Transmit @p len bytes as a variable-length packet (leading length byte), then strobe TX.
 * @return true; false if @p len is 0 or exceeds 63 (one FIFO fill).
 */
bool pc_cc1101_send(const pc_cc1101_bus *bus, const uint8_t *data, uint8_t len);

/** @brief True once the state machine has returned to IDLE after a transmit. */
bool pc_cc1101_tx_done(const pc_cc1101_bus *bus);

/** @brief Flush RX and enter receive mode (strobe RX). Then poll pc_cc1101_recv(). */
void pc_cc1101_set_rx(const pc_cc1101_bus *bus);

/**
 * @brief If a packet is waiting, read it (length byte + payload + appended RSSI/LQI status).
 * @param[out] rssi_dbm set to the decoded RSSI in dBm (may be null).
 * @return the payload length (capped at @p cap), or -1 if the RX FIFO is empty.
 */
int pc_cc1101_recv(const pc_cc1101_bus *bus, uint8_t *buf, uint8_t cap, int16_t *rssi_dbm);

/** @brief Convert a raw CC1101 RSSI register value to dBm (TI datasheet formula). Pure. */
int16_t pc_cc1101_rssi_dbm(uint8_t raw);

#endif // PC_ENABLE_CC1101

#endif // PROTOCORE_CC1101_H
