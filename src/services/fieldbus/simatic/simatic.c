// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file simatic.c
 * @brief Siemens SIMATIC serial: 3964R link protocol + RK512 telegrams. See simatic.h.
 */

#include "services/fieldbus/simatic/simatic.h"

#if PC_ENABLE_SIMATIC

#include <string.h>

// ---------------------------------------------------------------------------
// Big-endian word helpers (Siemens words are big-endian; no stdlib)
// ---------------------------------------------------------------------------

static inline void wr_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

static inline uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

// ---------------------------------------------------------------------------
// 3964R block framing
// ---------------------------------------------------------------------------

uint8_t pc_3964r_bcc(const uint8_t *data, size_t len)
{
    uint8_t x = 0;
    for (size_t i = 0; i < len; i++)
    {
        x ^= data[i];
    }
    return x;
}

size_t pc_3964r_build_block(uint8_t *buf, size_t cap, const uint8_t *data, size_t len, proto_bool with_bcc)
{
    if (!buf || (!data && len))
    {
        return 0;
    }
    size_t o = 0;
    for (size_t i = 0; i < len; i++)
    {
        if (o >= cap)
        {
            return 0;
        }
        buf[o++] = data[i];
        if (data[i] == SIMATIC_DLE) // transparency: a payload DLE is doubled
        {
            if (o >= cap)
            {
                return 0;
            }
            buf[o++] = SIMATIC_DLE;
        }
    }
    if (o + 2 > cap)
    {
        return 0;
    }
    buf[o++] = SIMATIC_DLE;
    buf[o++] = SIMATIC_ETX;
    if (with_bcc)
    {
        if (o >= cap)
        {
            return 0;
        }
        buf[o] = pc_3964r_bcc(buf, o); // XOR over the stuffed data + DLE ETX
        o++;
    }
    return o;
}

// Append one destuffed payload byte; false when the caller's buffer is full.
static proto_bool put_byte(uint8_t *out, size_t out_cap, size_t *oo, uint8_t b)
{
    if (*oo >= out_cap)
    {
        return PROTO_FALSE;
    }
    out[(*oo)++] = b;
    return PROTO_TRUE;
}

// DLE ETX consumed at @p i: the trailing BCC must be present and match the XOR over the
// stuffed data + DLE ETX.
static proto_bool bcc_ok(const uint8_t *buf, size_t i, size_t len, proto_bool with_bcc)
{
    if (!with_bcc)
    {
        return PROTO_TRUE;
    }
    if (i >= len)
    {
        return PROTO_FALSE; // missing BCC
    }
    return pc_3964r_bcc(buf, i) == buf[i];
}

proto_bool pc_3964r_parse_block(const uint8_t *buf, size_t len, proto_bool with_bcc, uint8_t *out, size_t out_cap,
                                size_t *out_len)
{
    if (!buf || !out || !out_len)
    {
        return PROTO_FALSE;
    }
    size_t oo = 0;
    size_t i = 0;
    while (i < len)
    {
        uint8_t b = buf[i];
        if (b != SIMATIC_DLE)
        {
            if (!put_byte(out, out_cap, &oo, b))
            {
                return PROTO_FALSE;
            }
            i++;
            continue;
        }
        if (i + 1 >= len)
        {
            return PROTO_FALSE; // dangling DLE (truncated)
        }
        uint8_t n = buf[i + 1];
        if (n == SIMATIC_DLE) // doubled -> one literal DLE
        {
            if (!put_byte(out, out_cap, &oo, SIMATIC_DLE))
            {
                return PROTO_FALSE;
            }
            i += 2;
            continue;
        }
        if (n != SIMATIC_ETX)
        {
            return PROTO_FALSE; // DLE + illegal control byte
        }
        i += 2; // terminator
        if (!bcc_ok(buf, i, len, with_bcc))
        {
            return PROTO_FALSE;
        }
        *out_len = oo;
        return PROTO_TRUE;
    }
    return PROTO_FALSE; // no DLE ETX terminator
}

