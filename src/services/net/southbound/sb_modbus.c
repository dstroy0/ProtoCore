// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sb_modbus.c
 * @brief Modbus-master southbound driver adapter (see sb_modbus.h).
 */

#include "services/net/southbound/sb_modbus.h"

#if PC_ENABLE_SOUTHBOUND && PC_ENABLE_MODBUS_MASTER

#include "services/fieldbus/modbus/modbus_master.h"

// Read a contiguous span of `n` registers (1..125) at `first` in one Modbus request; write the parsed
// values to `out` as int32. Shared by the single-point and block reads. Returns the register count
// (>= 0), a negative transport error (propagated from txn), PC_SB_MODBUS_EXCEPTION on a Modbus
// exception reply, or Sb::SB_ERR_ARG on a bad argument / malformed reply.
static int sb_modbus_read_span(pc_sb_modbus_ctx *c, uint32_t first, int32_t *out, size_t n)
{
    // A Modbus register address is 16-bit and a single request reads at most 125 registers.
    if (n == 0 || n > 125 || first > 0xFFFFu || first + n > 0x10000u)
    {
        return Sb::SB_ERR_ARG;
    }

    uint8_t req[12];
    size_t rn =
        pc_modbus_build_read((uint8_t)c->fc, c->txid++, c->unit, (uint16_t)first, (uint16_t)n, req, sizeof(req));
    if (rn == 0)
    {
        return Sb::SB_ERR_ARG;
    }

    uint8_t resp[MODBUS_ADU_MAX];
    int pn = c->txn(c->io, req, rn, resp, sizeof(resp));
    if (pn < 0)
    {
        return pn; // transport error, propagated unchanged
    }

    uint16_t regs[125];
    uint8_t ex = 0;
    int got = pc_modbus_parse_response(resp, (size_t)pn, regs, n, &ex);
    if (got < 0)
    {
        return Sb::SB_ERR_ARG; // malformed / short frame
    }
    c->last_exception = ex;
    if (ex)
    {
        return PC_SB_MODBUS_EXCEPTION;
    }
    for (int i = 0; i < got; i++)
    {
        out[i] = (int32_t)regs[i];
    }
    return got;
}

static int sb_modbus_read(void *vctx, uint32_t point, int32_t *value_out)
{
    pc_sb_modbus_ctx *c = (pc_sb_modbus_ctx *)vctx;
    int got = sb_modbus_read_span(c, point, value_out, 1);
    if (got < 0)
    {
        return got;
    }
    return (got == 1) ? Sb::SB_OK : Sb::SB_ERR_ARG; // a valid reply always carries the one register
}

static int sb_modbus_read_block(void *vctx, uint32_t first, int32_t *out, size_t n)
{
    return sb_modbus_read_span((pc_sb_modbus_ctx *)vctx, first, out, n);
}

// Run one write request through the transport seam and interpret the reply. Shared by the single-point
// and block writes: `req`/`req_len` is the built request. Returns the register count written (>= 0), a
// propagated transport error, PC_SB_MODBUS_EXCEPTION on a Modbus exception reply, or Sb::SB_ERR_ARG on
// a malformed reply.
static int sb_modbus_write_txn(pc_sb_modbus_ctx *c, const uint8_t *req, size_t req_len)
{
    uint8_t resp[MODBUS_ADU_MAX];
    int pn = c->txn(c->io, req, req_len, resp, sizeof(resp));
    if (pn < 0)
    {
        return pn; // transport error, propagated unchanged
    }
    uint8_t ex = 0;
    int w = pc_modbus_parse_write_response(resp, (size_t)pn, NULL, &ex);
    if (w < 0)
    {
        return Sb::SB_ERR_ARG; // malformed / short frame
    }
    c->last_exception = ex;
    if (ex)
    {
        return PC_SB_MODBUS_EXCEPTION;
    }
    return w;
}

static int sb_modbus_write(void *vctx, uint32_t point, int32_t value)
{
    pc_sb_modbus_ctx *c = (pc_sb_modbus_ctx *)vctx;
    if (point > 0xFFFFu || value < 0 || value > 0xFFFF) // a Modbus register is a 16-bit address / value
    {
        return Sb::SB_ERR_ARG;
    }
    uint8_t req[12];
    size_t rn = pc_modbus_build_write_single(c->txid++, c->unit, (uint16_t)point, (uint16_t)value, req, sizeof(req));
    if (rn == 0)
    {
        return Sb::SB_ERR_ARG;
    }
    int w = sb_modbus_write_txn(c, req, rn);
    if (w < 0)
    {
        return w;
    }
    return (w == 1) ? Sb::SB_OK : Sb::SB_ERR_ARG; // a valid reply echoes the one register
}

static int sb_modbus_write_block(void *vctx, uint32_t first, const int32_t *in, size_t n)
{
    pc_sb_modbus_ctx *c = (pc_sb_modbus_ctx *)vctx;
    // FC 0x10 writes at most 123 registers per request; the span must stay in the 16-bit address space.
    if (n == 0 || n > 123 || first > 0xFFFFu || first + n > 0x10000u)
    {
        return Sb::SB_ERR_ARG;
    }
    uint16_t vals[123];
    for (size_t i = 0; i < n; i++)
    {
        if (in[i] < 0 || in[i] > 0xFFFF)
        {
            return Sb::SB_ERR_ARG;
        }
        vals[i] = (uint16_t)in[i];
    }
    uint8_t req[13 + 2 * 123];
    size_t rn =
        pc_modbus_build_write_multiple(c->txid++, c->unit, (uint16_t)first, vals, (uint16_t)n, req, sizeof(req));
    if (rn == 0)
    {
        return Sb::SB_ERR_ARG;
    }
    return sb_modbus_write_txn(c, req, rn); // count written (>= 0) / negative code
}

int pc_sb_modbus_init(pc_sb_modbus_ctx *ctx, pc_sb_modbus_txn txn, void *io, ModbusFunction fc, uint8_t unit)
{
    if (!ctx || !txn)
    {
        return Sb::SB_ERR_ARG;
    }
    if (fc != MODBUS_FC_READ_HOLDING_REGS && fc != MODBUS_FC_READ_INPUT_REGS)
    {
        return Sb::SB_ERR_ARG;
    }
    ctx->txn = txn;
    ctx->io = io;
    ctx->fc = fc;
    ctx->unit = unit;
    ctx->txid = 0;
    ctx->last_exception = 0;
    return Sb::SB_OK;
}

int pc_sb_modbus_driver(SouthboundDriver *drv_out, const char *name, pc_sb_modbus_ctx *ctx)
{
    if (!drv_out || !name || !ctx || !ctx->txn)
    {
        return Sb::SB_ERR_ARG;
    }
    // Holding registers are read/write; input registers are read-only (a Modbus input register cannot be
    // written), so an input-register driver leaves write / write_block unbound (framework: SB_ERR_UNSUPPORTED).
    proto_bool writable = (ctx->fc == MODBUS_FC_READ_HOLDING_REGS);
    drv_out->name = name;
    drv_out->read = &sb_modbus_read;
    drv_out->write = writable ? &sb_modbus_write : NULL;
    drv_out->read_block = &sb_modbus_read_block;
    drv_out->write_block = writable ? &sb_modbus_write_block : NULL;
    drv_out->ctx = ctx;
    return Sb::SB_OK;
}

#endif // PC_ENABLE_SOUTHBOUND && PC_ENABLE_MODBUS_MASTER
