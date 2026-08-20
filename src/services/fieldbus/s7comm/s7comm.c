// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file s7comm.c
 * @brief Siemens S7comm PDU builder + parser (pure, host-tested; constants per Wireshark).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_S7COMM

#include "mmgr/protomem/protomem.h"
#include "services/fieldbus/s7comm/s7comm.h"

PROTOCORE_BEGIN_DECLS

static size_t put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
    return 2;
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

// Write the 10-octet job/request header (protocol id, ROSCTR, redundancy, pdu-ref, lengths).
static size_t write_job_header(uint8_t *buf, uint16_t pdu_ref, uint16_t param_len, uint16_t data_len)
{
    size_t p = 0;
    buf[p++] = S7_PROTOCOL_ID;
    buf[p++] = S7_ROSCTR_JOB;
    p += put16(buf + p, 0); // redundancy id (reserved)
    p += put16(buf + p, pdu_ref);
    p += put16(buf + p, param_len);
    p += put16(buf + p, data_len);
    return p; // 10
}

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_s7comm_build_setup(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = S7commV.build_setup_args.buf;
    size_t cap = S7commV.build_setup_args.cap;
    uint16_t pdu_ref = S7commV.build_setup_args.pdu_ref;
    uint16_t max_amq_calling = S7commV.build_setup_args.max_amq_calling;
    uint16_t max_amq_called = S7commV.build_setup_args.max_amq_called;
    uint16_t pdu_size = S7commV.build_setup_args.pdu_size;

    if (!buf)
    {
        S7commV.n = 0;
        return;
    }
    const uint16_t param_len = 8;
    size_t total = 10 + param_len;
    if (total > cap)
    {
        S7commV.n = 0;
        return;
    }
    size_t p = write_job_header(buf, pdu_ref, param_len, 0);
    buf[p++] = S7_FUNC_SETUP_COMM;
    buf[p++] = 0x00; // reserved
    p += put16(buf + p, max_amq_calling);
    p += put16(buf + p, max_amq_called);
    p += put16(buf + p, pdu_size);
    S7commV.n = p;
}

void protocore_s7comm_build_read_request(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = S7commV.build_read_request_args.buf;
    size_t cap = S7commV.build_read_request_args.cap;
    uint16_t pdu_ref = S7commV.build_read_request_args.pdu_ref;
    const S7ReadItem *items = S7commV.build_read_request_args.items;
    size_t n = S7commV.build_read_request_args.n;

    if (!buf || !items || n == 0 || n > 0xFF)
    {
        S7commV.n = 0;
        return;
    }
    uint16_t param_len = (uint16_t)(2 + 12 * n); // func + count + items
    size_t total = 10 + param_len;
    if (total > cap)
    {
        S7commV.n = 0;
        return;
    }
    size_t p = write_job_header(buf, pdu_ref, param_len, 0);
    buf[p++] = S7_FUNC_READ_VAR;
    buf[p++] = (uint8_t)n;
    for (size_t i = 0; i < n; i++)
    {
        const S7ReadItem *it = &items[i];
        buf[p++] = 0x12;            // variable specification
        buf[p++] = 0x0A;            // length of the address spec that follows
        buf[p++] = S7_SYNTAX_S7ANY; // syntax id
        buf[p++] = it->transport_size;
        p += put16(buf + p, it->count);
        p += put16(buf + p, it->db_number);
        buf[p++] = it->area;
        uint32_t addr = it->byte_address << 3; // bit address = byte * 8 (bit offset 0)
        buf[p++] = (uint8_t)(addr >> 16);
        buf[p++] = (uint8_t)(addr >> 8);
        buf[p++] = (uint8_t)(addr & 0xFF);
    }
    S7commV.n = p;
}

// The 2-octet data-item length is expressed in bits for the bit/byte/int data transport sizes, else in bytes
// (the inverse of S7comm.read_next_item's decode).
static uint16_t s7_data_wire_len(uint8_t data_transport_size, uint16_t data_len)
{
    if (data_transport_size == S7_DTS_BIT || data_transport_size == S7_DTS_BYTE || data_transport_size == S7_DTS_INT)
    {
        return (uint16_t)(data_len * 8);
    }
    return data_len;
}