// ---------------------------------------------------------------------------
// 3964R link state machine
// ---------------------------------------------------------------------------

static inline void emit(Simatic3964Ctx *ctx, uint8_t b)
{
    if (ctx->tx)
    {
        ctx->tx(ctx->user, b);
    }
}

static void send_stx_await_conn(Simatic3964Ctx *ctx, uint32_t now_ms)
{
    emit(ctx, SIMATIC_STX);
    ctx->state = SIMATIC3964_STATE_TX_AWAIT_CONN;
    ctx->deadline_ms = now_ms + PC_SIMATIC_QVZ_MS;
}

static void send_block(Simatic3964Ctx *ctx, uint32_t now_ms)
{
    for (size_t i = 0; i < ctx->txlen; i++)
    {
        emit(ctx, ctx->txbuf[i]);
    }
    ctx->state = SIMATIC3964_STATE_TX_AWAIT_END;
    ctx->deadline_ms = now_ms + PC_SIMATIC_QVZ_MS;
}

static void begin_receive(Simatic3964Ctx *ctx, uint32_t now_ms)
{
    emit(ctx, SIMATIC_DLE); // ready
    ctx->state = SIMATIC3964_STATE_RX_COLLECT;
    ctx->rxpos = 0;
    ctx->prev_dle = PROTO_FALSE;
    ctx->await_bcc = PROTO_FALSE;
    ctx->deadline_ms = now_ms + PC_SIMATIC_ZVZ_MS;
}

void pc_3964r_init(Simatic3964Ctx *ctx, proto_bool high_priority, proto_bool with_bcc, Simatic3964TxFn tx,
                   Simatic3964RxFn rx, void *user)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = SIMATIC3964_STATE_IDLE;
    ctx->high_priority = high_priority;
    ctx->with_bcc = with_bcc;
    ctx->tx = tx;
    ctx->rx = rx;
    ctx->user = user;
}

proto_bool pc_3964r_send(Simatic3964Ctx *ctx, const uint8_t *data, size_t len, uint32_t now_ms)
{
    if (ctx->state != SIMATIC3964_STATE_IDLE)
    {
        return PROTO_FALSE;
    }
    size_t n = pc_3964r_build_block(ctx->txbuf, sizeof(ctx->txbuf), data, len, ctx->with_bcc);
    if (n == 0)
    {
        return PROTO_FALSE;
    }
    ctx->txlen = n;
    ctx->block_retries = 0;
    ctx->conn_retries = 0;
    send_stx_await_conn(ctx, now_ms);
    return PROTO_TRUE;
}

static void deliver_or_nak(Simatic3964Ctx *ctx)
{
    uint8_t out[PC_SIMATIC_BLOCK_MAX];
    size_t olen = 0;
    if (pc_3964r_parse_block(ctx->rxbuf, ctx->rxpos, ctx->with_bcc, out, sizeof(out), &olen))
    {
        emit(ctx, SIMATIC_DLE); // ack the received block
        // Return to IDLE BEFORE the delivery callback: a request/response peer replies from inside rx (e.g.
        // an RK512 FETCH -> a reaction telegram), and pc_3964r_send requires an idle link.
        ctx->state = SIMATIC3964_STATE_IDLE;
        if (ctx->rx)
        {
            ctx->rx(ctx->user, out, olen);
        }
    }
    else
    {
        emit(ctx, SIMATIC_NAK); // bad framing / BCC
        ctx->state = SIMATIC3964_STATE_IDLE;
    }
}

