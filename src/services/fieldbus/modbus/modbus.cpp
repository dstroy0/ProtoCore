// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file modbus.cpp
 * @brief Modbus TCP slave: data model, MBAP/PDU codec, and the TCP transport.
 */

#include "services/fieldbus/modbus/modbus.h"
#include "shared_primitives/crc.h" // PC_CRC16_MODBUS

#if PC_ENABLE_MODBUS

#include <string.h>

// ---------------------------------------------------------------------------
// Data model (all BSS - no heap)
// ---------------------------------------------------------------------------

// All Modbus data-model state, owned by one instance (internal linkage): the coil / discrete
// bitfields, the holding / input registers, and the write callback, grouped so it is one
// named owner, unreachable from any other translation unit.
#if defined(ARDUINO)
#include "network_drivers/session/proto_handler.h"
#include "network_drivers/transport/tcp.h"
#endif
struct ModbusCtx
{
    uint8_t coils[(PC_MODBUS_COILS + 7) / 8];
    uint8_t discrete[(PC_MODBUS_DISCRETE_INPUTS + 7) / 8];
    uint16_t holding[PC_MODBUS_HOLDING_REGS];
    uint16_t input[PC_MODBUS_INPUT_REGS];
    ModbusWriteCb write_cb = nullptr;
};
static ModbusCtx s_modbus;

static bool bit_get(const uint8_t *a, uint16_t i)
{
    return (a[i >> 3] >> (i & 7)) & 1u;
}
static void bit_set(uint8_t *a, uint16_t i, bool v)
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

void pc_modbus_server_init()
{
    memset(s_modbus.coils, 0, sizeof(s_modbus.coils));
    memset(s_modbus.discrete, 0, sizeof(s_modbus.discrete));
    memset(s_modbus.holding, 0, sizeof(s_modbus.holding));
    memset(s_modbus.input, 0, sizeof(s_modbus.input));
    s_modbus.write_cb = nullptr;
}

void pc_modbus_on_write(ModbusWriteCb cb)
{
    s_modbus.write_cb = cb;
}

