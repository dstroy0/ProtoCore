// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ld2410.c
 * @brief HLK-LD2410 mmWave radar codec - implementation. See ld2410.h.
 *
 * Frame layout (little-endian lengths / distances), per the Hi-Link serial protocol:
 *   report:  F4 F3 F2 F1 | len(2) | type | AA | state | mv_cm(2) mv_e | st_cm(2) st_e |
 *            protocore_cm(2) | [engineering: maxMvGate maxStGate mvE[9] stE[9] light out] |
 *            55 | check | F8 F7 F6 F5     (len = 0x0D basic, 0x23 engineering)
 *   command: FD FC FB FA | len(2) | word(2) | [value] | 04 03 02 01
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_LD2410

#include "mmgr/protomem/protomem.h"
#include "mmgr/secure/secure.h" // the persistent end this module's state is taken from
#include "server/peripherals/ld2410/ld2410.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_HAS_BUS
#include "server/peripherals/uart.h" // the shared UART owner
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

// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_LD2410_BORROW persistent bytes
} Ld2410OwnCtx;
static Ld2410OwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_ld2410_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_LD2410_BORROW).buf;
    }
    return s_own.span;
}

static void ld2410_cmd_config_enable(uint8_t *restrict work);
static void ld2410_cmd_config_end(uint8_t *restrict work);
static void ld2410_cmd_engineering(uint8_t *restrict work);
static void ld2410_cmd_restart(uint8_t *restrict work);
static void ld2410_parse_report(uint8_t *restrict work);
static void ld2410_stream_push(uint8_t *restrict work);
static void ld2410_stream_reset(uint8_t *restrict work);

static void ld2410_parse_report(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *f = Ld2410.parse_report_args.frame;
    size_t len = Ld2410.parse_report_args.len;
    Ld2410Report *out = Ld2410.parse_report_args.out;

    if (!f || !out || len < (size_t)(6 + LEN_BASIC + 4))
    {
        Ld2410.ok = PROTO_FALSE;
        return;
    }
    if (mem.cmp(f, HDR, 4) != 0)
    {
        Ld2410.ok = PROTO_FALSE;
        return;
    }
    uint16_t dl = rd16(f + 4);
    if ((size_t)(6 + dl + 4) != len)
    {
        Ld2410.ok = PROTO_FALSE;
        return; // length field must frame the buffer exactly
    }
    if (mem.cmp(f + 6 + dl, FTR, 4) != 0)
    {
        Ld2410.ok = PROTO_FALSE;
        return;
    }

    const uint8_t *p = f + 6;
    Ld2410Report r;
    mem.set(&r, 0, sizeof(r));
    if (p[0] == 0x02)
    {
        if (dl != LEN_BASIC)
        {
            Ld2410.ok = PROTO_FALSE;
            return;
        }
        r.engineering = 0;
    }
    else if (p[0] == 0x01)
    {
        if (dl != LEN_ENGINEERING)
        {
            Ld2410.ok = PROTO_FALSE;
            return;
        }
        r.engineering = 1;
    }
    else
    {
        Ld2410.ok = PROTO_FALSE;
        return; // unknown data type
    }
    if (p[1] != 0xAA)
    {
        Ld2410.ok = PROTO_FALSE;
        return; // intra-frame head marker
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
            Ld2410.ok = PROTO_FALSE;
            return; // tail
        }
    }
    else if (p[11] != 0x55)
    {
        Ld2410.ok = PROTO_FALSE;
        return; // tail
    }
    *out = r;
    Ld2410.ok = PROTO_TRUE;
}

static void ld2410_stream_reset(uint8_t *restrict work)
{
    (void)work;
    Ld2410Stream *s = Ld2410.stream_reset_args.s;

    s->pos = 0;
    s->total = 0;
    s->hdr_match = 0;
    s->phase = 0;
}