static void rx_collect_byte(Simatic3964Ctx *ctx, uint8_t b, uint32_t now_ms)
{
    if (ctx->rxpos >= sizeof(ctx->rxbuf))
    {
        emit(ctx, SIMATIC_NAK); // overflow -> reject
        ctx->state = SIMATIC3964_STATE_IDLE;
        return;
    }
    ctx->rxbuf[ctx->rxpos++] = b;
    ctx->deadline_ms = now_ms + PC_SIMATIC_ZVZ_MS;

    if (ctx->await_bcc) // this byte was the BCC that follows DLE ETX
    {
        deliver_or_nak(ctx);
        return;
    }
    if (ctx->prev_dle)
    {
        ctx->prev_dle = PROTO_FALSE;
        if (b == SIMATIC_DLE)
        {
            return; // doubled literal DLE
        }
        if (b == SIMATIC_ETX)
        {
            if (ctx->with_bcc)
            {
                ctx->await_bcc = PROTO_TRUE; // one more byte (BCC) then finalize
            }
            else
            {
                deliver_or_nak(ctx);
            }
            return;
        }
        // DLE + illegal control byte -> framing error
        emit(ctx, SIMATIC_NAK);
        ctx->state = SIMATIC3964_STATE_IDLE;
        return;
    }
    if (b == SIMATIC_DLE)
    {
        ctx->prev_dle = PROTO_TRUE;
    }
}

void pc_3964r_rx_byte(Simatic3964Ctx *ctx, uint8_t b, uint32_t now_ms)
{
    switch (ctx->state)
    {
    case SIMATIC3964_STATE_IDLE:
        if (b == SIMATIC_STX)
        {
            begin_receive(ctx, now_ms);
        }
        break;
    case SIMATIC3964_STATE_TX_AWAIT_CONN:
        if (b == SIMATIC_DLE) // connect acknowledged
        {
            send_block(ctx, now_ms);
        }
        else if (b == SIMATIC_STX) // collision: low-priority station yields to receive
        {
            if (!ctx->high_priority)
            {
                begin_receive(ctx, now_ms);
            }
            // high-priority: ignore, keep awaiting our connect DLE (the partner yields)
        }
        else if (b == SIMATIC_NAK)
        {
            if (++ctx->conn_retries < SIMATIC_MAX_CONN_RETRY)
            {
                send_stx_await_conn(ctx, now_ms);
            }
            else
            {
                ctx->state = SIMATIC3964_STATE_IDLE; // give up
            }
        }
        break;
    case SIMATIC3964_STATE_TX_AWAIT_END:
        if (b == SIMATIC_DLE) // block acknowledged -> done
        {
            ctx->state = SIMATIC3964_STATE_IDLE;
        }
        else if (b == SIMATIC_NAK)
        {
            if (++ctx->block_retries < SIMATIC_MAX_BLOCK_RETRY)
            {
                send_stx_await_conn(ctx, now_ms); // repeat the block (from STX)
            }
            else
            {
                ctx->state = SIMATIC3964_STATE_IDLE;
            }
        }
        break;
    case SIMATIC3964_STATE_RX_COLLECT:
        rx_collect_byte(ctx, b, now_ms);
        break;
    }
}

void pc_3964r_tick(Simatic3964Ctx *ctx, uint32_t now_ms)
{
    if (ctx->state == SIMATIC3964_STATE_IDLE)
    {
        return;
    }
    if ((int32_t)(now_ms - ctx->deadline_ms) < 0)
    {
        return; // not yet expired
    }
    switch (ctx->state)
    {
    case SIMATIC3964_STATE_TX_AWAIT_CONN:
        if (++ctx->conn_retries < SIMATIC_MAX_CONN_RETRY)
        {
            send_stx_await_conn(ctx, now_ms);
        }
        else
        {
            ctx->state = SIMATIC3964_STATE_IDLE;
        }
        break;
    case SIMATIC3964_STATE_TX_AWAIT_END:
        if (++ctx->block_retries < SIMATIC_MAX_BLOCK_RETRY)
        {
            send_stx_await_conn(ctx, now_ms);
        }
        else
        {
            ctx->state = SIMATIC3964_STATE_IDLE;
        }
        break;
    case SIMATIC3964_STATE_RX_COLLECT:
        emit(ctx, SIMATIC_NAK); // ZVZ inter-char timeout -> abort receive
        ctx->state = SIMATIC3964_STATE_IDLE;
        break;
    default:
        break;
    }
}

proto_bool pc_3964r_idle(const Simatic3964Ctx *ctx)
{
    return ctx->state == SIMATIC3964_STATE_IDLE;
}

