// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_CC1101_H
#define PROTOCORE_CC1101_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

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
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */

// PROTOCORE_CC1101_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

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

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*init)(uint8_t *restrict, const protocore_cc1101_bus *, const protocore_cc1101_config *);
    proto_bool (*send)(uint8_t *restrict, const protocore_cc1101_bus *, const uint8_t *, uint8_t);
    proto_bool (*tx_done)(uint8_t *restrict, const protocore_cc1101_bus *);
    void (*set_rx)(uint8_t *restrict, const protocore_cc1101_bus *);
    int (*recv)(uint8_t *restrict, const protocore_cc1101_bus *, uint8_t *, uint8_t, int16_t *);
    int16_t (*rssi_dbm)(uint8_t *restrict, uint8_t);
} Cc1101Ns;
PROTOCORE_NS_LAYOUT(Cc1101Ns, init, send, tx_done, set_rx, recv, rssi_dbm);

/**
 * @brief Reset the CC1101, apply cfg, set the channel, and confirm it is .
 * @param work PROTOCORE_CC1101_BORROW bytes the caller took. Not held past the call.
 * @param bus Bus
 * @param cfg Cfg
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_cc1101_init(uint8_t *restrict work, const protocore_cc1101_bus *bus,
                                 const protocore_cc1101_config *cfg);
/**
 * @brief Transmit len bytes as a variable-length packet (leading length .
 * @param work PROTOCORE_CC1101_BORROW bytes the caller took. Not held past the call.
 * @param bus Bus
 * @param data Data
 * @param len Len
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_cc1101_send(uint8_t *restrict work, const protocore_cc1101_bus *bus, const uint8_t *data,
                                 uint8_t len);
/**
 * @brief True once the state machine has returned to IDLE after a transmit.
 * @param work PROTOCORE_CC1101_BORROW bytes the caller took. Not held past the call.
 * @param bus Bus
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_cc1101_tx_done(uint8_t *restrict work, const protocore_cc1101_bus *bus);
/**
 * @brief Flush RX and enter receive mode (strobe RX). Then poll .
 * @param work PROTOCORE_CC1101_BORROW bytes the caller took. Not held past the call.
 * @param bus Bus
 */
void protocore_cc1101_set_rx(uint8_t *restrict work, const protocore_cc1101_bus *bus);
/**
 * @brief If a packet is waiting, read it (length byte + payload + appended .
 * @param work PROTOCORE_CC1101_BORROW bytes the caller took. Not held past the call.
 * @param bus Bus
 * @param buf Buf
 * @param cap Cap
 * @param rssi_dbm Rssi dbm
 * @return The int.
 */
int protocore_cc1101_recv(uint8_t *restrict work, const protocore_cc1101_bus *bus, uint8_t *buf, uint8_t cap,
                          int16_t *rssi_dbm);
/**
 * @brief Convert a raw CC1101 RSSI register value to dBm (TI datasheet .
 * @param work PROTOCORE_CC1101_BORROW bytes the caller took. Not held past the call.
 * @param raw Raw
 * @return The int16_t.
 */
int16_t protocore_cc1101_rssi_dbm(uint8_t *restrict work, uint8_t raw);

/** @brief Full-duplex SPI transfer of @p len bytes (chip-select toggled by the callback). */
typedef void (*protocore_cc1101_spi_fn)(const uint8_t *tx, uint8_t *rx, uint8_t len, void *ctx);

/** @brief Module namespace. */
PROTOCORE_NS Cc1101Ns Cc1101 PROTOCORE_UNUSED = {.init = protocore_cc1101_init,
                                                 .send = protocore_cc1101_send,
                                                 .tx_done = protocore_cc1101_tx_done,
                                                 .set_rx = protocore_cc1101_set_rx,
                                                 .recv = protocore_cc1101_recv,
                                                 .rssi_dbm = protocore_cc1101_rssi_dbm};

PROTOCORE_END_DECLS

#endif // PROTOCORE_CC1101_H
