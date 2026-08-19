// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hmmd.c
 * @brief Waveshare HMMD mmWave radar codec - implementation. See hmmd.h.
 *
 * Frame layout (little-endian throughout), per the S3KM1110 serial protocol:
 *   report:  F4 F3 F2 F1 | len(2)=35 | detect(1) | dist(2) | gate[16](2 each) | F8 F7 F6 F5
 *   command: FD FC FB FA | len(2)    | word(2)   | [value]                    | 04 03 02 01
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HMMD

#include "mmgr/protomem/protomem.h"
#include "mmgr/secure/secure.h" // the persistent end this module's state is taken from
#include "server/peripherals/hmmd/hmmd.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_HAS_BUS
#include "server/peripherals/uart.h" // the shared UART owner
#endif
static const uint8_t HDR[4] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t FTR[4] = {0xF8, 0xF7, 0xF6, 0xF5};
static const uint8_t CMD_HDR[4] = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t CMD_FTR[4] = {0x04, 0x03, 0x02, 0x01};

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_HMMD_BORROW persistent bytes
} HmmdOwnCtx;
static HmmdOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_hmmd_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_HMMD_BORROW).buf;
    }
    return s_own.span;
}

static void hmmd_cmd_build(uint8_t *restrict work);
static void hmmd_parse_report(uint8_t *restrict work);
static void hmmd_stream_push(uint8_t *restrict work);
static void hmmd_stream_reset(uint8_t *restrict work);

static void hmmd_parse_report(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *f = Hmmd.parse_report_args.frame;
    size_t len = Hmmd.parse_report_args.len;
    HmmdReport *out = Hmmd.parse_report_args.out;

    if (!f || !out || len != PROTOCORE_HMMD_FRAME_MAX)
    {
        Hmmd.ok = PROTO_FALSE;
        return;
    }
    if (mem.cmp(f, HDR, 4) != 0)
    {
        Hmmd.ok = PROTO_FALSE;
        return;
    }
    if (rd16(f + 4) != PROTOCORE_HMMD_REPORT_LEN)
    {
        Hmmd.ok = PROTO_FALSE;
        return; // the only report length this module emits
    }
    if (mem.cmp(f + 6 + PROTOCORE_HMMD_REPORT_LEN, FTR, 4) != 0)
    {
        Hmmd.ok = PROTO_FALSE;
        return;
    }

    const uint8_t *p = f + 6;
    HmmdReport r;
    mem.set(&r, 0, sizeof(r));
    r.detected = (p[0] == 0x01) ? 1u : 0u;
    r.distance_cm = rd16(p + 1);
    for (int i = 0; i < PROTOCORE_HMMD_GATES; i++)
    {
        r.gate_energy[i] = rd16(p + 3 + 2 * i);
    }
    *out = r;
    Hmmd.ok = PROTO_TRUE;
}

static void hmmd_stream_reset(uint8_t *restrict work)
{
    (void)work;
    HmmdStream *s = Hmmd.stream_reset_args.s;

    if (!s)
    {
        return;
    }
    s->pos = 0;
    s->total = 0;
    s->hdr_match = 0;
    s->phase = 0;
}

static void hmmd_stream_push(uint8_t *restrict work)
{
    (void)work;
    HmmdStream *s = Hmmd.stream_push_args.s;
    uint8_t b = Hmmd.stream_push_args.byte;
    HmmdReport *out = Hmmd.stream_push_args.out;

    if (!s || !out)
    {
        Hmmd.ok = PROTO_FALSE;
        return;
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
        Hmmd.ok = PROTO_FALSE;
        return;
    case 1: // little-endian length field
        s->buf[s->pos++] = b;
        if (s->pos == 6)
        {
            // Widen before comparing: 6 + 0xFFFF + 4 wraps a uint16_t, defeating the guard.
            uint32_t total = 6u + (uint32_t)rd16(s->buf + 4) + 4u;
            if (total > PROTOCORE_HMMD_FRAME_MAX)
            {
                Hmmd.stream_reset_args.s = s;
                hmmd_stream_reset(work); // absurd length: drop and resync
                Hmmd.ok = PROTO_FALSE;
                return;
            }
            s->total = (uint16_t)total;
            s->phase = 2;
        }
        Hmmd.ok = PROTO_FALSE;
        return;
    default: // body + footer
        s->buf[s->pos++] = b;
        if (s->pos >= s->total)
        {
            Hmmd.parse_report_args.frame = s->buf;
            Hmmd.parse_report_args.len = s->total;
            Hmmd.parse_report_args.out = out;
            hmmd_parse_report(work);
            proto_bool ok = Hmmd.ok;
            Hmmd.stream_reset_args.s = s;
            hmmd_stream_reset(work);
            Hmmd.ok = ok;
            return;
        }
        Hmmd.ok = PROTO_FALSE;
        return;
    }
}

