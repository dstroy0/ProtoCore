// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ads.c
 * @brief Beckhoff ADS / AMS builder + parser (pure, host-tested). All fields little-endian.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_ADS

#include "mmgr/protomem/protomem.h"
#include "services/fieldbus/ads/ads.h"

#include "mmgr/endian/endian.h"

PROTOCORE_BEGIN_DECLS

// Write the AMS/TCP header (with the final total length) + the 32-octet AMS header. The payload
// is appended by the caller; `payload_len` is cbData. Returns ADS_HDR_LEN, or 0 if too small.
static size_t write_header(uint8_t *buf, size_t cap, const AdsRequest *r, AdsCommand cmd, uint32_t payload_len)
{
    if (!buf || !r || cap < (size_t)ADS_HDR_LEN + payload_len)
    {
        return 0;
    }
    size_t p = 0;
    // AMS/TCP header: reserved(2) + length(4). length covers the AMS header + payload.
    buf[p++] = 0x00;
    buf[p++] = 0x00;
    p += endian.wr32le(buf + p, (uint32_t)ADS_AMS_HDR_LEN + payload_len);
    // AMS header.
    mem.cpy(buf + p, r->target.net_id, ADS_NET_ID_LEN);
    p += ADS_NET_ID_LEN;
    p += endian.wr16le(buf + p, r->target.port);
    mem.cpy(buf + p, r->source.net_id, ADS_NET_ID_LEN);
    p += ADS_NET_ID_LEN;
    p += endian.wr16le(buf + p, r->source.port);
    p += endian.wr16le(buf + p, (uint16_t)cmd); // wire byte in
    p += endian.wr16le(buf + p, ADS_STATE_REQUEST);
    p += endian.wr32le(buf + p, payload_len); // cbData
    p += endian.wr32le(buf + p, 0);           // error code (0 on a request)
    p += endian.wr32le(buf + p, r->invoke_id);
    return p; // == ADS_HDR_LEN
}

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_ads_build_read_device_info(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = AdsV.build_read_device_info_args.buf;
    size_t cap = AdsV.build_read_device_info_args.cap;
    const AdsRequest *r = AdsV.build_read_device_info_args.r;

    AdsV.n = write_header(buf, cap, r, ADS_COMMAND_READ_DEVICE_INFO, 0);
}

void protocore_ads_build_read_state(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = AdsV.build_read_state_args.buf;
    size_t cap = AdsV.build_read_state_args.cap;
    const AdsRequest *r = AdsV.build_read_state_args.r;

    AdsV.n = write_header(buf, cap, r, ADS_COMMAND_READ_STATE, 0);
}

void protocore_ads_build_read(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = AdsV.build_read_args.buf;
    size_t cap = AdsV.build_read_args.cap;
    const AdsRequest *r = AdsV.build_read_args.r;
    uint32_t index_group = AdsV.build_read_args.index_group;
    uint32_t index_offset = AdsV.build_read_args.index_offset;
    uint32_t read_len = AdsV.build_read_args.read_len;

    size_t p = write_header(buf, cap, r, ADS_COMMAND_READ, 12);
    if (!p)
    {
        AdsV.n = 0;
        return;
    }
    p += endian.wr32le(buf + p, index_group);
    p += endian.wr32le(buf + p, index_offset);
    p += endian.wr32le(buf + p, read_len);
    AdsV.n = p;
}

void protocore_ads_build_write(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = AdsV.build_write_args.buf;
    size_t cap = AdsV.build_write_args.cap;
    const AdsRequest *r = AdsV.build_write_args.r;
    uint32_t index_group = AdsV.build_write_args.index_group;
    uint32_t index_offset = AdsV.build_write_args.index_offset;
    const uint8_t *data = AdsV.build_write_args.data;
    uint32_t len = AdsV.build_write_args.len;

    if (len && !data)
    {
        AdsV.n = 0;
        return;
    }
    size_t p = write_header(buf, cap, r, ADS_COMMAND_WRITE, 12 + len);
    if (!p)
    {
        AdsV.n = 0;
        return;
    }
    p += endian.wr32le(buf + p, index_group);
    p += endian.wr32le(buf + p, index_offset);
    p += endian.wr32le(buf + p, len);
    if (len)
    {
        mem.cpy(buf + p, data, len);
        p += len;
    }
    AdsV.n = p;
}

void protocore_ads_build_read_write(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = AdsV.build_read_write_args.buf;
    size_t cap = AdsV.build_read_write_args.cap;
    const AdsRequest *r = AdsV.build_read_write_args.r;
    uint32_t index_group = AdsV.build_read_write_args.index_group;
    uint32_t index_offset = AdsV.build_read_write_args.index_offset;
    uint32_t read_len = AdsV.build_read_write_args.read_len;
    const uint8_t *write_data = AdsV.build_read_write_args.write_data;
    uint32_t write_len = AdsV.build_read_write_args.write_len;

    if (write_len && !write_data)
    {
        AdsV.n = 0;
        return;
    }
    size_t p = write_header(buf, cap, r, ADS_COMMAND_READ_WRITE, 16 + write_len);
    if (!p)
    {
        AdsV.n = 0;
        return;
    }
    p += endian.wr32le(buf + p, index_group);
    p += endian.wr32le(buf + p, index_offset);
    p += endian.wr32le(buf + p, read_len);
    p += endian.wr32le(buf + p, write_len);
    if (write_len)
    {
        mem.cpy(buf + p, write_data, write_len);
        p += write_len;
    }
    AdsV.n = p;
}

