// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_SMBUS_H
#define PROTOCORE_SMBUS_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

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
 * (::PROTOCORE_CRC8_SMBUS in shared/crc/crc.h) rather than a loop written here. Turn it on with
 * ::protocore_smbus_set_pec; a part that does not implement PEC NACKs the extra byte.
 *
 * The PEC computation is pure and host-tested. The transfers are I2C, so a build with no bus seam
 * refuses them.
 *
 * @c work is PROTOCORE_SMBUS_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#define PROTOCORE_SMBUS_BLOCK_MAX 32

#define PROTOCORE_SMBUS_WRITE 0u

#define PROTOCORE_SMBUS_READ 1u

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    uint8_t (*addr_byte)(uint8_t *restrict, uint8_t, uint8_t);
    uint8_t (*pec_write)(uint8_t *restrict, uint8_t, const uint8_t *, size_t);
    uint8_t (*pec_read)(uint8_t *restrict, uint8_t, const uint8_t *, size_t, const uint8_t *, size_t);
    void (*set_pec)(uint8_t *restrict, proto_bool);
    proto_bool (*pec_enabled)(uint8_t *restrict);
    proto_bool (*begin)(uint8_t *restrict);
    proto_bool (*quick)(uint8_t *restrict, uint8_t, uint8_t);
    proto_bool (*send_byte)(uint8_t *restrict, uint8_t, uint8_t);
    proto_bool (*receive_byte)(uint8_t *restrict, uint8_t, uint8_t *);
    proto_bool (*write_byte)(uint8_t *restrict, uint8_t, uint8_t, uint8_t);
    proto_bool (*read_byte)(uint8_t *restrict, uint8_t, uint8_t, uint8_t *);
    proto_bool (*write_word)(uint8_t *restrict, uint8_t, uint8_t, uint16_t);
    proto_bool (*read_word)(uint8_t *restrict, uint8_t, uint8_t, uint16_t *);
    proto_bool (*write_block)(uint8_t *restrict, uint8_t, uint8_t, const uint8_t *, size_t);
    proto_bool (*read_block)(uint8_t *restrict, uint8_t, uint8_t, uint8_t *, size_t, size_t *);
    proto_bool (*process_call)(uint8_t *restrict, uint8_t, uint8_t, uint16_t, uint16_t *);
    proto_bool (*block_process_call)(uint8_t *restrict, uint8_t, uint8_t, const uint8_t *, size_t, uint8_t *, size_t,
                                     size_t *);
} SmbusNs;
PROTOCORE_NS_LAYOUT(SmbusNs, addr_byte, pec_write, pec_read, set_pec, pec_enabled, begin, quick, send_byte,
                    receive_byte, write_byte, read_byte, write_word, read_word, write_block, read_block, process_call,
                    block_process_call);

/**
 * @brief The address byte as it goes on the wire: the 7-bit address shifted .
 * @param work PROTOCORE_SMBUS_BORROW bytes the caller took. Not held past the call.
 * @param addr Addr
 * @param rw Rw
 * @return The uint8_t.
 */
uint8_t protocore_smbus_addr_byte(uint8_t *restrict work, uint8_t addr, uint8_t rw);
/**
 * @brief PEC over a write transaction: the write address byte, then len .
 * @param work PROTOCORE_SMBUS_BORROW bytes the caller took. Not held past the call.
 * @param addr 7-bit device address
 * @param payload everything after the address byte (command, then data)
 * @param len Len
 * @return The uint8_t.
 */
uint8_t protocore_smbus_pec_write(uint8_t *restrict work, uint8_t addr, const uint8_t *payload, size_t len);
/**
 * @brief PEC over a read transaction, which covers both halves and the .
 * @param work PROTOCORE_SMBUS_BORROW bytes the caller took. Not held past the call.
 * @param addr Addr
 * @param sent Sent
 * @param slen Slen
 * @param got Got
 * @param glen Glen
 * @return The uint8_t.
 */
uint8_t protocore_smbus_pec_read(uint8_t *restrict work, uint8_t addr, const uint8_t *sent, size_t slen,
                                 const uint8_t *got, size_t glen);
/**
 * @brief Turn the Packet Error Code on or off for every transaction that .
 * @param work PROTOCORE_SMBUS_BORROW bytes the caller took. Not held past the call.
 * @param on On
 */
