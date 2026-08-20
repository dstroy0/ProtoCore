// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dmx.c
 * @brief DMX512 + RDM (ANSI E1.20) codec (pure, host-tested).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_DMX

#include "mmgr/protomem/protomem.h"
#include "server/peripherals/dmx/dmx.h"

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_dmx_rdm_checksum(uint8_t *restrict work);

void protocore_dmx_build(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = DmxV.build_args.buf;
    size_t cap = DmxV.build_args.cap;
    uint8_t start_code = DmxV.build_args.start_code;
    const uint8_t *channels = DmxV.build_args.channels;
    uint16_t n = DmxV.build_args.n;

    if (!buf || n > DMX_MAX_CHANNELS || (n && !channels))
    {
        DmxV.n = 0;
        return;
    }
    size_t total = (size_t)1 + n;
    if (cap < total)
    {
        DmxV.n = 0;
        return;
    }
    buf[0] = start_code;
    if (n)
    {
        mem.cpy(buf + 1, channels, n);
    }
    DmxV.n = total;
}

void protocore_dmx_get_channel(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = DmxV.get_channel_args.buf;
    size_t len = DmxV.get_channel_args.len;
    uint16_t ch = DmxV.get_channel_args.ch;

    if (!buf || ch < 1 || ch > DMX_MAX_CHANNELS || (size_t)ch >= len)
    {
        DmxV.u8 = 0;
        return; // slot ch lives at buf[ch] (buf[0] is the start code)
    }
    DmxV.u8 = buf[ch];
}

void protocore_dmx_rdm_uid(uint8_t *restrict work)
{
    (void)work;
    uint16_t manufacturer = DmxV.rdm_uid_args.manufacturer;
    uint32_t device = DmxV.rdm_uid_args.device;

    DmxV.uid = ((uint64_t)manufacturer << 32) | device;
}

void protocore_dmx_rdm_checksum(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = DmxV.rdm_checksum_args.buf;
    size_t len = DmxV.rdm_checksum_args.len;

    uint16_t s = 0;
    for (size_t i = 0; i < len; i++)
    {
        s = (uint16_t)(s + buf[i]);
    }
    DmxV.checksum = s;
}

// Write a 48-bit UID big-endian (manufacturer high).
static void put_uid(uint8_t *p, uint64_t uid)
{
    p[0] = (uint8_t)(uid >> 40);
    p[1] = (uint8_t)(uid >> 32);
    p[2] = (uint8_t)(uid >> 24);
    p[3] = (uint8_t)(uid >> 16);
    p[4] = (uint8_t)(uid >> 8);
    p[5] = (uint8_t)uid;
}

static uint64_t get_uid(const uint8_t *p)
{
    return ((uint64_t)p[0] << 40) | ((uint64_t)p[1] << 32) | ((uint64_t)p[2] << 24) | ((uint64_t)p[3] << 16) |
           ((uint64_t)p[4] << 8) | (uint64_t)p[5];
}

void protocore_dmx_rdm_build(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = DmxV.rdm_build_args.buf;
    size_t cap = DmxV.rdm_build_args.cap;
    const RdmPacket *p = DmxV.rdm_build_args.p;
    const uint8_t *pdata = DmxV.rdm_build_args.pdata;
    uint8_t pdl = DmxV.rdm_build_args.pdl;

    if (!buf || !p || (pdl && !pdata))
    {
        DmxV.n = 0;
        return;
    }
    uint8_t ml = (uint8_t)(24 + pdl); // message length: SC..end of parameter data (excludes checksum)
    size_t total = (size_t)ml + 2;
    if (cap < total)
    {
        DmxV.n = 0;
        return;
    }
    buf[0] = RDM_SC;
    buf[1] = RDM_SUB_SC;
    buf[2] = ml;
    put_uid(buf + 3, p->dest_uid);
    put_uid(buf + 9, p->src_uid);
    buf[15] = p->tn;
    buf[16] = p->port_id;
    buf[17] = p->msg_count;
    buf[18] = (uint8_t)(p->sub_device >> 8); // sub-device, big-endian
    buf[19] = (uint8_t)p->sub_device;
    buf[20] = p->cc;
    buf[21] = (uint8_t)(p->pid >> 8); // PID, big-endian
    buf[22] = (uint8_t)p->pid;
    buf[23] = pdl;
    if (pdl)
    {
        mem.cpy(buf + 24, pdata, pdl);
    }
    DmxV.rdm_checksum_args.buf = buf;
    DmxV.rdm_checksum_args.len = ml;
    protocore_dmx_rdm_checksum(work);
    uint16_t cs = DmxV.checksum; // checksum over SC..end of parameter data
    buf[ml] = (uint8_t)(cs >> 8);
    buf[ml + 1] = (uint8_t)cs;
    DmxV.n = total;
}

