// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file directnet.c
 * @brief AutomationDirect / Koyo DirectNET serial frame codec (see directnet.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_DIRECTNET

#include "mmgr/protomem.h"
#include "services/fieldbus/directnet/directnet.h"

PROTOCORE_BEGIN_DECLS

static char hex_digit(uint8_t nibble)
{
    nibble &= 0x0F;
    return (char)(nibble < 10 ? ('0' + nibble) : ('A' + nibble - 10));
}
// Write @p value as @p digits ASCII-hex chars (big-endian) at p.
static void put_hex(uint8_t *p, uint32_t value, int digits)
{
    for (int i = 0; i < digits; i++)
    {
        p[i] = (uint8_t)hex_digit((uint8_t)(value >> (4 * (digits - 1 - i))));
    }
}

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void directnet_lrc(uint8_t *restrict work);

static void directnet_lrc(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *bytes = Directnet.lrc_args.bytes;
    size_t len = Directnet.lrc_args.len;

    uint8_t lrc = 0;
    for (size_t i = 0; i < len; i++)
    {
        lrc ^= bytes[i];
    }
    Directnet.value = lrc;
}

static void directnet_header(uint8_t *restrict work)
{
    uint8_t slave = Directnet.header_args.slave;
    uint8_t type = Directnet.header_args.type;
    uint16_t address = Directnet.header_args.address;
    uint8_t blocks = Directnet.header_args.blocks;
    uint8_t *out = Directnet.header_args.out;
    size_t cap = Directnet.header_args.cap;

    // SOH + slave(2) + type(1) + addr(4) + blocks(2) + ETB + LRC = 11 bytes.
    const size_t n = 1 + 2 + 1 + 4 + 2 + 1 + 1;
    if (!out || cap < n)
    {
        Directnet.n = 0;
        return;
    }
    size_t i = 0;
    out[i++] = DNET_SOH;
    put_hex(out + i, slave, 2);
    i += 2;
    out[i++] = type;
    put_hex(out + i, address, 4);
    i += 4;
    put_hex(out + i, blocks, 2);
    i += 2;
    out[i++] = DNET_ETB;
    // LRC over the framed body (slave..ETB), i.e. everything after SOH up to and including ETB.
    Directnet.lrc_args.bytes = out + 1;
    Directnet.lrc_args.len = i - 1;
    directnet_lrc(work);
    out[i] = Directnet.value;
    i++;
    Directnet.n = i;
}

static void directnet_data(uint8_t *restrict work)
{
    const uint8_t *data = Directnet.data_args.data;
    size_t data_len = Directnet.data_args.data_len;
    uint8_t *out = Directnet.data_args.out;
    size_t cap = Directnet.data_args.cap;

    if (!out || (data_len && !data))
    {
        Directnet.n = 0;
        return;
    }
    size_t n = 1 + data_len + 1 + 1; // STX + data + ETX + LRC
    if (n > cap)
    {
        Directnet.n = 0;
        return;
    }
    size_t i = 0;
    out[i++] = DNET_STX;
    if (data_len)
    {
        mem.cpy(out + i, data, data_len);
        i += data_len;
    }
    out[i++] = DNET_ETX;
    // LRC over data..ETX (everything after STX up to and including ETX).
    Directnet.lrc_args.bytes = out + 1;
    Directnet.lrc_args.len = i - 1;
    directnet_lrc(work);
    out[i] = Directnet.value;
    i++;
    Directnet.n = i;
}

static void directnet_data_parse(uint8_t *restrict work)
{
    const uint8_t *frame = Directnet.data_parse_args.frame;
    size_t len = Directnet.data_parse_args.len;
    const uint8_t **data = Directnet.data_parse_args.data;
    size_t *data_len = Directnet.data_parse_args.data_len;

    if (!frame || len < 3) // STX + ETX + LRC minimum
    {
        Directnet.ok = PROTO_FALSE;
        return;
    }
    if (frame[0] != DNET_STX)
    {
        Directnet.ok = PROTO_FALSE;
        return;
    }
    // The byte before the LRC must be ETX.
    size_t etx_idx = len - 2;
    if (frame[etx_idx] != DNET_ETX)
    {
        Directnet.ok = PROTO_FALSE;
        return;
    }
    Directnet.lrc_args.bytes = frame + 1;
    Directnet.lrc_args.len = len - 2;
    directnet_lrc(work);
    if (Directnet.value != frame[len - 1]) // over data..ETX
    {
        Directnet.ok = PROTO_FALSE;
        return;
    }
    if (data)
    {
        *data = (etx_idx > 1) ? (frame + 1) : NULL;
    }
    if (data_len)
    {
        *data_len = etx_idx - 1;
    }
    Directnet.ok = PROTO_TRUE;
}

DirectnetNs Directnet = {
    .lrc = directnet_lrc, .header = directnet_header, .data = directnet_data, .data_parse = directnet_data_parse};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DIRECTNET
