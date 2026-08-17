// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file melsec.c
 * @brief Mitsubishi MELSEC MC binary 3E builder + parser (pure, host-tested).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_MELSEC

#include "mmgr/protomem.h"
#include "services/fieldbus/melsec/melsec.h"

#include "mmgr/endian.h"

PROTOCORE_BEGIN_DECLS

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void melsec_build_read(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Melsec.build_read_args.buf;
    size_t cap = Melsec.build_read_args.cap;
    uint8_t device_code = Melsec.build_read_args.device_code;
    uint32_t head_device = Melsec.build_read_args.head_device;
    uint16_t points = Melsec.build_read_args.points;
    uint16_t monitoring_timer = Melsec.build_read_args.monitoring_timer;

    if (!buf || cap < MELSEC_3E_READ_REQ_LEN)
    {
        Melsec.n = 0;
        return;
    }
    size_t p = 0;
    buf[p++] = MELSEC_3E_REQ_SUBHEADER0;
    buf[p++] = MELSEC_3E_REQ_SUBHEADER1;
    buf[p++] = MELSEC_NETWORK_DEFAULT;
    buf[p++] = MELSEC_PROTOCORE_DEFAULT;
    p += endian.wr16le(buf + p, MELSEC_DEST_IO_DEFAULT);
    buf[p++] = MELSEC_DEST_MULTIDROP_DEFAULT;
    // request data length = the octets from the monitoring timer onward:
    // timer(2) + command(2) + subcommand(2) + head device(3) + device code(1) + points(2) = 12
    p += endian.wr16le(buf + p, MELSEC_3E_READ_REQ_DATA_LEN);
    p += endian.wr16le(buf + p, monitoring_timer);
    p += endian.wr16le(buf + p, MELSEC_CMD_BATCH_READ);
    p += endian.wr16le(buf + p, MELSEC_SUBCMD_WORD);
    buf[p++] = (uint8_t)(head_device & 0xFF); // head device number, 3 octets little-endian
    buf[p++] = (uint8_t)((head_device >> 8) & 0xFF);
    buf[p++] = (uint8_t)((head_device >> 16) & 0xFF);
    buf[p++] = device_code;
    p += endian.wr16le(buf + p, points);
    Melsec.n = p; // == MELSEC_3E_READ_REQ_LEN
}

static void melsec_build_write(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Melsec.build_write_args.buf;
    size_t cap = Melsec.build_write_args.cap;
    uint8_t device_code = Melsec.build_write_args.device_code;
    uint32_t head_device = Melsec.build_write_args.head_device;
    uint16_t points = Melsec.build_write_args.points;
    uint16_t monitoring_timer = Melsec.build_write_args.monitoring_timer;
    const uint8_t *data = Melsec.build_write_args.data;
    size_t data_len = Melsec.build_write_args.data_len;

    if (!buf || (data_len && !data))
    {
        Melsec.n = 0;
        return;
    }
    if (data_len > (size_t)(0xFFFFu - MELSEC_3E_READ_REQ_DATA_LEN)) // the request-length field is 16-bit
    {
        Melsec.n = 0;
        return;
    }
    if (cap < MELSEC_3E_READ_REQ_LEN + data_len)
    {
        Melsec.n = 0;
        return;
    }
    size_t p = 0;
    buf[p++] = MELSEC_3E_REQ_SUBHEADER0;
    buf[p++] = MELSEC_3E_REQ_SUBHEADER1;
    buf[p++] = MELSEC_NETWORK_DEFAULT;
    buf[p++] = MELSEC_PROTOCORE_DEFAULT;
    p += endian.wr16le(buf + p, MELSEC_DEST_IO_DEFAULT);
    buf[p++] = MELSEC_DEST_MULTIDROP_DEFAULT;
    // request data length = the fixed 12 (timer..points) plus the write data octets.
    p += endian.wr16le(buf + p, (uint16_t)(MELSEC_3E_READ_REQ_DATA_LEN + data_len));
    p += endian.wr16le(buf + p, monitoring_timer);
    p += endian.wr16le(buf + p, MELSEC_CMD_BATCH_WRITE);
    p += endian.wr16le(buf + p, MELSEC_SUBCMD_WORD);
    buf[p++] = (uint8_t)(head_device & 0xFF); // head device number, 3 octets little-endian
    buf[p++] = (uint8_t)((head_device >> 8) & 0xFF);
    buf[p++] = (uint8_t)((head_device >> 16) & 0xFF);
    buf[p++] = device_code;
    p += endian.wr16le(buf + p, points);
    if (data_len)
    {
        mem.cpy(buf + p, data, data_len);
        p += data_len;
    }
    Melsec.n = p; // == MELSEC_3E_READ_REQ_LEN + data_len
}

static void melsec_parse_response(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = Melsec.parse_response_args.buf;
    size_t len = Melsec.parse_response_args.len;
    MelsecResponse *out = Melsec.parse_response_args.out;

    // subheader(2)+net(1)+pc(1)+io(2)+multidrop(1)+length(2)+endcode(2) = MELSEC_3E_RES_MIN_LEN
    if (!buf || !out || len < MELSEC_3E_RES_MIN_LEN)
    {
        Melsec.ok = PROTO_FALSE;
        return;
    }
    if (buf[0] != MELSEC_3E_RES_SUBHEADER0 || buf[1] != MELSEC_3E_RES_SUBHEADER1)
    {
        Melsec.ok = PROTO_FALSE;
        return;
    }
    uint16_t data_length = endian.rd16le(buf + MELSEC_3E_RES_LEN_OFFSET); // covers the end code + the response data
    if (data_length < MELSEC_ENDCODE_LEN)
    {
        Melsec.ok = PROTO_FALSE;
        return;
    }
    if (MELSEC_3E_RES_DATALEN_BASE + (size_t)data_length > len)
    {
        Melsec.ok = PROTO_FALSE;
        return;
    }
    out->end_code = endian.rd16le(buf + MELSEC_3E_RES_DATALEN_BASE);
    out->data = buf + MELSEC_3E_RES_DATA_OFFSET;
    out->data_len = (size_t)data_length - MELSEC_ENDCODE_LEN; // minus the 2-octet end code
    Melsec.ok = PROTO_TRUE;
}

MelsecNs Melsec = {
    .build_read = melsec_build_read, .build_write = melsec_build_write, .parse_response = melsec_parse_response};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MELSEC