void protocore_s7comm_build_write_request(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = S7commV.build_write_request_args.buf;
    size_t cap = S7commV.build_write_request_args.cap;
    uint16_t pdu_ref = S7commV.build_write_request_args.pdu_ref;
    const S7WriteItem *items = S7commV.build_write_request_args.items;
    size_t n = S7commV.build_write_request_args.n;

    if (!buf || !items || n == 0 || n > 0xFF)
    {
        S7commV.n = 0;
        return;
    }
    uint16_t param_len = (uint16_t)(2 + 12 * n); // func + count + item specs (same 12-octet spec as a read)
    // Sum the data section: each item is a 4-octet data header + the value bytes, even-padded except the last.
    size_t data_len = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (items[i].data_len && !items[i].data)
        {
            S7commV.n = 0;
            return;
        }
        size_t item_bytes = 4 + items[i].data_len;
        if (i + 1 < n && (items[i].data_len & 1)) // pad every item but the last to an even length
        {
            item_bytes++;
        }
        data_len += item_bytes;
    }
    if (data_len > 0xFFFF)
    {
        S7commV.n = 0;
        return;
    }
    size_t total = 10 + param_len + data_len;
    if (total > cap)
    {
        S7commV.n = 0;
        return;
    }

    size_t p = write_job_header(buf, pdu_ref, param_len, (uint16_t)data_len);
    buf[p++] = S7_FUNC_WRITE_VAR;
    buf[p++] = (uint8_t)n;
    for (size_t i = 0; i < n; i++) // parameter: one S7-ANY item spec per item (identical layout to a read)
    {
        const S7WriteItem *it = &items[i];
        buf[p++] = 0x12;
        buf[p++] = 0x0A;
        buf[p++] = S7_SYNTAX_S7ANY;
        buf[p++] = it->transport_size;
        p += put16(buf + p, it->count);
        p += put16(buf + p, it->db_number);
        buf[p++] = it->area;
        uint32_t addr = it->byte_address << 3; // bit address = byte * 8 (bit offset 0)
        buf[p++] = (uint8_t)(addr >> 16);
        buf[p++] = (uint8_t)(addr >> 8);
        buf[p++] = (uint8_t)(addr & 0xFF);
    }
    for (size_t i = 0; i < n; i++) // data: one data item per item
    {
        const S7WriteItem *it = &items[i];
        buf[p++] = 0x00; // return code (reserved in a request)
        buf[p++] = it->data_transport_size;
        p += put16(buf + p, s7_data_wire_len(it->data_transport_size, it->data_len));
        if (it->data_len)
        {
            mem.cpy(buf + p, it->data, it->data_len);
            p += it->data_len;
        }
        if (i + 1 < n && (it->data_len & 1)) // even-pad all but the last
        {
            buf[p++] = 0x00;
        }
    }
    S7commV.n = p;
}

void protocore_s7comm_parse_header(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = S7commV.parse_header_args.buf;
    size_t len = S7commV.parse_header_args.len;
    S7Header *out = S7commV.parse_header_args.out;

    if (!buf || !out || len < 10)
    {
        S7commV.ok = PROTO_FALSE;
        return;
    }
    if (buf[0] != S7_PROTOCOL_ID)
    {
        S7commV.ok = PROTO_FALSE;
        return;
    }
    out->rosctr = buf[1];
    out->pdu_ref = get16(buf + 4);
    out->param_len = get16(buf + 6);
    out->data_len = get16(buf + 8);
    out->error_class = 0;
    out->error_code = 0;
    // A response ROSCTR (Ack or Ack_Data) carries an error class + error code after the lengths.
    out->header_len = (out->rosctr == S7_ROSCTR_ACK || out->rosctr == S7_ROSCTR_ACK_DATA) ? 12 : 10;
    if (out->header_len == 12)
    {
        if (len < 12)
        {
            S7commV.ok = PROTO_FALSE;
            return;
        }
        out->error_class = buf[10];
        out->error_code = buf[11];
    }
    if (out->header_len + (size_t)out->param_len + (size_t)out->data_len > len)
    {
        S7commV.ok = PROTO_FALSE;
        return; // not fully buffered
    }
    out->param = buf + out->header_len;
    out->data = out->param + out->param_len;
    S7commV.ok = PROTO_TRUE;
}

void protocore_s7comm_read_next_item(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *data = S7commV.read_next_item_args.data;
    size_t data_len = S7commV.read_next_item_args.data_len;
    size_t *offset = S7commV.read_next_item_args.offset;
    S7DataItem *out = S7commV.read_next_item_args.out;

    if (!data || !offset || !out)
    {
        S7commV.ok = PROTO_FALSE;
        return;
    }
    size_t o = *offset;
    if (o + 4 > data_len) // return code + transport size + 2-octet length
    {
        S7commV.ok = PROTO_FALSE;
        return;
    }
    out->return_code = data[o];
    out->transport_size = data[o + 1];
    uint16_t raw_len = get16(data + o + 2);

    // The length is in bits for the bit/byte/int transport sizes, otherwise in bytes.
    size_t bytes;
    if (out->transport_size == S7_DTS_BIT || out->transport_size == S7_DTS_BYTE || out->transport_size == S7_DTS_INT)
    {
        bytes = (raw_len + 7) / 8;
    }
    else
    {
        bytes = raw_len;
    }

    if (o + 4 + bytes > data_len)
    {
        S7commV.ok = PROTO_FALSE;
        return;
    }
    out->data = data + o + 4;
    out->data_len = bytes;

    o += 4 + bytes;
    // Each item except the last is padded to an even length; skip the fill byte.
    if (o < data_len && (bytes & 1))
    {
        o++;
    }
    *offset = o;
    S7commV.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
S7commVars S7commV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_S7COMM
