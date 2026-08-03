// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rawl2.c
 * @brief Raw Layer-2 Ethernet frame codec (see rawl2.h).
 */

#include "services/fieldbus/rawl2/rawl2.h"
#include "shared_primitives/crc.h" // PC_CRC32_ISO_HDLC

#if PC_ENABLE_RAWL2

#include <string.h>

size_t pc_eth_build(const uint8_t *dst, const uint8_t *src, uint16_t ethertype, const uint8_t *payload,
                    size_t payload_len, uint8_t *out, size_t cap)
{
    if (!dst || !src || !out || (payload_len && !payload))
    {
        return 0;
    }
    size_t n = ETH_HDR_LEN + payload_len;
    if (n > cap)
    {
        return 0;
    }
    memcpy(out, dst, ETH_ALEN);
    memcpy(out + ETH_ALEN, src, ETH_ALEN);
    out[12] = (uint8_t)(ethertype >> 8);
    out[13] = (uint8_t)ethertype;
    if (payload_len)
    {
        memcpy(out + ETH_HDR_LEN, payload, payload_len);
    }
    return n;
}

size_t pc_eth_build_vlan(const uint8_t *dst, const uint8_t *src, uint8_t pcp, proto_bool dei, uint16_t vid,
                         uint16_t ethertype, const uint8_t *payload, size_t payload_len, uint8_t *out, size_t cap)
{
    if (!dst || !src || !out || (payload_len && !payload))
    {
        return 0;
    }
    size_t n = ETH_VLAN_HDR_LEN + payload_len;
    if (n > cap)
    {
        return 0;
    }
    memcpy(out, dst, ETH_ALEN);
    memcpy(out + ETH_ALEN, src, ETH_ALEN);
    out[12] = (uint8_t)(ETH_TPID_8021Q >> 8);
    out[13] = (uint8_t)ETH_TPID_8021Q;
    uint16_t tci = (uint16_t)(((pcp & 0x7) << 13) | ((dei ? 1 : 0) << 12) | (vid & 0x0FFF));
    out[14] = (uint8_t)(tci >> 8);
    out[15] = (uint8_t)tci;
    out[16] = (uint8_t)(ethertype >> 8);
    out[17] = (uint8_t)ethertype;
    if (payload_len)
    {
        memcpy(out + ETH_VLAN_HDR_LEN, payload, payload_len);
    }
    return n;
}

proto_bool pc_eth_parse(const uint8_t *frame, size_t len, EthFrame *out)
{
    if (!frame || !out || len < ETH_HDR_LEN)
    {
        return PROTO_FALSE;
    }
    out->dst = frame;
    out->src = frame + ETH_ALEN;
    uint16_t et = (uint16_t)((frame[12] << 8) | frame[13]);
    if (et == ETH_TPID_8021Q)
    {
        if (len < ETH_VLAN_HDR_LEN)
        {
            return PROTO_FALSE;
        }
        uint16_t tci = (uint16_t)((frame[14] << 8) | frame[15]);
        out->vlan = PROTO_TRUE;
        out->pcp = (uint8_t)((tci >> 13) & 0x7);
        out->vid = (uint16_t)(tci & 0x0FFF);
        out->ethertype = (uint16_t)((frame[16] << 8) | frame[17]);
        out->payload = frame + ETH_VLAN_HDR_LEN;
        out->payload_len = len - ETH_VLAN_HDR_LEN;
    }
    else
    {
        out->vlan = PROTO_FALSE;
        out->pcp = 0;
        out->vid = 0;
        out->ethertype = et;
        out->payload = frame + ETH_HDR_LEN;
        out->payload_len = len - ETH_HDR_LEN;
    }
    return PROTO_TRUE;
}

uint32_t pc_eth_fcs(const uint8_t *bytes, size_t len)
{
    // CRC-32/ISO-HDLC (the Ethernet FCS): reflected poly 0xEDB88320, init/xorout 0xFFFFFFFF.
    // test_crc diffs the shared engine against the loop that used to live here over every length
    // 0..64, so this is byte-identical to it.
    return pc_crc(&PC_CRC32_ISO_HDLC, bytes, len);
}

#endif // PC_ENABLE_RAWL2
