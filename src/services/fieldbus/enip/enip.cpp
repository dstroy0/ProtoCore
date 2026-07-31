// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file enip.cpp
 * @brief EtherNet/IP encapsulation builder + parser (pure, host-tested; constants per Wireshark).
 */

#include "services/fieldbus/enip/enip.h"

#if PC_ENABLE_ENIP

#include <string.h>

// EtherNet/IP fields are little-endian.
static size_t put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
    return 2;
}

static size_t put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
    return 4;
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

size_t pc_eip_build(uint8_t *buf, size_t cap, const EipHeader *h, const uint8_t *data, size_t data_len)
{
    if (!buf || !h || (data_len && !data) || data_len > 0xFFFF)
    {
        return 0;
    }
    size_t total = EIP_HEADER_SIZE + data_len;
    if (total > cap)
    {
        return 0;
    }
    size_t p = 0;
    p += put16(buf + p, h->command);
    p += put16(buf + p, (uint16_t)data_len); // length covers the command data only
    p += put32(buf + p, h->session_handle);
    p += put32(buf + p, h->status);
    memcpy(buf + p, h->sender_context, 8);
    p += 8;
    p += put32(buf + p, h->options);
    if (data_len)
    {
        memcpy(buf + p, data, data_len);
        p += data_len;
    }
    return p;
}

bool pc_eip_parse(const uint8_t *buf, size_t len, EipHeader *out, const uint8_t **data, size_t *data_len)
{
    if (!buf || !out || len < EIP_HEADER_SIZE)
    {
        return false;
    }
    out->command = get16(buf);
    out->length = get16(buf + 2);
    out->session_handle = get32(buf + 4);
    out->status = get32(buf + 8);
    memcpy(out->sender_context, buf + 12, 8);
    out->options = get32(buf + 20);
    if ((size_t)EIP_HEADER_SIZE + out->length > len) // declared data not fully buffered
    {
        return false;
    }
    if (data)
    {
        *data = buf + EIP_HEADER_SIZE;
    }
    if (data_len)
    {
        *data_len = out->length;
    }
    return true;
}

size_t pc_eip_build_register_session(uint8_t *buf, size_t cap, const uint8_t sender_context[8])
{
    EipHeader h;
    memset(&h, 0, sizeof(h));
    h.command = EIP_CMD_REGISTER_SESSION;
    if (sender_context)
    {
        memcpy(h.sender_context, sender_context, 8);
    }
    uint8_t data[4];
    put16(data, 1);     // protocol version
    put16(data + 2, 0); // options flags
    return pc_eip_build(buf, cap, &h, data, sizeof(data));
}

size_t pc_eip_build_unregister_session(uint8_t *buf, size_t cap, uint32_t session_handle,
                                       const uint8_t sender_context[8])
{
    EipHeader h;
    memset(&h, 0, sizeof(h));
    h.command = EIP_CMD_UNREGISTER_SESSION;
    h.session_handle = session_handle; // the session to close
    if (sender_context)
    {
        memcpy(h.sender_context, sender_context, 8);
    }
    return pc_eip_build(buf, cap, &h, nullptr, 0); // no command-specific data
}

