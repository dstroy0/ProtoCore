// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file southbound.h
 * @brief Southbound protocol-driver framework (PC_ENABLE_SOUTHBOUND).
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
 */

#ifndef PROTOCORE_SOUTHBOUND_H
#define PROTOCORE_SOUTHBOUND_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_SOUTHBOUND

// Southbound result codes. The API returns int (SB_OK / a count on success, or a negative code), so
// the return stays int and these stay plain integer constants - callers keep their `< 0` and
// `== SB_OK` checks cast-free. A driver may also return its own negative transport error, which is
// passed through unchanged.
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

/**
 * @brief Register a driver (borrowed; must outlive the registry).
 * @return SB_OK, SB_ERR_ARG (null / no name), SB_ERR_DUP (name taken), or SB_ERR_FULL.
 */
int pc_southbound_register(const SouthboundDriver *drv);

/** @brief Drop all registrations (test / re-init helper). */
void pc_southbound_clear(void);

/** @brief Number of registered drivers. */
size_t pc_southbound_count(void);

/** @brief Look up a driver by name, or null. */
const SouthboundDriver *pc_southbound_find(const char *name);

/** @brief Read one point from a named driver. @return SB_OK / negative code. */
int pc_southbound_read(const char *name, uint32_t point, int32_t *value_out);

/** @brief Write one point to a named driver. @return SB_OK / negative code. */
int pc_southbound_write(const char *name, uint32_t point, int32_t value);

/** @brief Atomically read @p n points starting at @p first. @return count read (>=0) / negative code. */
int pc_southbound_read_block(const char *name, uint32_t first, int32_t *out, size_t n);

/** @brief Atomically write @p n points starting at @p first. @return count written (>=0) / negative code. */
int pc_southbound_write_block(const char *name, uint32_t first, const int32_t *in, size_t n);

#endif // PC_ENABLE_SOUTHBOUND

PROTO_END_DECLS

#endif // PROTOCORE_SOUTHBOUND_H
