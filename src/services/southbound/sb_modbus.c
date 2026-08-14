// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sb_modbus.c
 * @brief Modbus-master southbound driver adapter (see sb_modbus.h).
 */

#include "services/southbound/sb_modbus.h"

#if PROTOCORE_ENABLE_SOUTHBOUND && PROTOCORE_ENABLE_MODBUS_MASTER

#include "services/fieldbus/modbus/modbus_master.h"

/**
 * @brief The module's handle onto its own calls - what SbModbusNs points at.
 *
 * No store member: the instance every call acts on is the caller's, reached through the handle.
 *
 * @var SbModbusInternal::ns  the handle a caller sets a call's members on
 */
struct SbModbusInternal
{
    SbModbusNs *ns;
};

static struct SbModbusInternal s_sb_modbus = {.ns = &SbModbus};

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
    size_t rn =
        protocore_modbus_build_read((uint8_t)c->fc, c->txid++, c->unit, (uint16_t)first, (uint16_t)n, req, sizeof(req));
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
    int got = protocore_modbus_parse_response(resp, (size_t)pn, regs, n, &ex);
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
    int w = protocore_modbus_parse_write_response(resp, (size_t)pn, NULL, &ex);
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
    size_t rn =
        protocore_modbus_build_write_single(c->txid++, c->unit, (uint16_t)point, (uint16_t)value, req, sizeof(req));
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
    size_t rn =
        protocore_modbus_build_write_multiple(c->txid++, c->unit, (uint16_t)first, vals, (uint16_t)n, req, sizeof(req));
    if (rn == 0)
    {
        return SB_ERR_ARG;
    }
    return sb_modbus_write_txn(c, req, rn); // count written (>= 0) / negative code
}

// Fill a caller-owned instance from the transport seam and the slave it addresses.
static void init(struct SbModbusInternal *restrict ctx)
{
    protocore_sb_modbus_ctx *c = ctx->ns->ctx;
    if (!c || !ctx->ns->txn)
    {
        ctx->ns->i32 = SB_ERR_ARG;
        return;
    }
    if (ctx->ns->fc != MODBUS_FC_READ_HOLDING_REGS && ctx->ns->fc != MODBUS_FC_READ_INPUT_REGS)
    {
        ctx->ns->i32 = SB_ERR_ARG;
        return;
    }
    c->txn = ctx->ns->txn;
    c->io = ctx->ns->io;
    c->fc = ctx->ns->fc;
    c->unit = ctx->ns->unit;
    c->txid = 0;
    c->last_exception = 0;
    ctx->ns->i32 = SB_OK;
}

// Bind the vtable the southbound registry dispatches through to one instance.
static void driver(struct SbModbusInternal *restrict ctx)
{
    protocore_sb_modbus_ctx *c = ctx->ns->ctx;
    SouthboundDriver *drv_out = ctx->ns->drv_out;
    if (!drv_out || !ctx->ns->name || !c || !c->txn)
    {
        ctx->ns->i32 = SB_ERR_ARG;
        return;
    }
    // Holding registers are read/write; input registers are read-only (a Modbus input register cannot be
    // written), so an input-register driver leaves write / write_block unbound (framework: SB_ERR_UNSUPPORTED).
    proto_bool writable = (c->fc == MODBUS_FC_READ_HOLDING_REGS);
    drv_out->name = ctx->ns->name;
    drv_out->read = &sb_modbus_read;
    drv_out->write = writable ? &sb_modbus_write : NULL;
    drv_out->read_block = &sb_modbus_read_block;
    drv_out->write_block = writable ? &sb_modbus_write_block : NULL;
    drv_out->ctx = c;
    ctx->ns->i32 = SB_OK;
}

// Designated, so a member's position in the struct does not decide what it binds to.
SbModbusNs SbModbus = {.init = init, .driver = driver, .internal = &s_sb_modbus};

#endif // PROTOCORE_ENABLE_SOUTHBOUND && PROTOCORE_ENABLE_MODBUS_MASTER
