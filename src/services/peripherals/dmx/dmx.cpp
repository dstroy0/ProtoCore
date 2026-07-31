// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dmx.cpp
 * @brief DMX512 + RDM (ANSI E1.20) codec (pure, host-tested).
 */

#include "services/peripherals/dmx/dmx.h"

#if PC_ENABLE_DMX

#include <string.h>

size_t pc_dmx_build(uint8_t *buf, size_t cap, uint8_t start_code, const uint8_t *channels, uint16_t n)
{
    if (!buf || n > DMX_MAX_CHANNELS || (n && !channels))
    {
        return 0;
    }
    size_t total = (size_t)1 + n;
    if (cap < total)
    {
        return 0;
    }
    buf[0] = start_code;
    if (n)
    {
        memcpy(buf + 1, channels, n);
    }
    return total;
}

uint8_t pc_dmx_get_channel(const uint8_t *buf, size_t len, uint16_t ch)
{
    if (!buf || ch < 1 || ch > DMX_MAX_CHANNELS || (size_t)ch >= len)
    {
        return 0; // slot ch lives at buf[ch] (buf[0] is the start code)
    }
    return buf[ch];
}

uint64_t pc_rdm_uid(uint16_t manufacturer, uint32_t device)
{
    return ((uint64_t)manufacturer << 32) | device;
}

uint16_t pc_rdm_checksum(const uint8_t *buf, size_t len)
{
    uint16_t s = 0;
    for (size_t i = 0; i < len; i++)
    {
        s = (uint16_t)(s + buf[i]);
    }
    return s;
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

size_t pc_rdm_build(uint8_t *buf, size_t cap, const RdmPacket *p, const uint8_t *pdata, uint8_t pdl)
{
    if (!buf || !p || (pdl && !pdata))
    {
        return 0;
    }
    uint8_t ml = (uint8_t)(24 + pdl); // message length: SC..end of parameter data (excludes checksum)
    size_t total = (size_t)ml + 2;
    if (cap < total)
    {
        return 0;
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
        memcpy(buf + 24, pdata, pdl);
    }
    uint16_t cs = pc_rdm_checksum(buf, ml); // checksum over SC..end of parameter data
    buf[ml] = (uint8_t)(cs >> 8);
    buf[ml + 1] = (uint8_t)cs;
    return total;
}

bool pc_rdm_parse(const uint8_t *buf, size_t len, RdmPacket *out, size_t *consumed)
{
    if (!buf || !out || len < RDM_OVERHEAD)
    {
        return false;
    }
    if (buf[0] != RDM_SC || buf[1] != RDM_SUB_SC)
    {
        return false;
    }
    uint8_t ml = buf[2];
    if (ml < 24)
    {
        return false;
    }
    uint8_t pdl = buf[23];
    if (ml != (uint8_t)(24 + pdl))
    {
        return false; // message length must match the declared PDL
    }
    size_t total = (size_t)ml + 2;
    if (len < total)
    {
        return false;
    }
    uint16_t cs = (uint16_t)((buf[ml] << 8) | buf[ml + 1]);
    if (cs != pc_rdm_checksum(buf, ml))
    {
        return false;
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
    out->pdata = pdl ? buf + 24 : nullptr;
    if (consumed)
    {
        *consumed = total;
    }
    return true;
}

bool pc_rdm_decode_disc_response(const uint8_t *buf, size_t len, uint64_t *uid)
{
    if (!buf || !uid)
    {
        return false;
    }
    // Skip the optional preamble (up to 7 octets of 0xFE), then require the 0xAA separator.
    size_t p = 0;
    while (p < len && p < 7 && buf[p] == 0xFE)
    {
        p++;
    }
    if (p >= len || buf[p] != 0xAA)
    {
        return false;
    }
    p++;
    if (len - p < 16) // 12 encoded UID octets + 4 encoded checksum octets
    {
        return false;
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
        return false;
    }
    // Recover the 6 UID octets (MSB first); each is the AND of its 0xAA / 0x55 encoded copies.
    uint64_t u = 0;
    for (int i = 0; i < 6; i++)
    {
        u = (u << 8) | (uint8_t)(euid[i * 2] & euid[i * 2 + 1]);
    }
    *uid = u;
    return true;
}

size_t pc_rdm_build_disc_response(uint8_t *buf, size_t cap, uint64_t uid, uint8_t preamble_len)
{
    if (!buf || preamble_len > 7) // E1.20 allows 0..7 preamble octets
    {
        return 0;
    }
    size_t total = (size_t)preamble_len + 1 + 16; // preamble + 0xAA separator + 12 UID + 4 checksum octets
    if (cap < total)
    {
        return 0;
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
    return p;
}

size_t pc_rdm_build_device_info(uint8_t *pdata, size_t cap, const RdmDeviceInfo *info)
{
    if (!pdata || !info || cap < PC_RDM_DEVICE_INFO_PDL)
    {
        return 0;
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
    return PC_RDM_DEVICE_INFO_PDL;
}

bool pc_rdm_parse_device_info(const uint8_t *pdata, uint8_t pdl, RdmDeviceInfo *out)
{
    if (!pdata || !out || pdl < PC_RDM_DEVICE_INFO_PDL)
    {
        return false;
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
    return true;
}

#endif // PC_ENABLE_DMX
