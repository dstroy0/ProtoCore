// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sb_modbus.h
 * @brief Modbus-master southbound driver adapter (PC_ENABLE_SOUTHBOUND && PC_ENABLE_MODBUS_MASTER).
 *
 * Binds the transport-agnostic Modbus TCP master codec (services/fieldbus/modbus/modbus_master) into the
 * southbound driver framework (services/net/southbound), so an app addresses a Modbus slave the same way
 * as any other field device: register the driver, then read/write *points* (register addresses) by name
 * through the one facade. A point id is a register address; the block (matrix) path is the atomic
 * multi-register transfer a single Modbus request satisfies (read up to 125, write up to 123 registers).
 *
 * A holding-register driver (FC 0x03) is read/write: read / read_block use FC 0x03/0x04, write /
 * write_block use Write Single (FC 0x06) / Write Multiple (FC 0x10). An input-register driver (FC 0x04)
 * is read-only - a Modbus input register cannot be written - so its write / write_block stay unbound
 * (the framework reports SB_ERR_UNSUPPORTED).
 *
 * The app owns the transport: it supplies a @ref pc_sb_modbus_txn seam that sends a request ADU and
 * receives the reply (over pc_client for Modbus TCP, or a serial gateway). Pure otherwise - no heap,
 * no sockets, host-testable with a mock transaction routed straight into the slave codec.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SB_MODBUS_H
#define PROTOCORE_SB_MODBUS_H

#include "protocore_config.h"
#include "services/fieldbus/modbus/modbus.h"    // ModbusFunction, MODBUS_ADU_MAX
#include "services/net/southbound/southbound.h" // SouthboundDriver, Sb

PROTO_BEGIN_DECLS

#if PC_ENABLE_SOUTHBOUND && PC_ENABLE_MODBUS_MASTER

/**
 * @brief Request/response transport seam.
 *
 * Send the @p req_len request ADU, receive the reply into @p resp (capacity @p resp_cap).
 * @return the reply length in bytes (> 0), or a negative transport error - the negative is propagated
 *         through the SouthboundDriver read call unchanged, so the app can tell a transport failure
 *         from a Modbus-level one (see PC_SB_MODBUS_EXCEPTION).
 */
typedef int (*pc_sb_modbus_txn)(void *io, const uint8_t *req, size_t req_len, uint8_t *resp, size_t resp_cap);

/** @brief A Modbus-level exception reply (not a transport error); the raw code is in ctx->last_exception. */
#define PC_SB_MODBUS_EXCEPTION (-100)

/**
 * @brief One Modbus-master southbound driver instance (borrowed by the registry for its lifetime).
 *
 * Fill it with pc_sb_modbus_init(), then build a SouthboundDriver over it with pc_sb_modbus_driver().
 */
typedef struct
{
    pc_sb_modbus_txn txn;   ///< app transport seam (send request, receive reply).
    void *io;               ///< opaque transport context passed to @ref txn.
    ModbusFunction fc;      ///< MODBUS_FC_READ_HOLDING_REGS (0x03) or MODBUS_FC_READ_INPUT_REGS (0x04).
    uint8_t unit;           ///< Modbus unit / slave id.
    uint16_t txid;          ///< rolling transaction id, incremented per request.
    uint8_t last_exception; ///< raw Modbus exception code from the last read (0 = none).
} pc_sb_modbus_ctx;

/**
 * @brief Initialize a driver context.
 * @param ctx   the instance to fill.
 * @param txn   the transport seam (must be non-null).
 * @param io    opaque context passed to @p txn on each request (may be null).
 * @param fc    MODBUS_FC_READ_HOLDING_REGS or ::MODBUS_FC_READ_INPUT_REGS.
 * @param unit  Modbus unit / slave id.
 * @return SB_OK, or SB_ERR_ARG on a null ctx/txn or an fc that is not a read function code.
 */
int pc_sb_modbus_init(pc_sb_modbus_ctx *ctx, pc_sb_modbus_txn txn, void *io, ModbusFunction fc, uint8_t unit);

/**
 * @brief Fill @p drv_out with a SouthboundDriver bound to @p ctx.
 *
 * A holding-register context binds read + read_block + write + write_block; an input-register context
 * binds read + read_block only (input registers are read-only).
 * @param drv_out  the driver vtable to fill (borrowed by the registry; must outlive it, as must @p ctx).
 * @param name     the driver's unique registry name (borrowed).
 * @param ctx      an initialized context (see pc_sb_modbus_init).
 * @return SB_OK, or SB_ERR_ARG on a null / uninitialized argument.
 */
int pc_sb_modbus_driver(SouthboundDriver *drv_out, const char *name, pc_sb_modbus_ctx *ctx);

#endif // PC_ENABLE_SOUTHBOUND && PC_ENABLE_MODBUS_MASTER

PROTO_END_DECLS

#endif // PROTOCORE_SB_MODBUS_H
