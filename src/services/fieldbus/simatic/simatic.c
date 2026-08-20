// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file simatic.c
 * @brief Siemens SIMATIC serial: 3964R link protocol + RK512 telegrams. See simatic.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SIMATIC

#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "mmgr/protomem/protomem.h"
#include "services/fieldbus/simatic/simatic.h"

PROTOCORE_BEGIN_DECLS

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

// The entries this file calls before reaching their definitions.

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_SIMATIC_BORROW persistent bytes
} SimaticOwnCtx;
static SimaticOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_simatic_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_SIMATIC_BORROW).buf;
    }
    return s_own.span;
}

void protocore_simatic_bcc_3964r(uint8_t *restrict work);
void protocore_simatic_build_block_3964r(uint8_t *restrict work);

void protocore_simatic_bcc_3964r(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *data = SimaticV.bcc_3964r_args.data;
    size_t len = SimaticV.bcc_3964r_args.len;

    uint8_t x = 0;
    for (size_t i = 0; i < len; i++)
    {
        x ^= data[i];
    }
    SimaticV.value = x;
}

void protocore_simatic_build_block_3964r(uint8_t *restrict work)
{
    uint8_t *buf = SimaticV.build_block_3964r_args.buf;
    size_t cap = SimaticV.build_block_3964r_args.cap;
    const uint8_t *data = SimaticV.build_block_3964r_args.data;
    size_t len = SimaticV.build_block_3964r_args.len;
    proto_bool with_bcc = SimaticV.build_block_3964r_args.with_bcc;

    if (!buf || (!data && len))
    {
        SimaticV.n = 0;
        return;
    }
    size_t o = 0;
    for (size_t i = 0; i < len; i++)
    {
        if (o >= cap)
        {
            SimaticV.n = 0;
            return;
        }
        buf[o++] = data[i];
        if (data[i] == SIMATIC_DLE) // transparency: a payload DLE is doubled
        {
            if (o >= cap)
            {
                SimaticV.n = 0;
                return;
            }
            buf[o++] = SIMATIC_DLE;
        }
    }
    if (o + 2 > cap)
    {
        SimaticV.n = 0;
        return;
    }
    buf[o++] = SIMATIC_DLE;
    buf[o++] = SIMATIC_ETX;
    if (with_bcc)
    {
        if (o >= cap)
        {
            SimaticV.n = 0;
            return;
        }
        SimaticV.bcc_3964r_args.data = buf;
        SimaticV.bcc_3964r_args.len = o;
        protocore_simatic_bcc_3964r(work);
        buf[o] = SimaticV.value; // XOR over the stuffed data + DLE ETX
        o++;
    }
    SimaticV.n = o;
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

// The operands the private helpers below read, all of them: the widest set any one caller supplies.
typedef struct
{
    const uint8_t *buf;   ///< the block being walked
    Simatic3964Ctx *link; ///< the caller's link, for the receive-side chain
    size_t i;             ///< the walk position the DLE ETX was consumed at
    size_t len;           ///< the block length
    uint32_t now_ms;      ///< the instant a received byte arrived
    proto_bool with_bcc;  ///< the "R" (BCC) variant
    uint8_t b;            ///< the received byte
} SimaticCtx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define SIMATIC_OFF_CTX 0u
static_assert(SIMATIC_OFF_CTX + sizeof(SimaticCtx) <= PROTOCORE_SIMATIC_BORROW,
              "PROTOCORE_SIMATIC_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(SIMATIC_OFF_CTX % _Alignof(SimaticCtx) == 0,
              "SIMATIC_OFF_CTX is not a multiple of alignof(SimaticCtx) - SIMATIC_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define SIMATIC_CTX(w) ((SimaticCtx *)(void *)((w) + SIMATIC_OFF_CTX))

// DLE ETX consumed at the walk position: the trailing BCC must be present and match the XOR over
// the stuffed data + DLE ETX.
static proto_bool bcc_ok(uint8_t *restrict work)
{
    if (!SIMATIC_CTX(work)->with_bcc)
    {
        return PROTO_TRUE;
    }
    if (SIMATIC_CTX(work)->i >= SIMATIC_CTX(work)->len)
    {
        return PROTO_FALSE; // missing BCC
    }
    SimaticV.bcc_3964r_args.data = SIMATIC_CTX(work)->buf;
    SimaticV.bcc_3964r_args.len = SIMATIC_CTX(work)->i;
    protocore_simatic_bcc_3964r(work);
    return SimaticV.value == SIMATIC_CTX(work)->buf[SIMATIC_CTX(work)->i];
}

void protocore_simatic_parse_block_3964r(uint8_t *restrict work)
{
    const uint8_t *buf = SimaticV.parse_block_3964r_args.buf;
    size_t len = SimaticV.parse_block_3964r_args.len;
    proto_bool with_bcc = SimaticV.parse_block_3964r_args.with_bcc;
    uint8_t *out = SimaticV.parse_block_3964r_args.out;
    size_t out_cap = SimaticV.parse_block_3964r_args.out_cap;
    size_t *out_len = SimaticV.parse_block_3964r_args.out_len;

    if (!buf || !out || !out_len)
    {
        SimaticV.ok = PROTO_FALSE;
        return;
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
                SimaticV.ok = PROTO_FALSE;
                return;
            }
            i++;
            continue;
        }
        if (i + 1 >= len)
        {
            SimaticV.ok = PROTO_FALSE; // dangling DLE (truncated)
            return;
        }
        uint8_t n = buf[i + 1];
        if (n == SIMATIC_DLE) // doubled -> one literal DLE
        {
            if (!put_byte(out, out_cap, &oo, SIMATIC_DLE))
            {
                SimaticV.ok = PROTO_FALSE;
                return;
            }
            i += 2;
            continue;
        }
        if (n != SIMATIC_ETX)
        {
            SimaticV.ok = PROTO_FALSE; // DLE + illegal control byte
            return;
        }
        i += 2; // terminator
        SIMATIC_CTX(work)->buf = buf;
        SIMATIC_CTX(work)->i = i;
        SIMATIC_CTX(work)->len = len;
        SIMATIC_CTX(work)->with_bcc = with_bcc;
        if (!bcc_ok(work))
        {
            SimaticV.ok = PROTO_FALSE;
            return;
        }
        *out_len = oo;
        SimaticV.ok = PROTO_TRUE;
        return;
    }
    SimaticV.ok = PROTO_FALSE; // no DLE ETX terminator
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
    ctx->deadline_ms = now_ms + PROTOCORE_SIMATIC_QVZ_MS;
}

