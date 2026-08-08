// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ld2410.c
 * @brief HLK-LD2410 mmWave radar codec - implementation. See ld2410.h.
 *
 * Frame layout (little-endian lengths / distances), per the Hi-Link serial protocol:
 *   report:  F4 F3 F2 F1 | len(2) | type | AA | state | mv_cm(2) mv_e | st_cm(2) st_e |
 *            pc_cm(2) | [engineering: maxMvGate maxStGate mvE[9] stE[9] light out] |
 *            55 | check | F8 F7 F6 F5     (len = 0x0D basic, 0x23 engineering)
 *   command: FD FC FB FA | len(2) | word(2) | [value] | 04 03 02 01
 */

#include "services/peripherals/ld2410/ld2410.h"
#include "protocore_config.h"

#if PC_ENABLE_LD2410

#if PC_HAS_BUS
#include "services/peripherals/uart.h" // the shared UART owner
#endif
static const uint8_t HDR[4] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t FTR[4] = {0xF8, 0xF7, 0xF6, 0xF5};
static const uint8_t CMD_HDR[4] = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t CMD_FTR[4] = {0x04, 0x03, 0x02, 0x01};

static const uint16_t LEN_BASIC = 13;       // payload length for a basic target frame
static const uint16_t LEN_ENGINEERING = 35; // ... and for an engineering-mode frame

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

// Build one command frame; returns its length or 0 if @p cap is too small.
static size_t cmd_frame(uint8_t *buf, size_t cap, uint16_t word, const uint8_t *val, size_t vlen)
{
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
    uint16_t dl = (uint16_t)(2 + vlen);
    buf[i++] = (uint8_t)(dl & 0xFF);
    buf[i++] = (uint8_t)(dl >> 8);
    buf[i++] = (uint8_t)(word & 0xFF);
    buf[i++] = (uint8_t)(word >> 8);
    for (size_t k = 0; k < vlen; k++)
    {
        buf[i++] = val[k];
    }
    for (int k = 0; k < 4; k++)
    {
        buf[i++] = CMD_FTR[k];
    }
    return i;
}

proto_bool pc_ld2410_parse_report(const uint8_t *f, size_t len, Ld2410Report *out)
{
    if (!f || !out || len < (size_t)(6 + LEN_BASIC + 4))
    {
        return PROTO_FALSE;
    }
    if (memcmp(f, HDR, 4) != 0)
    {
        return PROTO_FALSE;
    }
    uint16_t dl = rd16(f + 4);
    if ((size_t)(6 + dl + 4) != len)
    {
        return PROTO_FALSE; // length field must frame the buffer exactly
    }
    if (memcmp(f + 6 + dl, FTR, 4) != 0)
    {
        return PROTO_FALSE;
    }

    const uint8_t *p = f + 6;
    Ld2410Report r;
    memset(&r, 0, sizeof(r));
    if (p[0] == 0x02)
    {
        if (dl != LEN_BASIC)
        {
            return PROTO_FALSE;
        }
        r.engineering = 0;
    }
    else if (p[0] == 0x01)
    {
        if (dl != LEN_ENGINEERING)
        {
            return PROTO_FALSE;
        }
        r.engineering = 1;
    }
    else
    {
        return PROTO_FALSE; // unknown data type
    }
    if (p[1] != 0xAA)
    {
        return PROTO_FALSE; // intra-frame head marker
    }

    r.state = p[2];
    r.moving_cm = rd16(p + 3);
    r.moving_energy = p[5];
    r.static_cm = rd16(p + 6);
    r.static_energy = p[8];
    r.detect_cm = rd16(p + 9);

    if (r.engineering)
    {
        r.max_moving_gate = p[11];
        r.max_static_gate = p[12];
        for (int i = 0; i < LD2410_MAX_GATES; i++)
        {
            r.moving_gate_energy[i] = p[13 + i];
        }
        for (int i = 0; i < LD2410_MAX_GATES; i++)
        {
            r.static_gate_energy[i] = p[22 + i];
        }
        r.light = p[31];
        r.out_pin = p[32];
        if (p[33] != 0x55)
        {
            return PROTO_FALSE; // tail
        }
    }
    else if (p[11] != 0x55)
    {
        return PROTO_FALSE; // tail
    }
    *out = r;
    return PROTO_TRUE;
}

void pc_ld2410_stream_reset(Ld2410Stream *s)
{
    s->pos = 0;
    s->total = 0;
    s->hdr_match = 0;
    s->phase = 0;
}

