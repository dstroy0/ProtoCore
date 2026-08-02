// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file focas.c
 * @brief FANUC FOCAS Ethernet builder + parser (pure, host-tested). All fields big-endian.
 */

#include "services/machine_tool/focas/focas.h"

#if PC_ENABLE_FOCAS

#include <string.h>

// C++14 out-of-line definitions for the FocasCommand selector table. The in-class initializers in
// focas.h declare them; a `static constexpr` member of CLASS type still needs a definition here
// once it is odr-used (passing one to pc_focas_build() binds it to a const reference, which
// counts). Without these the link fails with "undefined reference to FocasCommand::read_macro" -
// it only stayed hidden where the optimizer happened to fold every use. C++17 would make these
// implicitly inline and let the whole block go away; the library targets C++14.
constexpr FocasCmd FocasCommand::read_cnc_param;
constexpr FocasCmd FocasCommand::read_macro;
constexpr FocasCmd FocasCommand::set_macro;
constexpr FocasCmd FocasCommand::sys_info;
constexpr FocasCmd FocasCommand::read_alarm;
constexpr FocasCmd FocasCommand::read_prg_num;
constexpr FocasCmd FocasCommand::read_seq_num;
constexpr FocasCmd FocasCommand::read_alarm_info;
constexpr FocasCmd FocasCommand::read_feedrate;
constexpr FocasCmd FocasCommand::read_spindle;
constexpr FocasCmd FocasCommand::read_position;
constexpr FocasCmd FocasCommand::read_diag;
constexpr FocasCmd FocasCommand::read_spindle2;
constexpr FocasCmd FocasCommand::read_datetime;
constexpr FocasCmd FocasCommand::read_servo_load;
constexpr FocasCmd FocasCommand::read_axis_names;
constexpr FocasCmd FocasCommand::read_spindle_names;
constexpr FocasCmd FocasCommand::read_cnc_param3;
constexpr FocasCmd FocasCommand::read_macro_dbl;
constexpr FocasCmd FocasCommand::read_pmc;

// FOCAS is big-endian throughout.
static size_t put16be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
    return 2;
}

static size_t put32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xFF);
    p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8) & 0xFF);
    p[3] = (uint8_t)(v & 0xFF);
    return 4;
}

static uint16_t get16be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

// Write the 10-octet envelope (magic + version + type + payload length). Returns FOCAS_FRAME_HDR_LEN,
// or 0 if the buffer cannot hold the header plus its declared payload.
static size_t write_envelope(uint8_t *buf, size_t cap, FocasFrameType type, uint16_t payload_len)
{
    if (!buf || cap < (size_t)FOCAS_FRAME_HDR_LEN + payload_len)
    {
        return 0;
    }
    buf[0] = 0xA0;
    buf[1] = 0xA0;
    buf[2] = 0xA0;
    buf[3] = 0xA0;
    size_t p = 4;
    p += put16be(buf + p, FOCAS_PROTO_VERSION);
    p += put16be(buf + p, (uint16_t)type); // wire byte in
    p += put16be(buf + p, payload_len);
    return p; // == FOCAS_FRAME_HDR_LEN
}

size_t pc_focas_build_open(uint8_t *buf, size_t cap)
{
    size_t p = write_envelope(buf, cap, FOCAS_FRAME_TYPE_OPEN_REQ, 2);
    if (!p)
    {
        return 0;
    }
    p += put16be(buf + p, FOCAS_FRAME_DST);
    return p;
}

size_t pc_focas_build_close(uint8_t *buf, size_t cap)
{
    return write_envelope(buf, cap, FOCAS_FRAME_TYPE_CLOSE_REQ, 0);
}

size_t pc_focas_build_request(uint8_t *buf, size_t cap, FocasCmd cmd, int32_t v1, int32_t v2, int32_t v3, int32_t v4,
                              int32_t v5, const uint8_t *extra, size_t extra_len)
{
    if (extra_len && !extra)
    {
        return 0;
    }
    if (extra_len > 0xFFFF - (size_t)FOCAS_REQ_BODY_LEN)
    {
        return 0;
    }
    uint16_t payload_len = (uint16_t)(FOCAS_REQ_BODY_LEN + extra_len);
    size_t p = write_envelope(buf, cap, FOCAS_FRAME_TYPE_COMMAND_REQ, payload_len);
    if (!p)
    {
        return 0;
    }
    p += put16be(buf + p, cmd.c1);
    p += put16be(buf + p, cmd.c2);
    p += put16be(buf + p, cmd.c3);
    p += put32be(buf + p, (uint32_t)v1);
    p += put32be(buf + p, (uint32_t)v2);
    p += put32be(buf + p, (uint32_t)v3);
    p += put32be(buf + p, (uint32_t)v4);
    p += put32be(buf + p, (uint32_t)v5);
    if (extra_len)
    {
        memcpy(buf + p, extra, extra_len);
        p += extra_len;
    }
    return p;
}

size_t pc_focas_build_sysinfo(uint8_t *buf, size_t cap)
{
    return pc_focas_build_request(buf, cap, FocasCommand::sys_info, 0, 0, 0, 0, 0, NULL, 0);
}

size_t pc_focas_build_read_alarm(uint8_t *buf, size_t cap)
{
    return pc_focas_build_request(buf, cap, FocasCommand::read_alarm, 0, 0, 0, 0, 0, NULL, 0);
}

