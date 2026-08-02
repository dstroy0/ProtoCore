// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dds.c
 * @brief DDS / RTPS wire-protocol framing codec (see dds.h).
 */

#include "services/iot/dds/dds.h"

#if PC_ENABLE_DDS

#include <string.h>

const uint8_t RTPS_VERSION[2] = {2, 4};

size_t pc_rtps_header(const uint8_t *guid_prefix, const uint8_t *vendor_id, uint8_t *out, size_t cap)
{
    if (!guid_prefix || !vendor_id || !out || cap < Rtps::RTPS_HEADER_LEN)
    {
        return 0;
    }
    out[0] = 'R';
    out[1] = 'T';
    out[2] = 'P';
    out[3] = 'S';
    out[4] = RTPS_VERSION[0];
    out[5] = RTPS_VERSION[1];
    out[6] = vendor_id[0];
    out[7] = vendor_id[1];
    memcpy(out + 8, guid_prefix, Rtps::RTPS_GUIDPREFIX_LEN);
    return Rtps::RTPS_HEADER_LEN;
}

size_t pc_rtps_submessage(uint8_t id, uint8_t flags, const uint8_t *body, uint16_t body_len, uint8_t *out, size_t cap)
{
    if (!out || (body_len && !body))
    {
        return 0;
    }
    size_t n = 4 + (size_t)body_len;
    if (n > cap)
    {
        return 0;
    }
    out[0] = id;
    out[1] = flags;
    // octetsToNextHeader is written in the submessage's own byte order (the E flag).
    if (flags & Rtps::RTPS_FLAG_ENDIAN)
    {
        out[2] = (uint8_t)body_len;
        out[3] = (uint8_t)(body_len >> 8);
    }
    else
    {
        out[2] = (uint8_t)(body_len >> 8);
        out[3] = (uint8_t)body_len;
    }
    if (body_len)
    {
        memcpy(out + 4, body, body_len);
    }
    return n;
}

proto_bool pc_rtps_parse(const uint8_t *msg, size_t len, pc_rtps_cb cb, void *arg)
{
    if (!msg || len < Rtps::RTPS_HEADER_LEN)
    {
        return PROTO_FALSE;
    }
    if (msg[0] != 'R' || msg[1] != 'T' || msg[2] != 'P' || msg[3] != 'S')
    {
        return PROTO_FALSE;
    }
    // Accept any peer whose protocol version is <= ours (RTPS is backward compatible).
    if (msg[4] != RTPS_VERSION[0] || msg[5] > RTPS_VERSION[1])
    {
        return PROTO_FALSE;
    }

    size_t off = Rtps::RTPS_HEADER_LEN;
    while (off + 4 <= len)
    {
        uint8_t id = msg[off];
        uint8_t flags = msg[off + 1];
        uint16_t oth = (flags & Rtps::RTPS_FLAG_ENDIAN) ? (uint16_t)(msg[off + 2] | (msg[off + 3] << 8))
                                                        : (uint16_t)((msg[off + 2] << 8) | msg[off + 3]);
        size_t body = oth ? oth : (len - (off + 4)); // 0 = extends to end of message
        if (off + 4 + body > len)
        {
            return PROTO_FALSE;
        }
        if (cb)
        {
            cb(id, flags, body ? (msg + off + 4) : NULL, body, arg);
        }
        off += 4 + body;
        if (oth == 0)
        {
            break; // a 0-length terminates the message
        }
    }
    return PROTO_TRUE;
}

#endif // PC_ENABLE_DDS
