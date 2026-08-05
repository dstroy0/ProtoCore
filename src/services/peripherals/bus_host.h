// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file bus_host.h
 * @brief Host-side capture for the I2C, SPI and UART owners, so wire output is testable.
 *
 * On a host build the bus owners have no controller to drive. Refusing every transfer makes them
 * safe but leaves the interesting half of a driver untested: which bytes it composes, in what
 * order, to which address. This records each transfer instead, and hands back bytes a test queued
 * in advance, so a suite drives a real driver through the real owner and asserts what came out.
 *
 * A test does three things: ::pc_bus_host_reset to start clean, ::pc_bus_host_load to queue what
 * the device would answer with, and ::pc_bus_host_tx or ::pc_bus_host_txn to read back what the
 * driver put on the wire.
 *
 * This exists only where PROTOCORE_HOT is 0. On a target the owners talk to the controller and
 * none of this is compiled.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_BUS_HOST_H
#define PROTOCORE_BUS_HOST_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if !PROTOCORE_HOT

/** @brief Bytes the capture holds across all transfers since the last reset. */
#ifndef PC_BUS_HOST_POOL
#define PC_BUS_HOST_POOL 512
#endif

/** @brief Transfers the capture records before it stops recording. */
#ifndef PC_BUS_HOST_MAX_TXN
#define PC_BUS_HOST_MAX_TXN 32
#endif

/** @brief Which owner ran a transfer. */
typedef enum
{
    PC_BUS_HOST_I2C = 0,
    PC_BUS_HOST_SPI = 1,
    PC_BUS_HOST_UART = 2,
} pc_bus_host_kind;

/** @brief One recorded transfer: what it addressed, what went out, and how much came back. */
typedef struct
{
    uint8_t kind;    ///< a ::pc_bus_host_kind
    uint16_t target; ///< I2C address, SPI host, or UART unit
    uint16_t wlen;   ///< bytes written, at @c woff in the write pool
    uint16_t woff;
    uint16_t rlen; ///< bytes the caller read back
} pc_bus_host_txn;

/** @brief Drop every recorded transfer and every queued response byte. */
void pc_bus_host_reset(void);

/** @brief Queue @p len bytes for the reads that follow, in the order they will be handed out. */
void pc_bus_host_load(const uint8_t *bytes, size_t len);

/** @brief Every byte written since the reset, concatenated across transfers. */
const uint8_t *pc_bus_host_tx(size_t *len);

/** @brief How many transfers were recorded. */
size_t pc_bus_host_count(void);

/** @brief Transfer @p i, or NULL past the end. */
const pc_bus_host_txn *pc_bus_host_txn_at(size_t i);

/** @brief The bytes transfer @p i wrote, or NULL past the end. */
const uint8_t *pc_bus_host_txn_bytes(size_t i, size_t *len);

/** @brief Make the next @p n transfers report failure, as a device that does not answer would. */
void pc_bus_host_fail_next(size_t n);

/** @brief Queued bytes not yet handed out, which is what a UART reports as available. */
size_t pc_bus_host_avail(void);

// Recording entry points. The bus owners call these; a test does not.
int pc_bus_host_write(uint8_t kind, uint16_t target, const uint8_t *buf, size_t len);
int pc_bus_host_read(uint8_t kind, uint16_t target, uint8_t *buf, size_t len);
int pc_bus_host_write_read(uint8_t kind, uint16_t target, const uint8_t *w, size_t wlen, uint8_t *r, size_t rlen);

#endif // !PROTOCORE_HOT

PROTO_END_DECLS

#endif // PROTOCORE_BUS_HOST_H