void protocore_dmx_rdm_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = DmxV.rdm_parse_args.buf;
    size_t len = DmxV.rdm_parse_args.len;
    RdmPacket *out = DmxV.rdm_parse_args.out;
    size_t *consumed = DmxV.rdm_parse_args.consumed;

    if (!buf || !out || len < RDM_OVERHEAD)
    {
        DmxV.ok = PROTO_FALSE;
        return;
    }
    if (buf[0] != RDM_SC || buf[1] != RDM_SUB_SC)
    {
        DmxV.ok = PROTO_FALSE;
        return;
    }
    uint8_t ml = buf[2];
    if (ml < 24)
    {
        DmxV.ok = PROTO_FALSE;
        return;
    }
    uint8_t pdl = buf[23];
    if (ml != (uint8_t)(24 + pdl))
    {
        DmxV.ok = PROTO_FALSE;
        return; // message length must match the declared PDL
    }
    size_t total = (size_t)ml + 2;
    if (len < total)
    {
        DmxV.ok = PROTO_FALSE;
        return;
    }
    uint16_t cs = (uint16_t)((buf[ml] << 8) | buf[ml + 1]);
    DmxV.rdm_checksum_args.buf = buf;
    DmxV.rdm_checksum_args.len = ml;
    protocore_dmx_rdm_checksum(work);
    if (cs != DmxV.checksum)
    {
        DmxV.ok = PROTO_FALSE;
        return;
    }

    out->dest_uid = get_uid(buf + 3);
    out->src_uid = get_uid(buf + 9);
    out->tn = buf[15];
    out->port_id = buf[16];
    out->msg_count = buf[17];
    out->sub_device = (uint16_t)((buf[18] << 8) | buf[19]);
    out->cc = buf[20];
    out->pid = (uint16_t)((buf[21] << 8) | buf[22]);
    out->pdl = pdl;
    out->pdata = pdl ? buf + 24 : NULL;
    if (consumed)
    {
        *consumed = total;
    }
    DmxV.ok = PROTO_TRUE;
}

void protocore_dmx_rdm_decode_disc_response(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = DmxV.rdm_decode_disc_response_args.buf;
    size_t len = DmxV.rdm_decode_disc_response_args.len;
    uint64_t *uid = DmxV.rdm_decode_disc_response_args.uid;

    if (!buf || !uid)
    {
        DmxV.ok = PROTO_FALSE;
        return;
    }
    // Skip the optional preamble (up to 7 octets of 0xFE), then require the 0xAA separator.
    size_t p = 0;
    while (p < len && p < 7 && buf[p] == 0xFE)
    {
        p++;
    }
    if (p >= len || buf[p] != 0xAA)
    {
        DmxV.ok = PROTO_FALSE;
        return;
    }
    p++;
    if (len - p < 16) // 12 encoded UID octets + 4 encoded checksum octets
    {
        DmxV.ok = PROTO_FALSE;
        return;
    }
    const uint8_t *euid = buf + p;
    // The checksum is the 16-bit additive sum of the 12 encoded UID octets.
    uint16_t sum = 0;
    for (int i = 0; i < 12; i++)
    {
        sum = (uint16_t)(sum + euid[i]);
    }
    uint8_t csum_hi = (uint8_t)(euid[12] & euid[13]); // AND the two encoded copies to recover the octet
    uint8_t csum_lo = (uint8_t)(euid[14] & euid[15]);
    if ((uint16_t)(((uint16_t)csum_hi << 8) | csum_lo) != sum)
    {
        DmxV.ok = PROTO_FALSE;
        return;
    }
    // Recover the 6 UID octets (MSB first); each is the AND of its 0xAA / 0x55 encoded copies.
    uint64_t u = 0;
    for (int i = 0; i < 6; i++)
    {
        u = (u << 8) | (uint8_t)(euid[i * 2] & euid[i * 2 + 1]);
    }
    *uid = u;
    DmxV.ok = PROTO_TRUE;
}

