// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ads.cpp
 * @brief Beckhoff ADS / AMS builder + parser (pure, host-tested). All fields little-endian.
 */

#include "services/fieldbus/ads/ads.h"

#if PC_ENABLE_ADS

#include <string.h>

#include "shared_primitives/endian.h"

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
    p += pc_wr32le(buf + p, (uint32_t)ADS_AMS_HDR_LEN + payload_len);
    // AMS header.
    memcpy(buf + p, r->target.net_id, ADS_NET_ID_LEN);
    p += ADS_NET_ID_LEN;
    p += pc_wr16le(buf + p, r->target.port);
    memcpy(buf + p, r->source.net_id, ADS_NET_ID_LEN);
    p += ADS_NET_ID_LEN;
    p += pc_wr16le(buf + p, r->source.port);
    p += pc_wr16le(buf + p, (uint16_t)cmd); // wire byte in
    p += pc_wr16le(buf + p, AdsStateFlags::request);
    p += pc_wr32le(buf + p, payload_len); // cbData
    p += pc_wr32le(buf + p, 0);           // error code (0 on a request)
    p += pc_wr32le(buf + p, r->invoke_id);
    return p; // == ADS_HDR_LEN
}

size_t pc_ads_build_read_device_info(uint8_t *buf, size_t cap, const AdsRequest *r)
{
    return write_header(buf, cap, r, AdsCommand::read_device_info, 0);
}

size_t pc_ads_build_read_state(uint8_t *buf, size_t cap, const AdsRequest *r)
{
    return write_header(buf, cap, r, AdsCommand::read_state, 0);
}

size_t pc_ads_build_read(uint8_t *buf, size_t cap, const AdsRequest *r, uint32_t index_group, uint32_t index_offset,
                         uint32_t read_len)
{
    size_t p = write_header(buf, cap, r, AdsCommand::read, 12);
    if (!p)
    {
        return 0;
    }
    p += pc_wr32le(buf + p, index_group);
    p += pc_wr32le(buf + p, index_offset);
    p += pc_wr32le(buf + p, read_len);
    return p;
}

size_t pc_ads_build_write(uint8_t *buf, size_t cap, const AdsRequest *r, uint32_t index_group, uint32_t index_offset,
                          const uint8_t *data, uint32_t len)
{
    if (len && !data)
    {
        return 0;
    }
    size_t p = write_header(buf, cap, r, AdsCommand::write, 12 + len);
    if (!p)
    {
        return 0;
    }
    p += pc_wr32le(buf + p, index_group);
    p += pc_wr32le(buf + p, index_offset);
    p += pc_wr32le(buf + p, len);
    if (len)
    {
        memcpy(buf + p, data, len);
        p += len;
    }
    return p;
}

size_t pc_ads_build_read_write(uint8_t *buf, size_t cap, const AdsRequest *r, uint32_t index_group,
                               uint32_t index_offset, uint32_t read_len, const uint8_t *write_data, uint32_t write_len)
{
    if (write_len && !write_data)
    {
        return 0;
    }
    size_t p = write_header(buf, cap, r, AdsCommand::read_write, 16 + write_len);
    if (!p)
    {
        return 0;
    }
    p += pc_wr32le(buf + p, index_group);
    p += pc_wr32le(buf + p, index_offset);
    p += pc_wr32le(buf + p, read_len);
    p += pc_wr32le(buf + p, write_len);
    if (write_len)
    {
        memcpy(buf + p, write_data, write_len);
        p += write_len;
    }
    return p;
}

size_t pc_ads_build_write_control(uint8_t *buf, size_t cap, const AdsRequest *r, uint16_t pc_ads_state,
                                  uint16_t device_state, const uint8_t *data, uint32_t len)
{
    if (len && !data)
    {
        return 0;
    }
    size_t p = write_header(buf, cap, r, AdsCommand::write_control, 8 + len);
    if (!p)
    {
        return 0;
    }
    p += pc_wr16le(buf + p, pc_ads_state);
    p += pc_wr16le(buf + p, device_state);
    p += pc_wr32le(buf + p, len);
    if (len)
    {
        memcpy(buf + p, data, len);
        p += len;
    }
    return p;
}

size_t pc_ads_build_add_notification(uint8_t *buf, size_t cap, const AdsRequest *r, uint32_t index_group,
                                     uint32_t index_offset, uint32_t length, AdsTransMode mode, uint32_t max_delay,
                                     uint32_t cycle_time)
{
    // IndexGroup + IndexOffset + Length + TransMode + MaxDelay + CycleTime + Reserved(16) = 40.
    size_t p = write_header(buf, cap, r, AdsCommand::add_notification, 40);
    if (!p)
    {
        return 0;
    }
    p += pc_wr32le(buf + p, index_group);
    p += pc_wr32le(buf + p, index_offset);
    p += pc_wr32le(buf + p, length);
    p += pc_wr32le(buf + p, (uint32_t)mode); // wire byte in
    p += pc_wr32le(buf + p, max_delay);
    p += pc_wr32le(buf + p, cycle_time);
    memset(buf + p, 0, 16); // reserved
    p += 16;
    return p;
}

