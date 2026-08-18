// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file nrf24.h
 * @brief nRF24L01+ radio driver (PROTOCORE_ENABLE_NRF24) - Nordic 2.4 GHz over SPI.
 *
 * A radio driver plugin for the gateway (PROTOCORE_ENABLE_GATEWAY): cheap point-to-multipoint
 * 2.4 GHz sensor links bridged to the web stack. Unlike the SX127x (plain register
 * read/write), the nRF24L01+ speaks an **SPI command protocol** (each transaction is a
 * command byte + data, and every command returns the STATUS register) and needs a separate
 * **CE** pin to key RX/TX - so the driver runs over an @ref nrf_bus that carries a
 * full-duplex SPI transfer plus a CE-set callback. That is the only board-specific code.
 *
 * The nRF24 does its own **hardware addressing** (5-byte pipe addresses), so a received
 * frame's "source" is the pipe number it arrived on - there is no in-payload header and
 * therefore no separate codec. It uses a **static payload width** (PROTOCORE_NRF24_PAYLOAD):
 * every frame is that many bytes (a short send is zero-padded). Bridge received payloads
 * northbound with protocore_gateway_uplink(port, pipe, payload, width, 0). The register/command
 * protocol is host-testable against a mock; the RF link needs the module.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_NRF24_H
#define PROTOCORE_NRF24_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_NRF24

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief Full-duplex SPI transfer of @p len bytes (chip-select toggled by the callback). */
typedef void (*nrf_spi_fn)(const uint8_t *tx, uint8_t *rx, uint8_t len, void *ctx);

/** @brief Drive the CE pin (true = high). */
typedef void (*nrf_ce_fn)(proto_bool level, void *ctx);

/** @brief The bus a driver call uses: your SPI transfer + CE control behind it. */
typedef struct
{
    nrf_spi_fn spi;
    nrf_ce_fn ce;
    void *ctx;
} nrf_bus;

/** @brief Radio configuration applied by protocore_nrf24_init(). */
typedef struct
{
    const uint8_t *address; ///< 5-byte pipe-0 / TX address (RX and TX share it here).
    uint8_t channel;        ///< RF channel 0..125 (2400 + channel MHz).
    uint8_t data_rate;      ///< 0 = 1 Mbps, 1 = 2 Mbps, 2 = 250 kbps.
    uint8_t tx_power;       ///< power level 0..3 (-18, -12, -6, 0 dBm).
} nrf_config;

/** @brief What init takes: bus, cfg. */
typedef struct
{
    const nrf_bus *bus;
    const nrf_config *cfg;
} Nrf24InitArgs;

/** @brief What send takes: bus, data, len. */
typedef struct
{
    const nrf_bus *bus;
    const uint8_t *data;
    uint8_t len;
} Nrf24SendArgs;

/** @brief What tx_done takes: bus. */
typedef struct
{
    const nrf_bus *bus;
} Nrf24TxDoneArgs;

/** @brief What set_rx takes: bus. */
typedef struct
{
    const nrf_bus *bus;
} Nrf24SetRxArgs;

/** @brief What recv takes: bus, buf, cap, pipe. */
typedef struct
{
    const nrf_bus *bus;
    uint8_t *buf;
    uint8_t cap;
    uint8_t *pipe;
} Nrf24RecvArgs;

/**
 * @brief nRF24L01+ radio driver (PROTOCORE_ENABLE_NRF24) - Nordic 2.4 GHz over SPI.
 *
 * A caller sets the members a call takes, invokes it through ::Nrf24 with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Nrf24.init_args.bus = ...;
 *   Nrf24.init_args.cfg = ...;
 *   Nrf24.init(work);
 *   // Nrf24.ok is what the call reports
 *
 * @var Nrf24Ns::init_args  what init takes: bus, cfg
 * @var Nrf24Ns::send_args  what send takes: bus, data, len
 * @var Nrf24Ns::tx_done_args  what tx_done takes: bus
 * @var Nrf24Ns::set_rx_args  what set_rx takes: bus
 * @var Nrf24Ns::recv_args  what recv takes: bus, buf, cap, pipe
 * @var Nrf24Ns::ok  true; false if a written register does not read back - i.e. the bus ...
 * @var Nrf24Ns::n  the payload width (PROTOCORE_NRF24_PAYLOAD, capped at cap), or -1 ...
 * @var Nrf24Ns::init  configure the nRF24L01+ and power it up (standby)
 * @var Nrf24Ns::send  transmit len bytes (zero-padded to PROTOCORE_NRF24_PAYLOAD). Poll ...
 * @var Nrf24Ns::tx_done  true once a transmit has finished (STATUS TX_DS); clears the flag
 * @var Nrf24Ns::set_rx  enter receive mode (PRX + CE high); then poll protocore_nrf24_recv()
 * @var Nrf24Ns::recv  if a frame is waiting, copy it into buf and report the pipe it ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    Nrf24InitArgs init_args;
    Nrf24SendArgs send_args;
    Nrf24TxDoneArgs tx_done_args;
    Nrf24SetRxArgs set_rx_args;
    Nrf24RecvArgs recv_args;

    proto_bool ok;
    int n;

    void (*const init)(uint8_t *restrict work);
    void (*const send)(uint8_t *restrict work);
    void (*const tx_done)(uint8_t *restrict work);
    void (*const set_rx)(uint8_t *restrict work);
    void (*const recv)(uint8_t *restrict work);
} Nrf24Ns;

/** @brief The one symbol this module exports. */
extern Nrf24Ns Nrf24;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_NRF24

#endif // PROTOCORE_NRF24_H