void protocore_dmx_rdm_build_disc_response(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = DmxV.rdm_build_disc_response_args.buf;
    size_t cap = DmxV.rdm_build_disc_response_args.cap;
    uint64_t uid = DmxV.rdm_build_disc_response_args.uid;
    uint8_t preamble_len = DmxV.rdm_build_disc_response_args.preamble_len;

    if (!buf || preamble_len > 7) // E1.20 allows 0..7 preamble octets
    {
        DmxV.n = 0;
        return;
    }
    size_t total = (size_t)preamble_len + 1 + 16; // preamble + 0xAA separator + 12 UID + 4 checksum octets
    if (cap < total)
    {
        DmxV.n = 0;
        return;
    }
    size_t p = 0;
    for (uint8_t i = 0; i < preamble_len; i++)
    {
        buf[p++] = 0xFE;
    }
    buf[p++] = 0xAA;
    // Encode the 6 UID octets (MSB first): each byte b -> (b | 0xAA), (b | 0x55); sum the encoded octets.
    uint16_t sum = 0;
    for (int i = 0; i < 6; i++)
    {
        uint8_t b = (uint8_t)(uid >> (8 * (5 - i)));
        uint8_t e0 = (uint8_t)(b | 0xAA);
        uint8_t e1 = (uint8_t)(b | 0x55);
        buf[p++] = e0;
        buf[p++] = e1;
        sum = (uint16_t)(sum + e0 + e1);
    }
    uint8_t csum_hi = (uint8_t)(sum >> 8);
    uint8_t csum_lo = (uint8_t)(sum & 0xFF);
    buf[p++] = (uint8_t)(csum_hi | 0xAA);
    buf[p++] = (uint8_t)(csum_hi | 0x55);
    buf[p++] = (uint8_t)(csum_lo | 0xAA);
    buf[p++] = (uint8_t)(csum_lo | 0x55);
    DmxV.n = p;
}

void protocore_dmx_rdm_build_device_info(uint8_t *restrict work)
{
    (void)work;
    uint8_t *pdata = DmxV.rdm_build_device_info_args.pdata;
    size_t cap = DmxV.rdm_build_device_info_args.cap;
    const RdmDeviceInfo *info = DmxV.rdm_build_device_info_args.info;

    if (!pdata || !info || cap < PROTOCORE_RDM_DEVICE_INFO_PDL)
    {
        DmxV.n = 0;
        return;
    }
    pdata[0] = info->proto_major;
    pdata[1] = info->proto_minor;
    pdata[2] = (uint8_t)(info->device_model_id >> 8); // all multi-octet fields big-endian
    pdata[3] = (uint8_t)info->device_model_id;
    pdata[4] = (uint8_t)(info->product_category >> 8);
    pdata[5] = (uint8_t)info->product_category;
    pdata[6] = (uint8_t)(info->software_version_id >> 24);
    pdata[7] = (uint8_t)(info->software_version_id >> 16);
    pdata[8] = (uint8_t)(info->software_version_id >> 8);
    pdata[9] = (uint8_t)info->software_version_id;
    pdata[10] = (uint8_t)(info->dmx_footprint >> 8);
    pdata[11] = (uint8_t)info->dmx_footprint;
    pdata[12] = info->current_personality;
    pdata[13] = info->personality_count;
    pdata[14] = (uint8_t)(info->dmx_start_address >> 8);
    pdata[15] = (uint8_t)info->dmx_start_address;
    pdata[16] = (uint8_t)(info->sub_device_count >> 8);
    pdata[17] = (uint8_t)info->sub_device_count;
    pdata[18] = info->sensor_count;
    DmxV.n = PROTOCORE_RDM_DEVICE_INFO_PDL;
}

void protocore_dmx_rdm_parse_device_info(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *pdata = DmxV.rdm_parse_device_info_args.pdata;
    uint8_t pdl = DmxV.rdm_parse_device_info_args.pdl;
    RdmDeviceInfo *out = DmxV.rdm_parse_device_info_args.out;

    if (!pdata || !out || pdl < PROTOCORE_RDM_DEVICE_INFO_PDL)
    {
        DmxV.ok = PROTO_FALSE;
        return;
    }
    out->proto_major = pdata[0];
    out->proto_minor = pdata[1];
    out->device_model_id = (uint16_t)((pdata[2] << 8) | pdata[3]);
    out->product_category = (uint16_t)((pdata[4] << 8) | pdata[5]);
    out->software_version_id =
        ((uint32_t)pdata[6] << 24) | ((uint32_t)pdata[7] << 16) | ((uint32_t)pdata[8] << 8) | (uint32_t)pdata[9];
    out->dmx_footprint = (uint16_t)((pdata[10] << 8) | pdata[11]);
    out->current_personality = pdata[12];
    out->personality_count = pdata[13];
    out->dmx_start_address = (uint16_t)((pdata[14] << 8) | pdata[15]);
    out->sub_device_count = (uint16_t)((pdata[16] << 8) | pdata[17]);
    out->sensor_count = pdata[18];
    DmxV.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
DmxVars DmxV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DMX
