// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file modbus.c
 * @brief Modbus TCP slave: data model, MBAP/PDU codec, and the TCP transport.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_MODBUS

#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "mmgr/protomem/protomem.h"
#include "services/fieldbus/modbus/modbus/modbus.h"
#include "shared/crc/crc.h" // PROTOCORE_CRC16_MODBUS

PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// Data model (all BSS - no heap)
// ---------------------------------------------------------------------------

// All Modbus data-model state, owned by one instance (internal linkage): the coil / discrete
// bitfields, the holding / input registers, and the write callback, grouped so it is one
// named owner, unreachable from any other translation unit.

#if PROTOCORE_HAS_NET_STACK
#include "network_drivers/transport/tcp/common.h"            // TcpConn, conn_pool: the slot a frame arrives on
#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool: the accepted slot
#include "network_drivers/transport/tcp/tcp.h"
#include "server/core/proto_handler.h"
#endif

typedef struct
{
    uint8_t coils[(PROTOCORE_MODBUS_COILS + 7) / 8];
    uint8_t discrete[(PROTOCORE_MODBUS_DISCRETE_INPUTS + 7) / 8];
    uint16_t holding[PROTOCORE_MODBUS_HOLDING_REGS];
    uint16_t input[PROTOCORE_MODBUS_INPUT_REGS];
    ModbusWriteCb write_cb;
} ModbusCtx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define MODBUS_OFF_CTX 0u
static_assert(MODBUS_OFF_CTX + sizeof(ModbusCtx) <= PROTOCORE_MODBUS_BORROW,
              "PROTOCORE_MODBUS_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(MODBUS_OFF_CTX % _Alignof(ModbusCtx) == 0,
              "MODBUS_OFF_CTX is not a multiple of alignof(ModbusCtx) - MODBUS_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define MODBUS_CTX(w) ((ModbusCtx *)(void *)((w) + MODBUS_OFF_CTX))

static proto_bool bit_get(const uint8_t *a, uint16_t i)
{
    return (a[i >> 3] >> (i & 7)) & 1u;
}
static void bit_set(uint8_t *a, uint16_t i, proto_bool v)
{
    if (v)
    {
        a[i >> 3] |= (uint8_t)(1u << (i & 7));
    }
    else
    {
        a[i >> 3] &= (uint8_t)~(1u << (i & 7));
    }
}

// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_MODBUS_BORROW persistent bytes
} ModbusOwnCtx;
static ModbusOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_modbus_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_MODBUS_BORROW).buf;
    }
    return s_own.span;
}

void protocore_modbus_process_adu(uint8_t *restrict work);

void protocore_modbus_server_init(uint8_t *restrict work)
{

    mem.set(MODBUS_CTX(work)->coils, 0, sizeof(MODBUS_CTX(work)->coils));
    mem.set(MODBUS_CTX(work)->discrete, 0, sizeof(MODBUS_CTX(work)->discrete));
    mem.set(MODBUS_CTX(work)->holding, 0, sizeof(MODBUS_CTX(work)->holding));
    mem.set(MODBUS_CTX(work)->input, 0, sizeof(MODBUS_CTX(work)->input));
    MODBUS_CTX(work)->write_cb = NULL;
}

void protocore_modbus_on_write(uint8_t *restrict work)
{
    ModbusWriteCb cb = ModbusV.on_write_args.cb;

    MODBUS_CTX(work)->write_cb = cb;
}

