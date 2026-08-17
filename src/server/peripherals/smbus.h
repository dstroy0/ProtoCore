// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
 * (::PROTOCORE_CRC8_SMBUS in shared/crc/crc.h) rather than a loop written here. Turn it on with
 * ::protocore_smbus_set_pec; a part that does not implement PEC NACKs the extra byte.
 *
 * The PEC computation is pure and host-tested. The transfers are I2C, so a build with no bus seam
 * refuses them.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SMBUS_H
#define PROTOCORE_SMBUS_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SMBUS

PROTOCORE_BEGIN_DECLS

// PROTOCORE_SMBUS_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

#define PROTOCORE_SMBUS_BLOCK_MAX 32

#define PROTOCORE_SMBUS_WRITE 0u

#define PROTOCORE_SMBUS_READ 1u

/** @brief What addr_byte takes: addr, rw. */
typedef struct
{
    uint8_t addr;
    uint8_t rw;
} SmbusAddrByteArgs;

/** @brief What pec_write takes: addr, payload, len. */
typedef struct
{
    uint8_t addr;           ///< 7-bit device address
    const uint8_t *payload; ///< everything after the address byte (command, then data)
    size_t len;
} SmbusPecWriteArgs;

/** @brief What pec_read takes: addr, sent, slen, got, glen. */
typedef struct
{
    uint8_t addr;
    const uint8_t *sent;
    size_t slen;
    const uint8_t *got;
    size_t glen;
} SmbusPecReadArgs;

/** @brief What set_pec takes: on. */
typedef struct
{
    proto_bool on;
} SmbusSetPecArgs;

/** @brief What quick takes: addr, rw. */
typedef struct
{
    uint8_t addr;
    uint8_t rw;
} SmbusQuickArgs;

/** @brief What send_byte takes: addr, value. */
typedef struct
{
    uint8_t addr;
    uint8_t value;
} SmbusSendByteArgs;

/** @brief What receive_byte takes: addr, out. */
typedef struct
{
    uint8_t addr;
    uint8_t *out;
} SmbusReceiveByteArgs;

/** @brief What write_byte takes: addr, cmd, value. */
typedef struct
{
    uint8_t addr;
    uint8_t cmd;
    uint8_t value;
} SmbusWriteByteArgs;

/** @brief What read_byte takes: addr, cmd, out. */
typedef struct
{
    uint8_t addr;
    uint8_t cmd;
    uint8_t *out;
} SmbusReadByteArgs;

/** @brief What write_word takes: addr, cmd, value. */
typedef struct
{
    uint8_t addr;
    uint8_t cmd;
    uint16_t value;
} SmbusWriteWordArgs;

/** @brief What read_word takes: addr, cmd, out. */
typedef struct
{
    uint8_t addr;
    uint8_t cmd;
    uint16_t *out;
} SmbusReadWordArgs;

/** @brief What write_block takes: addr, cmd, buf, len. */
typedef struct
{
    uint8_t addr;
    uint8_t cmd;
    const uint8_t *buf;
    size_t len;
} SmbusWriteBlockArgs;

/** @brief What read_block takes: addr, cmd, out, cap, len. */
typedef struct
{
    uint8_t addr;
    uint8_t cmd;
    uint8_t *out; ///< caller-owned, cap bytes
    size_t cap;
    size_t *len; ///< out: how many bytes the part returned
} SmbusReadBlockArgs;

/** @brief What process_call takes: addr, cmd, value, out. */
typedef struct
{
    uint8_t addr;
    uint8_t cmd;
    uint16_t value;
    uint16_t *out;
} SmbusProcessCallArgs;

/** @brief What block_process_call takes: addr, cmd, buf, len, out, ... */
typedef struct
{
    uint8_t addr;
    uint8_t cmd;
    const uint8_t *buf;
    size_t len;
    uint8_t *out;
    size_t cap;
    size_t *out_len;
} SmbusBlockProcessCallArgs;