static void send_block(Simatic3964Ctx *ctx, uint32_t now_ms)
{
    for (size_t i = 0; i < ctx->txlen; i++)
    {
        emit(ctx, ctx->txbuf[i]);
    }
    ctx->state = SIMATIC3964_STATE_TX_AWAIT_END;
    ctx->deadline_ms = now_ms + PROTOCORE_SIMATIC_QVZ_MS;
}

static void begin_receive(Simatic3964Ctx *ctx, uint32_t now_ms)
{
    emit(ctx, SIMATIC_DLE); // ready
    ctx->state = SIMATIC3964_STATE_RX_COLLECT;
    ctx->rxpos = 0;
    ctx->prev_dle = PROTO_FALSE;
    ctx->await_bcc = PROTO_FALSE;
    ctx->deadline_ms = now_ms + PROTOCORE_SIMATIC_ZVZ_MS;
}

void protocore_simatic_init_3964r(uint8_t *restrict work)
{
    (void)work;
    Simatic3964Ctx *ctx = SimaticV.init_3964r_args.ctx;
    proto_bool high_priority = SimaticV.init_3964r_args.high_priority;
    proto_bool with_bcc = SimaticV.init_3964r_args.with_bcc;
    Simatic3964TxFn tx = SimaticV.init_3964r_args.tx;
    Simatic3964RxFn rx = SimaticV.init_3964r_args.rx;
    void *user = SimaticV.init_3964r_args.user;

    mem.set(ctx, 0, sizeof(*ctx));
    ctx->state = SIMATIC3964_STATE_IDLE;
    ctx->high_priority = high_priority;
    ctx->with_bcc = with_bcc;
    ctx->tx = tx;
    ctx->rx = rx;
    ctx->user = user;
}

