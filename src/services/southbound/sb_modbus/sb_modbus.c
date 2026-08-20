// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sb_modbus.c
 * @brief Modbus-master southbound driver adapter (see sb_modbus.h).
 */

#include "services/southbound/sb_modbus/sb_modbus.h"

static uint8_t modbus_master_work[16]; // the borrow an entry takes; ModbusMaster never reads it

#if PROTOCORE_ENABLE_SOUTHBOUND && PROTOCORE_ENABLE_MODBUS_MASTER

#include "services/fieldbus/modbus/modbus_master/modbus_master.h"

// Read a contiguous span of `n` registers (1..125) at `first` in one Modbus request; write the parsed
// values to `out` as int32. Shared by the single-point and block reads. Returns the register count
// (>= 0), a negative transport error (propagated from txn), PROTOCORE_SB_MODBUS_EXCEPTION on a Modbus
// exception reply, or SB_ERR_ARG on a bad argument / malformed reply.
static int sb_modbus_read_span(protocore_sb_modbus_ctx *c, uint32_t first, int32_t *out, size_t n)
{
    // A Modbus register address is 16-bit and a single request reads at most 125 registers.
    if (n == 0 || n > 125 || first > 0xFFFFu || first + n > 0x10000u)
    {
        return SB_ERR_ARG;
    }

    uint8_t req[12];
    ModbusMasterV.build_read_args.fc = (uint8_t)c->fc;
    ModbusMasterV.build_read_args.txid = c->txid++;
    ModbusMasterV.build_read_args.unit = c->unit;
    ModbusMasterV.build_read_args.start = (uint16_t)first;
    ModbusMasterV.build_read_args.count = (uint16_t)n;
    ModbusMasterV.build_read_args.out = req;
    ModbusMasterV.build_read_args.cap = sizeof(req);
    ModbusMaster.build_read(modbus_master_work);
    size_t rn = ModbusMasterV.n;
    if (rn == 0)
    {
        return SB_ERR_ARG;
    }

    uint8_t resp[MODBUS_ADU_MAX];
    int pn = c->txn(c->io, req, rn, resp, sizeof(resp));
    if (pn < 0)
    {
        return pn; // transport error, propagated unchanged
    }

    uint16_t regs[125];
    uint8_t ex = 0;
    ModbusMasterV.parse_response_args.adu = resp;
    ModbusMasterV.parse_response_args.len = (size_t)pn;
    ModbusMasterV.parse_response_args.regs_out = regs;
    ModbusMasterV.parse_response_args.max_regs = n;
    ModbusMasterV.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    int got = ModbusMasterV.i32;
    if (got < 0)
    {
        return SB_ERR_ARG; // malformed / short frame
    }
    c->last_exception = ex;
    if (ex)
    {
        return PROTOCORE_SB_MODBUS_EXCEPTION;
    }
    for (int i = 0; i < got; i++)
    {
        out[i] = (int32_t)regs[i];
    }
    return got;
}

static int sb_modbus_read(void *vctx, uint32_t point, int32_t *value_out)
{
    protocore_sb_modbus_ctx *c = (protocore_sb_modbus_ctx *)vctx;
    int got = sb_modbus_read_span(c, point, value_out, 1);
    if (got < 0)
    {
        return got;
    }
    return (got == 1) ? SB_OK : SB_ERR_ARG; // a valid reply always carries the one register
}

static int sb_modbus_read_block(void *vctx, uint32_t first, int32_t *out, size_t n)
{
    return sb_modbus_read_span((protocore_sb_modbus_ctx *)vctx, first, out, n);
}

// Run one write request through the transport seam and interpret the reply. Shared by the single-point
// and block writes: `req`/`req_len` is the built request. Returns the register count written (>= 0), a
// propagated transport error, PROTOCORE_SB_MODBUS_EXCEPTION on a Modbus exception reply, or SB_ERR_ARG on
// a malformed reply.
static int sb_modbus_write_txn(protocore_sb_modbus_ctx *c, const uint8_t *req, size_t req_len)
{
    uint8_t resp[MODBUS_ADU_MAX];
    int pn = c->txn(c->io, req, req_len, resp, sizeof(resp));
    if (pn < 0)
    {
        return pn; // transport error, propagated unchanged
    }
    uint8_t ex = 0;
    ModbusMasterV.parse_write_response_args.adu = resp;
    ModbusMasterV.parse_write_response_args.len = (size_t)pn;
    ModbusMasterV.parse_write_response_args.addr_out = NULL;
    ModbusMasterV.parse_write_response_args.exception_out = &ex;
    ModbusMaster.parse_write_response(modbus_master_work);
    int w = ModbusMasterV.i32;
    if (w < 0)
    {
        return SB_ERR_ARG; // malformed / short frame
    }
    c->last_exception = ex;
    if (ex)
    {
        return PROTOCORE_SB_MODBUS_EXCEPTION;
    }
    return w;
}

