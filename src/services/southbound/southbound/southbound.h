// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file southbound.h
 * @brief Southbound protocol-driver framework (PROTOCORE_ENABLE_SOUTHBOUND).
 *
 * The northbound surface of this library is HTTP/WS/SNMP/etc. to a controller; the *southbound* surface
 * is the field devices it polls and drives (a Modbus slave, a BACnet controller, a raw sensor over
 * SPI/I2C/UART). Today Modbus master is the one such driver, hand-wired by the app. This is the uniform
 * seam every southbound driver plugs into, so the app addresses any field device the same way regardless
 * of the wire protocol underneath: register a driver (a small vtable + its instance context), then
 * read/write *points* (registers, coils, objects) by driver name through one facade.
 *
 * One owner, one API: the framework owns driver lookup + dispatch; each driver owns its own transport
 * (it is handed the point id and returns the value, doing whatever Modbus/BACnet/GPIO I/O it needs). The
 * block (matrix) read/write is the atomic multi-point path - a contiguous span of points moved in one
 * driver call, which a protocol like Modbus can satisfy as a single request.
 *
 * Pure registry + dispatch: no heap, no stdlib, host-testable with a fake driver. Drivers are borrowed
 * (the caller keeps the SouthboundDriver + its ctx alive for the registry's lifetime).
 *
 * A caller sets the members a call takes, invokes it through ::Southbound, and reads the outcome off the
 * same handle. The module exports one symbol, @ref Southbound; everything in southbound.c has internal
 * linkage.
 */

#ifndef PROTOCORE_SOUTHBOUND_H
#define PROTOCORE_SOUTHBOUND_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_SOUTHBOUND

PROTOCORE_BEGIN_DECLS

// Southbound result codes. A dispatch reports SB_OK / a count, or a negative code, through
// SouthboundNs::i32, so a caller's `< 0` and `== SB_OK` checks stay cast-free. A driver may also
// return its own negative transport error, which is passed through unchanged.
#define SB_OK 0               ///< success.
#define SB_ERR_NOT_FOUND -1   ///< no registered driver by that name.
#define SB_ERR_UNSUPPORTED -2 ///< the driver does not implement that operation.
#define SB_ERR_ARG -3         ///< a null / out-of-range argument.
#define SB_ERR_FULL -4        ///< the registry is full (registration only).
#define SB_ERR_DUP -5         ///< a driver with that name is already registered.

/**
 * @brief One southbound driver: a vtable over an application-owned transport context.
 *
 * @p read / @p write move a single point; @p read_block / @p write_block move a contiguous span of
 * points atomically (may be null - the framework then reports SB_ERR_UNSUPPORTED). Every callback takes
 * the driver's own @p ctx first. A callback returns SB_OK / a count on success, or a negative code on
 * failure (its own transport error, propagated unchanged).
 */
typedef struct
{
    const char *name;                                                           ///< unique driver name (borrowed).
    int (*read)(void *ctx, uint32_t point, int32_t *value_out);                 ///< read one point.
    int (*write)(void *ctx, uint32_t point, int32_t value);                     ///< write one point.
    int (*read_block)(void *ctx, uint32_t first, int32_t *out, size_t n);       ///< read n points -> out (>=0 count).
    int (*write_block)(void *ctx, uint32_t first, const int32_t *in, size_t n); ///< write n points (>=0 count).
    void *ctx;                                                                  ///< driver instance state (borrowed).
} SouthboundDriver;

/** @brief The one point a read or a write moves. */
typedef struct
{
    uint32_t point;     ///< the point id (register, coil, object) the call addresses
    int32_t value;      ///< the value a write carries
    int32_t *value_out; ///< where a read lands the value it got
} SouthboundPointArgs;

/** @brief The contiguous span of points a block read or a block write moves in one driver call. */
typedef struct
{
    uint32_t first;    ///< the first point id of the span
    int32_t *out;      ///< where a block read lands the values it got
    const int32_t *in; ///< the values a block write carries
    size_t n;          ///< how many points the span covers
} SouthboundBlockArgs;

/**
 * @brief The southbound facade: register drivers, then move points by driver name.
 *
 * @c add takes the keyword-free name of the registration call; every other member carries the name of
 * the operation it performs.
 *
 * @var SouthboundNs::name     the driver a lookup or a dispatch addresses
 * @var SouthboundNs::drv      the driver an add registers (borrowed; must outlive the registry)
 * @var SouthboundNs::point    the one point a read or a write moves
 * @var SouthboundNs::block    the span of points a block read or a block write moves
 * @var SouthboundNs::i32      SB_OK / a count from a registration or a dispatch, or a negative code
 * @var SouthboundNs::n        how many drivers the registry holds
 * @var SouthboundNs::driver   the driver a find matched, or null
 * @var SouthboundNs::add         register @c drv: SB_OK, SB_ERR_ARG, SB_ERR_DUP or SB_ERR_FULL
 * @var SouthboundNs::clear       drop all registrations
 * @var SouthboundNs::count       how many drivers are registered
 * @var SouthboundNs::find        look up @c name
 * @var SouthboundNs::read        read @c point.point from @c name
 * @var SouthboundNs::write       write @c point.value to @c point.point of @c name
 * @var SouthboundNs::read_block  read @c block.n points from @c name, starting at @c block.first
 * @var SouthboundNs::write_block write @c block.n points to @c name, starting at @c block.first
 */
typedef struct
{
    const char *name;            ///< the driver a lookup or a dispatch addresses
    const SouthboundDriver *drv; ///< the driver an add registers

    SouthboundPointArgs point; ///< the one point a read or a write moves
    SouthboundBlockArgs block; ///< the span a block read or a block write moves

    int32_t i32;
    size_t n;
    const SouthboundDriver *driver;

    void (*const add)(uint8_t *restrict work);
    void (*const clear)(uint8_t *restrict work);
    void (*const count)(uint8_t *restrict work);
    void (*const find)(uint8_t *restrict work);
    void (*const read)(uint8_t *restrict work);
    void (*const write)(uint8_t *restrict work);
    void (*const read_block)(uint8_t *restrict work);
    void (*const write_block)(uint8_t *restrict work);
} SouthboundNs;

/** @brief The one symbol this module exports. */
extern SouthboundNs Southbound;

/**
 * @brief The PROTOCORE_SOUTHBOUND_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_southbound_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SOUTHBOUND

#endif // PROTOCORE_SOUTHBOUND_H
