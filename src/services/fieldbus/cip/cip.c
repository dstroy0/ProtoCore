// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cip.c
 * @brief CIP message request builder + response parser (pure, host-tested; constants per Wireshark).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_CIP

#include "mmgr/protomem/protomem.h"
#include "services/fieldbus/cip/cip.h"

PROTOCORE_BEGIN_DECLS

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

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_cip_build_epath(uint8_t *restrict work);
void protocore_cip_build_request(uint8_t *restrict work);

void protocore_cip_build_epath(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = CipV.build_epath_args.buf;
    size_t cap = CipV.build_epath_args.cap;
    uint16_t class_id = CipV.build_epath_args.class_id;
    uint16_t instance_id = CipV.build_epath_args.instance_id;
    uint16_t attribute_id = CipV.build_epath_args.attribute_id;
    proto_bool with_attribute = CipV.build_epath_args.with_attribute;

    if (!buf)
    {
        CipV.n = 0;
        return;
    }
    size_t p = 0;
    size_t s = write_segment(buf + p, cap - p, CIP_SEG_CLASS, class_id);
    if (!s)
    {
        CipV.n = 0;
        return;
    }
    p += s;
    s = write_segment(buf + p, cap - p, CIP_SEG_INSTANCE, instance_id);
    if (!s)
    {
        CipV.n = 0;
        return;
    }
    p += s;
    if (with_attribute)
    {
        s = write_segment(buf + p, cap - p, CIP_SEG_ATTRIBUTE, attribute_id);
        if (!s)
        {
            CipV.n = 0;
            return;
        }
        p += s;
    }
    CipV.n = p;
}

void protocore_cip_build_request(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = CipV.build_request_args.buf;
    size_t cap = CipV.build_request_args.cap;
    uint8_t service = CipV.build_request_args.service;
    const uint8_t *epath = CipV.build_request_args.epath;
    size_t epath_len = CipV.build_request_args.epath_len;
    const uint8_t *data = CipV.build_request_args.data;
    size_t data_len = CipV.build_request_args.data_len;

    // EPATH must be whole 16-bit words and fit the 1-octet word count.
    if (!buf || !epath || (epath_len & 1) || (epath_len / 2) > 0xFF || (data_len && !data))
    {
        CipV.n = 0;
        return;
    }
    size_t total = 2 + epath_len + data_len; // service + path size + EPATH + data
    if (total > cap)
    {
        CipV.n = 0;
        return;
    }
    size_t p = 0;
    buf[p++] = service;
    buf[p++] = (uint8_t)(epath_len / 2); // path size in words
    mem.cpy(buf + p, epath, epath_len);
    p += epath_len;
    if (data_len)
    {
        mem.cpy(buf + p, data, data_len);
        p += data_len;
    }
    CipV.n = p;
}

void protocore_cip_build_get_attr_single(uint8_t *restrict work)
{
    uint8_t *buf = CipV.build_get_attr_single_args.buf;
    size_t cap = CipV.build_get_attr_single_args.cap;
    uint16_t class_id = CipV.build_get_attr_single_args.class_id;
    uint16_t instance_id = CipV.build_get_attr_single_args.instance_id;
    uint16_t attribute_id = CipV.build_get_attr_single_args.attribute_id;

    uint8_t epath[12];
    CipV.build_epath_args.buf = epath;
    CipV.build_epath_args.cap = sizeof(epath);
    CipV.build_epath_args.class_id = class_id;
    CipV.build_epath_args.instance_id = instance_id;
    CipV.build_epath_args.attribute_id = attribute_id;
    CipV.build_epath_args.with_attribute = PROTO_TRUE;
    protocore_cip_build_epath(work);
    size_t elen = CipV.n;
    if (!elen)
    {
        CipV.n = 0;
        return;
    }
    CipV.build_request_args.buf = buf;
    CipV.build_request_args.cap = cap;
    CipV.build_request_args.service = CIP_SC_GET_ATTR_SINGLE;
    CipV.build_request_args.epath = epath;
    CipV.build_request_args.epath_len = elen;
    CipV.build_request_args.data = NULL;
    CipV.build_request_args.data_len = 0;
    protocore_cip_build_request(work);
}

