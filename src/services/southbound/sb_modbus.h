// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sb_modbus.h
 * @brief Modbus-master southbound driver adapter (PROTOCORE_ENABLE_SOUTHBOUND && PROTOCORE_ENABLE_MODBUS_MASTER).
 *
 * Binds the transport-agnostic Modbus TCP master codec (services/fieldbus/modbus/modbus_master) into the
 * southbound driver framework (services/southbound), so an app addresses a Modbus slave the same way
 * as any other field device: register the driver, then read/write *points* (register addresses) by name
 * through the one facade. A point id is a register address; the block (matrix) path is the atomic
 * multi-register transfer a single Modbus request satisfies (read up to 125, write up to 123 registers).
 *
 * A holding-register driver (FC 0x03) is read/write: read / read_block use FC 0x03/0x04, write /
 * write_block use Write Single (FC 0x06) / Write Multiple (FC 0x10). An input-register driver (FC 0x04)
 * is read-only - a Modbus input register cannot be written - so its write / write_block stay unbound
 * (the framework reports SB_ERR_UNSUPPORTED).
 *
 * The app owns the transport: it supplies a @ref protocore_sb_modbus_txn seam that sends a request ADU and
 * receives the reply (over protocore_client for Modbus TCP, or a serial gateway). Pure otherwise - no heap,
 * no sockets, host-testable with a mock transaction routed straight into the slave codec.
 *
 * A caller sets the members a call takes, invokes it through ::SbModbus, and reads the outcome off the
 * same handle. The module exports one symbol, @ref SbModbus; everything in sb_modbus.c has internal
 * linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SB_MODBUS_H
#define PROTOCORE_SB_MODBUS_H

#include "protocore_config.h"
#include "services/fieldbus/modbus/modbus.h" // ModbusFunction, MODBUS_ADU_MAX
#include "services/southbound/southbound.h"  // SouthboundDriver, Southbound

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_SOUTHBOUND && PROTOCORE_ENABLE_MODBUS_MASTER

/**
 * @brief Request/response transport seam.
 *
 * Send the @p req_len request ADU, receive the reply into @p resp (capacity @p resp_cap).
 * @return the reply length in bytes (> 0), or a negative transport error - the negative is propagated
 *         through the SouthboundDriver read call unchanged, so the app can tell a transport failure
 *         from a Modbus-level one (see PROTOCORE_SB_MODBUS_EXCEPTION).
 */
typedef int (*protocore_sb_modbus_txn)(void *io, const uint8_t *req, size_t req_len, uint8_t *resp, size_t resp_cap);

/** @brief A Modbus-level exception reply (not a transport error); the raw code is in ctx->last_exception. */
#define PROTOCORE_SB_MODBUS_EXCEPTION (-100)

/**
 * @brief One Modbus-master southbound driver instance (borrowed by the registry for its lifetime).
 *
 * Fill it through ::SbModbus @c init, then build a SouthboundDriver over it through ::SbModbus @c driver.
 */
typedef struct
{
    protocore_sb_modbus_txn txn; ///< app transport seam (send request, receive reply).
    void *io;                    ///< opaque transport context passed to @ref txn.
    ModbusFunction fc;           ///< MODBUS_FC_READ_HOLDING_REGS (0x03) or MODBUS_FC_READ_INPUT_REGS (0x04).
    uint8_t unit;                ///< Modbus unit / slave id.
    uint16_t txid;               ///< rolling transaction id, incremented per request.
    uint8_t last_exception;      ///< raw Modbus exception code from the last read (0 = none).
} protocore_sb_modbus_ctx;

/** @brief The module's handle onto its own calls, described only in sb_modbus.c. */
struct SbModbusInternal;

/**
 * @brief The Modbus-master adapter: fill a driver instance, then build a SouthboundDriver over it.
 *
 * No storage member: the instance every call acts on is the caller's, named by @c ctx.
 *
 * @var SbModbusNs::ctx      the driver instance an init fills and a driver call binds
 * @var SbModbusNs::txn      the transport seam an init takes; a null one is rejected
 * @var SbModbusNs::io       the opaque context handed to @c txn on each request; may be null
 * @var SbModbusNs::fc       MODBUS_FC_READ_HOLDING_REGS or MODBUS_FC_READ_INPUT_REGS
 * @var SbModbusNs::unit     the Modbus unit / slave id an init takes
 * @var SbModbusNs::drv_out  the driver vtable a driver call fills (borrowed by the registry)
 * @var SbModbusNs::name     the unique registry name a driver call binds (borrowed)
 * @var SbModbusNs::i32      SB_OK, or SB_ERR_ARG on a null / out-of-range argument
 * @var SbModbusNs::init     fill @c ctx from @c txn, @c io, @c fc and @c unit
 * @var SbModbusNs::driver   fill @c drv_out with a SouthboundDriver bound to @c ctx
 * @var SbModbusNs::internal the module's handle onto the calls above
 */
typedef struct
{
    protocore_sb_modbus_ctx *ctx; ///< the driver instance a call acts on
    protocore_sb_modbus_txn txn;  ///< the transport seam an init takes
    void *io;                     ///< the opaque context handed to @c txn
    ModbusFunction fc;            ///< the read function code an init takes
    uint8_t unit;                 ///< the Modbus unit / slave id an init takes

    SouthboundDriver *drv_out; ///< the driver vtable a driver call fills
    const char *name;          ///< the registry name a driver call binds

    int32_t i32;

    void (*init)(struct SbModbusInternal *ctx);
    void (*driver)(struct SbModbusInternal *ctx);

    struct SbModbusInternal *internal;
} SbModbusNs;

/** @brief The one symbol this module exports. */
extern SbModbusNs SbModbus;

#endif // PROTOCORE_ENABLE_SOUTHBOUND && PROTOCORE_ENABLE_MODBUS_MASTER

PROTOCORE_END_DECLS

#endif // PROTOCORE_SB_MODBUS_H
