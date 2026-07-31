// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file nrf24.h
 * @brief nRF24L01+ radio driver (PC_ENABLE_NRF24) - Nordic 2.4 GHz over SPI.
 *
 * A radio driver plugin for the gateway (PC_ENABLE_GATEWAY): cheap point-to-multipoint
 * 2.4 GHz sensor links bridged to the web stack. Unlike the SX127x (plain register
 * read/write), the nRF24L01+ speaks an **SPI command protocol** (each transaction is a
 * command byte + data, and every command returns the STATUS register) and needs a separate
 * **CE** pin to key RX/TX - so the driver runs over an @ref nrf_bus that carries a
 * full-duplex SPI transfer plus a CE-set callback. That is the only board-specific code.
 *
 * The nRF24 does its own **hardware addressing** (5-byte pipe addresses), so a received
 * frame's "source" is the pipe number it arrived on - there is no in-payload header and
 * therefore no separate codec. It uses a **static payload width** (PC_NRF24_PAYLOAD):
 * every frame is that many bytes (a short send is zero-padded). Bridge received payloads
 * northbound with pc_gateway_uplink(port, pipe, payload, width, 0). The register/command
 * protocol is host-testable against a mock; the RF link needs the module.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_NRF24_H
#define PROTOCORE_NRF24_H

#include "protocore_config.h"

#if PC_ENABLE_NRF24

#include <stddef.h>
#include <stdint.h>

/** @brief Full-duplex SPI transfer of @p len bytes (chip-select toggled by the callback). */
typedef void (*nrf_spi_fn)(const uint8_t *tx, uint8_t *rx, uint8_t len, void *ctx);
/** @brief Drive the CE pin (true = high). */
typedef void (*nrf_ce_fn)(bool level, void *ctx);

/** @brief The bus a driver call uses: your SPI transfer + CE control behind it. */
struct nrf_bus
{
    nrf_spi_fn spi;
    nrf_ce_fn ce;
    void *ctx;
};

/** @brief Radio configuration applied by pc_nrf24_init(). */
struct nrf_config
{
    const uint8_t *address; ///< 5-byte pipe-0 / TX address (RX and TX share it here).
    uint8_t channel;        ///< RF channel 0..125 (2400 + channel MHz).
    uint8_t data_rate;      ///< 0 = 1 Mbps, 1 = 2 Mbps, 2 = 250 kbps.
    uint8_t tx_power;       ///< power level 0..3 (-18, -12, -6, 0 dBm).
};

/**
 * @brief Configure the nRF24L01+ and power it up (standby).
 * @return true; false if a written register does not read back - i.e. the bus is not
 *         talking to the chip.
 */
bool pc_nrf24_init(const nrf_bus *bus, const nrf_config *cfg);

/**
 * @brief Transmit @p len bytes (zero-padded to PC_NRF24_PAYLOAD). Poll pc_nrf24_tx_done().
 * @return true; false if @p len exceeds PC_NRF24_PAYLOAD.
 */
bool pc_nrf24_send(const nrf_bus *bus, const uint8_t *data, uint8_t len);

/** @brief True once a transmit has finished (STATUS TX_DS); clears the flag. */
bool pc_nrf24_tx_done(const nrf_bus *bus);

/** @brief Enter receive mode (PRX + CE high); then poll pc_nrf24_recv(). */
void pc_nrf24_set_rx(const nrf_bus *bus);

/**
 * @brief If a frame is waiting, copy it into @p buf and report the pipe it arrived on.
 * @param[out] pipe set to the receiving pipe number 0..5 (may be null).
 * @return the payload width (PC_NRF24_PAYLOAD, capped at @p cap), or -1 if none.
 */
int pc_nrf24_recv(const nrf_bus *bus, uint8_t *buf, uint8_t cap, uint8_t *pipe);

#endif // PC_ENABLE_NRF24

#endif // PROTOCORE_NRF24_H
