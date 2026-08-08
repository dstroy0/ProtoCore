// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cip.c
 * @brief CIP message request builder + response parser (pure, host-tested; constants per Wireshark).
 */

#include "services/fieldbus/cip/cip.h"

#if PC_ENABLE_CIP

// Write one logical segment (class/instance/attribute) for @p id; 8-bit when it fits, else
// 16-bit (segment byte + pad + LE value). Returns the octets written (2 or 4), or 0 if it
// does not fit in [p, p+cap).
static size_t write_segment(uint8_t *p, size_t cap, uint8_t logical_type, uint16_t id)
{
    if (id <= 0xFF)
    {
        if (cap < 2)
        {
            return 0;
        }
        p[0] = (uint8_t)(CIP_SEG_LOGICAL | logical_type | CIP_SEG_8BIT);
        p[1] = (uint8_t)id;
        return 2;
    }
    if (cap < 4)
    {
        return 0;
    }
    p[0] = (uint8_t)(CIP_SEG_LOGICAL | logical_type | CIP_SEG_16BIT);
    p[1] = 0x00; // pad to align the 16-bit value
    p[2] = (uint8_t)(id & 0xFF);
    p[3] = (uint8_t)(id >> 8);
    return 4;
}

size_t pc_cip_build_epath(uint8_t *buf, size_t cap, uint16_t class_id, uint16_t instance_id, uint16_t attribute_id,
                          proto_bool with_attribute)
{
    if (!buf)
    {
        return 0;
    }
    size_t p = 0;
    size_t s = write_segment(buf + p, cap - p, CIP_SEG_CLASS, class_id);
    if (!s)
    {
        return 0;
    }
    p += s;
    s = write_segment(buf + p, cap - p, CIP_SEG_INSTANCE, instance_id);
    if (!s)
    {
        return 0;
    }
    p += s;
    if (with_attribute)
    {
        s = write_segment(buf + p, cap - p, CIP_SEG_ATTRIBUTE, attribute_id);
        if (!s)
        {
            return 0;
        }
        p += s;
    }
    return p;
}

size_t pc_cip_build_request(uint8_t *buf, size_t cap, uint8_t service, const uint8_t *epath, size_t epath_len,
                            const uint8_t *data, size_t data_len)
{
    // EPATH must be whole 16-bit words and fit the 1-octet word count.
    if (!buf || !epath || (epath_len & 1) || (epath_len / 2) > 0xFF || (data_len && !data))
    {
        return 0;
    }
    size_t total = 2 + epath_len + data_len; // service + path size + EPATH + data
    if (total > cap)
    {
        return 0;
    }
    size_t p = 0;
    buf[p++] = service;
    buf[p++] = (uint8_t)(epath_len / 2); // path size in words
    memcpy(buf + p, epath, epath_len);
    p += epath_len;
    if (data_len)
    {
        memcpy(buf + p, data, data_len);
        p += data_len;
    }
    return p;
}

size_t pc_cip_build_get_attr_single(uint8_t *buf, size_t cap, uint16_t class_id, uint16_t instance_id,
                                    uint16_t attribute_id)
{
    uint8_t epath[12];
    size_t elen = pc_cip_build_epath(epath, sizeof(epath), class_id, instance_id, attribute_id, PROTO_TRUE);
    if (!elen)
    {
        return 0;
    }
    return pc_cip_build_request(buf, cap, CIP_SC_GET_ATTR_SINGLE, epath, elen, NULL, 0);
}

size_t pc_cip_build_get_attr_all(uint8_t *buf, size_t cap, uint16_t class_id, uint16_t instance_id)
{
    uint8_t epath[8]; // class + instance logical segments only (no attribute), worst case 4B each
    size_t elen = pc_cip_build_epath(epath, sizeof(epath), class_id, instance_id, 0, PROTO_FALSE);
    if (!elen)
    {
        return 0;
    }
    return pc_cip_build_request(buf, cap, CIP_SC_GET_ATTR_ALL, epath, elen, NULL, 0);
}

size_t pc_cip_build_set_attr_single(uint8_t *buf, size_t cap, uint16_t class_id, uint16_t instance_id,
                                    uint16_t attribute_id, const uint8_t *value, size_t value_len)
{
    uint8_t epath[12];
    size_t elen = pc_cip_build_epath(epath, sizeof(epath), class_id, instance_id, attribute_id, PROTO_TRUE);
    if (!elen)
    {
        return 0;
    }
    return pc_cip_build_request(buf, cap, CIP_SC_SET_ATTR_SINGLE, epath, elen, value, value_len);
}

proto_bool pc_cip_parse_response(const uint8_t *buf, size_t len, CipResponse *out)
{
    if (!buf || !out || len < 4) // service + reserved + general status + additional-status size
    {
        return PROTO_FALSE;
    }
    out->service = buf[0];
    out->general_status = buf[2];
    uint8_t addl_words = buf[3];
    size_t data_start = 4 + (size_t)addl_words * 2;
    if (data_start > len)
    {
        return PROTO_FALSE;
    }
    out->data = buf + data_start;
    out->data_len = len - data_start;
    return PROTO_TRUE;
}

#endif // PC_ENABLE_CIP
