// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file profinet.c
 * @brief PROFINET DCP frame codec (see profinet.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_PROFINET

#include "mmgr/protomem.h"
#include "services/fieldbus/profinet/profinet.h"

PROTOCORE_BEGIN_DECLS

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void profinet_dcp_header(uint8_t *restrict work)
{
    (void)work;
    uint16_t frame_id = Profinet.dcp_header_args.frame_id;
    uint8_t service_id = Profinet.dcp_header_args.service_id;
    uint8_t service_type = Profinet.dcp_header_args.service_type;
    uint32_t xid = Profinet.dcp_header_args.xid;
    uint16_t response_delay = Profinet.dcp_header_args.response_delay;
    uint16_t data_length = Profinet.dcp_header_args.data_length;
    uint8_t *out = Profinet.dcp_header_args.out;
    size_t cap = Profinet.dcp_header_args.cap;

    if (!out || cap < PN_DCP_HDR_LEN)
    {
        Profinet.n = 0;
        return;
    }
    out[0] = (uint8_t)(frame_id >> 8);
    out[1] = (uint8_t)frame_id;
    out[2] = service_id;
    out[3] = service_type;
    out[4] = (uint8_t)(xid >> 24);
    out[5] = (uint8_t)(xid >> 16);
    out[6] = (uint8_t)(xid >> 8);
    out[7] = (uint8_t)xid;
    out[8] = (uint8_t)(response_delay >> 8);
    out[9] = (uint8_t)response_delay;
    out[10] = (uint8_t)(data_length >> 8);
    out[11] = (uint8_t)data_length;
    Profinet.n = PN_DCP_HDR_LEN;
}

static void profinet_dcp_block(uint8_t *restrict work)
{
    (void)work;
    uint8_t option = Profinet.dcp_block_args.option;
    uint8_t suboption = Profinet.dcp_block_args.suboption;
    const uint8_t *value = Profinet.dcp_block_args.value;
    size_t value_len = Profinet.dcp_block_args.value_len;
    uint8_t *out = Profinet.dcp_block_args.out;
    size_t cap = Profinet.dcp_block_args.cap;

    if (!out || (value_len && !value) || value_len > 0xFFFF)
    {
        Profinet.n = 0;
        return;
    }
    proto_bool pad = (value_len & 1) != 0; // pad to an even total length
    size_t n = 4 + value_len + (pad ? 1 : 0);
    if (n > cap)
    {
        Profinet.n = 0;
        return;
    }
    out[0] = option;
    out[1] = suboption;
    out[2] = (uint8_t)(value_len >> 8);
    out[3] = (uint8_t)value_len;
    if (value_len)
    {
        mem.cpy(out + 4, value, value_len);
    }
    if (pad)
    {
        out[4 + value_len] = 0x00;
    }
    Profinet.n = n;
}

static void profinet_dcp_parse_header(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *frame = Profinet.dcp_parse_header_args.frame;
    size_t len = Profinet.dcp_parse_header_args.len;
    PnDcpHeader *out = Profinet.dcp_parse_header_args.out;

    if (!frame || !out || len < PN_DCP_HDR_LEN)
    {
        Profinet.ok = PROTO_FALSE;
        return;
    }
    out->frame_id = (uint16_t)((frame[0] << 8) | frame[1]);
    out->service_id = frame[2];
    out->service_type = frame[3];
    out->xid = ((uint32_t)frame[4] << 24) | ((uint32_t)frame[5] << 16) | ((uint32_t)frame[6] << 8) | frame[7];
    out->response_delay = (uint16_t)((frame[8] << 8) | frame[9]);
    out->data_length = (uint16_t)((frame[10] << 8) | frame[11]);
    Profinet.ok = PROTO_TRUE;
}

static void profinet_dcp_walk(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *blocks = Profinet.dcp_walk_args.blocks;
    size_t len = Profinet.dcp_walk_args.len;
    protocore_pn_dcp_block_cb cb = Profinet.dcp_walk_args.cb;
    void *arg = Profinet.dcp_walk_args.arg;

    size_t off = 0;
    while (off + 4 <= len)
    {
        uint8_t option = blocks[off];
        uint8_t suboption = blocks[off + 1];
        uint16_t blen = (uint16_t)((blocks[off + 2] << 8) | blocks[off + 3]);
        if (off + 4 + blen > len)
        {
            Profinet.ok = PROTO_FALSE;
            return;
        }
        if (cb)
        {
            cb(option, suboption, blen ? (blocks + off + 4) : NULL, blen, arg);
        }
        size_t adv = 4 + blen + ((blen & 1) ? 1 : 0); // skip the even-pad filler
        off += adv;
    }
    Profinet.ok = PROTO_TRUE;
}

ProfinetNs Profinet = {.dcp_header = profinet_dcp_header,
                       .dcp_block = profinet_dcp_block,
                       .dcp_parse_header = profinet_dcp_parse_header,
                       .dcp_walk = profinet_dcp_walk};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_PROFINET