void protocore_simatic_send_3964r(uint8_t *restrict work)
{
    Simatic3964Ctx *ctx = SimaticV.send_3964r_args.ctx;
    const uint8_t *data = SimaticV.send_3964r_args.data;
    size_t len = SimaticV.send_3964r_args.len;
    uint32_t now_ms = SimaticV.send_3964r_args.now_ms;

    if (ctx->state != SIMATIC3964_STATE_IDLE)
    {
        SimaticV.ok = PROTO_FALSE;
        return;
    }
    SimaticV.build_block_3964r_args.buf = ctx->txbuf;
    SimaticV.build_block_3964r_args.cap = sizeof(ctx->txbuf);
    SimaticV.build_block_3964r_args.data = data;
    SimaticV.build_block_3964r_args.len = len;
    SimaticV.build_block_3964r_args.with_bcc = ctx->with_bcc;
    protocore_simatic_build_block_3964r(work);
    size_t n = SimaticV.n;
    if (n == 0)
    {
        SimaticV.ok = PROTO_FALSE;
        return;
    }
    ctx->txlen = n;
    ctx->block_retries = 0;
    ctx->conn_retries = 0;
    send_stx_await_conn(ctx, now_ms);
    SimaticV.ok = PROTO_TRUE;
}

static void deliver_or_nak(uint8_t *restrict work)
{
    Simatic3964Ctx *ctx = SIMATIC_CTX(work)->link;
    uint8_t out[PROTOCORE_SIMATIC_BLOCK_MAX];
    size_t olen = 0;
    SimaticV.parse_block_3964r_args.buf = ctx->rxbuf;
    SimaticV.parse_block_3964r_args.len = ctx->rxpos;
    SimaticV.parse_block_3964r_args.with_bcc = ctx->with_bcc;
    SimaticV.parse_block_3964r_args.out = out;
    SimaticV.parse_block_3964r_args.out_cap = sizeof(out);
    SimaticV.parse_block_3964r_args.out_len = &olen;
    protocore_simatic_parse_block_3964r(work);
    if (SimaticV.ok)
    {
        emit(ctx, SIMATIC_DLE); // ack the received block
        // Return to IDLE BEFORE the delivery callback: a request/response peer replies from inside rx (e.g.
        // an RK512 FETCH -> a reaction telegram), and Simatic.send_3964r requires an idle link.
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

static void rx_collect_byte(uint8_t *restrict work)
{
    Simatic3964Ctx *ctx = SIMATIC_CTX(work)->link;
    uint8_t b = SIMATIC_CTX(work)->b;
    uint32_t now_ms = SIMATIC_CTX(work)->now_ms;
    if (ctx->rxpos >= sizeof(ctx->rxbuf))
    {
        emit(ctx, SIMATIC_NAK); // overflow -> reject
        ctx->state = SIMATIC3964_STATE_IDLE;
        return;
    }
    ctx->rxbuf[ctx->rxpos++] = b;
    ctx->deadline_ms = now_ms + PROTOCORE_SIMATIC_ZVZ_MS;

    if (ctx->await_bcc) // this byte was the BCC that follows DLE ETX
    {
        SIMATIC_CTX(work)->link = ctx;
        deliver_or_nak(work);
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
                SIMATIC_CTX(work)->link = ctx;
                deliver_or_nak(work);
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

void protocore_simatic_rx_byte_3964r(uint8_t *restrict work)
{
    Simatic3964Ctx *ctx = SimaticV.rx_byte_3964r_args.ctx;
    uint8_t b = SimaticV.rx_byte_3964r_args.b;
    uint32_t now_ms = SimaticV.rx_byte_3964r_args.now_ms;

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
        SIMATIC_CTX(work)->link = ctx;
        SIMATIC_CTX(work)->b = b;
        SIMATIC_CTX(work)->now_ms = now_ms;
        rx_collect_byte(work);
        break;
    }
}

void protocore_simatic_tick_3964r(uint8_t *restrict work)
{
    (void)work;
    Simatic3964Ctx *ctx = SimaticV.tick_3964r_args.ctx;
    uint32_t now_ms = SimaticV.tick_3964r_args.now_ms;

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

void protocore_simatic_idle_3964r(uint8_t *restrict work)
{
    (void)work;
    const Simatic3964Ctx *ctx = SimaticV.idle_3964r_args.ctx;

    SimaticV.ok = ctx->state == SIMATIC3964_STATE_IDLE;
}

// ---------------------------------------------------------------------------
// RK512 telegrams (big-endian words). Field ORDER + BE encoding are the spec invariants; the exact
// command / area byte values are verify-against-the-CP-manual (noted in the header + roadmap).
// ---------------------------------------------------------------------------

// Request header: [cmd, coord=0, area, dbnr, addr_hi, addr_lo, count_hi, count_lo]  (8 bytes)
#define RK512_HDR_LEN 8

void protocore_simatic_build_send_rk512(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = SimaticV.build_send_rk512_args.buf;
    size_t cap = SimaticV.build_send_rk512_args.cap;
    Rk512Area area = SimaticV.build_send_rk512_args.area;
    uint8_t dbnr = SimaticV.build_send_rk512_args.dbnr;
    uint16_t addr = SimaticV.build_send_rk512_args.addr;
    const uint16_t *words = SimaticV.build_send_rk512_args.words;
    uint16_t wcount = SimaticV.build_send_rk512_args.wcount;

    if (!buf || (!words && wcount))
    {
        SimaticV.n = 0;
        return;
    }
    size_t need = RK512_HDR_LEN + (size_t)wcount * 2;
    if (need > cap)
    {
        SimaticV.n = 0;
        return;
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
    SimaticV.n = need;
}

void protocore_simatic_build_fetch_rk512(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = SimaticV.build_fetch_rk512_args.buf;
    size_t cap = SimaticV.build_fetch_rk512_args.cap;
    Rk512Area area = SimaticV.build_fetch_rk512_args.area;
    uint8_t dbnr = SimaticV.build_fetch_rk512_args.dbnr;
    uint16_t addr = SimaticV.build_fetch_rk512_args.addr;
    uint16_t wcount = SimaticV.build_fetch_rk512_args.wcount;

    if (!buf || cap < RK512_HDR_LEN)
    {
        SimaticV.n = 0;
        return;
    }
    buf[0] = (uint8_t)RK512_CMD_FETCH;
    buf[1] = 0x00;
    buf[2] = (uint8_t)area;
    buf[3] = dbnr;
    wr_u16(buf + 4, addr);
    wr_u16(buf + 6, wcount);
    SimaticV.n = RK512_HDR_LEN;
}

// Reaction: [cmd=REACTION, status_hi, status_lo]  (+ FETCH-response data words appended by the caller)
void protocore_simatic_build_reaction_rk512(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = SimaticV.build_reaction_rk512_args.buf;
    size_t cap = SimaticV.build_reaction_rk512_args.cap;
    uint16_t status = SimaticV.build_reaction_rk512_args.status;

    if (!buf || cap < 3)
    {
        SimaticV.n = 0;
        return;
    }
    buf[0] = (uint8_t)RK512_CMD_REACTION;
    wr_u16(buf + 1, status);
    SimaticV.n = 3;
}

static proto_bool area_valid(uint8_t a)
{
    return a >= (uint8_t)RK512_AREA_DB && a <= (uint8_t)RK512_AREA_TB;
}

void protocore_simatic_parse_header_rk512(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = SimaticV.parse_header_rk512_args.buf;
    size_t len = SimaticV.parse_header_rk512_args.len;
    Rk512Header *out = SimaticV.parse_header_rk512_args.out;

    if (!buf || !out || len < RK512_HDR_LEN)
    {
        SimaticV.ok = PROTO_FALSE;
        return;
    }
    uint8_t cmd = buf[0];
    if (cmd != (uint8_t)RK512_CMD_SEND && cmd != (uint8_t)RK512_CMD_FETCH)
    {
        SimaticV.ok = PROTO_FALSE;
        return;
    }
    if (!area_valid(buf[2]))
    {
        SimaticV.ok = PROTO_FALSE;
        return;
    }
    out->cmd = (Rk512Cmd)cmd;
    out->area = (Rk512Area)buf[2];
    out->dbnr = buf[3];
    out->addr = rd_u16(buf + 4);
    out->count = rd_u16(buf + 6);
    SimaticV.ok = PROTO_TRUE;
}

void protocore_simatic_parse_reaction_rk512(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = SimaticV.parse_reaction_rk512_args.buf;
    size_t len = SimaticV.parse_reaction_rk512_args.len;
    uint16_t *status = SimaticV.parse_reaction_rk512_args.status;
    const uint8_t **data = SimaticV.parse_reaction_rk512_args.data;
    size_t *dlen = SimaticV.parse_reaction_rk512_args.dlen;

    if (!buf || !status || len < 3)
    {
        SimaticV.ok = PROTO_FALSE;
        return;
    }
    if (buf[0] != (uint8_t)RK512_CMD_REACTION)
    {
        SimaticV.ok = PROTO_FALSE;
        return;
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
    SimaticV.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
SimaticVars SimaticV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SIMATIC