size_t pc_eip_build_send_rr_data(uint8_t *buf, size_t cap, uint32_t session_handle, const uint8_t sender_context[8],
                                 uint16_t timeout, const uint8_t *cip, size_t pc_cip_len)
{
    if (!buf || (pc_cip_len && !cip) || pc_cip_len > 0xFFFF)
    {
        return 0;
    }
    // command data: interface handle(4) + timeout(2) + CPF{ count(2) + null item(4) + unconn item(4+cip) }
    size_t data_len = 4 + 2 + 2 + 4 + 4 + pc_cip_len;
    size_t total = EIP_HEADER_SIZE + data_len;
    if (total > cap || data_len > 0xFFFF)
    {
        return 0;
    }

    // Write the header (length = the command-data length) then the command data straight into
    // buf - no temp buffer, so a large CIP payload never lands on the stack.
    EipHeader h;
    memset(&h, 0, sizeof(h));
    h.command = EIP_CMD_SEND_RR_DATA;
    h.session_handle = session_handle;
    if (sender_context)
    {
        memcpy(h.sender_context, sender_context, 8);
    }
    // GCOVR_EXCL_START  unreachable: total>cap above already proved cap>=40>EIP_HEADER_SIZE, so this
    // header-only build (data_len=0) can never fail here; the branch on the `if` itself is dead too.
    if (pc_eip_build(buf, cap, &h, nullptr, 0) == 0) // writes only the 24-octet header, length 0
    {
        return 0;
    }
    // GCOVR_EXCL_STOP
    // Patch the length field (offset 2) to the real command-data length.
    put16(buf + 2, (uint16_t)data_len);

    size_t p = EIP_HEADER_SIZE;
    p += put32(buf + p, 0);       // interface handle (CIP)
    p += put16(buf + p, timeout); // timeout
    p += put16(buf + p, 2);       // CPF item count
    p += put16(buf + p, EIP_CPF_NULL);
    p += put16(buf + p, 0); // null address item length
    p += put16(buf + p, EIP_CPF_UNCONNECTED_DATA);
    p += put16(buf + p, (uint16_t)pc_cip_len);
    if (pc_cip_len)
    {
        memcpy(buf + p, cip, pc_cip_len);
        p += pc_cip_len;
    }
    return p;
}

size_t pc_eip_build_list_identity(uint8_t *buf, size_t cap, const uint8_t sender_context[8])
{
    EipHeader h;
    memset(&h, 0, sizeof(h));
    h.command = EIP_CMD_LIST_IDENTITY;
    if (sender_context)
    {
        memcpy(h.sender_context, sender_context, 8);
    }
    return pc_eip_build(buf, cap, &h, nullptr, 0); // no command-specific data
}

bool pc_eip_parse_list_identity(const uint8_t *data, size_t data_len, EipIdentity *out)
{
    if (!data || !out || data_len < 2) // item count
    {
        return false;
    }
    uint16_t item_count = get16(data);
    size_t pos = 2;
    for (uint16_t i = 0; i < item_count; i++)
    {
        if (pos + 4 > data_len)
        {
            return false;
        }
        uint16_t type = get16(data + pos);
        uint16_t ilen = get16(data + pos + 2);
        pos += 4;
        if (pos + ilen > data_len)
        {
            return false;
        }
        if (type == EIP_CPF_LIST_IDENTITY)
        {
            const uint8_t *it = data + pos;
            if (ilen <
                33) // proto(2) + sockaddr(16) + vendor/type/code(6) + rev(2) + status(2) + serial(4) + namelen(1)
            {
                return false;
            }
            uint8_t name_len = it[32];
            if ((size_t)ilen < (size_t)34 + name_len) // + the name + the trailing state octet
            {
                return false;
            }
            out->protocol_version = get16(it);
            // it[2..17] is the 16-octet CIP socket address (network-order); not reinterpreted here.
            out->vendor_id = get16(it + 18);
            out->device_type = get16(it + 20);
            out->product_code = get16(it + 22);
            out->revision_major = it[24];
            out->revision_minor = it[25];
            out->status = get16(it + 26);
            out->serial_number = get32(it + 28);
            out->product_name_len = name_len;
            out->product_name = (const char *)(it + 33);
            out->state = it[33 + name_len];
            return true;
        }
        pos += ilen;
    }
    return false; // no List Identity item
}

bool pc_eip_parse_send_rr_data(const uint8_t *data, size_t data_len, const uint8_t **cip, size_t *pc_cip_len)
{
    if (!data || data_len < 8) // interface handle(4) + timeout(2) + item count(2)
    {
        return false;
    }
    size_t pos = 6; // skip interface handle + timeout
    uint16_t item_count = get16(data + pos);
    pos += 2;
    for (uint16_t i = 0; i < item_count; i++)
    {
        if (pos + 4 > data_len)
        {
            return false;
        }
        uint16_t type = get16(data + pos);
        uint16_t ilen = get16(data + pos + 2);
        pos += 4;
        if (pos + ilen > data_len)
        {
            return false;
        }
        if (type == EIP_CPF_UNCONNECTED_DATA)
        {
            if (cip)
            {
                *cip = data + pos;
            }
            if (pc_cip_len)
            {
                *pc_cip_len = ilen;
            }
            return true;
        }
        pos += ilen;
    }
    return false; // no unconnected data item
}

#endif // PC_ENABLE_ENIP
