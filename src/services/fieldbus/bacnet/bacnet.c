// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file bacnet.c
 * @brief BACnet/IP BVLC + NPDU builder + parser (pure, host-tested).
 */

#include "services/fieldbus/bacnet/bacnet.h"

#if PC_ENABLE_BACNET

#include <string.h>

size_t pc_bvlc_build(uint8_t *buf, size_t cap, uint8_t function, const uint8_t *npdu, size_t pc_npdu_len)
{
    if (!buf || (pc_npdu_len && !npdu))
    {
        return 0;
    }
    size_t total = BVLC_HEADER_SIZE + pc_npdu_len;
    if (total > 0xFFFF || total > cap)
    {
        return 0;
    }
    buf[0] = BVLC_TYPE_BIP;
    buf[1] = function;
    buf[2] = (uint8_t)(total >> 8); // length, big-endian, the whole BVLL
    buf[3] = (uint8_t)(total & 0xFF);
    if (pc_npdu_len)
    {
        memcpy(buf + BVLC_HEADER_SIZE, npdu, pc_npdu_len);
    }
    return total;
}

proto_bool pc_bvlc_parse(const uint8_t *buf, size_t len, uint8_t *function, const uint8_t **npdu, size_t *pc_npdu_len)
{
    if (!buf || len < BVLC_HEADER_SIZE || buf[0] != BVLC_TYPE_BIP)
    {
        return PROTO_FALSE;
    }
    size_t total = ((size_t)buf[2] << 8) | buf[3];
    if (total < BVLC_HEADER_SIZE || total > len)
    {
        return PROTO_FALSE;
    }
    if (function)
    {
        *function = buf[1];
    }
    if (npdu)
    {
        *npdu = buf + BVLC_HEADER_SIZE;
    }
    if (pc_npdu_len)
    {
        *pc_npdu_len = total - BVLC_HEADER_SIZE;
    }
    return PROTO_TRUE;
}

size_t pc_npdu_build(uint8_t *buf, size_t cap, proto_bool expecting_reply, uint8_t priority, proto_bool has_dest,
                     uint16_t dnet, const uint8_t *dadr, uint8_t dadr_len, uint8_t hop_count, const uint8_t *apdu,
                     size_t apdu_len)
{
    if (!buf || (apdu_len && !apdu) || (dadr_len && !dadr))
    {
        return 0;
    }
    size_t need = 2 + apdu_len; // version + control + apdu
    if (has_dest)
    {
        need += 2 + 1 + dadr_len + 1; // DNET + DLEN + DADR + hop count
    }
    if (need > cap)
    {
        return 0;
    }

    size_t p = 0;
    buf[p++] = NPDU_VERSION;
    uint8_t control = (uint8_t)(priority & NPCI_PRIORITY_MASK);
    if (expecting_reply)
    {
        control |= NPCI_EXPECTING_REPLY;
    }
    if (has_dest)
    {
        control |= NPCI_DEST_PRESENT;
    }
    buf[p++] = control;
    if (has_dest)
    {
        buf[p++] = (uint8_t)(dnet >> 8);
        buf[p++] = (uint8_t)(dnet & 0xFF);
        buf[p++] = dadr_len;
        if (dadr_len)
        {
            memcpy(buf + p, dadr, dadr_len);
            p += dadr_len;
        }
        buf[p++] = hop_count; // follows the (absent) source fields when a destination is present
    }
    if (apdu_len)
    {
        memcpy(buf + p, apdu, apdu_len);
        p += apdu_len;
    }
    return p;
}

proto_bool pc_npdu_parse(const uint8_t *buf, size_t len, NpduInfo *out)
{
    if (!buf || !out || len < 2 || buf[0] != NPDU_VERSION)
    {
        return PROTO_FALSE;
    }
    uint8_t control = buf[1];
    size_t p = 2;

    out->control = control;
    out->network_message = (control & NPCI_NETWORK_MSG) != 0;
    out->dest_present = (control & NPCI_DEST_PRESENT) != 0;
    out->src_present = (control & NPCI_SRC_PRESENT) != 0;
    out->dnet = 0;
    out->snet = 0;
    out->hop_count = 0;

    if (out->dest_present)
    {
        if (p + 3 > len)
        {
            return PROTO_FALSE;
        }
        out->dnet = (uint16_t)((buf[p] << 8) | buf[p + 1]);
        uint8_t dlen = buf[p + 2];
        p += 3 + dlen;
        if (p > len)
        {
            return PROTO_FALSE;
        }
    }
    if (out->src_present)
    {
        if (p + 3 > len)
        {
            return PROTO_FALSE;
        }
        out->snet = (uint16_t)((buf[p] << 8) | buf[p + 1]);
        uint8_t slen = buf[p + 2];
        p += 3 + slen;
        if (p > len)
        {
            return PROTO_FALSE;
        }
    }
    if (out->dest_present) // the hop count follows the source fields
    {
        if (p + 1 > len)
        {
            return PROTO_FALSE;
        }
        out->hop_count = buf[p++];
    }
    out->apdu = buf + p;
    out->apdu_len = len - p;
    return PROTO_TRUE;
}

