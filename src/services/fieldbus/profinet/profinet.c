// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file profinet.c
 * @brief PROFINET DCP frame codec (see profinet.h).
 */

#include "services/fieldbus/profinet/profinet.h"

#if PC_ENABLE_PROFINET

#include <string.h>

size_t pc_pn_dcp_header(uint16_t frame_id, uint8_t service_id, uint8_t service_type, uint32_t xid, uint16_t data_length,
                        uint8_t *out, size_t cap)
{
    if (!out || cap < PN_DCP_HDR_LEN)
    {
        return 0;
    }
    out[0] = (uint8_t)(frame_id >> 8);
    out[1] = (uint8_t)frame_id;
    out[2] = service_id;
    out[3] = service_type;
    out[4] = (uint8_t)(xid >> 24);
    out[5] = (uint8_t)(xid >> 16);
    out[6] = (uint8_t)(xid >> 8);
    out[7] = (uint8_t)xid;
    // octets 8..9 carry dataLength (the responseDelay of an Identify request shares this field slot).
    out[8] = (uint8_t)(data_length >> 8);
    out[9] = (uint8_t)data_length;
    return PN_DCP_HDR_LEN;
}

size_t pc_pn_dcp_block(uint8_t option, uint8_t suboption, const uint8_t *value, size_t value_len, uint8_t *out,
                       size_t cap)
{
    if (!out || (value_len && !value) || value_len > 0xFFFF)
    {
        return 0;
    }
    proto_bool pad = (value_len & 1) != 0; // pad to an even total length
    size_t n = 4 + value_len + (pad ? 1 : 0);
    if (n > cap)
    {
        return 0;
    }
    out[0] = option;
    out[1] = suboption;
    out[2] = (uint8_t)(value_len >> 8);
    out[3] = (uint8_t)value_len;
    if (value_len)
    {
        memcpy(out + 4, value, value_len);
    }
    if (pad)
    {
        out[4 + value_len] = 0x00;
    }
    return n;
}

proto_bool pc_pn_dcp_parse_header(const uint8_t *frame, size_t len, PnDcpHeader *out)
{
    if (!frame || !out || len < PN_DCP_HDR_LEN)
    {
        return PROTO_FALSE;
    }
    out->frame_id = (uint16_t)((frame[0] << 8) | frame[1]);
    out->service_id = frame[2];
    out->service_type = frame[3];
    out->xid = ((uint32_t)frame[4] << 24) | ((uint32_t)frame[5] << 16) | ((uint32_t)frame[6] << 8) | frame[7];
    out->data_length = (uint16_t)((frame[8] << 8) | frame[9]);
    return PROTO_TRUE;
}

proto_bool pc_pn_dcp_walk(const uint8_t *blocks, size_t len, pc_pn_dcp_block_cb cb, void *arg)
{
    size_t off = 0;
    while (off + 4 <= len)
    {
        uint8_t option = blocks[off];
        uint8_t suboption = blocks[off + 1];
        uint16_t blen = (uint16_t)((blocks[off + 2] << 8) | blocks[off + 3]);
        if (off + 4 + blen > len)
        {
            return PROTO_FALSE;
        }
        if (cb)
        {
            cb(option, suboption, blen ? (blocks + off + 4) : NULL, blen, arg);
        }
        size_t adv = 4 + blen + ((blen & 1) ? 1 : 0); // skip the even-pad filler
        off += adv;
    }
    return PROTO_TRUE;
}

#endif // PC_ENABLE_PROFINET