// ---------------------------------------------------------------------------
// RK512 telegrams (big-endian words). Field ORDER + BE encoding are the spec invariants; the exact
// command / area byte values are verify-against-the-CP-manual (noted in the header + roadmap).
// ---------------------------------------------------------------------------

// Request header: [cmd, coord=0, area, dbnr, addr_hi, addr_lo, count_hi, count_lo]  (8 bytes)
#define RK512_HDR_LEN 8

size_t pc_rk512_build_send(uint8_t *buf, size_t cap, Rk512Area area, uint8_t dbnr, uint16_t addr, const uint16_t *words,
                           uint16_t wcount)
{
    if (!buf || (!words && wcount))
    {
        return 0;
    }
    size_t need = RK512_HDR_LEN + (size_t)wcount * 2;
    if (need > cap)
    {
        return 0;
    }
    buf[0] = (uint8_t)RK512_CMD_SEND;
    buf[1] = 0x00; // coordination / follow-up flag (single block)
    buf[2] = (uint8_t)area;
    buf[3] = dbnr;
    wr_u16(buf + 4, addr);
    wr_u16(buf + 6, wcount);
    for (uint16_t i = 0; i < wcount; i++)
    {
        wr_u16(buf + RK512_HDR_LEN + (size_t)i * 2, words[i]);
    }
    return need;
}

size_t pc_rk512_build_fetch(uint8_t *buf, size_t cap, Rk512Area area, uint8_t dbnr, uint16_t addr, uint16_t wcount)
{
    if (!buf || cap < RK512_HDR_LEN)
    {
        return 0;
    }
    buf[0] = (uint8_t)RK512_CMD_FETCH;
    buf[1] = 0x00;
    buf[2] = (uint8_t)area;
    buf[3] = dbnr;
    wr_u16(buf + 4, addr);
    wr_u16(buf + 6, wcount);
    return RK512_HDR_LEN;
}

// Reaction: [cmd=REACTION, status_hi, status_lo]  (+ FETCH-response data words appended by the caller)
size_t pc_rk512_build_reaction(uint8_t *buf, size_t cap, uint16_t status)
{
    if (!buf || cap < 3)
    {
        return 0;
    }
    buf[0] = (uint8_t)RK512_CMD_REACTION;
    wr_u16(buf + 1, status);
    return 3;
}

static proto_bool area_valid(uint8_t a)
{
    return a >= (uint8_t)RK512_AREA_DB && a <= (uint8_t)RK512_AREA_TB;
}

proto_bool pc_rk512_parse_header(const uint8_t *buf, size_t len, Rk512Header *out)
{
    if (!buf || !out || len < RK512_HDR_LEN)
    {
        return PROTO_FALSE;
    }
    uint8_t cmd = buf[0];
    if (cmd != (uint8_t)RK512_CMD_SEND && cmd != (uint8_t)RK512_CMD_FETCH)
    {
        return PROTO_FALSE;
    }
    if (!area_valid(buf[2]))
    {
        return PROTO_FALSE;
    }
    out->cmd = (Rk512Cmd)cmd;
    out->area = (Rk512Area)buf[2];
    out->dbnr = buf[3];
    out->addr = rd_u16(buf + 4);
    out->count = rd_u16(buf + 6);
    return PROTO_TRUE;
}

proto_bool pc_rk512_parse_reaction(const uint8_t *buf, size_t len, uint16_t *status, const uint8_t **data, size_t *dlen)
{
    if (!buf || !status || len < 3)
    {
        return PROTO_FALSE;
    }
    if (buf[0] != (uint8_t)RK512_CMD_REACTION)
    {
        return PROTO_FALSE;
    }
    *status = rd_u16(buf + 1);
    if (data)
    {
        *data = (len > 3) ? buf + 3 : NULL;
    }
    if (dlen)
    {
        *dlen = len - 3;
    }
    return PROTO_TRUE;
}

#endif // PC_ENABLE_SIMATIC