static int sb_modbus_write(void *vctx, uint32_t point, int32_t value)
{
    protocore_sb_modbus_ctx *c = (protocore_sb_modbus_ctx *)vctx;
    if (point > 0xFFFFu || value < 0 || value > 0xFFFF) // a Modbus register is a 16-bit address / value
    {
        return SB_ERR_ARG;
    }
    uint8_t req[12];
    ModbusMasterV.build_write_single_args.txid = c->txid++;
    ModbusMasterV.build_write_single_args.unit = c->unit;
    ModbusMasterV.build_write_single_args.addr = (uint16_t)point;
    ModbusMasterV.build_write_single_args.value = (uint16_t)value;
    ModbusMasterV.build_write_single_args.out = req;
    ModbusMasterV.build_write_single_args.cap = sizeof(req);
    ModbusMaster.build_write_single(modbus_master_work);
    size_t rn = ModbusMasterV.n;
    if (rn == 0)
    {
        return SB_ERR_ARG;
    }
    int w = sb_modbus_write_txn(c, req, rn);
    if (w < 0)
    {
        return w;
    }
    return (w == 1) ? SB_OK : SB_ERR_ARG; // a valid reply echoes the one register
}

static int sb_modbus_write_block(void *vctx, uint32_t first, const int32_t *in, size_t n)
{
    protocore_sb_modbus_ctx *c = (protocore_sb_modbus_ctx *)vctx;
    // FC 0x10 writes at most 123 registers per request; the span must stay in the 16-bit address space.
    if (n == 0 || n > 123 || first > 0xFFFFu || first + n > 0x10000u)
    {
        return SB_ERR_ARG;
    }
    uint16_t vals[123];
    for (size_t i = 0; i < n; i++)
    {
        if (in[i] < 0 || in[i] > 0xFFFF)
        {
            return SB_ERR_ARG;
        }
        vals[i] = (uint16_t)in[i];
    }
    uint8_t req[13 + 2 * 123];
    ModbusMasterV.build_write_multiple_args.txid = c->txid++;
    ModbusMasterV.build_write_multiple_args.unit = c->unit;
    ModbusMasterV.build_write_multiple_args.start = (uint16_t)first;
    ModbusMasterV.build_write_multiple_args.values = vals;
    ModbusMasterV.build_write_multiple_args.count = (uint16_t)n;
    ModbusMasterV.build_write_multiple_args.out = req;
    ModbusMasterV.build_write_multiple_args.cap = sizeof(req);
    ModbusMaster.build_write_multiple(modbus_master_work);
    size_t rn = ModbusMasterV.n;
    if (rn == 0)
    {
        return SB_ERR_ARG;
    }
    return sb_modbus_write_txn(c, req, rn); // count written (>= 0) / negative code
}

// Fill a caller-owned instance from the transport seam and the slave it addresses.
static void init(uint8_t *restrict work)
{
    (void)work;
    protocore_sb_modbus_ctx *c = SbModbusV.ctx;
    if (!c || !SbModbusV.txn)
    {
        SbModbusV.i32 = SB_ERR_ARG;
        return;
    }
    if (SbModbusV.fc != MODBUS_FC_READ_HOLDING_REGS && SbModbusV.fc != MODBUS_FC_READ_INPUT_REGS)
    {
        SbModbusV.i32 = SB_ERR_ARG;
        return;
    }
    c->txn = SbModbusV.txn;
    c->io = SbModbusV.io;
    c->fc = SbModbusV.fc;
    c->unit = SbModbusV.unit;
    c->txid = 0;
    c->last_exception = 0;
    SbModbusV.i32 = SB_OK;
}

// Bind the vtable the southbound registry dispatches through to one instance.
static void driver(uint8_t *restrict work)
{
    (void)work;
    protocore_sb_modbus_ctx *c = SbModbusV.ctx;
    SouthboundDriver *drv_out = SbModbusV.drv_out;
    if (!drv_out || !SbModbusV.name || !c || !c->txn)
    {
        SbModbusV.i32 = SB_ERR_ARG;
        return;
    }
    // Holding registers are read/write; input registers are read-only (a Modbus input register cannot be
    // written), so an input-register driver leaves write / write_block unbound (framework: SB_ERR_UNSUPPORTED).
    proto_bool writable = (c->fc == MODBUS_FC_READ_HOLDING_REGS);
    drv_out->name = SbModbusV.name;
    drv_out->read = &sb_modbus_read;
    drv_out->write = writable ? &sb_modbus_write : NULL;
    drv_out->read_block = &sb_modbus_read_block;
    drv_out->write_block = writable ? &sb_modbus_write_block : NULL;
    drv_out->ctx = c;
    SbModbusV.i32 = SB_OK;
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
SbModbusVars SbModbusV;

#endif // PROTOCORE_ENABLE_SOUTHBOUND && PROTOCORE_ENABLE_MODBUS_MASTER