// Encode a BACnet tagged unsigned integer (tag octet = tag-number<<4 | class | length), using the minimal
// number of value octets (big-endian). @p context selects the tag class: a context tag ORs 0x08 into the tag
// octet, an application tag does not. Callers pass values that fit the 4-bit length field (<= 4 octets).
static size_t bacnet_put_tagged_uint(uint8_t *buf, uint8_t tag_number, uint32_t value, proto_bool context)
{
    uint8_t v[4];
    size_t vlen = 0;
    if (value == 0)
    {
        v[vlen++] = 0;
    }
    else
    {
        for (int shift = 24; shift >= 0; shift -= 8) // big-endian, dropping the leading zero octets
        {
            uint8_t b = (uint8_t)(value >> shift);
            if (vlen == 0 && b == 0)
            {
                continue;
            }
            v[vlen++] = b;
        }
    }
    size_t p = 0;
    buf[p++] = (uint8_t)(((uint32_t)tag_number << 4) | (context ? 0x08u : 0x00u) | (uint8_t)vlen);
    for (size_t i = 0; i < vlen; i++)
    {
        buf[p++] = v[i];
    }
    return p;
}

size_t pc_apdu_build_who_is(uint8_t *buf, size_t cap, uint32_t low_limit, uint32_t high_limit, proto_bool has_limits)
{
    if (!buf)
    {
        return 0;
    }
    uint8_t tmp[16]; // worst case: 2 header + 2 * (1 tag + 3 value) = 10
    size_t p = 0;
    tmp[p++] = (uint8_t)(BACNET_PDU_UNCONFIRMED_REQUEST << 4); // 0x10, no flags
    tmp[p++] = BACNET_SVC_UN_WHO_IS;                           // service choice 8
    if (has_limits)
    {
        if (low_limit > BACNET_MAX_INSTANCE || high_limit > BACNET_MAX_INSTANCE || low_limit > high_limit)
        {
            return 0;
        }
        p += bacnet_put_tagged_uint(tmp + p, 0, low_limit, PROTO_TRUE);
        p += bacnet_put_tagged_uint(tmp + p, 1, high_limit, PROTO_TRUE);
    }
    if (cap < p)
    {
        return 0;
    }
    memcpy(buf, tmp, p);
    return p;
}

size_t pc_apdu_build_i_am(uint8_t *buf, size_t cap, uint32_t device_instance, uint32_t max_apdu, uint8_t segmentation,
                          uint16_t vendor_id)
{
    if (!buf || device_instance > BACNET_MAX_INSTANCE || segmentation > 3)
    {
        return 0;
    }
    uint8_t tmp[24]; // worst case: 2 header + 5 oid + 5 max-apdu + 2 seg + 5 vendor = 19
    size_t p = 0;
    tmp[p++] = (uint8_t)(BACNET_PDU_UNCONFIRMED_REQUEST << 4); // 0x10, no flags
    tmp[p++] = BACNET_SVC_UN_I_AM;                             // service choice 0
    // I-Am device object identifier: application tag 12, a 4-octet (object-type << 22) | instance.
    tmp[p++] = 0xC4; // application tag 12, length 4
    uint32_t oid = ((uint32_t)BACNET_OBJ_DEVICE << 22) | device_instance;
    tmp[p++] = (uint8_t)(oid >> 24);
    tmp[p++] = (uint8_t)(oid >> 16);
    tmp[p++] = (uint8_t)(oid >> 8);
    tmp[p++] = (uint8_t)oid;
    p += bacnet_put_tagged_uint(tmp + p, 2, max_apdu, PROTO_FALSE); // max APDU length accepted (unsigned)
    tmp[p++] = 0x91; // segmentation supported: application tag 9 (enumerated), length 1
    tmp[p++] = segmentation;
    p += bacnet_put_tagged_uint(tmp + p, 2, vendor_id, PROTO_FALSE); // vendor id (unsigned)
    if (cap < p)
    {
        return 0;
    }
    memcpy(buf, tmp, p);
    return p;
}