static void hmmd_present(uint8_t *restrict work)
{
    (void)work;
    const HmmdReport *r = Hmmd.present_args.r;

    Hmmd.ok = r && r->detected != 0;
}

static void hmmd_distance_cm(uint8_t *restrict work)
{
    (void)work;
    const HmmdReport *r = Hmmd.distance_cm_args.r;

    Hmmd.cm = (r && r->detected) ? r->distance_cm : 0;
}

// --- command encoders ------------------------------------------------------

static void hmmd_cmd_build(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Hmmd.cmd_build_args.buf;
    size_t cap = Hmmd.cmd_build_args.cap;
    uint16_t word = Hmmd.cmd_build_args.word;
    const uint8_t *value = Hmmd.cmd_build_args.value;
    size_t vlen = Hmmd.cmd_build_args.vlen;

    if (vlen && !value)
    {
        Hmmd.n = 0;
        return;
    }
    size_t need = 4 + 2 + 2 + vlen + 4;
    if (!buf || cap < need)
    {
        Hmmd.n = 0;
        return;
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
    Hmmd.n = i;
}

static void hmmd_cmd_open(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Hmmd.cmd_open_args.buf;
    size_t cap = Hmmd.cmd_open_args.cap;

    static const uint8_t v[2] = {0x01, 0x00}; // value 0x0001
    Hmmd.cmd_build_args.buf = buf;
    Hmmd.cmd_build_args.cap = cap;
    Hmmd.cmd_build_args.word = 0x00FF;
    Hmmd.cmd_build_args.value = v;
    Hmmd.cmd_build_args.vlen = 2;
    hmmd_cmd_build(work);
}

static void hmmd_cmd_close(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Hmmd.cmd_close_args.buf;
    size_t cap = Hmmd.cmd_close_args.cap;

    Hmmd.cmd_build_args.buf = buf;
    Hmmd.cmd_build_args.cap = cap;
    Hmmd.cmd_build_args.word = 0x00FE;
    Hmmd.cmd_build_args.value = NULL;
    Hmmd.cmd_build_args.vlen = 0;
    hmmd_cmd_build(work);
}

static void hmmd_cmd_read_firmware(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Hmmd.cmd_read_firmware_args.buf;
    size_t cap = Hmmd.cmd_read_firmware_args.cap;

    Hmmd.cmd_build_args.buf = buf;
    Hmmd.cmd_build_args.cap = cap;
    Hmmd.cmd_build_args.word = 0x0000;
    Hmmd.cmd_build_args.value = NULL;
    Hmmd.cmd_build_args.vlen = 0;
    hmmd_cmd_build(work);
}

static void hmmd_cmd_read_serial(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Hmmd.cmd_read_serial_args.buf;
    size_t cap = Hmmd.cmd_read_serial_args.cap;

    Hmmd.cmd_build_args.buf = buf;
    Hmmd.cmd_build_args.cap = cap;
    Hmmd.cmd_build_args.word = 0x0011;
    Hmmd.cmd_build_args.value = NULL;
    Hmmd.cmd_build_args.vlen = 0;
    hmmd_cmd_build(work);
}

static void hmmd_cmd_read_config(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Hmmd.cmd_read_config_args.buf;
    size_t cap = Hmmd.cmd_read_config_args.cap;

    Hmmd.cmd_build_args.buf = buf;
    Hmmd.cmd_build_args.cap = cap;
    Hmmd.cmd_build_args.word = 0x0008;
    Hmmd.cmd_build_args.value = NULL;
    Hmmd.cmd_build_args.vlen = 0;
    hmmd_cmd_build(work);
}

static void hmmd_cmd_read_register(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Hmmd.cmd_read_register_args.buf;
    size_t cap = Hmmd.cmd_read_register_args.cap;
    const uint8_t *value = Hmmd.cmd_read_register_args.value;
    size_t vlen = Hmmd.cmd_read_register_args.vlen;

    Hmmd.cmd_build_args.buf = buf;
    Hmmd.cmd_build_args.cap = cap;
    Hmmd.cmd_build_args.word = 0x0002;
    Hmmd.cmd_build_args.value = value;
    Hmmd.cmd_build_args.vlen = vlen;
    hmmd_cmd_build(work);
}

// --- command-ACK decoding --------------------------------------------------

static void hmmd_parse_ack(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *f = Hmmd.parse_ack_args.frame;
    size_t len = Hmmd.parse_ack_args.len;
    HmmdAck *out = Hmmd.parse_ack_args.out;

    // layout: four header bytes, two length bytes, two command-word bytes, optional data, four footer bytes
    if (!f || !out || len < 12)
    {
        Hmmd.ok = PROTO_FALSE;
        return;
    }
    if (mem.cmp(f, CMD_HDR, 4) != 0)
    {
        Hmmd.ok = PROTO_FALSE;
        return;
    }
    size_t dl = (size_t)rd16(f + 4); // command word + data
    if (dl < 2 || len != 4 + 2 + dl + 4)
    {
        Hmmd.ok = PROTO_FALSE;
        return; // the declared length must account for exactly this frame
    }
    if (mem.cmp(f + 6 + dl, CMD_FTR, 4) != 0)
    {
        Hmmd.ok = PROTO_FALSE;
        return;
    }
    out->command = rd16(f + 6);
    out->payload_len = dl - 2;
    out->payload = out->payload_len ? f + 8 : NULL;
    Hmmd.ok = PROTO_TRUE;
}

static void hmmd_ack_matches(uint8_t *restrict work)
{
    (void)work;
    const HmmdAck *ack = Hmmd.ack_matches_args.ack;
    uint16_t word = Hmmd.ack_matches_args.word;

    Hmmd.ok = ack && (uint8_t)(ack->command & 0xFF) == (uint8_t)(word & 0xFF);
}

// ---------------------------------------------------------------------------
// UART binding
// ---------------------------------------------------------------------------

#if PROTOCORE_HAS_BUS

// Bytes taken from the UART per poll. A report frame is far shorter, so one poll carries at least
// a whole frame and the read stays bounded (SRC_LAW rule 5).
#define HMMD_RX_CHUNK 64

// All HMMD UART-binding state, owned by one instance (internal linkage): the frame reassembler, the
// last decoded report, the have-report flag, and the receive chunk, grouped so it is one named
// owner unreachable from any other translation unit. The chunk is a member rather than a local
// because it is filled per poll on the request path.
typedef struct
{
    HmmdStream stream;
    HmmdReport last;
    proto_bool have;
    uint8_t rx[HMMD_RX_CHUNK];
} HmmdCtx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define HMMD_OFF_CTX 0u
static_assert(HMMD_OFF_CTX + sizeof(HmmdCtx) <= PROTOCORE_HMMD_BORROW,
              "PROTOCORE_HMMD_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define HMMD_CTX(w) ((HmmdCtx *)(void *)((w) + HMMD_OFF_CTX))

static void hmmd_begin(uint8_t *restrict work)
{
    int rx_pin = Hmmd.begin_args.rx_pin;
    int tx_pin = Hmmd.begin_args.tx_pin;

    Hmmd.stream_reset_args.s = &HMMD_CTX(work)->stream;
    hmmd_stream_reset(work);
    HMMD_CTX(work)->have = PROTO_FALSE;
    Hmmd.ok = protocore_uart_begin((uint8_t)PROTOCORE_HMMD_UART, PROTOCORE_HMMD_BAUD, rx_pin, tx_pin);
}

static void hmmd_poll(uint8_t *restrict work)
{

    proto_bool fresh = PROTO_FALSE;
    size_t n = protocore_uart_read((uint8_t)PROTOCORE_HMMD_UART, HMMD_CTX(work)->rx, sizeof(HMMD_CTX(work)->rx), 0);
    for (size_t i = 0; i < n; i++)
    {
        HmmdReport r;
        Hmmd.stream_push_args.s = &HMMD_CTX(work)->stream;
        Hmmd.stream_push_args.byte = HMMD_CTX(work)->rx[i];
        Hmmd.stream_push_args.out = &r;
        hmmd_stream_push(work);
        if (Hmmd.ok)
        {
            HMMD_CTX(work)->last = r;
            HMMD_CTX(work)->have = PROTO_TRUE;
            fresh = PROTO_TRUE;
        }
    }
    Hmmd.ok = fresh;
}

static void hmmd_last(uint8_t *restrict work)
{

    Hmmd.report = HMMD_CTX(work)->have ? &HMMD_CTX(work)->last : NULL;
}

#else // no bus seam

proto_bool protocore_hmmd_begin(int rx_pin, int tx_pin)
{
    (void)rx_pin;
    (void)tx_pin;
    return PROTO_FALSE;
}

proto_bool protocore_hmmd_poll(void)
{
    return PROTO_FALSE;
}

const HmmdReport *protocore_hmmd_last(void)
{
    return NULL;
}

#endif // PROTOCORE_HAS_BUS

HmmdNs Hmmd = {.parse_report = hmmd_parse_report,
               .stream_reset = hmmd_stream_reset,
               .stream_push = hmmd_stream_push,
               .present = hmmd_present,
               .distance_cm = hmmd_distance_cm,
               .cmd_build = hmmd_cmd_build,
               .cmd_open = hmmd_cmd_open,
               .cmd_close = hmmd_cmd_close,
               .cmd_read_firmware = hmmd_cmd_read_firmware,
               .cmd_read_serial = hmmd_cmd_read_serial,
               .cmd_read_config = hmmd_cmd_read_config,
               .cmd_read_register = hmmd_cmd_read_register,
               .parse_ack = hmmd_parse_ack,
               .ack_matches = hmmd_ack_matches,
               .begin = hmmd_begin,
               .poll = hmmd_poll,
               .last = hmmd_last};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HMMD
