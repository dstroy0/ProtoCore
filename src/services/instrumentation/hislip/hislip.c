// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hislip.c
 * @brief HiSLIP (IVI-6.1) message codec (pure, host-tested).
 */

#include "services/instrumentation/hislip/hislip.h"

#if PC_ENABLE_HISLIP

#include "mmgr/endian.h"

size_t pc_hislip_build_header(uint8_t *buf, size_t cap, HislipMsg type, uint8_t control, uint32_t parameter,
                              uint64_t payload_len)
{
    if (!buf || cap < PC_HISLIP_HEADER_LEN)
    {
        return 0;
    }
    buf[0] = 'H';
    buf[1] = 'S';
    buf[2] = (uint8_t)(type);
    buf[3] = control;
    pc_wr32be(buf + 4, parameter);
    pc_wr64be(buf + 8, payload_len);
    return PC_HISLIP_HEADER_LEN;
}

proto_bool pc_hislip_parse_header(const uint8_t *buf, size_t len, HislipHeader *out)
{
    if (!buf || !out || len < PC_HISLIP_HEADER_LEN || buf[0] != 'H' || buf[1] != 'S')
    {
        return PROTO_FALSE;
    }
    out->type = (HislipMsg)(buf[2]);
    out->control = buf[3];
    out->parameter = pc_rd32be(buf + 4);
    out->payload_len = pc_rd64be(buf + 8);
    return PROTO_TRUE;
}

// Build a header + optional payload into buf; returns total or 0 on overflow / bad input.
static size_t build_with_payload(uint8_t *buf, size_t cap, HislipMsg type, uint8_t control, uint32_t parameter,
                                 const uint8_t *payload, size_t payload_len)
{
    if (!buf || (payload_len && !payload))
    {
        return 0;
    }
    size_t total = PC_HISLIP_HEADER_LEN + payload_len;
    if (cap < total)
    {
        return 0;
    }
    pc_hislip_build_header(buf, cap, type, control, parameter, payload_len);
    if (payload_len)
    {
        memcpy(buf + PC_HISLIP_HEADER_LEN, payload, payload_len);
    }
    return total;
}

size_t pc_hislip_build_initialize(uint8_t *buf, size_t cap, uint16_t protocol_version, uint16_t vendor_id,
                                  const char *sub_address)
{
    size_t sub_len = sub_address ? strnlen(sub_address, cap) : 0;
    uint32_t parameter = ((uint32_t)protocol_version << 16) | vendor_id;
    return build_with_payload(buf, cap, HISLIP_MSG_INITIALIZE, 0, parameter, (const uint8_t *)sub_address, sub_len);
}

size_t pc_hislip_build_initialize_response(uint8_t *buf, size_t cap, uint8_t control, uint16_t protocol_version,
                                           uint16_t session_id)
{
    uint32_t parameter = ((uint32_t)protocol_version << 16) | session_id;
    return pc_hislip_build_header(buf, cap, HISLIP_MSG_INITIALIZE_RESPONSE, control, parameter, 0);
}

size_t pc_hislip_build_async_initialize(uint8_t *buf, size_t cap, uint16_t session_id)
{
    return pc_hislip_build_header(buf, cap, HISLIP_MSG_ASYNC_INITIALIZE, 0, session_id, 0);
}

size_t pc_hislip_build_async_initialize_response(uint8_t *buf, size_t cap, uint8_t control, uint16_t server_vendor_id)
{
    return pc_hislip_build_header(buf, cap, HISLIP_MSG_ASYNC_INITIALIZE_RESPONSE, control, server_vendor_id, 0);
}

size_t pc_hislip_build_data(uint8_t *buf, size_t cap, proto_bool is_end, uint8_t control, uint32_t message_id,
                            const uint8_t *payload, size_t payload_len)
{
    HislipMsg type = is_end ? HISLIP_MSG_DATA_END : HISLIP_MSG_DATA;
    return build_with_payload(buf, cap, type, control, message_id, payload, payload_len);
}

uint32_t pc_hislip_next_message_id(uint32_t id)
{
    return id + 2; // unsigned 32-bit wrap is well-defined
}

proto_bool pc_hislip_parse_initialize(const uint8_t *buf, size_t len, HislipInitialize *out)
{
    HislipHeader h;
    if (!out || !pc_hislip_parse_header(buf, len, &h) || h.type != HISLIP_MSG_INITIALIZE)
    {
        return PROTO_FALSE;
    }
    if (h.payload_len > (uint64_t)(len - PC_HISLIP_HEADER_LEN)) // the sub-address must be fully present
    {
        return PROTO_FALSE;
    }
    out->protocol_version = (uint16_t)(h.parameter >> 16);
    out->vendor_id = (uint16_t)(h.parameter & 0xFFFF);
    out->sub_address = (const char *)(buf + PC_HISLIP_HEADER_LEN);
    out->sub_address_len = (size_t)h.payload_len;
    return PROTO_TRUE;
}

proto_bool pc_hislip_parse_initialize_response(const uint8_t *buf, size_t len, HislipInitializeResponse *out)
{
    HislipHeader h;
    if (!out || !pc_hislip_parse_header(buf, len, &h) || h.type != HISLIP_MSG_INITIALIZE_RESPONSE)
    {
        return PROTO_FALSE;
    }
    out->protocol_version = (uint16_t)(h.parameter >> 16);
    out->session_id = (uint16_t)(h.parameter & 0xFFFF);
    out->overlap = (h.control & PC_HISLIP_INITRESP_OVERLAP) != 0;
    out->encryption_mandatory = (h.control & PC_HISLIP_INITRESP_ENC_MANDATORY) != 0;
    return PROTO_TRUE;
}

#endif // PC_ENABLE_HISLIP
