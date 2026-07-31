// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file s7comm.cpp
 * @brief Siemens S7comm PDU builder + parser (pure, host-tested; constants per Wireshark).
 */

#include "services/fieldbus/s7comm/s7comm.h"

#if PC_ENABLE_S7COMM

#include <string.h>

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

size_t pc_s7_build_setup(uint8_t *buf, size_t cap, uint16_t pdu_ref, uint16_t max_amq_calling, uint16_t max_amq_called,
                         uint16_t pdu_size)
{
    if (!buf)
    {
        return 0;
    }
    const uint16_t param_len = 8;
    size_t total = 10 + param_len;
    if (total > cap)
    {
        return 0;
    }
    size_t p = write_job_header(buf, pdu_ref, param_len, 0);
    buf[p++] = S7_FUNC_SETUP_COMM;
    buf[p++] = 0x00; // reserved
    p += put16(buf + p, max_amq_calling);
    p += put16(buf + p, max_amq_called);
    p += put16(buf + p, pdu_size);
    return p;
}

size_t pc_s7_build_read_request(uint8_t *buf, size_t cap, uint16_t pdu_ref, const S7ReadItem *items, size_t n)
{
    if (!buf || !items || n == 0 || n > 0xFF)
    {
        return 0;
    }
    uint16_t param_len = (uint16_t)(2 + 12 * n); // func + count + items
    size_t total = 10 + param_len;
    if (total > cap)
    {
        return 0;
    }
    size_t p = write_job_header(buf, pdu_ref, param_len, 0);
    buf[p++] = S7_FUNC_READ_VAR;
    buf[p++] = (uint8_t)n;
    for (size_t i = 0; i < n; i++)
    {
        const S7ReadItem &it = items[i];
        buf[p++] = 0x12;            // variable specification
        buf[p++] = 0x0A;            // length of the address spec that follows
        buf[p++] = S7_SYNTAX_S7ANY; // syntax id
        buf[p++] = it.transport_size;
        p += put16(buf + p, it.count);
        p += put16(buf + p, it.db_number);
        buf[p++] = it.area;
        uint32_t addr = it.byte_address << 3; // bit address = byte * 8 (bit offset 0)
        buf[p++] = (uint8_t)(addr >> 16);
        buf[p++] = (uint8_t)(addr >> 8);
        buf[p++] = (uint8_t)(addr & 0xFF);
    }
    return p;
}

// The 2-octet data-item length is expressed in bits for the bit/byte/int data transport sizes, else in bytes
// (the inverse of pc_s7_read_next_item's decode).
static uint16_t s7_data_wire_len(uint8_t data_transport_size, uint16_t data_len)
{
    if (data_transport_size == S7_DTS_BIT || data_transport_size == S7_DTS_BYTE || data_transport_size == S7_DTS_INT)
    {
        return (uint16_t)(data_len * 8);
    }
    return data_len;
}

size_t pc_s7_build_write_request(uint8_t *buf, size_t cap, uint16_t pdu_ref, const S7WriteItem *items, size_t n)
{
    if (!buf || !items || n == 0 || n > 0xFF)
    {
        return 0;
    }
    uint16_t param_len = (uint16_t)(2 + 12 * n); // func + count + item specs (same 12-octet spec as a read)
    // Sum the data section: each item is a 4-octet data header + the value bytes, even-padded except the last.
    size_t data_len = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (items[i].data_len && !items[i].data)
        {
            return 0;
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
        return 0;
    }
    size_t total = 10 + param_len + data_len;
    if (total > cap)
    {
        return 0;
    }

    size_t p = write_job_header(buf, pdu_ref, param_len, (uint16_t)data_len);
    buf[p++] = S7_FUNC_WRITE_VAR;
    buf[p++] = (uint8_t)n;
    for (size_t i = 0; i < n; i++) // parameter: one S7-ANY item spec per item (identical layout to a read)
    {
        const S7WriteItem &it = items[i];
        buf[p++] = 0x12;
        buf[p++] = 0x0A;
        buf[p++] = S7_SYNTAX_S7ANY;
        buf[p++] = it.transport_size;
        p += put16(buf + p, it.count);
        p += put16(buf + p, it.db_number);
        buf[p++] = it.area;
        uint32_t addr = it.byte_address << 3; // bit address = byte * 8 (bit offset 0)
        buf[p++] = (uint8_t)(addr >> 16);
        buf[p++] = (uint8_t)(addr >> 8);
        buf[p++] = (uint8_t)(addr & 0xFF);
    }
    for (size_t i = 0; i < n; i++) // data: one data item per item
    {
        const S7WriteItem &it = items[i];
        buf[p++] = 0x00; // return code (reserved in a request)
        buf[p++] = it.data_transport_size;
        p += put16(buf + p, s7_data_wire_len(it.data_transport_size, it.data_len));
        if (it.data_len)
        {
            memcpy(buf + p, it.data, it.data_len);
            p += it.data_len;
        }
        if (i + 1 < n && (it.data_len & 1)) // even-pad all but the last
        {
            buf[p++] = 0x00;
        }
    }
    return p;
}

bool pc_s7_parse_header(const uint8_t *buf, size_t len, S7Header *out)
{
    if (!buf || !out || len < 10)
    {
        return false;
    }
    if (buf[0] != S7_PROTOCOL_ID)
    {
        return false;
    }
    out->rosctr = buf[1];
    out->pdu_ref = get16(buf + 4);
    out->param_len = get16(buf + 6);
    out->data_len = get16(buf + 8);
    out->error_class = 0;
    out->error_code = 0;
    out->header_len = (out->rosctr == S7_ROSCTR_ACK_DATA) ? 12 : 10; // Ack_Data adds a 2-octet error code
    if (out->header_len == 12)
    {
        if (len < 12)
        {
            return false;
        }
        out->error_class = buf[10];
        out->error_code = buf[11];
    }
    if (out->header_len + (size_t)out->param_len + (size_t)out->data_len > len)
    {
        return false; // not fully buffered
    }
    out->param = buf + out->header_len;
    out->data = out->param + out->param_len;
    return true;
}

bool pc_s7_read_next_item(const uint8_t *data, size_t data_len, size_t *offset, S7DataItem *out)
{
    if (!data || !offset || !out)
    {
        return false;
    }
    size_t o = *offset;
    if (o + 4 > data_len) // return code + transport size + 2-octet length
    {
        return false;
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
        return false;
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
    return true;
}

#endif // PC_ENABLE_S7COMM