size_t pc_apdu_build_read_property(uint8_t *buf, size_t cap, uint8_t invoke_id, uint8_t max_resp, uint16_t object_type,
                                   uint32_t object_instance, uint32_t property_id)
{
    if (!buf || object_instance > BACNET_MAX_INSTANCE || object_type > 0x3FFu) // object type is 10 bits
    {
        return 0;
    }
    uint8_t tmp[16]; // 4 header + 5 object-id (tag + 4) + 5 property (tag + <= 4) = 14 worst case
    size_t p = 0;
    tmp[p++] = (uint8_t)(BACNET_PDU_CONFIRMED_REQUEST << 4); // 0x00, unsegmented
    tmp[p++] = max_resp;                                     // max segments accepted / max APDU length accepted
    tmp[p++] = invoke_id;
    tmp[p++] = BACNET_SVC_CONF_READ_PROPERTY; // service choice 12
    // Object identifier: context tag 0, a 4-octet (object-type << 22) | instance.
    tmp[p++] = 0x0C; // context tag 0, length 4
    uint32_t oid = ((uint32_t)object_type << 22) | object_instance;
    tmp[p++] = (uint8_t)(oid >> 24);
    tmp[p++] = (uint8_t)(oid >> 16);
    tmp[p++] = (uint8_t)(oid >> 8);
    tmp[p++] = (uint8_t)oid;
    // Property identifier: context tag 1 (enumerated), minimal-length.
    p += bacnet_put_tagged_uint(tmp + p, 1, property_id, PROTO_TRUE);
    if (cap < p)
    {
        return 0;
    }
    memcpy(buf, tmp, p);
    return p;
}

// Confirmed-Request APDU header: flags + the max-segs/max-apdu octet + invoke id, then a segment
// sequence/window pair when segmented, then the service choice. Advances *p; false on a short buffer.
static proto_bool apdu_parse_confirmed_request(const uint8_t *apdu, size_t len, BacnetApdu *out, size_t *p)
{
    out->segmented = (apdu[0] & BACNET_APDU_SEG) != 0;
    out->more_follows = (apdu[0] & BACNET_APDU_MOR) != 0;
    out->sa = (apdu[0] & BACNET_APDU_SA) != 0;
    if (len < *p + 2) // max-segs/max-apdu octet + invoke id
    {
        return PROTO_FALSE;
    }
    out->invoke_id = apdu[*p + 1]; // apdu[1] is max segments / max APDU, apdu[2] is the invoke id
    *p += 2;
    if (out->segmented) // a segmented request carries a sequence number + proposed window size
    {
        if (len < *p + 2)
        {
            return PROTO_FALSE;
        }
        *p += 2;
    }
    if (len < *p + 1)
    {
        return PROTO_FALSE;
    }
    out->service_choice = apdu[(*p)++];
    return PROTO_TRUE;
}

// Complex-ACK APDU header: flags + invoke id, then a segment sequence/window pair when segmented, then
// the service-ACK choice. Advances *p; false on a short buffer.
static proto_bool apdu_parse_complex_ack(const uint8_t *apdu, size_t len, BacnetApdu *out, size_t *p)
{
    out->segmented = (apdu[0] & BACNET_APDU_SEG) != 0;
    out->more_follows = (apdu[0] & BACNET_APDU_MOR) != 0;
    if (len < *p + 1) // invoke id
    {
        return PROTO_FALSE;
    }
    out->invoke_id = apdu[(*p)++];
    if (out->segmented)
    {
        if (len < *p + 2)
        {
            return PROTO_FALSE;
        }
        *p += 2;
    }
    if (len < *p + 1)
    {
        return PROTO_FALSE;
    }
    out->service_choice = apdu[(*p)++];
    return PROTO_TRUE;
}

proto_bool pc_apdu_parse(const uint8_t *apdu, size_t len, BacnetApdu *out)
{
    if (!apdu || !out || len < 1)
    {
        return PROTO_FALSE;
    }
    memset(out, 0, sizeof(*out));
    out->pdu_type = (uint8_t)(apdu[0] >> 4);
    size_t p = 1;
    switch (out->pdu_type)
    {
    case BACNET_PDU_CONFIRMED_REQUEST:
        if (!apdu_parse_confirmed_request(apdu, len, out, &p))
        {
            return PROTO_FALSE;
        }
        break;
    case BACNET_PDU_UNCONFIRMED_REQUEST:
        if (len < p + 1)
        {
            return PROTO_FALSE;
        }
        out->service_choice = apdu[p++];
        break;
    case BACNET_PDU_SIMPLE_ACK:
        if (len < p + 2) // invoke id + service-ACK choice
        {
            return PROTO_FALSE;
        }
        out->invoke_id = apdu[p++];
        out->service_choice = apdu[p++];
        break;
    case BACNET_PDU_COMPLEX_ACK:
        if (!apdu_parse_complex_ack(apdu, len, out, &p))
        {
            return PROTO_FALSE;
        }
        break;
    default:
        return PROTO_FALSE; // segment-ack / error / reject / abort are not decoded here
    }
    out->service_data = (p < len) ? apdu + p : NULL;
    out->service_data_len = len - p;
    return PROTO_TRUE;
}

#endif // PC_ENABLE_BACNET