void protocore_modbus_get_coil(uint8_t *restrict work)
{
    uint16_t addr = ModbusV.get_coil_args.addr;

    ModbusV.ok = (addr < PROTOCORE_MODBUS_COILS) ? bit_get(MODBUS_CTX(work)->coils, addr) : PROTO_FALSE;
}
void protocore_modbus_set_coil(uint8_t *restrict work)
{
    uint16_t addr = ModbusV.set_coil_args.addr;
    proto_bool on = ModbusV.set_coil_args.on;

    if (addr < PROTOCORE_MODBUS_COILS)
    {
        bit_set(MODBUS_CTX(work)->coils, addr, on);
    }
}
void protocore_modbus_get_discrete_input(uint8_t *restrict work)
{
    uint16_t addr = ModbusV.get_discrete_input_args.addr;

    ModbusV.ok = (addr < PROTOCORE_MODBUS_DISCRETE_INPUTS) ? bit_get(MODBUS_CTX(work)->discrete, addr) : PROTO_FALSE;
}
void protocore_modbus_set_discrete_input(uint8_t *restrict work)
{
    uint16_t addr = ModbusV.set_discrete_input_args.addr;
    proto_bool on = ModbusV.set_discrete_input_args.on;

    if (addr < PROTOCORE_MODBUS_DISCRETE_INPUTS)
    {
        bit_set(MODBUS_CTX(work)->discrete, addr, on);
    }
}
void protocore_modbus_get_holding_reg(uint8_t *restrict work)
{
    uint16_t addr = ModbusV.get_holding_reg_args.addr;

    ModbusV.value = (addr < PROTOCORE_MODBUS_HOLDING_REGS) ? MODBUS_CTX(work)->holding[addr] : 0;
}
void protocore_modbus_set_holding_reg(uint8_t *restrict work)
{
    uint16_t addr = ModbusV.set_holding_reg_args.addr;
    uint16_t value = ModbusV.set_holding_reg_args.value;

    if (addr < PROTOCORE_MODBUS_HOLDING_REGS)
    {
        MODBUS_CTX(work)->holding[addr] = value;
    }
}
void protocore_modbus_get_input_reg(uint8_t *restrict work)
{
    uint16_t addr = ModbusV.get_input_reg_args.addr;

    ModbusV.value = (addr < PROTOCORE_MODBUS_INPUT_REGS) ? MODBUS_CTX(work)->input[addr] : 0;
}
void protocore_modbus_set_input_reg(uint8_t *restrict work)
{
    uint16_t addr = ModbusV.set_input_reg_args.addr;
    uint16_t value = ModbusV.set_input_reg_args.value;

    if (addr < PROTOCORE_MODBUS_INPUT_REGS)
    {
        MODBUS_CTX(work)->input[addr] = value;
    }
}

// ---------------------------------------------------------------------------
// PDU codec (host-testable)
// ---------------------------------------------------------------------------

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}
static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

// Build an exception PDU (fc|0x80, code). Always 2 bytes.
static size_t pdu_exception(ModbusFunction fc, ModbusException code, uint8_t *out)
{
    out[0] = (uint8_t)((uint8_t)fc | 0x80);
    out[1] = (uint8_t)code;
    return 2;
}