bool pc_modbus_get_coil(uint16_t addr)
{
    return (addr < PC_MODBUS_COILS) ? bit_get(s_modbus.coils, addr) : false;
}
void pc_modbus_set_coil(uint16_t addr, bool on)
{
    if (addr < PC_MODBUS_COILS)
    {
        bit_set(s_modbus.coils, addr, on);
    }
}
bool pc_modbus_get_discrete_input(uint16_t addr)
{
    return (addr < PC_MODBUS_DISCRETE_INPUTS) ? bit_get(s_modbus.discrete, addr) : false;
}
void pc_modbus_set_discrete_input(uint16_t addr, bool on)
{
    if (addr < PC_MODBUS_DISCRETE_INPUTS)
    {
        bit_set(s_modbus.discrete, addr, on);
    }
}
uint16_t pc_modbus_get_holding_reg(uint16_t addr)
{
    return (addr < PC_MODBUS_HOLDING_REGS) ? s_modbus.holding[addr] : 0;
}
void pc_modbus_set_holding_reg(uint16_t addr, uint16_t value)
{
    if (addr < PC_MODBUS_HOLDING_REGS)
    {
        s_modbus.holding[addr] = value;
    }
}
uint16_t pc_modbus_get_input_reg(uint16_t addr)
{
    return (addr < PC_MODBUS_INPUT_REGS) ? s_modbus.input[addr] : 0;
}
void pc_modbus_set_input_reg(uint16_t addr, uint16_t value)
{
    if (addr < PC_MODBUS_INPUT_REGS)
    {
        s_modbus.input[addr] = value;
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
static size_t pc_modbus_process_pdu(const uint8_t *pdu, size_t pdu_len, uint8_t *out, size_t out_cap)
{
    // Both callers already guarantee at least one PDU byte, so this guard cannot fire: the TCP path
    // rejects len < 2 before computing pdu_len = len - 1, and the RTU path rejects req_len < 4 before
    // computing pdu_len = req_len - 3. Kept as defense in depth for any future caller.
    if (pdu_len < 1) // GCOVR_EXCL_LINE
    {
        return 0; // GCOVR_EXCL_LINE
    }
    ModbusFunction fc = static_cast<ModbusFunction>(pdu[0]);

    // Dispatch on the function code; each case validates its own request length and
    // address/quantity range, replying with the data or a Modbus exception PDU.
    switch (fc)
    {
    // FC1/FC2: read up to 2000 single-bit coils / discrete inputs, packed 8 per byte.
    case ModbusFunction::MODBUS_FC_READ_COILS:
    case ModbusFunction::MODBUS_FC_READ_DISCRETE_INPUTS: {
        if (pdu_len < 5)
        {
            return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        uint16_t start = rd16(pdu + 1), qty = rd16(pdu + 3);
        uint16_t limit = (fc == ModbusFunction::MODBUS_FC_READ_COILS) ? PC_MODBUS_COILS : PC_MODBUS_DISCRETE_INPUTS;
        const uint8_t *src = (fc == ModbusFunction::MODBUS_FC_READ_COILS) ? s_modbus.coils : s_modbus.discrete;
        if (qty < 1 || qty > 2000)
        {
            return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        if ((uint32_t)start + qty > limit)
        {
            return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_DATA_ADDRESS, out);
        }
        uint8_t bytes = (uint8_t)((qty + 7) / 8);
        if ((size_t)2 + bytes > out_cap)
        {
            return 0;
        }
        out[0] = (uint8_t)fc;
        out[1] = bytes;
        memset(out + 2, 0, bytes);
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
    case ModbusFunction::MODBUS_FC_READ_HOLDING_REGS:
    case ModbusFunction::MODBUS_FC_READ_INPUT_REGS: {
        if (pdu_len < 5)
        {
            return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        uint16_t start = rd16(pdu + 1), qty = rd16(pdu + 3);
        uint16_t limit =
            (fc == ModbusFunction::MODBUS_FC_READ_HOLDING_REGS) ? PC_MODBUS_HOLDING_REGS : PC_MODBUS_INPUT_REGS;
        const uint16_t *src = (fc == ModbusFunction::MODBUS_FC_READ_HOLDING_REGS) ? s_modbus.holding : s_modbus.input;
        if (qty < 1 || qty > 125)
        {
            return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        if ((uint32_t)start + qty > limit)
        {
            return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_DATA_ADDRESS, out);
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
    case ModbusFunction::MODBUS_FC_WRITE_SINGLE_COIL: {
        if (pdu_len < 5)
        {
            return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        uint16_t addr = rd16(pdu + 1), value = rd16(pdu + 3);
        if (value != 0x0000 && value != 0xFF00)
        {
            return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        if (addr >= PC_MODBUS_COILS)
        {
            return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_DATA_ADDRESS, out);
        }
        bit_set(s_modbus.coils, addr, value == 0xFF00);
        if (s_modbus.write_cb)
        {
            s_modbus.write_cb((uint8_t)fc, addr, 1);
        }
        if (out_cap < 5)
        {
            return 0;
        }
        memcpy(out, pdu, 5); // echo request
        return 5;
    }

    // FC6: write one holding register; echo the request back.
    case ModbusFunction::MODBUS_FC_WRITE_SINGLE_REG: {
        if (pdu_len < 5)
        {
            return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        uint16_t addr = rd16(pdu + 1), value = rd16(pdu + 3);
        if (addr >= PC_MODBUS_HOLDING_REGS)
        {
            return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_DATA_ADDRESS, out);
        }
        s_modbus.holding[addr] = value;
        if (s_modbus.write_cb)
        {
            s_modbus.write_cb((uint8_t)fc, addr, 1);
        }
        if (out_cap < 5)
        {
            return 0;
        }
        memcpy(out, pdu, 5);
        return 5;
    }

    // FC15: write up to 1968 coils from a packed bitfield; reply start + quantity.
    case ModbusFunction::MODBUS_FC_WRITE_MULTIPLE_COILS: {
        if (pdu_len < 6)
        {
            return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        uint16_t start = rd16(pdu + 1), qty = rd16(pdu + 3);
        uint8_t bc = pdu[5];
        if (qty < 1 || qty > 1968 || bc != (uint8_t)((qty + 7) / 8) || pdu_len < (size_t)6 + bc)
        {
            return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        if ((uint32_t)start + qty > PC_MODBUS_COILS)
        {
            return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_DATA_ADDRESS, out);
        }
        for (uint16_t i = 0; i < qty; i++)
        {
            bool v = (pdu[6 + (i >> 3)] >> (i & 7)) & 1u;
            bit_set(s_modbus.coils, (uint16_t)(start + i), v);
        }
        if (s_modbus.write_cb)
        {
            s_modbus.write_cb((uint8_t)fc, start, qty);
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
    case ModbusFunction::MODBUS_FC_WRITE_MULTIPLE_REGS: {
        if (pdu_len < 6)
        {
            return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        uint16_t start = rd16(pdu + 1), qty = rd16(pdu + 3);
        uint8_t bc = pdu[5];
        if (qty < 1 || qty > 123 || bc != (uint8_t)(qty * 2) || pdu_len < (size_t)6 + bc)
        {
            return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        if ((uint32_t)start + qty > PC_MODBUS_HOLDING_REGS)
        {
            return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_DATA_ADDRESS, out);
        }
        for (uint16_t i = 0; i < qty; i++)
        {
            s_modbus.holding[start + i] = rd16(pdu + 6 + i * 2);
        }
        if (s_modbus.write_cb)
        {
            s_modbus.write_cb((uint8_t)fc, start, qty);
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
    case ModbusFunction::MODBUS_FC_MASK_WRITE_REG: {
        if (pdu_len < 7)
        {
            return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        uint16_t addr = rd16(pdu + 1);
        uint16_t and_mask = rd16(pdu + 3);
        uint16_t or_mask = rd16(pdu + 5);
        if (addr >= PC_MODBUS_HOLDING_REGS)
        {
            return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_DATA_ADDRESS, out);
        }
        uint16_t cur = s_modbus.holding[addr];
        s_modbus.holding[addr] = (uint16_t)((cur & and_mask) | (or_mask & (uint16_t)~and_mask));
        if (s_modbus.write_cb)
        {
            s_modbus.write_cb((uint8_t)fc, addr, 1);
        }
        if (out_cap < 7)
        {
            return 0;
        }
        memcpy(out, pdu, 7); // echo request (address + both masks)
        return 7;
    }

    // FC23: read/write multiple holding registers in one transaction - the write is applied first, then
    // the read reflects it (a contiguous read span + a contiguous write span). Reply is the read data.
    case ModbusFunction::MODBUS_FC_READ_WRITE_MULTIPLE_REGS: {
        if (pdu_len < 10)
        {
            return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        uint16_t r_start = rd16(pdu + 1);
        uint16_t r_qty = rd16(pdu + 3);
        uint16_t w_start = rd16(pdu + 5);
        uint16_t w_qty = rd16(pdu + 7);
        uint8_t w_bc = pdu[9];
        if (r_qty < 1 || r_qty > 125 || w_qty < 1 || w_qty > 121 || w_bc != (uint8_t)(w_qty * 2) ||
            pdu_len < (size_t)10 + w_bc)
        {
            return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_DATA_VALUE, out);
        }
        if ((uint32_t)r_start + r_qty > PC_MODBUS_HOLDING_REGS || (uint32_t)w_start + w_qty > PC_MODBUS_HOLDING_REGS)
        {
            return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_DATA_ADDRESS, out);
        }
        for (uint16_t i = 0; i < w_qty; i++) // write first (§6.17)
        {
            s_modbus.holding[w_start + i] = rd16(pdu + 10 + i * 2);
        }
        if (s_modbus.write_cb)
        {
            s_modbus.write_cb((uint8_t)fc, w_start, w_qty);
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
            wr16(out + 2 + i * 2, s_modbus.holding[r_start + i]);
        }
        return (size_t)2 + r_bytes;
    }

    // Any unsupported function code: reply with the ILLEGAL FUNCTION exception.
    default:
        return pdu_exception(fc, ModbusException::MODBUS_EX_ILLEGAL_FUNCTION, out);
    }
}

size_t pc_modbus_process_adu(const uint8_t *req, size_t req_len, uint8_t *resp, size_t pc_resp_cap)
{
    if (req_len < 8 || pc_resp_cap < 8)
    {
        return 0; // need MBAP (7) + at least a function code
    }

    uint16_t tid = rd16(req);
    uint16_t pid = rd16(req + 2);
    uint16_t len = rd16(req + 4);
    uint8_t uid = req[6];

    if (pid != 0)
    {
        return 0; // not Modbus
    }
    if (len < 2 || (size_t)6 + len > req_len)
    {
        return 0; // length field disagrees with the frame
    }

    const uint8_t *pdu = req + 7;
    size_t pdu_len = (size_t)len - 1; // len counts the unit id + the PDU

    size_t rlen = pc_modbus_process_pdu(pdu, pdu_len, resp + 7, pc_resp_cap - 7);
    if (rlen == 0)
    {
        return 0;
    }

    wr16(resp, tid);                      // echo transaction id
    wr16(resp + 2, 0);                    // protocol id = 0
    wr16(resp + 4, (uint16_t)(1 + rlen)); // length = unit id + response PDU
    resp[6] = uid;                        // echo unit id
    return 7 + rlen;
}

#if PC_ENABLE_MODBUS_RTU
// CRC16-Modbus (init 0xFFFF, reflected poly 0xA001); transmitted low byte first. test_crc diffs the
// shared engine against the loop that used to live here over every length 0..64.
static uint16_t pc_modbus_crc16(const uint8_t *data, size_t len)
{
    return (uint16_t)pc_crc(&PC_CRC16_MODBUS, data, len);
}

size_t pc_modbus_rtu_process_adu(const uint8_t *req, size_t req_len, uint8_t *resp, size_t pc_resp_cap, uint8_t my_addr)
{
    if (req_len < 4 || pc_resp_cap < 4) // addr(1) + min PDU(1) + CRC(2)
    {
        return 0;
    }

    // Validate the trailing CRC over [addr .. last PDU byte] (low byte first).
    uint16_t want = pc_modbus_crc16(req, req_len - 2);
    uint16_t got = (uint16_t)(req[req_len - 2] | (req[req_len - 1] << 8));
    if (want != got)
    {
        return 0; // corrupt frame - drop silently (no response), per Modbus RTU
    }

    uint8_t addr = req[0];
    bool broadcast = (addr == 0);
    if (!broadcast && addr != my_addr)
    {
        return 0; // not addressed to this slave
    }

    const uint8_t *pdu = req + 1;
    size_t pdu_len = req_len - 3; // strip addr + 2 CRC bytes

    size_t rlen = pc_modbus_process_pdu(pdu, pdu_len, resp + 1, pc_resp_cap - 3); // leave addr + CRC room
    if (rlen == 0)
    {
        return 0;
    }
    if (broadcast)
    {
        return 0; // executed, but a broadcast gets no reply
    }

    resp[0] = my_addr;
    uint16_t crc = pc_modbus_crc16(resp, 1 + rlen);
    resp[1 + rlen] = (uint8_t)(crc & 0xFFu);
    resp[2 + rlen] = (uint8_t)(crc >> 8);
    return 1 + rlen + 2;
}
#endif // PC_ENABLE_MODBUS_RTU

// ---------------------------------------------------------------------------
// TCP transport (ESP32-only; the core above is host-tested standalone)
// ---------------------------------------------------------------------------

#if defined(ARDUINO)

// Bytes available in the slot's rx ring.
// Thin adapters over the transport RX read API - the ring is owned by transport;
// this service never indexes rx_buffer or advances rx_tail itself.
static size_t ring_avail(const TcpConn *c)
{
    return pc_conn_available(c->id);
}
static void ring_peek(const TcpConn *c, size_t off, uint8_t *dst, size_t n)
{
    pc_conn_peek(c->id, off, dst, n);
}
static void ring_consume(TcpConn *c, size_t n)
{
    pc_conn_consume(c->id, n);
}

static void raw_send(uint8_t slot, const void *data, size_t n)
{
    if (!pc_conn_active(slot) || n == 0)
    {
        return;
    }
    pc_conn_send(slot, data, (u16_t)n);
    pc_conn_flush(slot);
}

static void close_conn(uint8_t slot)
{
    pc_conn_close(slot); // transport owns detach + slot reset + close
}

void pc_modbus_rx(uint8_t slot)
{
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
        size_t rl = pc_modbus_process_adu(adu, frame_total, resp, sizeof(resp));
        if (rl)
        {
            raw_send(slot, resp, rl);
        }
    }
}

// The Modbus ProtoHandler (Layer 5 dispatch seam) - only a data handler; a partial ADU waits in the
// rx ring, so there is no per-connection accept/close/poll state. Returned by accessor (no session
// dependency); proto_register_builtins() installs it.
static const ProtoHandler s_modbus_handler = {nullptr, pc_modbus_rx, nullptr, nullptr};
const ProtoHandler *pc_modbus_proto_handler(void)
{
    return &s_modbus_handler;
}

#else // !ARDUINO

// Host builds test the pure ADU codec; there is no TCP transport handler.
const ProtoHandler *pc_modbus_proto_handler(void)
{
    return nullptr;
}

#endif // ARDUINO

#endif // PC_ENABLE_MODBUS
