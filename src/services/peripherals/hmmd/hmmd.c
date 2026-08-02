// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hmmd.c
 * @brief Waveshare HMMD mmWave radar codec - implementation. See hmmd.h.
 *
 * Frame layout (little-endian throughout), per the S3KM1110 serial protocol:
 *   report:  F4 F3 F2 F1 | len(2)=35 | detect(1) | dist(2) | gate[16](2 each) | F8 F7 F6 F5
 *   command: FD FC FB FA | len(2)    | word(2)   | [value]                    | 04 03 02 01
 */

#include "services/peripherals/hmmd/hmmd.h"

#if PC_ENABLE_HMMD

#include <string.h>

#if PROTOCORE_HOT
#endif
static const uint8_t HDR[4] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t FTR[4] = {0xF8, 0xF7, 0xF6, 0xF5};
static const uint8_t CMD_HDR[4] = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t CMD_FTR[4] = {0x04, 0x03, 0x02, 0x01};

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

proto_bool pc_hmmd_parse_report(const uint8_t *f, size_t len, HmmdReport *out)
{
    if (!f || !out || len != PC_HMMD_FRAME_MAX)
    {
        return PROTO_FALSE;
    }
    if (memcmp(f, HDR, 4) != 0)
    {
        return PROTO_FALSE;
    }
    if (rd16(f + 4) != PC_HMMD_REPORT_LEN)
    {
        return PROTO_FALSE; // the only report length this module emits
    }
    if (memcmp(f + 6 + PC_HMMD_REPORT_LEN, FTR, 4) != 0)
    {
        return PROTO_FALSE;
    }

    const uint8_t *p = f + 6;
    HmmdReport r;
    memset(&r, 0, sizeof(r));
    r.detected = (p[0] == 0x01) ? 1u : 0u;
    r.distance_cm = rd16(p + 1);
    for (int i = 0; i < PC_HMMD_GATES; i++)
    {
        r.gate_energy[i] = rd16(p + 3 + 2 * i);
    }
    *out = r;
    return PROTO_TRUE;
}

void pc_hmmd_stream_reset(HmmdStream *s)
{
    if (!s)
    {
        return;
    }
    s->pos = 0;
    s->total = 0;
    s->hdr_match = 0;
    s->phase = 0;
}

proto_bool pc_hmmd_stream_push(HmmdStream *s, uint8_t b, HmmdReport *out)
{
    if (!s || !out)
    {
        return PROTO_FALSE;
    }
    switch (s->phase)
    {
    case 0: // sync on the 4-octet header (its octets are distinct, so a resync restarts at most at [0])
        if (b == HDR[s->hdr_match])
        {
            s->buf[s->hdr_match++] = b;
            if (s->hdr_match == 4)
            {
                s->pos = 4;
                s->phase = 1;
            }
        }
        else
        {
            s->hdr_match = (b == HDR[0]) ? 1 : 0;
            if (s->hdr_match)
            {
                s->buf[0] = b;
            }
        }
        return PROTO_FALSE;
    case 1: // little-endian length field
        s->buf[s->pos++] = b;
        if (s->pos == 6)
        {
            // Widen before comparing: 6 + 0xFFFF + 4 wraps a uint16_t, defeating the guard.
            uint32_t total = 6u + (uint32_t)rd16(s->buf + 4) + 4u;
            if (total > PC_HMMD_FRAME_MAX)
            {
                pc_hmmd_stream_reset(s); // absurd length: drop and resync
                return PROTO_FALSE;
            }
            s->total = (uint16_t)total;
            s->phase = 2;
        }
        return PROTO_FALSE;
    default: // body + footer
        s->buf[s->pos++] = b;
        if (s->pos >= s->total)
        {
            proto_bool ok = pc_hmmd_parse_report(s->buf, s->total, out);
            pc_hmmd_stream_reset(s);
            return ok;
        }
        return PROTO_FALSE;
    }
}

proto_bool pc_hmmd_present(const HmmdReport *r)
{
    return r && r->detected != 0;
}

uint16_t pc_hmmd_distance_cm(const HmmdReport *r)
{
    return (r && r->detected) ? r->distance_cm : 0;
}

// --- command encoders ------------------------------------------------------

size_t pc_hmmd_cmd_build(uint8_t *buf, size_t cap, uint16_t word, const uint8_t *value, size_t vlen)
{
    if (vlen && !value)
    {
        return 0;
    }
    size_t need = 4 + 2 + 2 + vlen + 4;
    if (!buf || cap < need)
    {
        return 0;
    }
    size_t i = 0;
    for (int k = 0; k < 4; k++)
    {
        buf[i++] = CMD_HDR[k];
    }
    uint16_t dl = (uint16_t)(2 + vlen); // command word + value; header/footer excluded
    buf[i++] = (uint8_t)(dl & 0xFF);
    buf[i++] = (uint8_t)(dl >> 8);
    buf[i++] = (uint8_t)(word & 0xFF);
    buf[i++] = (uint8_t)(word >> 8);
    for (size_t k = 0; k < vlen; k++)
    {
        buf[i++] = value[k];
    }
    for (int k = 0; k < 4; k++)
    {
        buf[i++] = CMD_FTR[k];
    }
    return i;
}