proto_bool pc_ld2410_stream_push(Ld2410Stream *s, uint8_t b, Ld2410Report *out)
{
    switch (s->phase)
    {
    case 0: // sync on the 4-byte header (bytes are distinct, so resync restarts at most at [0])
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
            if (total > LD2410_FRAME_MAX)
            {
                pc_ld2410_stream_reset(s); // absurd length: drop and resync
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
            proto_bool ok = pc_ld2410_parse_report(s->buf, s->total, out);
            pc_ld2410_stream_reset(s);
            return ok;
        }
        return PROTO_FALSE;
    }
}

proto_bool pc_ld2410_present(const Ld2410Report *r)
{
    return r && r->state != LD2410_STATE_NONE;
}

uint16_t pc_ld2410_distance_cm(const Ld2410Report *r)
{
    if (!r)
    {
        return 0;
    }
    if (r->state == LD2410_STATE_MOVING || r->state == LD2410_STATE_BOTH)
    {
        return r->moving_cm;
    }
    if (r->state == LD2410_STATE_STATIC)
    {
        return r->static_cm;
    }
    return 0;
}

size_t pc_ld2410_cmd_config_enable(uint8_t *buf, size_t cap)
{
    static const uint8_t v[2] = {0x01, 0x00}; // value 0x0001
    return cmd_frame(buf, cap, 0x00FF, v, 2);
}
size_t pc_ld2410_cmd_config_end(uint8_t *buf, size_t cap)
{
    return cmd_frame(buf, cap, 0x00FE, NULL, 0);
}
size_t pc_ld2410_cmd_engineering(uint8_t *buf, size_t cap, proto_bool on)
{
    return cmd_frame(buf, cap, on ? 0x0062 : 0x0063, NULL, 0);
}
size_t pc_ld2410_cmd_restart(uint8_t *buf, size_t cap)
{
    return cmd_frame(buf, cap, 0x00A3, NULL, 0);
}

// --- LD2410B-only ----------------------------------------------------------
size_t pc_ld2410_cmd_bluetooth(uint8_t *buf, size_t cap, proto_bool on)
{
    const uint8_t v[2] = {(uint8_t)(on ? 0x01 : 0x00), 0x00}; // value 0x0001 on / 0x0000 off
    return cmd_frame(buf, cap, 0x00A4, v, 2);
}

size_t pc_ld2410_cmd_get_mac(uint8_t *buf, size_t cap)
{
    static const uint8_t v[2] = {0x01, 0x00}; // value 0x0001
    return cmd_frame(buf, cap, 0x00A5, v, 2);
}

size_t pc_ld2410_cmd_set_bt_password(uint8_t *buf, size_t cap, const char password[6])
{
    if (!password)
    {
        return 0;
    }
    // Exactly 6 octets, natural order - the spec's worked example sends "HiLink" as 48 69 4C 69 6E 6B.
    const uint8_t v[6] = {(uint8_t)password[0], (uint8_t)password[1], (uint8_t)password[2],
                          (uint8_t)password[3], (uint8_t)password[4], (uint8_t)password[5]};
    return cmd_frame(buf, cap, 0x00A9, v, 6);
}

// --- command-ACK decoding --------------------------------------------------
proto_bool pc_ld2410_parse_ack(const uint8_t *f, size_t len, Ld2410Ack *out)
{
    // layout: four header bytes, two length bytes, two command-word bytes, two status bytes, optional data, four footer
    // bytes
    if (!f || !out || len < 14)
    {
        return PROTO_FALSE;
    }
    for (int k = 0; k < 4; k++)
    {
        if (f[k] != CMD_HDR[k])
        {
            return PROTO_FALSE;
        }
    }
    size_t dl = (size_t)f[4] | ((size_t)f[5] << 8); // intra-frame length: word + status + data
    if (dl < 4 || len != 4 + 2 + dl + 4)
    {
        return PROTO_FALSE; // the declared length must account for exactly this frame
    }
    for (int k = 0; k < 4; k++)
    {
        if (f[6 + dl + k] != CMD_FTR[k])
        {
            return PROTO_FALSE;
        }
    }
    out->command = (uint16_t)((uint16_t)f[6] | ((uint16_t)f[7] << 8));
    out->status = (uint16_t)((uint16_t)f[8] | ((uint16_t)f[9] << 8));
    out->payload_len = dl - 4;
    out->payload = out->payload_len ? f + 10 : NULL;
    return PROTO_TRUE;
}