// Process one PDU (function code + data) against the data model. Returns the
// response PDU length, or 0 if it cannot fit (caller treats 0 as "send nothing").
static size_t protocore_modbus_process_pdu(uint8_t *restrict work, const uint8_t *pdu, size_t pdu_len, uint8_t *out,
                                           size_t out_cap)
{
    // Both callers already guarantee at least one PDU byte, so this guard cannot fire: the TCP path
    // rejects len < 2 before computing pdu_len = len - 1, and the RTU path rejects req_len < 4 before
    // computing pdu_len = req_len - 3. Kept as defense in depth for any future caller.
    if (pdu_len < 1)
    {
        return 0;
    }
    ModbusFunction fc = (ModbusFunction)(pdu[0]);

    // Dispatch on the function code; each case validates its own request length and
    // address/quantity range, replying with the data or a Modbus exception PDU.
    switch (fc)
    {
    // FC1/FC2: read up to 2000 single-bit coils / discrete inputs, packed 8 per byte.
    case MODBUS_FC_READ_COILS:
    case MODBUS_FC_READ_DISCRETE_INPUTS: {
        if (pdu_len < 5)
        {
            return pdu_exception(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        uint16_t start = rd16(pdu + 1), qty = rd16(pdu + 3);
        uint16_t limit = (fc == MODBUS_FC_READ_COILS) ? PROTOCORE_MODBUS_COILS : PROTOCORE_MODBUS_DISCRETE_INPUTS;
        const uint8_t *src = (fc == MODBUS_FC_READ_COILS) ? MODBUS_CTX(work)->coils : MODBUS_CTX(work)->discrete;
        if (qty < 1 || qty > 2000)
        {
            return pdu_exception(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        if ((uint32_t)start + qty > limit)
        {
            return pdu_exception(fc, MODBUS_EX_ILLEGAL_DATA_ADDRESS, out);
        }
        uint8_t bytes = (uint8_t)((qty + 7) / 8);
        if ((size_t)2 + bytes > out_cap)
        {
            return 0;
        }
        out[0] = (uint8_t)fc;
        out[1] = bytes;
        mem.set(out + 2, 0, bytes);
        for (uint16_t i = 0; i < qty; i++)
        {
            if (bit_get(src, (uint16_t)(start + i)))
            {
                out[2 + (i >> 3)] |= (uint8_t)(1u << (i & 7));
            }
        }
        return (size_t)2 + bytes;
    }

    // FC3/FC4: read up to 125 16-bit holding / input registers, big-endian.
    case MODBUS_FC_READ_HOLDING_REGS:
    case MODBUS_FC_READ_INPUT_REGS: {
        if (pdu_len < 5)
        {
            return pdu_exception(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        uint16_t start = rd16(pdu + 1), qty = rd16(pdu + 3);
        uint16_t limit =
            (fc == MODBUS_FC_READ_HOLDING_REGS) ? PROTOCORE_MODBUS_HOLDING_REGS : PROTOCORE_MODBUS_INPUT_REGS;
        const uint16_t *src = (fc == MODBUS_FC_READ_HOLDING_REGS) ? MODBUS_CTX(work)->holding : MODBUS_CTX(work)->input;
        if (qty < 1 || qty > 125)
        {
            return pdu_exception(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        if ((uint32_t)start + qty > limit)
        {
            return pdu_exception(fc, MODBUS_EX_ILLEGAL_DATA_ADDRESS, out);
        }
        uint8_t bytes = (uint8_t)(qty * 2);
        if ((size_t)2 + bytes > out_cap)
        {
            return 0;
        }
        out[0] = (uint8_t)fc;
        out[1] = bytes;
        for (uint16_t i = 0; i < qty; i++)
        {
            wr16(out + 2 + i * 2, src[start + i]);
        }
        return (size_t)2 + bytes;
    }

    // FC5: write one coil (value 0xFF00 = on, 0x0000 = off); echo the request back.
    case MODBUS_FC_WRITE_SINGLE_COIL: {
        if (pdu_len < 5)
        {
            return pdu_exception(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        uint16_t addr = rd16(pdu + 1), value = rd16(pdu + 3);
        if (value != 0x0000 && value != 0xFF00)
        {
            return pdu_exception(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        if (addr >= PROTOCORE_MODBUS_COILS)
        {
            return pdu_exception(fc, MODBUS_EX_ILLEGAL_DATA_ADDRESS, out);
        }
        bit_set(MODBUS_CTX(work)->coils, addr, value == 0xFF00);
        if (MODBUS_CTX(work)->write_cb)
        {
            MODBUS_CTX(work)->write_cb((uint8_t)fc, addr, 1);
        }
        if (out_cap < 5)
        {
            return 0;
        }
        mem.cpy(out, pdu, 5); // echo request
        return 5;
    }

    // FC6: write one holding register; echo the request back.
    case MODBUS_FC_WRITE_SINGLE_REG: {
        if (pdu_len < 5)
        {
            return pdu_exception(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        uint16_t addr = rd16(pdu + 1), value = rd16(pdu + 3);
        if (addr >= PROTOCORE_MODBUS_HOLDING_REGS)
        {
            return pdu_exception(fc, MODBUS_EX_ILLEGAL_DATA_ADDRESS, out);
        }
        MODBUS_CTX(work)->holding[addr] = value;
        if (MODBUS_CTX(work)->write_cb)
        {
            MODBUS_CTX(work)->write_cb((uint8_t)fc, addr, 1);
        }
        if (out_cap < 5)
        {
            return 0;
        }
        mem.cpy(out, pdu, 5);
        return 5;
    }

    // FC15: write up to 1968 coils from a packed bitfield; reply start + quantity.
    case MODBUS_FC_WRITE_MULTIPLE_COILS: {
        if (pdu_len < 6)
        {
            return pdu_exception(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        uint16_t start = rd16(pdu + 1), qty = rd16(pdu + 3);
        uint8_t bc = pdu[5];
        if (qty < 1 || qty > 1968 || bc != (uint8_t)((qty + 7) / 8) || pdu_len < (size_t)6 + bc)
        {
            return pdu_exception(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        if ((uint32_t)start + qty > PROTOCORE_MODBUS_COILS)
        {
            return pdu_exception(fc, MODBUS_EX_ILLEGAL_DATA_ADDRESS, out);
        }
        for (uint16_t i = 0; i < qty; i++)
        {
            proto_bool v = (pdu[6 + (i >> 3)] >> (i & 7)) & 1u;
            bit_set(MODBUS_CTX(work)->coils, (uint16_t)(start + i), v);
        }
        if (MODBUS_CTX(work)->write_cb)
        {
            MODBUS_CTX(work)->write_cb((uint8_t)fc, start, qty);
        }
        if (out_cap < 5)
        {
            return 0;
        }
        out[0] = (uint8_t)fc;
        wr16(out + 1, start);
        wr16(out + 3, qty);
        return 5;
    }

    // FC16: write up to 123 holding registers; reply start + quantity.
    case MODBUS_FC_WRITE_MULTIPLE_REGS: {
        if (pdu_len < 6)
        {
            return pdu_exception(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        uint16_t start = rd16(pdu + 1), qty = rd16(pdu + 3);
        uint8_t bc = pdu[5];
        if (qty < 1 || qty > 123 || bc != (uint8_t)(qty * 2) || pdu_len < (size_t)6 + bc)
        {
            return pdu_exception(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        if ((uint32_t)start + qty > PROTOCORE_MODBUS_HOLDING_REGS)
        {
            return pdu_exception(fc, MODBUS_EX_ILLEGAL_DATA_ADDRESS, out);
        }
        for (uint16_t i = 0; i < qty; i++)
        {
            MODBUS_CTX(work)->holding[start + i] = rd16(pdu + 6 + i * 2);
        }
        if (MODBUS_CTX(work)->write_cb)
        {
            MODBUS_CTX(work)->write_cb((uint8_t)fc, start, qty);
        }
        if (out_cap < 5)
        {
            return 0;
        }
        out[0] = (uint8_t)fc;
        wr16(out + 1, start);
        wr16(out + 3, qty);
        return 5;
    }

    // FC22: mask-write one holding register: reg = (reg AND And_Mask) OR (Or_Mask AND NOT And_Mask). Echo.
    case MODBUS_FC_MASK_WRITE_REG: {
        if (pdu_len < 7)
        {
            return pdu_exception(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        uint16_t addr = rd16(pdu + 1);
        uint16_t and_mask = rd16(pdu + 3);
        uint16_t or_mask = rd16(pdu + 5);
        if (addr >= PROTOCORE_MODBUS_HOLDING_REGS)
        {
            return pdu_exception(fc, MODBUS_EX_ILLEGAL_DATA_ADDRESS, out);
        }
        uint16_t cur = MODBUS_CTX(work)->holding[addr];
        MODBUS_CTX(work)->holding[addr] = (uint16_t)((cur & and_mask) | (or_mask & (uint16_t)~and_mask));
        if (MODBUS_CTX(work)->write_cb)
        {
            MODBUS_CTX(work)->write_cb((uint8_t)fc, addr, 1);
        }
        if (out_cap < 7)
        {
            return 0;
        }
        mem.cpy(out, pdu, 7); // echo request (address + both masks)
        return 7;
    }

    // FC23: read/write multiple holding registers in one transaction - the write is applied first, then
    // the read reflects it (a contiguous read span + a contiguous write span). Reply is the read data.
    case MODBUS_FC_READ_WRITE_MULTIPLE_REGS: {
        if (pdu_len < 10)
        {
            return pdu_exception(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        uint16_t r_start = rd16(pdu + 1);
        uint16_t r_qty = rd16(pdu + 3);
        uint16_t w_start = rd16(pdu + 5);
        uint16_t w_qty = rd16(pdu + 7);
        uint8_t w_bc = pdu[9];
        if (r_qty < 1 || r_qty > 125 || w_qty < 1 || w_qty > 121 || w_bc != (uint8_t)(w_qty * 2) ||
            pdu_len < (size_t)10 + w_bc)
        {
            return pdu_exception(fc, MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        if ((uint32_t)r_start + r_qty > PROTOCORE_MODBUS_HOLDING_REGS ||
            (uint32_t)w_start + w_qty > PROTOCORE_MODBUS_HOLDING_REGS)
        {
            return pdu_exception(fc, MODBUS_EX_ILLEGAL_DATA_ADDRESS, out);
        }
        for (uint16_t i = 0; i < w_qty; i++) // write first (§6.17)
        {
            MODBUS_CTX(work)->holding[w_start + i] = rd16(pdu + 10 + i * 2);
        }
        if (MODBUS_CTX(work)->write_cb)
        {
            MODBUS_CTX(work)->write_cb((uint8_t)fc, w_start, w_qty);
        }
        uint8_t r_bytes = (uint8_t)(r_qty * 2);
        if ((size_t)2 + r_bytes > out_cap)
        {
            return 0;
        }
        out[0] = (uint8_t)fc;
        out[1] = r_bytes;
        for (uint16_t i = 0; i < r_qty; i++)
        {
            wr16(out + 2 + i * 2, MODBUS_CTX(work)->holding[r_start + i]);
        }
        return (size_t)2 + r_bytes;
    }

    // Any unsupported function code: reply with the ILLEGAL FUNCTION exception.
    default:
        return pdu_exception(fc, MODBUS_EX_ILLEGAL_FUNCTION, out);
    }
}

void protocore_modbus_process_adu(uint8_t *restrict work)
{
    const uint8_t *req = ModbusV.process_adu_args.req;
    size_t req_len = ModbusV.process_adu_args.req_len;
    uint8_t *resp = ModbusV.process_adu_args.resp;
    size_t protocore_resp_cap = ModbusV.process_adu_args.protocore_resp_cap;

    if (req_len < 8 || protocore_resp_cap < 8)
    {
        ModbusV.n = 0; // need MBAP (7) + at least a function code
        return;
    }

    uint16_t tid = rd16(req);
    uint16_t pid = rd16(req + 2);
    uint16_t len = rd16(req + 4);
    uint8_t uid = req[6];

    if (pid != 0)
    {
        ModbusV.n = 0; // not Modbus
        return;
    }
    if (len < 2 || (size_t)6 + len > req_len)
    {
        ModbusV.n = 0; // length field disagrees with the frame
        return;
    }

    const uint8_t *pdu = req + 7;
    size_t pdu_len = (size_t)len - 1; // len counts the unit id + the PDU

    size_t rlen = protocore_modbus_process_pdu(work, pdu, pdu_len, resp + 7, protocore_resp_cap - 7);
    if (rlen == 0)
    {
        ModbusV.n = 0;
        return;
    }

    wr16(resp, tid);                      // echo transaction id
    wr16(resp + 2, 0);                    // protocol id = 0
    wr16(resp + 4, (uint16_t)(1 + rlen)); // length = unit id + response PDU
    resp[6] = uid;                        // echo unit id
    ModbusV.n = 7 + rlen;
}

#if PROTOCORE_ENABLE_MODBUS_RTU
// CRC16-Modbus (init 0xFFFF, reflected poly 0xA001); transmitted low byte first.
static uint16_t protocore_modbus_crc16(const uint8_t *data, size_t len)
{
    CrcV.args.params = &PROTOCORE_CRC16_MODBUS;
    CrcV.args.data = data;
    CrcV.args.len = len;
    Crc.compute(protocore_modbus_span());
    return (uint16_t)CrcV.value;
}

void protocore_modbus_rtu_process_adu(uint8_t *restrict work)
{
    const uint8_t *req = ModbusV.rtu_process_adu_args.req;
    size_t req_len = ModbusV.rtu_process_adu_args.req_len;
    uint8_t *resp = ModbusV.rtu_process_adu_args.resp;
    size_t protocore_resp_cap = ModbusV.rtu_process_adu_args.protocore_resp_cap;
    uint8_t my_addr = ModbusV.rtu_process_adu_args.my_addr;

    if (req_len < 4 || protocore_resp_cap < 4) // addr(1) + min PDU(1) + CRC(2)
    {
        ModbusV.n = 0;
        return;
    }

    // Validate the trailing CRC over [addr .. last PDU byte] (low byte first).
    uint16_t want = protocore_modbus_crc16(req, req_len - 2);
    uint16_t got = (uint16_t)(req[req_len - 2] | (req[req_len - 1] << 8));
    if (want != got)
    {
        ModbusV.n = 0; // corrupt frame - drop silently (no response), per Modbus RTU
        return;
    }

    uint8_t addr = req[0];
    proto_bool broadcast = (addr == 0);
    if (!broadcast && addr != my_addr)
    {
        ModbusV.n = 0; // not addressed to this slave
        return;
    }

    const uint8_t *pdu = req + 1;
    size_t pdu_len = req_len - 3; // strip addr + 2 CRC bytes

    size_t rlen =
        protocore_modbus_process_pdu(work, pdu, pdu_len, resp + 1, protocore_resp_cap - 3); // leave addr + CRC room
    if (rlen == 0)
    {
        ModbusV.n = 0;
        return;
    }
    if (broadcast)
    {
        ModbusV.n = 0; // executed, but a broadcast gets no reply
        return;
    }

    resp[0] = my_addr;
    uint16_t crc = protocore_modbus_crc16(resp, 1 + rlen);
    resp[1 + rlen] = (uint8_t)(crc & 0xFFu);
    resp[2 + rlen] = (uint8_t)(crc >> 8);
    ModbusV.n = 1 + rlen + 2;
}
#endif // PROTOCORE_ENABLE_MODBUS_RTU

// ---------------------------------------------------------------------------
// TCP transport (ESP32-only; the core above is host-tested standalone)
// ---------------------------------------------------------------------------

#if PROTOCORE_HAS_NET_STACK

// Bytes available in the slot's rx ring.
// Thin adapters over the transport RX read API - the ring is owned by transport;
// this service never indexes rx_buffer or advances rx_tail itself.
static size_t ring_avail(const TcpConn *c)
{
    ConnPoolV.slot = c->id;
    ConnPool.available(protocore_conn_pool_span());
    return ConnPoolV.n;
}
static void ring_peek(const TcpConn *c, size_t off, uint8_t *dst, size_t n)
{
    ConnPoolV.slot = c->id;
    ConnPoolV.io.off = off;
    ConnPoolV.io.buf = dst;
    ConnPoolV.io.count = n;
    ConnPool.peek(protocore_conn_pool_span());
}
static void ring_consume(TcpConn *c, size_t n)
{
    ConnPoolV.slot = c->id;
    ConnPoolV.io.count = n;
    ConnPool.consume(protocore_conn_pool_span());
}

static void raw_send(uint8_t slot, const void *data, size_t n)
{
    ConnPoolV.slot = slot;
    ConnPool.active(protocore_conn_pool_span());
    if (!ConnPoolV.ok || n == 0)
    {
        return;
    }
    ConnPoolV.slot = slot;
    ConnPoolV.io.data = data;
    ConnPoolV.io.len = (proto_u16)n;
    ConnPool.send(protocore_conn_pool_span());
    ConnPoolV.slot = slot;
    ConnPool.flush(protocore_conn_pool_span());
}

static void close_conn(uint8_t slot)
{
    ConnPoolV.slot = slot;
    ConnPool.close(protocore_conn_pool_span()); // transport owns detach + slot reset + close
}

void protocore_modbus_rx(uint8_t *restrict work)
{
    uint8_t slot = ModbusV.rx_args.slot;

    TcpConn *conn = &conn_pool[slot];

    // Frame complete ADUs out of the rx ring; a partial frame stays buffered.
    for (;;)
    {
        size_t avail = ring_avail(conn);
        if (avail < 8) // MBAP (7) + at least a function code
        {
            break;
        }

        uint8_t hdr[6];
        ring_peek(conn, 0, hdr, 6);
        uint16_t pid = (uint16_t)((hdr[2] << 8) | hdr[3]);
        uint16_t len = (uint16_t)((hdr[4] << 8) | hdr[5]);
        if (pid != 0 || len < 2 || len > (MODBUS_ADU_MAX - 6))
        {
            close_conn(slot); // not a Modbus TCP frame - drop the connection
            return;
        }
        size_t frame_total = (size_t)6 + len;
        if (avail < frame_total)
        {
            break; // wait for the rest of the frame
        }

        uint8_t adu[MODBUS_ADU_MAX];
        ring_peek(conn, 0, adu, frame_total);
        ring_consume(conn, frame_total);

        uint8_t resp[MODBUS_ADU_MAX];
        ModbusV.process_adu_args.req = adu;
        ModbusV.process_adu_args.req_len = frame_total;
        ModbusV.process_adu_args.resp = resp;
        ModbusV.process_adu_args.protocore_resp_cap = sizeof(resp);
        protocore_modbus_process_adu(work);
        size_t rl = ModbusV.n;
        if (rl)
        {
            raw_send(slot, resp, rl);
        }
    }
}

// The Modbus ProtoHandler (Layer 5 dispatch seam) - only a data handler; a partial ADU waits in the
// rx ring, so there is no per-connection accept/close/poll state. Returned by accessor (no session
// dependency); Session.proto->register_builtins() installs it.
// Designated, so a member's position in the struct does not decide what it binds to. Only the data
// seam is served; the rest stay null.

// The dispatcher calls one fixed signature, so the slot arrives as a parameter and goes onto the
// args; the module's own borrow carries the data model the entry reads.
static void modbus_on_data(uint8_t slot)
{
    ModbusV.rx_args.slot = slot;
    protocore_modbus_rx(protocore_modbus_span());
}

static const ProtoHandler s_modbus_handler = {.on_data = modbus_on_data};

void protocore_modbus_handler(uint8_t *restrict work)
{
    (void)work;

    ModbusV.ptr = &s_modbus_handler;
}

#endif // PROTOCORE_HAS_NET_STACK

/** @brief The operands and the outcome. */
ModbusVars ModbusV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MODBUS