void protocore_cip_build_get_attr_all(uint8_t *restrict work)
{
    uint8_t *buf = CipV.build_get_attr_all_args.buf;
    size_t cap = CipV.build_get_attr_all_args.cap;
    uint16_t class_id = CipV.build_get_attr_all_args.class_id;
    uint16_t instance_id = CipV.build_get_attr_all_args.instance_id;

    uint8_t epath[8]; // class + instance logical segments only (no attribute), worst case 4B each
    CipV.build_epath_args.buf = epath;
    CipV.build_epath_args.cap = sizeof(epath);
    CipV.build_epath_args.class_id = class_id;
    CipV.build_epath_args.instance_id = instance_id;
    CipV.build_epath_args.attribute_id = 0;
    CipV.build_epath_args.with_attribute = PROTO_FALSE;
    protocore_cip_build_epath(work);
    size_t elen = CipV.n;
    if (!elen)
    {
        CipV.n = 0;
        return;
    }
    CipV.build_request_args.buf = buf;
    CipV.build_request_args.cap = cap;
    CipV.build_request_args.service = CIP_SC_GET_ATTR_ALL;
    CipV.build_request_args.epath = epath;
    CipV.build_request_args.epath_len = elen;
    CipV.build_request_args.data = NULL;
    CipV.build_request_args.data_len = 0;
    protocore_cip_build_request(work);
}

void protocore_cip_build_set_attr_single(uint8_t *restrict work)
{
    uint8_t *buf = CipV.build_set_attr_single_args.buf;
    size_t cap = CipV.build_set_attr_single_args.cap;
    uint16_t class_id = CipV.build_set_attr_single_args.class_id;
    uint16_t instance_id = CipV.build_set_attr_single_args.instance_id;
    uint16_t attribute_id = CipV.build_set_attr_single_args.attribute_id;
    const uint8_t *value = CipV.build_set_attr_single_args.value;
    size_t value_len = CipV.build_set_attr_single_args.value_len;

    uint8_t epath[12];
    CipV.build_epath_args.buf = epath;
    CipV.build_epath_args.cap = sizeof(epath);
    CipV.build_epath_args.class_id = class_id;
    CipV.build_epath_args.instance_id = instance_id;
    CipV.build_epath_args.attribute_id = attribute_id;
    CipV.build_epath_args.with_attribute = PROTO_TRUE;
    protocore_cip_build_epath(work);
    size_t elen = CipV.n;
    if (!elen)
    {
        CipV.n = 0;
        return;
    }
    CipV.build_request_args.buf = buf;
    CipV.build_request_args.cap = cap;
    CipV.build_request_args.service = CIP_SC_SET_ATTR_SINGLE;
    CipV.build_request_args.epath = epath;
    CipV.build_request_args.epath_len = elen;
    CipV.build_request_args.data = value;
    CipV.build_request_args.data_len = value_len;
    protocore_cip_build_request(work);
}

void protocore_cip_parse_response(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = CipV.parse_response_args.buf;
    size_t len = CipV.parse_response_args.len;
    CipResponse *out = CipV.parse_response_args.out;

    if (!buf || !out || len < 4) // service + reserved + general status + additional-status size
    {
        CipV.ok = PROTO_FALSE;
        return;
    }
    out->service = buf[0];
    out->general_status = buf[2];
    uint8_t addl_words = buf[3];
    size_t data_start = 4 + (size_t)addl_words * 2;
    if (data_start > len)
    {
        CipV.ok = PROTO_FALSE;
        return;
    }
    out->data = buf + data_start;
    out->data_len = len - data_start;
    CipV.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
CipVars CipV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CIP
