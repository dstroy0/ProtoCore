// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cc1101.h
 * @brief CC1101 sub-GHz radio driver (PROTOCORE_ENABLE_CC1101) - TI 300-928 MHz over SPI.
 *
 * A radio driver plugin for the gateway (PROTOCORE_ENABLE_GATEWAY): generic ISM-band remotes and sensors
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
 * with protocore_gateway_uplink. The register/strobe/FIFO protocol is host-testable against a mock; the RF link
 * needs the module.
 */

#ifndef PROTOCORE_CC1101_H
#define PROTOCORE_CC1101_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_CC1101

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief Full-duplex SPI transfer of @p len bytes (chip-select toggled by the callback). */
typedef void (*protocore_cc1101_spi_fn)(const uint8_t *tx, uint8_t *rx, uint8_t len, void *ctx);

/** @brief The bus a driver call uses: your SPI transfer behind it. */
typedef struct
{
    protocore_cc1101_spi_fn spi;
    void *ctx;
} protocore_cc1101_bus;
/** @brief One modem-config register write (address + value). */
typedef struct
{
    uint8_t addr;
    uint8_t value;
} protocore_cc1101_reg;
/** @brief Radio configuration applied by protocore_cc1101_init(). */
typedef struct
{
    const protocore_cc1101_reg *regs; ///< SmartRF-exported register settings (may be null for none).
    size_t nregs;
    uint8_t channel; ///< CHANNR (0x0A): channel number on top of the base frequency.
} protocore_cc1101_config;
/** @brief What init takes: bus, cfg. */
typedef struct
{
    const protocore_cc1101_bus *bus;
    const protocore_cc1101_config *cfg;
} Cc1101InitArgs;
/** @brief What send takes: bus, data, len. */
typedef struct
{
    const protocore_cc1101_bus *bus;
    const uint8_t *data;
    uint8_t len;
} Cc1101SendArgs;
/** @brief What tx_done takes: bus. */
typedef struct
{
    const protocore_cc1101_bus *bus;
} Cc1101TxDoneArgs;
/** @brief What set_rx takes: bus. */
typedef struct
{
    const protocore_cc1101_bus *bus;
} Cc1101SetRxArgs;
/** @brief What recv takes: bus, buf, cap, rssi_dbm. */
typedef struct
{
    const protocore_cc1101_bus *bus;
    uint8_t *buf;
    uint8_t cap;
    int16_t *rssi_dbm;
} Cc1101RecvArgs;
/** @brief What rssi_dbm takes: raw. */
typedef struct
{
    uint8_t raw;
} Cc1101RssiDbmArgs;
/**
 * @brief CC1101 sub-GHz radio driver (PROTOCORE_ENABLE_CC1101) - TI 300-928 MHz over SPI.
 *
 * A caller sets the members a call takes, invokes it through ::Cc1101 with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Cc1101.init_args.bus = ...;
 *   Cc1101.init_args.cfg = ...;
 *   Cc1101.init(work);
 *   // Cc1101.ok is what the call reports
 *
 * @var Cc1101Ns::init_args  what init takes: bus, cfg
 * @var Cc1101Ns::send_args  what send takes: bus, data, len
 * @var Cc1101Ns::tx_done_args  what tx_done takes: bus
 * @var Cc1101Ns::set_rx_args  what set_rx takes: bus
 * @var Cc1101Ns::recv_args  what recv takes: bus, buf, cap, rssi_dbm
 * @var Cc1101Ns::rssi_dbm_args  what rssi_dbm takes: raw
 * @var Cc1101Ns::ok  true; false if the VERSION status register reads 0x00 / 0xFF (the ...
 * @var Cc1101Ns::n  the payload length (capped at cap), or -1 if the RX FIFO is empty
 * @var Cc1101Ns::value  the value a call reports
 * @var Cc1101Ns::init  reset the CC1101, apply cfg, set the channel, and confirm it is ...
 * @var Cc1101Ns::send  transmit len bytes as a variable-length packet (leading length ...
 * @var Cc1101Ns::tx_done  true once the state machine has returned to IDLE after a transmit
 * @var Cc1101Ns::set_rx  flush RX and enter receive mode (strobe RX). Then poll ...
 * @var Cc1101Ns::recv  if a packet is waiting, read it (length byte + payload + appended ...
 * @var Cc1101Ns::rssi_dbm  convert a raw CC1101 RSSI register value to dBm (TI datasheet ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    Cc1101InitArgs init_args;
    Cc1101SendArgs send_args;
    Cc1101TxDoneArgs tx_done_args;
    Cc1101SetRxArgs set_rx_args;
    Cc1101RecvArgs recv_args;
    Cc1101RssiDbmArgs rssi_dbm_args;
    proto_bool ok;
    int n;
    int16_t value;
} Cc1101Vars;

/** @brief The operands and the outcome. */
extern Cc1101Vars Cc1101V;

/** @brief The entries. */
typedef struct
{
    void (*const init)(uint8_t *restrict work);
    void (*const send)(uint8_t *restrict work);
    void (*const tx_done)(uint8_t *restrict work);
    void (*const set_rx)(uint8_t *restrict work);
    void (*const recv)(uint8_t *restrict work);
    void (*const rssi_dbm)(uint8_t *restrict work);
} Cc1101Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Cc1101V or a region of the borrow at a fixed offset.
void protocore_cc1101_init(uint8_t *restrict work);
void protocore_cc1101_send(uint8_t *restrict work);
void protocore_cc1101_tx_done(uint8_t *restrict work);
void protocore_cc1101_set_rx(uint8_t *restrict work);
void protocore_cc1101_recv(uint8_t *restrict work);
void protocore_cc1101_rssi_dbm(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Cc1101.init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Cc1101Ns Cc1101 __attribute__((unused)) = {
    .init = protocore_cc1101_init,
    .send = protocore_cc1101_send,
    .tx_done = protocore_cc1101_tx_done,
    .set_rx = protocore_cc1101_set_rx,
    .recv = protocore_cc1101_recv,
    .rssi_dbm = protocore_cc1101_rssi_dbm,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CC1101

#endif // PROTOCORE_CC1101_H