size_t pc_hmmd_cmd_open(uint8_t *buf, size_t cap)
{
    static const uint8_t v[2] = {0x01, 0x00}; // value 0x0001
    return pc_hmmd_cmd_build(buf, cap, 0x00FF, v, 2);
}

size_t pc_hmmd_cmd_close(uint8_t *buf, size_t cap)
{
    return pc_hmmd_cmd_build(buf, cap, 0x00FE, NULL, 0);
}

size_t pc_hmmd_cmd_read_firmware(uint8_t *buf, size_t cap)
{
    return pc_hmmd_cmd_build(buf, cap, 0x0000, NULL, 0);
}

size_t pc_hmmd_cmd_read_serial(uint8_t *buf, size_t cap)
{
    return pc_hmmd_cmd_build(buf, cap, 0x0011, NULL, 0);
}

size_t pc_hmmd_cmd_read_config(uint8_t *buf, size_t cap)
{
    return pc_hmmd_cmd_build(buf, cap, 0x0008, NULL, 0);
}

size_t pc_hmmd_cmd_read_register(uint8_t *buf, size_t cap, const uint8_t *value, size_t vlen)
{
    return pc_hmmd_cmd_build(buf, cap, 0x0002, value, vlen);
}

// --- command-ACK decoding --------------------------------------------------

proto_bool pc_hmmd_parse_ack(const uint8_t *f, size_t len, HmmdAck *out)
{
    // layout: four header bytes, two length bytes, two command-word bytes, optional data, four footer bytes
    if (!f || !out || len < 12)
    {
        return PROTO_FALSE;
    }
    if (memcmp(f, CMD_HDR, 4) != 0)
    {
        return PROTO_FALSE;
    }
    size_t dl = (size_t)rd16(f + 4); // command word + data
    if (dl < 2 || len != 4 + 2 + dl + 4)
    {
        return PROTO_FALSE; // the declared length must account for exactly this frame
    }
    if (memcmp(f + 6 + dl, CMD_FTR, 4) != 0)
    {
        return PROTO_FALSE;
    }
    out->command = rd16(f + 6);
    out->payload_len = dl - 2;
    out->payload = out->payload_len ? f + 8 : NULL;
    return PROTO_TRUE;
}

proto_bool pc_hmmd_ack_matches(const HmmdAck *ack, uint16_t word)
{
    return ack && (uint8_t)(ack->command & 0xFF) == (uint8_t)(word & 0xFF);
}

// ---------------------------------------------------------------------------
// UART binding
// ---------------------------------------------------------------------------

#if PROTOCORE_HOT

// All HMMD UART-binding state, owned by one instance (internal linkage): the frame reassembler, the
// last decoded report, and the have-report flag, grouped so it is one named owner unreachable from
// any other translation unit.
typedef struct
{
    HmmdStream stream;
    HmmdReport last;
    proto_bool have = PROTO_FALSE;
} HmmdCtx;
static HmmdCtx s_hmmd;

proto_bool pc_hmmd_begin(int rx_pin, int tx_pin)
{
    pc_hmmd_stream_reset(&s_hmmd.stream);
    s_hmmd.have = PROTO_FALSE;
    Serial2.begin(PC_HMMD_BAUD, SERIAL_8N1, rx_pin, tx_pin);
    return PROTO_TRUE;
}

proto_bool pc_hmmd_poll()
{
    proto_bool fresh = PROTO_FALSE;
    while (Serial2.available())
    {
        HmmdReport r;
        if (pc_hmmd_stream_push(&s_hmmd.stream, (uint8_t)Serial2.read(), &r))
        {
            s_hmmd.last = r;
            s_hmmd.have = PROTO_TRUE;
            fresh = PROTO_TRUE;
        }
    }
    return fresh;
}

const HmmdReport *pc_hmmd_last()
{
    return s_hmmd.have ? &s_hmmd.last : NULL;
}

#else // host build: no UART

proto_bool pc_hmmd_begin(int, int)
{
    return PROTO_FALSE;
}

proto_bool pc_hmmd_poll()
{
    return PROTO_FALSE;
}

const HmmdReport *pc_hmmd_last()
{
    return NULL;
}

#endif // PROTOCORE_HOT

#endif // PC_ENABLE_HMMD