void protocore_ads_build_write_control(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = AdsV.build_write_control_args.buf;
    size_t cap = AdsV.build_write_control_args.cap;
    const AdsRequest *r = AdsV.build_write_control_args.r;
    uint16_t protocore_ads_state = AdsV.build_write_control_args.protocore_ads_state;
    uint16_t device_state = AdsV.build_write_control_args.device_state;
    const uint8_t *data = AdsV.build_write_control_args.data;
    uint32_t len = AdsV.build_write_control_args.len;

    if (len && !data)
    {
        AdsV.n = 0;
        return;
    }
    size_t p = write_header(buf, cap, r, ADS_COMMAND_WRITE_CONTROL, 8 + len);
    if (!p)
    {
        AdsV.n = 0;
        return;
    }
    p += endian.wr16le(buf + p, protocore_ads_state);
    p += endian.wr16le(buf + p, device_state);
    p += endian.wr32le(buf + p, len);
    if (len)
    {
        mem.cpy(buf + p, data, len);
        p += len;
    }
    AdsV.n = p;
}

void protocore_ads_build_add_notification(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = AdsV.build_add_notification_args.buf;
    size_t cap = AdsV.build_add_notification_args.cap;
    const AdsRequest *r = AdsV.build_add_notification_args.r;
    uint32_t index_group = AdsV.build_add_notification_args.index_group;
    uint32_t index_offset = AdsV.build_add_notification_args.index_offset;
    uint32_t length = AdsV.build_add_notification_args.length;
    AdsTransMode mode = AdsV.build_add_notification_args.mode;
    uint32_t max_delay = AdsV.build_add_notification_args.max_delay;
    uint32_t cycle_time = AdsV.build_add_notification_args.cycle_time;

    // IndexGroup + IndexOffset + Length + TransMode + MaxDelay + CycleTime + Reserved(16) = 40.
    size_t p = write_header(buf, cap, r, ADS_COMMAND_ADD_NOTIFICATION, 40);
    if (!p)
    {
        AdsV.n = 0;
        return;
    }
    p += endian.wr32le(buf + p, index_group);
    p += endian.wr32le(buf + p, index_offset);
    p += endian.wr32le(buf + p, length);
    p += endian.wr32le(buf + p, (uint32_t)mode); // wire byte in
    p += endian.wr32le(buf + p, max_delay);
    p += endian.wr32le(buf + p, cycle_time);
    mem.set(buf + p, 0, 16); // reserved
    p += 16;
    AdsV.n = p;
}

void protocore_ads_build_del_notification(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = AdsV.build_del_notification_args.buf;
    size_t cap = AdsV.build_del_notification_args.cap;
    const AdsRequest *r = AdsV.build_del_notification_args.r;
    uint32_t notification_handle = AdsV.build_del_notification_args.notification_handle;

    size_t p = write_header(buf, cap, r, ADS_COMMAND_DEL_NOTIFICATION, 4);
    if (!p)
    {
        AdsV.n = 0;
        return;
    }
    p += endian.wr32le(buf + p, notification_handle);
    AdsV.n = p;
}

void protocore_ads_parse_ams_header(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = AdsV.parse_ams_header_args.buf;
    size_t len = AdsV.parse_ams_header_args.len;
    AdsAmsHeader *out = AdsV.parse_ams_header_args.out;

    if (!buf || !out || len < (size_t)ADS_HDR_LEN)
    {
        AdsV.ok = PROTO_FALSE;
        return;
    }
    if (buf[0] != 0x00 || buf[1] != 0x00) // AMS/TCP reserved
    {
        AdsV.ok = PROTO_FALSE;
        return;
    }
    uint32_t frame_len = endian.rd32le(buf + 2); // AMS header + payload
    if (frame_len < (uint32_t)ADS_AMS_HDR_LEN)
    {
        AdsV.ok = PROTO_FALSE;
        return;
    }
    if ((size_t)ADS_AMSTCP_HDR_LEN + frame_len > len)
    {
        AdsV.ok = PROTO_FALSE;
        return;
    }
    const uint8_t *a = buf + ADS_AMSTCP_HDR_LEN;
    mem.cpy(out->target.net_id, a, ADS_NET_ID_LEN);
    out->target.port = endian.rd16le(a + 6);
    mem.cpy(out->source.net_id, a + 8, ADS_NET_ID_LEN);
    out->source.port = endian.rd16le(a + 14);
    out->cmd = (AdsCommand)endian.rd16le(a + 16); // wire byte out
    out->state_flags = endian.rd16le(a + 18);
    out->data_len = endian.rd32le(a + 20);
    out->error_code = endian.rd32le(a + 24);
    out->invoke_id = endian.rd32le(a + 28);
    // cbData must fit inside the frame the AMS/TCP length promised.
    if ((uint32_t)ADS_AMS_HDR_LEN + out->data_len > frame_len)
    {
        AdsV.ok = PROTO_FALSE;
        return;
    }
    out->data = a + ADS_AMS_HDR_LEN;
    AdsV.ok = PROTO_TRUE;
}

