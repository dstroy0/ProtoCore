// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_NRF24_H
#define PROTOCORE_NRF24_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

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
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_NRF24_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

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

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*init)(uint8_t *, const nrf_bus *, const nrf_config *);
    proto_bool (*send)(uint8_t *, const nrf_bus *, const uint8_t *, uint8_t);
    proto_bool (*tx_done)(uint8_t *, const nrf_bus *);
    void (*set_rx)(uint8_t *, const nrf_bus *);
    int (*recv)(uint8_t *, const nrf_bus *, uint8_t *, uint8_t, uint8_t *);
} Nrf24Ns;
PROTOCORE_NS_LAYOUT(Nrf24Ns, init, send, tx_done, set_rx, recv);

/**
 * @brief Configure the nRF24L01+ and power it up (standby).
 * @param work PROTOCORE_NRF24_BORROW bytes the caller took. Not held past the call.
 * @param bus Bus
 * @param cfg Cfg
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_nrf24_init(uint8_t *work, const nrf_bus *bus, const nrf_config *cfg);
/**
 * @brief Transmit len bytes (zero-padded to PROTOCORE_NRF24_PAYLOAD). Poll .
 * @param work PROTOCORE_NRF24_BORROW bytes the caller took. Not held past the call.
 * @param bus Bus
 * @param data Data
 * @param len Len
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_nrf24_send(uint8_t *work, const nrf_bus *bus, const uint8_t *data, uint8_t len);
/**
 * @brief True once a transmit has finished (STATUS TX_DS); clears the flag.
 * @param work PROTOCORE_NRF24_BORROW bytes the caller took. Not held past the call.
 * @param bus Bus
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_nrf24_tx_done(uint8_t *work, const nrf_bus *bus);
/**
 * @brief Enter receive mode (PRX + CE high); then poll protocore_nrf24_recv().
 * @param work PROTOCORE_NRF24_BORROW bytes the caller took. Not held past the call.
 * @param bus Bus
 */
void protocore_nrf24_set_rx(uint8_t *work, const nrf_bus *bus);
/**
 * @brief If a frame is waiting, copy it into buf and report the pipe it .
 * @param work PROTOCORE_NRF24_BORROW bytes the caller took. Not held past the call.
 * @param bus Bus
 * @param buf Buf
 * @param cap Cap
 * @param pipe Pipe
 * @return The int.
 */
int protocore_nrf24_recv(uint8_t *work, const nrf_bus *bus, uint8_t *buf, uint8_t cap, uint8_t *pipe);

/** @brief Full-duplex SPI transfer of @p len bytes (chip-select toggled by the callback). */
typedef void (*nrf_spi_fn)(const uint8_t *tx, uint8_t *rx, uint8_t len, void *ctx);

/** @brief Module namespace. */
PROTOCORE_NS Nrf24Ns Nrf24 PROTOCORE_UNUSED = {.init = protocore_nrf24_init,
                                               .send = protocore_nrf24_send,
                                               .tx_done = protocore_nrf24_tx_done,
                                               .set_rx = protocore_nrf24_set_rx,
                                               .recv = protocore_nrf24_recv};

PROTOCORE_END_DECLS

#endif // PROTOCORE_NRF24_H