static void ld2410_stream_push(uint8_t *restrict work)
{
    (void)work;
    Ld2410Stream *s = Ld2410.stream_push_args.s;
    uint8_t b = Ld2410.stream_push_args.byte;
    Ld2410Report *out = Ld2410.stream_push_args.out;

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
        Ld2410.ok = PROTO_FALSE;
        return;
    case 1: // little-endian length field
        s->buf[s->pos++] = b;
        if (s->pos == 6)
        {
            // Widen before comparing: 6 + 0xFFFF + 4 wraps a uint16_t, defeating the guard.
            uint32_t total = 6u + (uint32_t)rd16(s->buf + 4) + 4u;
            if (total > LD2410_FRAME_MAX)
            {
                Ld2410.stream_reset_args.s = s;
                ld2410_stream_reset(work); // absurd length: drop and resync
                Ld2410.ok = PROTO_FALSE;
                return;
            }
            s->total = (uint16_t)total;
            s->phase = 2;
        }
        Ld2410.ok = PROTO_FALSE;
        return;
    default: // body + footer
        s->buf[s->pos++] = b;
        if (s->pos >= s->total)
        {
            Ld2410.parse_report_args.frame = s->buf;
            Ld2410.parse_report_args.len = s->total;
            Ld2410.parse_report_args.out = out;
            ld2410_parse_report(work);
            proto_bool ok = Ld2410.ok;
            Ld2410.stream_reset_args.s = s;
            ld2410_stream_reset(work);
            Ld2410.ok = ok;
            return;
        }
        Ld2410.ok = PROTO_FALSE;
        return;
    }
}

static void ld2410_present(uint8_t *restrict work)
{
    (void)work;
    const Ld2410Report *r = Ld2410.present_args.r;

    Ld2410.ok = r && r->state != LD2410_STATE_NONE;
}

static void ld2410_distance_cm(uint8_t *restrict work)
{
    (void)work;
    const Ld2410Report *r = Ld2410.distance_cm_args.r;

    if (!r)
    {
        Ld2410.cm = 0;
        return;
    }
    if (r->state == LD2410_STATE_MOVING || r->state == LD2410_STATE_BOTH)
    {
        Ld2410.cm = r->moving_cm;
        return;
    }
    if (r->state == LD2410_STATE_STATIC)
    {
        Ld2410.cm = r->static_cm;
        return;
    }
    Ld2410.cm = 0;
}

static void ld2410_cmd_config_enable(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Ld2410.cmd_config_enable_args.buf;
    size_t cap = Ld2410.cmd_config_enable_args.cap;

    static const uint8_t v[2] = {0x01, 0x00}; // value 0x0001
    Ld2410.n = cmd_frame(buf, cap, 0x00FF, v, 2);
}
static void ld2410_cmd_config_end(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Ld2410.cmd_config_end_args.buf;
    size_t cap = Ld2410.cmd_config_end_args.cap;

    Ld2410.n = cmd_frame(buf, cap, 0x00FE, NULL, 0);
}
static void ld2410_cmd_engineering(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Ld2410.cmd_engineering_args.buf;
    size_t cap = Ld2410.cmd_engineering_args.cap;
    proto_bool on = Ld2410.cmd_engineering_args.on;

    Ld2410.n = cmd_frame(buf, cap, on ? 0x0062 : 0x0063, NULL, 0);
}
static void ld2410_cmd_restart(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Ld2410.cmd_restart_args.buf;
    size_t cap = Ld2410.cmd_restart_args.cap;

    Ld2410.n = cmd_frame(buf, cap, 0x00A3, NULL, 0);
}

// --- LD2410B-only ----------------------------------------------------------
static void ld2410_cmd_bluetooth(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Ld2410.cmd_bluetooth_args.buf;
    size_t cap = Ld2410.cmd_bluetooth_args.cap;
    proto_bool on = Ld2410.cmd_bluetooth_args.on;

    const uint8_t v[2] = {(uint8_t)(on ? 0x01 : 0x00), 0x00}; // value 0x0001 on / 0x0000 off
    Ld2410.n = cmd_frame(buf, cap, 0x00A4, v, 2);
}

static void ld2410_cmd_get_mac(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Ld2410.cmd_get_mac_args.buf;
    size_t cap = Ld2410.cmd_get_mac_args.cap;

    static const uint8_t v[2] = {0x01, 0x00}; // value 0x0001
    Ld2410.n = cmd_frame(buf, cap, 0x00A5, v, 2);
}

static void ld2410_cmd_set_bt_password(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Ld2410.cmd_set_bt_password_args.buf;
    size_t cap = Ld2410.cmd_set_bt_password_args.cap;
    const char *password = Ld2410.cmd_set_bt_password_args.password;

    if (!password)
    {
        Ld2410.n = 0;
        return;
    }
    // Exactly 6 octets, natural order - the spec's worked example sends "HiLink" as 48 69 4C 69 6E 6B.
    const uint8_t v[6] = {(uint8_t)password[0], (uint8_t)password[1], (uint8_t)password[2],
                          (uint8_t)password[3], (uint8_t)password[4], (uint8_t)password[5]};
    Ld2410.n = cmd_frame(buf, cap, 0x00A9, v, 6);
}

