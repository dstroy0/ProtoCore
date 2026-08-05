// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file smbus.h
 * @brief SMBus 3.1 transaction shapes over the shared I2C bus.
 *
 * SMBus is I2C with the transaction shapes named and a checksum defined. A part that speaks it
 * (a battery gauge, a fan controller, a power sequencer, a temperature sensor) answers a fixed
 * set of forms rather than whatever register layout its datasheet invents, so one driver reaches
 * all of them: quick command, send / receive byte, write / read byte and word, block write and
 * read, and the two process calls.
 *
 * The Packet Error Code is a CRC-8 over every byte of the transaction, the address bytes and
 * their R/W bits included. It is the catalogue's CRC-8/SMBUS, so it comes from the shared engine
 * (::PC_CRC8_SMBUS in shared_primitives/crc.h) rather than a loop written here. Turn it on with
 * ::pc_smbus_set_pec; a part that does not implement PEC NACKs the extra byte.
 *
 * The PEC computation is pure and host-tested. The transfers are I2C, so a host build refuses
 * them.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SMBUS_H
#define PROTOCORE_SMBUS_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_SMBUS

/** @brief Longest block payload the protocol carries. */
#define PC_SMBUS_BLOCK_MAX 32

/** @brief Direction bit of the address byte a transaction opens with. */
#define PC_SMBUS_WRITE 0u
#define PC_SMBUS_READ 1u

/**
 * @brief The address byte as it goes on the wire: the 7-bit address shifted up, @p rw in bit 0.
 *
 * The PEC covers this byte, not the address on its own, so it is computed rather than assumed.
 */
uint8_t pc_smbus_addr_byte(uint8_t addr, uint8_t rw);

/**
 * @brief PEC over a write transaction: the write address byte, then @p len payload bytes.
 * @param addr     7-bit device address.
 * @param payload  everything after the address byte (command, then data).
 */
uint8_t pc_smbus_pec_write(uint8_t addr, const uint8_t *payload, size_t len);

/**
 * @brief PEC over a read transaction, which covers both halves and the repeated-start address.
 *
 * The byte sequence is: the write address byte, @p sent (the command), the read address byte,
 * then @p got (what the part returned).
 */
uint8_t pc_smbus_pec_read(uint8_t addr, const uint8_t *sent, size_t slen, const uint8_t *got, size_t glen);

/** @brief Turn the Packet Error Code on or off for every transaction that follows. */
void pc_smbus_set_pec(proto_bool on);

/** @brief Whether the Packet Error Code is on. */
proto_bool pc_smbus_pec_enabled(void);

/** @brief Bring up the shared I2C bus for SMBus traffic. */
proto_bool pc_smbus_begin(void);

/**
 * @brief Quick command: address the part with @p rw and stop. The direction bit is the payload,
 *        which is how a part is turned on or off with no data at all. Never carries a PEC.
 */
proto_bool pc_smbus_quick(uint8_t addr, uint8_t rw);

/** @brief Send byte: one byte with no command code in front of it. */
proto_bool pc_smbus_send_byte(uint8_t addr, uint8_t value);

/** @brief Receive byte: one byte with no command code, from whatever the part last pointed at. */
proto_bool pc_smbus_receive_byte(uint8_t addr, uint8_t *out);

/** @brief Write byte: @p cmd then one data byte. */
proto_bool pc_smbus_write_byte(uint8_t addr, uint8_t cmd, uint8_t value);

/** @brief Read byte: @p cmd, a repeated start, then one data byte back. */
proto_bool pc_smbus_read_byte(uint8_t addr, uint8_t cmd, uint8_t *out);

/** @brief Write word: @p cmd then two data bytes, low byte first. */
proto_bool pc_smbus_write_word(uint8_t addr, uint8_t cmd, uint16_t value);

/** @brief Read word: @p cmd, a repeated start, then two data bytes back, low byte first. */
proto_bool pc_smbus_read_word(uint8_t addr, uint8_t cmd, uint16_t *out);

/**
 * @brief Block write: @p cmd, a count byte, then @p len payload bytes (at most
 *        ::PC_SMBUS_BLOCK_MAX).
 */
proto_bool pc_smbus_write_block(uint8_t addr, uint8_t cmd, const uint8_t *buf, size_t len);

/**
 * @brief Block read: @p cmd, a repeated start, then a count byte and that many payload bytes.
 * @param out  caller-owned, @p cap bytes.
 * @param len  out: how many bytes the part returned.
 * @return false if the part answered a count over @p cap or over ::PC_SMBUS_BLOCK_MAX.
 */
proto_bool pc_smbus_read_block(uint8_t addr, uint8_t cmd, uint8_t *out, size_t cap, size_t *len);

/** @brief Process call: write a word to @p cmd and read a word back in the same transaction. */
proto_bool pc_smbus_process_call(uint8_t addr, uint8_t cmd, uint16_t value, uint16_t *out);

/**
 * @brief Block process call: write @p len bytes to @p cmd and read a block back in the same
 *        transaction. The two blocks together are capped at ::PC_SMBUS_BLOCK_MAX by the protocol.
 */
proto_bool pc_smbus_block_process_call(uint8_t addr, uint8_t cmd, const uint8_t *buf, size_t len, uint8_t *out,
                                       size_t cap, size_t *out_len);

#endif // PC_ENABLE_SMBUS

PROTO_END_DECLS

#endif // PROTOCORE_SMBUS_H