void protocore_smbus_set_pec(uint8_t *restrict work, proto_bool on);
/**
 * @brief Whether the Packet Error Code is on.
 * @param work PROTOCORE_SMBUS_BORROW bytes the caller took. Not held past the call.
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smbus_pec_enabled(uint8_t *restrict work);
/**
 * @brief Bring up the shared I2C bus for SMBus traffic.
 * @param work PROTOCORE_SMBUS_BORROW bytes the caller took. Not held past the call.
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smbus_begin(uint8_t *restrict work);
/**
 * @brief Quick command: address the part with rw and stop. The direction bit .
 * @param work PROTOCORE_SMBUS_BORROW bytes the caller took. Not held past the call.
 * @param addr Addr
 * @param rw Rw
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smbus_quick(uint8_t *restrict work, uint8_t addr, uint8_t rw);
/**
 * @brief Send byte: one byte with no command code in front of it.
 * @param work PROTOCORE_SMBUS_BORROW bytes the caller took. Not held past the call.
 * @param addr Addr
 * @param value Value
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smbus_send_byte(uint8_t *restrict work, uint8_t addr, uint8_t value);
/**
 * @brief Receive byte: one byte with no command code, from whatever the part .
 * @param work PROTOCORE_SMBUS_BORROW bytes the caller took. Not held past the call.
 * @param addr Addr
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smbus_receive_byte(uint8_t *restrict work, uint8_t addr, uint8_t *out);
/**
 * @brief Write byte: cmd then one data byte.
 * @param work PROTOCORE_SMBUS_BORROW bytes the caller took. Not held past the call.
 * @param addr Addr
 * @param cmd Cmd
 * @param value Value
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smbus_write_byte(uint8_t *restrict work, uint8_t addr, uint8_t cmd, uint8_t value);
/**
 * @brief Read byte: cmd, a repeated start, then one data byte back.
 * @param work PROTOCORE_SMBUS_BORROW bytes the caller took. Not held past the call.
 * @param addr Addr
 * @param cmd Cmd
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smbus_read_byte(uint8_t *restrict work, uint8_t addr, uint8_t cmd, uint8_t *out);
/**
 * @brief Write word: cmd then two data bytes, low byte first.
 * @param work PROTOCORE_SMBUS_BORROW bytes the caller took. Not held past the call.
 * @param addr Addr
 * @param cmd Cmd
 * @param value Value
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smbus_write_word(uint8_t *restrict work, uint8_t addr, uint8_t cmd, uint16_t value);
/**
 * @brief Read word: cmd, a repeated start, then two data bytes back, low .
 * @param work PROTOCORE_SMBUS_BORROW bytes the caller took. Not held past the call.
 * @param addr Addr
 * @param cmd Cmd
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smbus_read_word(uint8_t *restrict work, uint8_t addr, uint8_t cmd, uint16_t *out);
/**
 * @brief Block write: cmd, a count byte, then len payload bytes (at most .
 * @param work PROTOCORE_SMBUS_BORROW bytes the caller took. Not held past the call.
 * @param addr Addr
 * @param cmd Cmd
 * @param buf Buf
 * @param len Len
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smbus_write_block(uint8_t *restrict work, uint8_t addr, uint8_t cmd, const uint8_t *buf,
                                       size_t len);
/**
 * @brief Block read: cmd, a repeated start, then a count byte and that many .
 * @param work PROTOCORE_SMBUS_BORROW bytes the caller took. Not held past the call.
 * @param addr Addr
 * @param cmd Cmd
 * @param out caller-owned, cap bytes
 * @param cap Cap
 * @param len out: how many bytes the part returned
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smbus_read_block(uint8_t *restrict work, uint8_t addr, uint8_t cmd, uint8_t *out, size_t cap,
                                      size_t *len);
/**
 * @brief Process call: write a word to cmd and read a word back in the same .
 * @param work PROTOCORE_SMBUS_BORROW bytes the caller took. Not held past the call.
 * @param addr Addr
 * @param cmd Cmd
 * @param value Value
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smbus_process_call(uint8_t *restrict work, uint8_t addr, uint8_t cmd, uint16_t value,
                                        uint16_t *out);
/**
 * @brief Block process call: write len bytes to cmd and read a block back in .
 * @param work PROTOCORE_SMBUS_BORROW bytes the caller took. Not held past the call.
 * @param addr Addr
 * @param cmd Cmd
 * @param buf Buf
 * @param len Len
 * @param out Out
 * @param cap Cap
 * @param out_len Out len
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_smbus_block_process_call(uint8_t *restrict work, uint8_t addr, uint8_t cmd, const uint8_t *buf,
                                              size_t len, uint8_t *out, size_t cap, size_t *out_len);

/**
 * @brief The PROTOCORE_SMBUS_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_smbus_span(void);

/** @brief Module namespace. */
PROTOCORE_NS SmbusNs Smbus PROTOCORE_UNUSED = {.addr_byte = protocore_smbus_addr_byte,
                                               .pec_write = protocore_smbus_pec_write,
                                               .pec_read = protocore_smbus_pec_read,
                                               .set_pec = protocore_smbus_set_pec,
                                               .pec_enabled = protocore_smbus_pec_enabled,
                                               .begin = protocore_smbus_begin,
                                               .quick = protocore_smbus_quick,
                                               .send_byte = protocore_smbus_send_byte,
                                               .receive_byte = protocore_smbus_receive_byte,
                                               .write_byte = protocore_smbus_write_byte,
                                               .read_byte = protocore_smbus_read_byte,
                                               .write_word = protocore_smbus_write_word,
                                               .read_word = protocore_smbus_read_word,
                                               .write_block = protocore_smbus_write_block,
                                               .read_block = protocore_smbus_read_block,
                                               .process_call = protocore_smbus_process_call,
                                               .block_process_call = protocore_smbus_block_process_call};

PROTOCORE_END_DECLS

#endif // PROTOCORE_SMBUS_H