// --- command-ACK decoding --------------------------------------------------
static void ld2410_parse_ack(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *f = Ld2410.parse_ack_args.frame;
    size_t len = Ld2410.parse_ack_args.len;
    Ld2410Ack *out = Ld2410.parse_ack_args.out;

    // layout: four header bytes, two length bytes, two command-word bytes, two status bytes, optional data, four footer
    // bytes
    if (!f || !out || len < 14)
    {
        Ld2410.ok = PROTO_FALSE;
        return;
    }
    for (int k = 0; k < 4; k++)
    {
        if (f[k] != CMD_HDR[k])
        {
            Ld2410.ok = PROTO_FALSE;
            return;
        }
    }
    size_t dl = (size_t)f[4] | ((size_t)f[5] << 8); // intra-frame length: word + status + data
    if (dl < 4 || len != 4 + 2 + dl + 4)
    {
        Ld2410.ok = PROTO_FALSE;
        return; // the declared length must account for exactly this frame
    }
    for (int k = 0; k < 4; k++)
    {
        if (f[6 + dl + k] != CMD_FTR[k])
        {
            Ld2410.ok = PROTO_FALSE;
            return;
        }
    }
    out->command = (uint16_t)((uint16_t)f[6] | ((uint16_t)f[7] << 8));
    out->status = (uint16_t)((uint16_t)f[8] | ((uint16_t)f[9] << 8));
    out->payload_len = dl - 4;
    out->payload = out->payload_len ? f + 10 : NULL;
    Ld2410.ok = PROTO_TRUE;
}

static void ld2410_ack_ok(uint8_t *restrict work)
{
    (void)work;
    const Ld2410Ack *ack = Ld2410.ack_ok_args.ack;

    Ld2410.ok = ack && ack->status == 0;
}