/**
 * @brief SMBus 3.1 transaction shapes over the shared I2C bus.
 *
 * A caller sets the members a call takes, invokes it through ::Smbus with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Smbus.addr_byte_args.addr = ...;
 *   Smbus.addr_byte_args.rw = ...;
 *   Smbus.addr_byte(work);
 *   // Smbus.value is what the call reports
 *
 * @var SmbusNs::addr_byte_args  what addr_byte takes: addr, rw
 * @var SmbusNs::pec_write_args  what pec_write takes: addr, payload, len
 * @var SmbusNs::pec_read_args  what pec_read takes: addr, sent, slen, got, glen
 * @var SmbusNs::set_pec_args  what set_pec takes: on
 * @var SmbusNs::quick_args  what quick takes: addr, rw
 * @var SmbusNs::send_byte_args  what send_byte takes: addr, value
 * @var SmbusNs::receive_byte_args  what receive_byte takes: addr, out
 * @var SmbusNs::write_byte_args  what write_byte takes: addr, cmd, value
 * @var SmbusNs::read_byte_args  what read_byte takes: addr, cmd, out
 * @var SmbusNs::write_word_args  what write_word takes: addr, cmd, value
 * @var SmbusNs::read_word_args  what read_word takes: addr, cmd, out
 * @var SmbusNs::write_block_args  what write_block takes: addr, cmd, buf, len
 * @var SmbusNs::read_block_args  what read_block takes: addr, cmd, out, cap, len
 * @var SmbusNs::process_call_args  what process_call takes: addr, cmd, value, out
 * @var SmbusNs::block_process_call_args  what block_process_call takes: addr, cmd, buf, len, out,
 * @var SmbusNs::ok  false if the part answered a count over cap or over ...
 * @var SmbusNs::value  the value a call reports
 * @var SmbusNs::addr_byte  the address byte as it goes on the wire: the 7-bit address shifted ...
 * @var SmbusNs::pec_write  PEC over a write transaction: the write address byte, then len ...
 * @var SmbusNs::pec_read  PEC over a read transaction, which covers both halves and the ...
 * @var SmbusNs::set_pec  turn the Packet Error Code on or off for every transaction that ...
 * @var SmbusNs::pec_enabled  whether the Packet Error Code is on
 * @var SmbusNs::begin  bring up the shared I2C bus for SMBus traffic
 * @var SmbusNs::quick  quick command: address the part with rw and stop. The direction bit ...
 * @var SmbusNs::send_byte  send byte: one byte with no command code in front of it
 * @var SmbusNs::receive_byte  receive byte: one byte with no command code, from whatever the part ...
 * @var SmbusNs::write_byte  write byte: cmd then one data byte
 * @var SmbusNs::read_byte  read byte: cmd, a repeated start, then one data byte back
 * @var SmbusNs::write_word  write word: cmd then two data bytes, low byte first
 * @var SmbusNs::read_word  read word: cmd, a repeated start, then two data bytes back, low ...
 * @var SmbusNs::write_block  block write: cmd, a count byte, then len payload bytes (at most ...
 * @var SmbusNs::read_block  block read: cmd, a repeated start, then a count byte and that many ...
 * @var SmbusNs::process_call  process call: write a word to cmd and read a word back in the same ...
 * @var SmbusNs::block_process_call  block process call: write len bytes to cmd and read a block back in ...
 *
 * @c work is PROTOCORE_SMBUS_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    SmbusAddrByteArgs addr_byte_args;
    SmbusPecWriteArgs pec_write_args;
    SmbusPecReadArgs pec_read_args;
    SmbusSetPecArgs set_pec_args;
    SmbusQuickArgs quick_args;
    SmbusSendByteArgs send_byte_args;
    SmbusReceiveByteArgs receive_byte_args;
    SmbusWriteByteArgs write_byte_args;
    SmbusReadByteArgs read_byte_args;
    SmbusWriteWordArgs write_word_args;
    SmbusReadWordArgs read_word_args;
    SmbusWriteBlockArgs write_block_args;
    SmbusReadBlockArgs read_block_args;
    SmbusProcessCallArgs process_call_args;
    SmbusBlockProcessCallArgs block_process_call_args;

    proto_bool ok;
    uint8_t value;

    void (*const addr_byte)(uint8_t *restrict work);
    void (*const pec_write)(uint8_t *restrict work);
    void (*const pec_read)(uint8_t *restrict work);
    void (*const set_pec)(uint8_t *restrict work);
    void (*const pec_enabled)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const quick)(uint8_t *restrict work);
    void (*const send_byte)(uint8_t *restrict work);
    void (*const receive_byte)(uint8_t *restrict work);
    void (*const write_byte)(uint8_t *restrict work);
    void (*const read_byte)(uint8_t *restrict work);
    void (*const write_word)(uint8_t *restrict work);
    void (*const read_word)(uint8_t *restrict work);
    void (*const write_block)(uint8_t *restrict work);
    void (*const read_block)(uint8_t *restrict work);
    void (*const process_call)(uint8_t *restrict work);
    void (*const block_process_call)(uint8_t *restrict work);
} SmbusNs;

/** @brief The one symbol this module exports. */
extern SmbusNs Smbus;

/**
 * @brief The PROTOCORE_SMBUS_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_smbus_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SMBUS

#endif // PROTOCORE_SMBUS_H