size_t pc_ads_build_del_notification(uint8_t *buf, size_t cap, const AdsRequest *r, uint32_t notification_handle)
{
    size_t p = write_header(buf, cap, r, AdsCommand::del_notification, 4);
    if (!p)
    {
        return 0;
    }
    p += pc_wr32le(buf + p, notification_handle);
    return p;
}

bool pc_ads_parse_ams_header(const uint8_t *buf, size_t len, AdsAmsHeader *out)
{
    if (!buf || !out || len < (size_t)ADS_HDR_LEN)
    {
        return false;
    }
    if (buf[0] != 0x00 || buf[1] != 0x00) // AMS/TCP reserved
    {
        return false;
    }
    uint32_t frame_len = pc_rd32le(buf + 2); // AMS header + payload
    if (frame_len < (uint32_t)ADS_AMS_HDR_LEN)
    {
        return false;
    }
    if ((size_t)ADS_AMSTCP_HDR_LEN + frame_len > len)
    {
        return false;
    }
    const uint8_t *a = buf + ADS_AMSTCP_HDR_LEN;
    memcpy(out->target.net_id, a, ADS_NET_ID_LEN);
    out->target.port = pc_rd16le(a + 6);
    memcpy(out->source.net_id, a + 8, ADS_NET_ID_LEN);
    out->source.port = pc_rd16le(a + 14);
    out->cmd = (AdsCommand)pc_rd16le(a + 16); // wire byte out
    out->state_flags = pc_rd16le(a + 18);
    out->data_len = pc_rd32le(a + 20);
    out->error_code = pc_rd32le(a + 24);
    out->invoke_id = pc_rd32le(a + 28);
    // cbData must fit inside the frame the AMS/TCP length promised.
    if ((uint32_t)ADS_AMS_HDR_LEN + out->data_len > frame_len)
    {
        return false;
    }
    out->data = a + ADS_AMS_HDR_LEN;
    return true;
}

bool pc_ads_parse_read(const uint8_t *data, size_t data_len, AdsReadResult *out)
{
    if (!data || !out || data_len < 8)
    {
        return false;
    }
    out->result = pc_rd32le(data);
    out->len = pc_rd32le(data + 4);
    if (8 + (size_t)out->len > data_len)
    {
        return false;
    }
    out->data = data + 8;
    return true;
}

bool pc_ads_parse_result(const uint8_t *data, size_t data_len, uint32_t *result)
{
    if (!data || !result || data_len < 4)
    {
        return false;
    }
    *result = pc_rd32le(data);
    return true;
}

bool pc_ads_parse_read_state(const uint8_t *data, size_t data_len, AdsReadStateResult *out)
{
    if (!data || !out || data_len < 8)
    {
        return false;
    }
    out->result = pc_rd32le(data);
    out->pc_ads_state = pc_rd16le(data + 4);
    out->device_state = pc_rd16le(data + 6);
    return true;
}

bool pc_ads_parse_read_device_info(const uint8_t *data, size_t data_len, AdsDeviceInfo *out)
{
    if (!data || !out || data_len < 4 + 4 + ADS_DEVICE_NAME_LEN)
    {
        return false;
    }
    out->result = pc_rd32le(data);
    out->version_major = data[4];
    out->version_minor = data[5];
    out->version_build = pc_rd16le(data + 6);
    memcpy(out->device_name, data + 8, ADS_DEVICE_NAME_LEN);
    out->device_name[ADS_DEVICE_NAME_LEN] = '\0'; // the field is not guaranteed NUL-terminated
    return true;
}

bool pc_ads_parse_add_notification(const uint8_t *data, size_t data_len, uint32_t *result, uint32_t *handle)
{
    if (!data || !result || !handle || data_len < 8)
    {
        return false;
    }
    *result = pc_rd32le(data);
    *handle = pc_rd32le(data + 4);
    return true;
}

bool pc_ads_parse_notification(const uint8_t *data, size_t data_len, AdsNotificationSampleFn on_sample, void *user)
{
    // Length(4) + Stamps(4), then per stamp: Timestamp(8) + Samples(4) + samples.
    if (!data || !on_sample || data_len < 8)
    {
        return false;
    }
    uint32_t length = pc_rd32le(data); // octets after this field
    uint32_t stamps = pc_rd32le(data + 4);
    if (4 + (size_t)length > data_len)
    {
        return false;
    }
    size_t p = 8;
    for (uint32_t s = 0; s < stamps; s++)
    {
        if (p + 12 > data_len) // timestamp(8) + samples(4)
        {
            return false;
        }
        uint64_t timestamp = pc_rd64le(data + p);
        uint32_t samples = pc_rd32le(data + p + 8);
        p += 12;
        for (uint32_t i = 0; i < samples; i++)
        {
            if (p + 8 > data_len) // handle(4) + size(4)
            {
                return false;
            }
            uint32_t handle = pc_rd32le(data + p);
            uint32_t size = pc_rd32le(data + p + 4);
            p += 8;
            if (p + (size_t)size > data_len)
            {
                return false;
            }
            on_sample(handle, data + p, size, timestamp, user);
            p += size;
        }
    }
    return true;
}

#endif // PC_ENABLE_ADS