static void ld2410_ack_mac(uint8_t *restrict work)
{
    (void)work;
    const Ld2410Ack *ack = Ld2410.ack_mac_args.ack;
    uint8_t *mac = Ld2410.ack_mac_args.mac;

    if (!ack || !mac || ack->command != 0x01A5 || ack->status != 0 || ack->payload_len < 6 || !ack->payload)
    {
        Ld2410.ok = PROTO_FALSE;
        return;
    }
    for (int k = 0; k < 6; k++)
    {
        mac[k] = ack->payload[k];
    }
    Ld2410.ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// UART binding
// ---------------------------------------------------------------------------

#if PROTOCORE_HAS_BUS

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
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define LD2410_OFF_CTX 0u
static_assert(LD2410_OFF_CTX + sizeof(Ld2410Ctx) <= PROTOCORE_LD2410_BORROW,
              "PROTOCORE_LD2410_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(LD2410_OFF_CTX % _Alignof(Ld2410Ctx) == 0,
              "LD2410_OFF_CTX is not a multiple of alignof(Ld2410Ctx) - LD2410_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define LD2410_CTX(w) ((Ld2410Ctx *)(void *)((w) + LD2410_OFF_CTX))

static void ld2410_begin(uint8_t *restrict work)
{
    int rx_pin = Ld2410.begin_args.rx_pin;
    int tx_pin = Ld2410.begin_args.tx_pin;

    Ld2410.stream_reset_args.s = &LD2410_CTX(work)->stream;
    ld2410_stream_reset(work);
    LD2410_CTX(work)->have = PROTO_FALSE;
    Ld2410.ok = protocore_uart_begin((uint8_t)PROTOCORE_LD2410_UART, PROTOCORE_LD2410_BAUD, rx_pin, tx_pin);
}

static void ld2410_poll(uint8_t *restrict work)
{

    proto_bool fresh = PROTO_FALSE;
    size_t n =
        protocore_uart_read((uint8_t)PROTOCORE_LD2410_UART, LD2410_CTX(work)->rx, sizeof(LD2410_CTX(work)->rx), 0);
    for (size_t i = 0; i < n; i++)
    {
        Ld2410Report r;
        Ld2410.stream_push_args.s = &LD2410_CTX(work)->stream;
        Ld2410.stream_push_args.byte = LD2410_CTX(work)->rx[i];
        Ld2410.stream_push_args.out = &r;
        ld2410_stream_push(work);
        if (Ld2410.ok)
        {
            LD2410_CTX(work)->last = r;
            LD2410_CTX(work)->have = PROTO_TRUE;
            fresh = PROTO_TRUE;
        }
    }
    Ld2410.ok = fresh;
}

static void ld2410_last(uint8_t *restrict work)
{

    Ld2410.report = LD2410_CTX(work)->have ? &LD2410_CTX(work)->last : NULL;
}

// A configuration change is three frames: open the config window, carry the change, close it.
// Each is built into the owned command buffer and put on the wire before the next is built.
static proto_bool send_cmd(uint8_t *restrict work, size_t n)
{
    return n > 0 && protocore_uart_write((uint8_t)PROTOCORE_LD2410_UART, LD2410_CTX(work)->cmd, n);
}

static void ld2410_set_engineering(uint8_t *restrict work)
{
    proto_bool on = Ld2410.set_engineering_args.on;

    Ld2410.cmd_config_enable_args.buf = LD2410_CTX(work)->cmd;
    Ld2410.cmd_config_enable_args.cap = sizeof(LD2410_CTX(work)->cmd);
    ld2410_cmd_config_enable(work);
    proto_bool ok = send_cmd(work, Ld2410.n);
    Ld2410.cmd_engineering_args.buf = LD2410_CTX(work)->cmd;
    Ld2410.cmd_engineering_args.cap = sizeof(LD2410_CTX(work)->cmd);
    Ld2410.cmd_engineering_args.on = on;
    ld2410_cmd_engineering(work);
    ok &= send_cmd(work, Ld2410.n);
    Ld2410.cmd_config_end_args.buf = LD2410_CTX(work)->cmd;
    Ld2410.cmd_config_end_args.cap = sizeof(LD2410_CTX(work)->cmd);
    ld2410_cmd_config_end(work);
    ok &= send_cmd(work, Ld2410.n);
    Ld2410.ok = ok;
}

static void ld2410_restart(uint8_t *restrict work)
{

    Ld2410.cmd_config_enable_args.buf = LD2410_CTX(work)->cmd;
    Ld2410.cmd_config_enable_args.cap = sizeof(LD2410_CTX(work)->cmd);
    ld2410_cmd_config_enable(work);
    proto_bool ok = send_cmd(work, Ld2410.n);
    Ld2410.cmd_restart_args.buf = LD2410_CTX(work)->cmd;
    Ld2410.cmd_restart_args.cap = sizeof(LD2410_CTX(work)->cmd);
    ld2410_cmd_restart(work);
    ok &= send_cmd(work, Ld2410.n);
    Ld2410.cmd_config_end_args.buf = LD2410_CTX(work)->cmd;
    Ld2410.cmd_config_end_args.cap = sizeof(LD2410_CTX(work)->cmd);
    ld2410_cmd_config_end(work);
    ok &= send_cmd(work, Ld2410.n);
    Ld2410.ok = ok;
}

#else // no bus seam. The codec above is host-tested.

proto_bool protocore_ld2410_begin(int rx_pin, int tx_pin)
{
    (void)rx_pin;
    (void)tx_pin;
    return PROTO_FALSE;
}
proto_bool protocore_ld2410_poll(void)
{
    return PROTO_FALSE;
}
const Ld2410Report *protocore_ld2410_last(void)
{
    return NULL;
}
proto_bool protocore_ld2410_set_engineering(proto_bool on)
{
    (void)on;
    return PROTO_FALSE;
}
proto_bool protocore_ld2410_restart(void)
{
    return PROTO_FALSE;
}

#endif // PROTOCORE_HAS_BUS

Ld2410Ns Ld2410 = {.parse_report = ld2410_parse_report,
                   .stream_reset = ld2410_stream_reset,
                   .stream_push = ld2410_stream_push,
                   .present = ld2410_present,
                   .distance_cm = ld2410_distance_cm,
                   .cmd_config_enable = ld2410_cmd_config_enable,
                   .cmd_config_end = ld2410_cmd_config_end,
                   .cmd_engineering = ld2410_cmd_engineering,
                   .cmd_restart = ld2410_cmd_restart,
                   .cmd_bluetooth = ld2410_cmd_bluetooth,
                   .cmd_get_mac = ld2410_cmd_get_mac,
                   .cmd_set_bt_password = ld2410_cmd_set_bt_password,
                   .parse_ack = ld2410_parse_ack,
                   .ack_ok = ld2410_ack_ok,
                   .ack_mac = ld2410_ack_mac,
                   .begin = ld2410_begin,
                   .poll = ld2410_poll,
                   .last = ld2410_last,
                   .set_engineering = ld2410_set_engineering,
                   .restart = ld2410_restart};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_LD2410
