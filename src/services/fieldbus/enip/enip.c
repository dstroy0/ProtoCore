// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file enip.c
 * @brief EtherNet/IP encapsulation builder + parser (pure, host-tested; constants per Wireshark).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_ENIP

#include "mmgr/protomem/protomem.h"
#include "services/fieldbus/enip/enip.h"

PROTOCORE_BEGIN_DECLS

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

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_enip_build(uint8_t *restrict work);

void protocore_enip_build(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = EnipV.build_args.buf;
    size_t cap = EnipV.build_args.cap;
    const EipHeader *h = EnipV.build_args.h;
    const uint8_t *data = EnipV.build_args.data;
    size_t data_len = EnipV.build_args.data_len;

    if (!buf || !h || (data_len && !data) || data_len > 0xFFFF)
    {
        EnipV.n = 0;
        return;
    }
    size_t total = EIP_HEADER_SIZE + data_len;
    if (total > cap)
    {
        EnipV.n = 0;
        return;
    }
    size_t p = 0;
    p += put16(buf + p, h->command);
    p += put16(buf + p, (uint16_t)data_len); // length covers the command data only
    p += put32(buf + p, h->session_handle);
    p += put32(buf + p, h->status);
    mem.cpy(buf + p, h->sender_context, 8);
    p += 8;
    p += put32(buf + p, h->options);
    if (data_len)
    {
        mem.cpy(buf + p, data, data_len);
        p += data_len;
    }
    EnipV.n = p;
}

void protocore_enip_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = EnipV.parse_args.buf;
    size_t len = EnipV.parse_args.len;
    EipHeader *out = EnipV.parse_args.out;
    const uint8_t **data = EnipV.parse_args.data;
    size_t *data_len = EnipV.parse_args.data_len;

    if (!buf || !out || len < EIP_HEADER_SIZE)
    {
        EnipV.ok = PROTO_FALSE;
        return;
    }
    out->command = get16(buf);
    out->length = get16(buf + 2);
    out->session_handle = get32(buf + 4);
    out->status = get32(buf + 8);
    mem.cpy(out->sender_context, buf + 12, 8);
    out->options = get32(buf + 20);
    if ((size_t)EIP_HEADER_SIZE + out->length > len) // declared data not fully buffered
    {
        EnipV.ok = PROTO_FALSE;
        return;
    }
    if (data)
    {
        *data = buf + EIP_HEADER_SIZE;
    }
    if (data_len)
    {
        *data_len = out->length;
    }
    EnipV.ok = PROTO_TRUE;
}

void protocore_enip_build_register_session(uint8_t *restrict work)
{
    uint8_t *buf = EnipV.build_register_session_args.buf;
    size_t cap = EnipV.build_register_session_args.cap;
    const uint8_t *sender_context = EnipV.build_register_session_args.sender_context;

    EipHeader h;
    mem.set(&h, 0, sizeof(h));
    h.command = EIP_CMD_REGISTER_SESSION;
    if (sender_context)
    {
        mem.cpy(h.sender_context, sender_context, 8);
    }
    uint8_t data[4];
    put16(data, 1);     // protocol version
    put16(data + 2, 0); // options flags
    EnipV.build_args.buf = buf;
    EnipV.build_args.cap = cap;
    EnipV.build_args.h = &h;
    EnipV.build_args.data = data;
    EnipV.build_args.data_len = sizeof(data);
    protocore_enip_build(work);
}

void protocore_enip_build_unregister_session(uint8_t *restrict work)
{
    uint8_t *buf = EnipV.build_unregister_session_args.buf;
    size_t cap = EnipV.build_unregister_session_args.cap;
    uint32_t session_handle = EnipV.build_unregister_session_args.session_handle;
    const uint8_t *sender_context = EnipV.build_unregister_session_args.sender_context;

    EipHeader h;
    mem.set(&h, 0, sizeof(h));
    h.command = EIP_CMD_UNREGISTER_SESSION;
    h.session_handle = session_handle; // the session to close
    if (sender_context)
    {
        mem.cpy(h.sender_context, sender_context, 8);
    }
    EnipV.build_args.buf = buf;
    EnipV.build_args.cap = cap;
    EnipV.build_args.h = &h;
    EnipV.build_args.data = NULL;
    EnipV.build_args.data_len = 0;
    protocore_enip_build(work); // no command-specific data
}

