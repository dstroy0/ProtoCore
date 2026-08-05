// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file melsec.c
 * @brief Mitsubishi MELSEC MC binary 3E builder + parser (pure, host-tested).
 */

#include "services/fieldbus/melsec/melsec.h"

#if PC_ENABLE_MELSEC

#include "mmgr/endian.h"

size_t pc_melsec_build_read(uint8_t *buf, size_t cap, uint8_t device_code, uint32_t head_device, uint16_t points,
                            uint16_t monitoring_timer)
{
    if (!buf || cap < MELSEC_3E_READ_REQ_LEN)
    {
        return 0;
    }
    size_t p = 0;
    buf[p++] = MELSEC_3E_REQ_SUBHEADER0;
    buf[p++] = MELSEC_3E_REQ_SUBHEADER1;
    buf[p++] = MELSEC_NETWORK_DEFAULT;
    buf[p++] = MELSEC_PC_DEFAULT;
    p += pc_wr16le(buf + p, MELSEC_DEST_IO_DEFAULT);
    buf[p++] = MELSEC_DEST_MULTIDROP_DEFAULT;
    // request data length = the octets from the monitoring timer onward:
    // timer(2) + command(2) + subcommand(2) + head device(3) + device code(1) + points(2) = 12
    p += pc_wr16le(buf + p, MELSEC_3E_READ_REQ_DATA_LEN);
    p += pc_wr16le(buf + p, monitoring_timer);
    p += pc_wr16le(buf + p, MELSEC_CMD_BATCH_READ);
    p += pc_wr16le(buf + p, MELSEC_SUBCMD_WORD);
    buf[p++] = (uint8_t)(head_device & 0xFF); // head device number, 3 octets little-endian
    buf[p++] = (uint8_t)((head_device >> 8) & 0xFF);
    buf[p++] = (uint8_t)((head_device >> 16) & 0xFF);
    buf[p++] = device_code;
    p += pc_wr16le(buf + p, points);
    return p; // == MELSEC_3E_READ_REQ_LEN
}

size_t pc_melsec_build_write(uint8_t *buf, size_t cap, uint8_t device_code, uint32_t head_device, uint16_t points,
                             uint16_t monitoring_timer, const uint8_t *data, size_t data_len)
{
    if (!buf || (data_len && !data))
    {
        return 0;
    }
    if (data_len > (size_t)(0xFFFFu - MELSEC_3E_READ_REQ_DATA_LEN)) // the request-length field is 16-bit
    {
        return 0;
    }
    if (cap < MELSEC_3E_READ_REQ_LEN + data_len)
    {
        return 0;
    }
    size_t p = 0;
    buf[p++] = MELSEC_3E_REQ_SUBHEADER0;
    buf[p++] = MELSEC_3E_REQ_SUBHEADER1;
    buf[p++] = MELSEC_NETWORK_DEFAULT;
    buf[p++] = MELSEC_PC_DEFAULT;
    p += pc_wr16le(buf + p, MELSEC_DEST_IO_DEFAULT);
    buf[p++] = MELSEC_DEST_MULTIDROP_DEFAULT;
    // request data length = the fixed 12 (timer..points) plus the write data octets.
    p += pc_wr16le(buf + p, (uint16_t)(MELSEC_3E_READ_REQ_DATA_LEN + data_len));
    p += pc_wr16le(buf + p, monitoring_timer);
    p += pc_wr16le(buf + p, MELSEC_CMD_BATCH_WRITE);
    p += pc_wr16le(buf + p, MELSEC_SUBCMD_WORD);
    buf[p++] = (uint8_t)(head_device & 0xFF); // head device number, 3 octets little-endian
    buf[p++] = (uint8_t)((head_device >> 8) & 0xFF);
    buf[p++] = (uint8_t)((head_device >> 16) & 0xFF);
    buf[p++] = device_code;
    p += pc_wr16le(buf + p, points);
    if (data_len)
    {
        memcpy(buf + p, data, data_len);
        p += data_len;
    }
    return p; // == MELSEC_3E_READ_REQ_LEN + data_len
}

proto_bool pc_melsec_parse_response(const uint8_t *buf, size_t len, MelsecResponse *out)
{
    // subheader(2)+net(1)+pc(1)+io(2)+multidrop(1)+length(2)+endcode(2) = MELSEC_3E_RES_MIN_LEN
    if (!buf || !out || len < MELSEC_3E_RES_MIN_LEN)
    {
        return PROTO_FALSE;
    }
    if (buf[0] != MELSEC_3E_RES_SUBHEADER0 || buf[1] != MELSEC_3E_RES_SUBHEADER1)
    {
        return PROTO_FALSE;
    }
    uint16_t data_length = pc_rd16le(buf + MELSEC_3E_RES_LEN_OFFSET); // covers the end code + the response data
    if (data_length < MELSEC_ENDCODE_LEN)
    {
        return PROTO_FALSE;
    }
    if (MELSEC_3E_RES_DATALEN_BASE + (size_t)data_length > len)
    {
        return PROTO_FALSE;
    }
    out->end_code = pc_rd16le(buf + MELSEC_3E_RES_DATALEN_BASE);
    out->data = buf + MELSEC_3E_RES_DATA_OFFSET;
    out->data_len = (size_t)data_length - MELSEC_ENDCODE_LEN; // minus the 2-octet end code
    return PROTO_TRUE;
}

#endif // PC_ENABLE_MELSEC