void protocore_ads_parse_read(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *data = AdsV.parse_read_args.data;
    size_t data_len = AdsV.parse_read_args.data_len;
    AdsReadResult *out = AdsV.parse_read_args.out;

    if (!data || !out || data_len < 8)
    {
        AdsV.ok = PROTO_FALSE;
        return;
    }
    out->result = endian.rd32le(data);
    out->len = endian.rd32le(data + 4);
    if (8 + (size_t)out->len > data_len)
    {
        AdsV.ok = PROTO_FALSE;
        return;
    }
    out->data = data + 8;
    AdsV.ok = PROTO_TRUE;
}

void protocore_ads_parse_result(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *data = AdsV.parse_result_args.data;
    size_t data_len = AdsV.parse_result_args.data_len;
    uint32_t *result = AdsV.parse_result_args.result;

    if (!data || !result || data_len < 4)
    {
        AdsV.ok = PROTO_FALSE;
        return;
    }
    *result = endian.rd32le(data);
    AdsV.ok = PROTO_TRUE;
}

void protocore_ads_parse_read_state(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *data = AdsV.parse_read_state_args.data;
    size_t data_len = AdsV.parse_read_state_args.data_len;
    AdsReadStateResult *out = AdsV.parse_read_state_args.out;

    if (!data || !out || data_len < 8)
    {
        AdsV.ok = PROTO_FALSE;
        return;
    }
    out->result = endian.rd32le(data);
    out->protocore_ads_state = endian.rd16le(data + 4);
    out->device_state = endian.rd16le(data + 6);
    AdsV.ok = PROTO_TRUE;
}

void protocore_ads_parse_read_device_info(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *data = AdsV.parse_read_device_info_args.data;
    size_t data_len = AdsV.parse_read_device_info_args.data_len;
    AdsDeviceInfo *out = AdsV.parse_read_device_info_args.out;

    if (!data || !out || data_len < 4 + 4 + ADS_DEVICE_NAME_LEN)
    {
        AdsV.ok = PROTO_FALSE;
        return;
    }
    out->result = endian.rd32le(data);
    out->version_major = data[4];
    out->version_minor = data[5];
    out->version_build = endian.rd16le(data + 6);
    mem.cpy(out->device_name, data + 8, ADS_DEVICE_NAME_LEN);
    out->device_name[ADS_DEVICE_NAME_LEN] = '\0'; // the field is not guaranteed NUL-terminated
    AdsV.ok = PROTO_TRUE;
}

void protocore_ads_parse_add_notification(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *data = AdsV.parse_add_notification_args.data;
    size_t data_len = AdsV.parse_add_notification_args.data_len;
    uint32_t *result = AdsV.parse_add_notification_args.result;
    uint32_t *handle = AdsV.parse_add_notification_args.handle;

    if (!data || !result || !handle || data_len < 8)
    {
        AdsV.ok = PROTO_FALSE;
        return;
    }
    *result = endian.rd32le(data);
    *handle = endian.rd32le(data + 4);
    AdsV.ok = PROTO_TRUE;
}

void protocore_ads_parse_notification(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *data = AdsV.parse_notification_args.data;
    size_t data_len = AdsV.parse_notification_args.data_len;
    AdsNotificationSampleFn on_sample = AdsV.parse_notification_args.on_sample;
    void *user = AdsV.parse_notification_args.user;

    // Length(4) + Stamps(4), then per stamp: Timestamp(8) + Samples(4) + samples.
    if (!data || !on_sample || data_len < 8)
    {
        AdsV.ok = PROTO_FALSE;
        return;
    }
    uint32_t length = endian.rd32le(data); // octets after this field
    uint32_t stamps = endian.rd32le(data + 4);
    if (4 + (size_t)length > data_len)
    {
        AdsV.ok = PROTO_FALSE;
        return;
    }
    size_t p = 8;
    for (uint32_t s = 0; s < stamps; s++)
    {
        if (p + 12 > data_len) // timestamp(8) + samples(4)
        {
            AdsV.ok = PROTO_FALSE;
            return;
        }
        uint64_t timestamp = endian.rd64le(data + p);
        uint32_t samples = endian.rd32le(data + p + 8);
        p += 12;
        for (uint32_t i = 0; i < samples; i++)
        {
            if (p + 8 > data_len) // handle(4) + size(4)
            {
                AdsV.ok = PROTO_FALSE;
                return;
            }
            uint32_t handle = endian.rd32le(data + p);
            uint32_t size = endian.rd32le(data + p + 4);
            p += 8;
            if (p + (size_t)size > data_len)
            {
                AdsV.ok = PROTO_FALSE;
                return;
            }
            on_sample(handle, data + p, size, timestamp, user);
            p += size;
        }
    }
    AdsV.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
AdsVars AdsV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_ADS