void protocore_enip_build_send_rr_data(uint8_t *restrict work)
{
    uint8_t *buf = EnipV.build_send_rr_data_args.buf;
    size_t cap = EnipV.build_send_rr_data_args.cap;
    uint32_t session_handle = EnipV.build_send_rr_data_args.session_handle;
    const uint8_t *sender_context = EnipV.build_send_rr_data_args.sender_context;
    uint16_t timeout = EnipV.build_send_rr_data_args.timeout;
    const uint8_t *cip = EnipV.build_send_rr_data_args.cip;
    size_t cip_len = EnipV.build_send_rr_data_args.cip_len;

    if (!buf || (cip_len && !cip) || cip_len > 0xFFFF)
    {
        EnipV.n = 0;
        return;
    }
    // command data: interface handle(4) + timeout(2) + CPF{ count(2) + null item(4) + unconn item(4+cip) }
    size_t data_len = 4 + 2 + 2 + 4 + 4 + cip_len;
    size_t total = EIP_HEADER_SIZE + data_len;
    if (total > cap || data_len > 0xFFFF)
    {
        EnipV.n = 0;
        return;
    }

    // Write the header (length = the command-data length) then the command data straight into
    // buf - no temp buffer, so a large CIP payload never lands on the stack.
    EipHeader h;
    mem.set(&h, 0, sizeof(h));
    h.command = EIP_CMD_SEND_RR_DATA;
    h.session_handle = session_handle;
    if (sender_context)
    {
        mem.cpy(h.sender_context, sender_context, 8);
    }
    EnipV.build_args.buf = buf;
    EnipV.build_args.cap = cap;
    EnipV.build_args.h = &h;
    EnipV.build_args.data = NULL;
    EnipV.build_args.data_len = 0;
    protocore_enip_build(work);
    if (EnipV.n == 0) // writes only the 24-octet header, length 0
    {
        EnipV.n = 0;
        return;
    }
    // Patch the length field (offset 2) to the real command-data length.
    put16(buf + 2, (uint16_t)data_len);

    size_t p = EIP_HEADER_SIZE;
    p += put32(buf + p, 0);       // interface handle (CIP)
    p += put16(buf + p, timeout); // timeout
    p += put16(buf + p, 2);       // CPF item count
    p += put16(buf + p, EIP_CPF_NULL);
    p += put16(buf + p, 0); // null address item length
    p += put16(buf + p, EIP_CPF_UNCONNECTED_DATA);
    p += put16(buf + p, (uint16_t)cip_len);
    if (cip_len)
    {
        mem.cpy(buf + p, cip, cip_len);
        p += cip_len;
    }
    EnipV.n = p;
}

void protocore_enip_build_list_identity(uint8_t *restrict work)
{
    uint8_t *buf = EnipV.build_list_identity_args.buf;
    size_t cap = EnipV.build_list_identity_args.cap;
    const uint8_t *sender_context = EnipV.build_list_identity_args.sender_context;

    EipHeader h;
    mem.set(&h, 0, sizeof(h));
    h.command = EIP_CMD_LIST_IDENTITY;
    if (sender_context)
    {
        mem.cpy(h.sender_context, sender_context, 8);
    }
    EnipV.build_args.buf = buf;
    EnipV.build_args.cap = cap;
    EnipV.build_args.h = &h;
    EnipV.build_args.data = NULL;
    EnipV.build_args.data_len = 0;
    protocore_enip_build(work); // no command-specific data
}

void protocore_enip_parse_list_identity(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *data = EnipV.parse_list_identity_args.data;
    size_t data_len = EnipV.parse_list_identity_args.data_len;
    EipIdentity *out = EnipV.parse_list_identity_args.out;

    if (!data || !out || data_len < 2) // item count
    {
        EnipV.ok = PROTO_FALSE;
        return;
    }
    uint16_t item_count = get16(data);
    size_t pos = 2;
    for (uint16_t i = 0; i < item_count; i++)
    {
        if (pos + 4 > data_len)
        {
            EnipV.ok = PROTO_FALSE;
            return;
        }
        uint16_t type = get16(data + pos);
        uint16_t ilen = get16(data + pos + 2);
        pos += 4;
        if (pos + ilen > data_len)
        {
            EnipV.ok = PROTO_FALSE;
            return;
        }
        if (type == EIP_CPF_LIST_IDENTITY)
        {
            const uint8_t *it = data + pos;
            if (ilen <
                33) // proto(2) + sockaddr(16) + vendor/type/code(6) + rev(2) + status(2) + serial(4) + namelen(1)
            {
                EnipV.ok = PROTO_FALSE;
                return;
            }
            uint8_t name_len = it[32];
            if ((size_t)ilen < (size_t)34 + name_len) // + the name + the trailing state octet
            {
                EnipV.ok = PROTO_FALSE;
                return;
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
            EnipV.ok = PROTO_TRUE;
            return;
        }
        pos += ilen;
    }
    EnipV.ok = PROTO_FALSE; // no List Identity item
}

void protocore_enip_parse_send_rr_data(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *data = EnipV.parse_send_rr_data_args.data;
    size_t data_len = EnipV.parse_send_rr_data_args.data_len;
    const uint8_t **cip = EnipV.parse_send_rr_data_args.cip;
    size_t *cip_len = EnipV.parse_send_rr_data_args.cip_len;

    if (!data || data_len < 8) // interface handle(4) + timeout(2) + item count(2)
    {
        EnipV.ok = PROTO_FALSE;
        return;
    }
    size_t pos = 6; // skip interface handle + timeout
    uint16_t item_count = get16(data + pos);
    pos += 2;
    for (uint16_t i = 0; i < item_count; i++)
    {
        if (pos + 4 > data_len)
        {
            EnipV.ok = PROTO_FALSE;
            return;
        }
        uint16_t type = get16(data + pos);
        uint16_t ilen = get16(data + pos + 2);
        pos += 4;
        if (pos + ilen > data_len)
        {
            EnipV.ok = PROTO_FALSE;
            return;
        }
        if (type == EIP_CPF_UNCONNECTED_DATA)
        {
            if (cip)
            {
                *cip = data + pos;
            }
            if (cip_len)
            {
                *cip_len = ilen;
            }
            EnipV.ok = PROTO_TRUE;
            return;
        }
        pos += ilen;
    }
    EnipV.ok = PROTO_FALSE; // no unconnected data item
}

/** @brief The operands and the outcome. */
EnipVars EnipV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_ENIP