proto_bool pc_ld2410_ack_ok(const Ld2410Ack *ack)
{
    return ack && ack->status == 0;
}

proto_bool pc_ld2410_ack_mac(const Ld2410Ack *ack, uint8_t mac[6])
{
    if (!ack || !mac || ack->command != 0x01A5 || ack->status != 0 || ack->payload_len < 6 || !ack->payload)
    {
        return PROTO_FALSE;
    }
    for (int k = 0; k < 6; k++)
    {
        mac[k] = ack->payload[k];
    }
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// UART binding
// ---------------------------------------------------------------------------

#if PC_HAS_BUS

// Bytes taken from the UART per poll. A report frame is far shorter, so one poll carries at least
// a whole frame and the read stays bounded (SRC_LAW rule 5).
#define LD2410_RX_CHUNK 64

// Widest command frame this driver builds: header, length, word, value and footer.
#define LD2410_CMD_MAX 16

// All LD2410 UART-binding state, owned by one instance (internal linkage): the frame stream
// assembler, the last decoded report, the have-report flag, the receive chunk, and the command
// frame, grouped so it is one named owner, unreachable from any other translation unit. Both
// buffers are members rather than locals because each is filled on the request path.
typedef struct
{
    Ld2410Stream stream;
    Ld2410Report last;
    proto_bool have;
    uint8_t rx[LD2410_RX_CHUNK];
    uint8_t cmd[LD2410_CMD_MAX];
} Ld2410Ctx;
static Ld2410Ctx s_ld;

proto_bool pc_ld2410_begin(int rx_pin, int tx_pin)
{
    pc_ld2410_stream_reset(&s_ld.stream);
    s_ld.have = PROTO_FALSE;
    return pc_uart_begin((uint8_t)PC_LD2410_UART, PC_LD2410_BAUD, rx_pin, tx_pin);
}

proto_bool pc_ld2410_poll(void)
{
    proto_bool fresh = PROTO_FALSE;
    size_t n = pc_uart_read((uint8_t)PC_LD2410_UART, s_ld.rx, sizeof(s_ld.rx), 0);
    for (size_t i = 0; i < n; i++)
    {
        Ld2410Report r;
        if (pc_ld2410_stream_push(&s_ld.stream, s_ld.rx[i], &r))
        {
            s_ld.last = r;
            s_ld.have = PROTO_TRUE;
            fresh = PROTO_TRUE;
        }
    }
    return fresh;
}

const Ld2410Report *pc_ld2410_last(void)
{
    return s_ld.have ? &s_ld.last : NULL;
}

// A configuration change is three frames: open the config window, carry the change, close it.
// Each is built into the owned command buffer and put on the wire before the next is built.
static proto_bool send_cmd(size_t n)
{
    return n > 0 && pc_uart_write((uint8_t)PC_LD2410_UART, s_ld.cmd, n);
}

proto_bool pc_ld2410_set_engineering(proto_bool on)
{
    proto_bool ok = send_cmd(pc_ld2410_cmd_config_enable(s_ld.cmd, sizeof(s_ld.cmd)));
    ok &= send_cmd(pc_ld2410_cmd_engineering(s_ld.cmd, sizeof(s_ld.cmd), on));
    ok &= send_cmd(pc_ld2410_cmd_config_end(s_ld.cmd, sizeof(s_ld.cmd)));
    return ok;
}

proto_bool pc_ld2410_restart(void)
{
    proto_bool ok = send_cmd(pc_ld2410_cmd_config_enable(s_ld.cmd, sizeof(s_ld.cmd)));
    ok &= send_cmd(pc_ld2410_cmd_restart(s_ld.cmd, sizeof(s_ld.cmd)));
    ok &= send_cmd(pc_ld2410_cmd_config_end(s_ld.cmd, sizeof(s_ld.cmd)));
    return ok;
}

#else // no bus seam. The codec above is host-tested.

proto_bool pc_ld2410_begin(int rx_pin, int tx_pin)
{
    (void)rx_pin;
    (void)tx_pin;
    return PROTO_FALSE;
}
proto_bool pc_ld2410_poll(void)
{
    return PROTO_FALSE;
}
const Ld2410Report *pc_ld2410_last(void)
{
    return NULL;
}
proto_bool pc_ld2410_set_engineering(proto_bool on)
{
    (void)on;
    return PROTO_FALSE;
}
proto_bool pc_ld2410_restart(void)
{
    return PROTO_FALSE;
}

#endif // PC_HAS_BUS

#endif // PC_ENABLE_LD2410
