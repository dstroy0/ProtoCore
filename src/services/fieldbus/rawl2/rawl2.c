// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rawl2.c
 * @brief Raw Layer-2 Ethernet frame codec (see rawl2.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_RAWL2

#include "mmgr/protomem/protomem.h"
#include "services/fieldbus/rawl2/rawl2.h"
#include "shared/crc/crc.h" // PROTOCORE_CRC32_ISO_HDLC

PROTOCORE_BEGIN_DECLS

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_rawl2_build(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *dst = Rawl2V.build_args.dst;
    const uint8_t *src = Rawl2V.build_args.src;
    uint16_t ethertype = Rawl2V.build_args.ethertype;
    const uint8_t *payload = Rawl2V.build_args.payload;
    size_t payload_len = Rawl2V.build_args.payload_len;
    uint8_t *out = Rawl2V.build_args.out;
    size_t cap = Rawl2V.build_args.cap;

    if (!dst || !src || !out || (payload_len && !payload))
    {
        Rawl2V.n = 0;
        return;
    }
    size_t n = ETH_HDR_LEN + payload_len;
    if (n > cap)
    {
        Rawl2V.n = 0;
        return;
    }
    mem.cpy(out, dst, ETH_ALEN);
    mem.cpy(out + ETH_ALEN, src, ETH_ALEN);
    out[12] = (uint8_t)(ethertype >> 8);
    out[13] = (uint8_t)ethertype;
    if (payload_len)
    {
        mem.cpy(out + ETH_HDR_LEN, payload, payload_len);
    }
    Rawl2V.n = n;
}

void protocore_rawl2_build_vlan(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *dst = Rawl2V.build_vlan_args.dst;
    const uint8_t *src = Rawl2V.build_vlan_args.src;
    uint8_t pcp = Rawl2V.build_vlan_args.pcp;
    proto_bool dei = Rawl2V.build_vlan_args.dei;
    uint16_t vid = Rawl2V.build_vlan_args.vid;
    uint16_t ethertype = Rawl2V.build_vlan_args.ethertype;
    const uint8_t *payload = Rawl2V.build_vlan_args.payload;
    size_t payload_len = Rawl2V.build_vlan_args.payload_len;
    uint8_t *out = Rawl2V.build_vlan_args.out;
    size_t cap = Rawl2V.build_vlan_args.cap;

    if (!dst || !src || !out || (payload_len && !payload))
    {
        Rawl2V.n = 0;
        return;
    }
    size_t n = ETH_VLAN_HDR_LEN + payload_len;
    if (n > cap)
    {
        Rawl2V.n = 0;
        return;
    }
    mem.cpy(out, dst, ETH_ALEN);
    mem.cpy(out + ETH_ALEN, src, ETH_ALEN);
    out[12] = (uint8_t)(ETH_TPID_8021Q >> 8);
    out[13] = (uint8_t)ETH_TPID_8021Q;
    uint16_t tci = (uint16_t)(((pcp & 0x7) << 13) | ((dei ? 1 : 0) << 12) | (vid & 0x0FFF));
    out[14] = (uint8_t)(tci >> 8);
    out[15] = (uint8_t)tci;
    out[16] = (uint8_t)(ethertype >> 8);
    out[17] = (uint8_t)ethertype;
    if (payload_len)
    {
        mem.cpy(out + ETH_VLAN_HDR_LEN, payload, payload_len);
    }
    Rawl2V.n = n;
}

void protocore_rawl2_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *frame = Rawl2V.parse_args.frame;
    size_t len = Rawl2V.parse_args.len;
    EthFrame *out = Rawl2V.parse_args.out;

    if (!frame || !out || len < ETH_HDR_LEN)
    {
        Rawl2V.ok = PROTO_FALSE;
        return;
    }
    out->dst = frame;
    out->src = frame + ETH_ALEN;
    uint16_t et = (uint16_t)((frame[12] << 8) | frame[13]);
    if (et == ETH_TPID_8021Q)
    {
        if (len < ETH_VLAN_HDR_LEN)
        {
            Rawl2V.ok = PROTO_FALSE;
            return;
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
    Rawl2V.ok = PROTO_TRUE;
}

void protocore_rawl2_fcs(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *bytes = Rawl2V.fcs_args.bytes;
    size_t len = Rawl2V.fcs_args.len;

    // CRC-32/ISO-HDLC (the Ethernet FCS): reflected poly 0xEDB88320, init/xorout 0xFFFFFFFF.
    CrcV.args.params = &PROTOCORE_CRC32_ISO_HDLC;
    CrcV.args.data = bytes;
    CrcV.args.len = len;
    Crc.compute(work);
    Rawl2V.u32 = CrcV.value;
}

/** @brief The operands and the outcome. */
Rawl2Vars Rawl2V;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_RAWL2