size_t pc_focas_build_read_param(uint8_t *buf, size_t cap, int32_t first, int32_t last, int32_t axis)
{
    return pc_focas_build_request(buf, cap, FocasCommand::read_cnc_param, first, last, axis, 0, 0, NULL, 0);
}

size_t pc_focas_build_read_macro(uint8_t *buf, size_t cap, int32_t first, int32_t last)
{
    return pc_focas_build_request(buf, cap, FocasCommand::read_macro, first, last, 0, 0, 0, NULL, 0);
}

size_t pc_focas_build_read_position(uint8_t *buf, size_t cap, int32_t kind, int32_t axis)
{
    return pc_focas_build_request(buf, cap, FocasCommand::read_position, kind, axis, 0, 0, 0, NULL, 0);
}

size_t pc_focas_build_read_feedrate(uint8_t *buf, size_t cap)
{
    return pc_focas_build_request(buf, cap, FocasCommand::read_feedrate, 0, 0, 0, 0, 0, NULL, 0);
}

size_t pc_focas_build_read_spindle(uint8_t *buf, size_t cap)
{
    return pc_focas_build_request(buf, cap, FocasCommand::read_spindle, 0, 0, 0, 0, 0, NULL, 0);
}

proto_bool pc_focas_parse_frame(const uint8_t *buf, size_t len, FocasFrame *out)
{
    if (!buf || !out || len < (size_t)FOCAS_FRAME_HDR_LEN)
    {
        return PROTO_FALSE;
    }
    if (buf[0] != 0xA0 || buf[1] != 0xA0 || buf[2] != 0xA0 || buf[3] != 0xA0)
    {
        return PROTO_FALSE;
    }
    out->version = get16be(buf + 4);
    out->type = (FocasFrameType)get16be(buf + 6); // wire byte out
    out->payload_len = get16be(buf + 8);
    if ((size_t)FOCAS_FRAME_HDR_LEN + out->payload_len > len)
    {
        return PROTO_FALSE;
    }
    out->payload = buf + FOCAS_FRAME_HDR_LEN;
    return PROTO_TRUE;
}

proto_bool pc_focas_parse_response(const uint8_t *payload, size_t payload_len, FocasResponse *out)
{
    if (!payload || !out || payload_len < (size_t)FOCAS_RESP_HDR_LEN)
    {
        return PROTO_FALSE;
    }
    out->c1 = get16be(payload);
    out->c2 = get16be(payload + 2);
    out->c3 = get16be(payload + 4);
    out->status = (int16_t)get16be(payload + 6); // signed FOCAS return code
    out->data_len = get16be(payload + 12);
    if ((size_t)FOCAS_RESP_HDR_LEN + out->data_len > payload_len)
    {
        return PROTO_FALSE;
    }
    out->data = payload + FOCAS_RESP_HDR_LEN;
    return PROTO_TRUE;
}

proto_bool pc_focas_parse_command_frame(const uint8_t *buf, size_t len, FocasResponse *out)
{
    FocasFrame f;
    if (!pc_focas_parse_frame(buf, len, &f))
    {
        return PROTO_FALSE;
    }
    if (f.type != FOCAS_FRAME_TYPE_COMMAND_RESP)
    {
        return PROTO_FALSE;
    }
    return pc_focas_parse_response(f.payload, f.payload_len, out);
}

proto_bool pc_focas_parse_sysinfo(const uint8_t *data, size_t data_len, FocasSysInfo *out)
{
    if (!data || !out || data_len < (size_t)FOCAS_SYSINFO_LEN)
    {
        return PROTO_FALSE;
    }
    out->add_info = get16be(data);
    out->max_axis = get16be(data + 2);
    memcpy(out->cnc_type, data + 4, 2);
    out->cnc_type[2] = '\0';
    memcpy(out->mt_type, data + 6, 2);
    out->mt_type[2] = '\0';
    memcpy(out->series, data + 8, 4);
    out->series[4] = '\0';
    memcpy(out->version, data + 12, 4);
    out->version[4] = '\0';
    memcpy(out->axes, data + 16, 2);
    out->axes[2] = '\0';
    return PROTO_TRUE;
}

proto_bool pc_focas_parse_alarm(const uint8_t *data, size_t data_len, uint32_t *alarm_status)
{
    if (!data || !alarm_status || data_len < 4)
    {
        return PROTO_FALSE;
    }
    *alarm_status = get32be(data);
    return PROTO_TRUE;
}

proto_bool pc_focas_decode8(const uint8_t *chunk, size_t len, FocasValue *out)
{
    if (!chunk || !out || len < (size_t)FOCAS_VALUE_LEN)
    {
        return PROTO_FALSE;
    }
    out->data = (int32_t)get32be(chunk);
    out->base = chunk[5];
    out->exp = chunk[7];
    // The 0xFFFF sentinel (octets 6-7) marks "no value"; only base 2 and 10 are decimal-scaled.
    out->valid = !(chunk[6] == 0xFF && chunk[7] == 0xFF) && (out->base == 2 || out->base == 10);
    return out->valid;
}

float pc_focas_value_f(const FocasValue *v)
{
    if (!v || !v->valid)
    {
        return 0.0f;
    }
    float div = 1.0f;
    for (uint8_t i = 0; i < v->exp; i++)
    {
        div *= (float)v->base;
    }
    return (float)v->data / div;
}

#endif // PC_ENABLE_FOCAS
