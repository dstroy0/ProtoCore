// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file telnet.c
 * @brief Minimal RFC 854 Telnet server implementation.
 */

#include "network_drivers/presentation/telnet/telnet.h"
#include "mmgr/protoframe.h" // frame.build: a console line is a spec, not a format string
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_TELNET

#include "server/core/proto_handler.h"
#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool: the slot a call acts on
#include <stdarg.h>

// Telnet protocol bytes (RFC 854 / 858 / 857).
#define T_SE 240
#define T_SB 250
#define T_WILL 251
#define T_WONT 252
#define T_DO 253
#define T_DONT 254
#define T_IAC 255
#define OPT_ECHO 1
#define OPT_SGA 3

// IAC parser state per connection (a mutually-exclusive state, not a wire value).
typedef enum PROTO_ENUM_PACKED
{
    TN_NORMAL,
    TN_IAC,
    TN_OPT,
    TN_SB,
    TN_SB_IAC
} TelnetState;

typedef struct
{
    uint8_t slot;
    proto_bool used;
    TelnetState st;    // IAC parser state
    uint8_t cmd;       // pending WILL/WONT/DO/DONT
    proto_bool saw_cr; // last data byte was CR: the next one completes CR LF or CR NUL
    uint16_t len;      // bytes in line[]
    char line[TELNET_BUF_SIZE];
} Nvt;

/**
 * @brief The console's compile-time storage: the per-slot connection table.
 *
 * All BSS, so a telnet client costs no heap.
 */
struct TelnetStorage
{
    Nvt tn[MAX_TELNET_CONNS];
    uint8_t rx[RX_BUF_SIZE]; ///< where a slot's bytes are staged for the IAC walk
};

/**
 * @brief The console's state and the calls that reach it - what TelnetNs points at.
 *
 * @var TelnetInternal::store   the per-slot connection table
 * @var TelnetInternal::ns      the handle a caller sets a call's members on
 * @var TelnetInternal::cmd_cb  the per-line command handler the application registered
 * @var TelnetInternal::conn    the row the private steps below act on
 */
struct TelnetInternal
{
    struct TelnetStorage *store;
    TelnetNs *ns;
    TelnetCommandCb cmd_cb;
    Nvt *conn;
};

static struct TelnetStorage s_store;

static struct TelnetInternal s_telnet = {.store = &s_store, .ns = &Telnet};

// Point ctx->conn at the row bound to ns->slot, or NULL.
static void find_conn(struct TelnetInternal *restrict ctx)
{
    ctx->conn = NULL;
    for (int i = 0; i < MAX_TELNET_CONNS; i++)
    {
        if (ctx->store->tn[i].used && ctx->store->tn[i].slot == ctx->ns->slot)
        {
            ctx->conn = &ctx->store->tn[i];
            return;
        }
    }
}

static void command_send(uint8_t slot, const void *data, size_t n)
{
    ConnPool.slot = slot;
    ConnPool.active(ConnPool.internal);
    if (!ConnPool.ok ||
        // fixed nonzero literal length. (Marker must sit on this line: gcov attributes
        // the whole multi-line condition's branches to the "if" line, not the operand's
        // own line - a marker on the next line silently fails to exclude anything.)
        n == 0)
    {
        return;
    }
    ConnPool.io.data = data;
    ConnPool.io.len = (proto_u16)n;
    ConnPool.send(ConnPool.internal);
    ConnPool.flush(ConnPool.internal);
}

// Send Telnet *data* (echo + application output): a literal IAC byte (0xFF) MUST be
// doubled so the client does not read it as a command introducer (RFC 854). Sends
// runs of non-IAC bytes directly and emits "\xff\xff" for each IAC. Protocol commands
// (IAC WILL/DO/...) use command_send directly - they send IAC intentionally.
static void data_send(uint8_t slot, const void *data, size_t n)
{
    ConnPool.slot = slot;
    ConnPool.active(ConnPool.internal);
    if (!ConnPool.ok || n == 0)
    {
        return;
    }
    const uint8_t *b = (const uint8_t *)data;
    size_t start = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (b[i] == 0xFF)
        {
            if (i > start)
            {
                ConnPool.io.data = b + start;
                ConnPool.io.len = (proto_u16)(i - start);
                ConnPool.send(ConnPool.internal);
            }
            ConnPool.io.data = "\xff\xff"; // doubled IAC
            ConnPool.io.len = 2;
            ConnPool.send(ConnPool.internal);
            start = i + 1;
        }
    }
    if (n > start)
    {
        ConnPool.io.data = b + start;
        ConnPool.io.len = (proto_u16)(n - start);
        ConnPool.send(ConnPool.internal);
    }
    ConnPool.flush(ConnPool.internal);
}

// ---------------------------------------------------------------------------
// Connection lifecycle (called from the session layer)
// ---------------------------------------------------------------------------

static void accept_conn(struct TelnetInternal *restrict ctx)
{
    Nvt *t = NULL;
    for (int i = 0; i < MAX_TELNET_CONNS; i++)
    {
        if (!ctx->store->tn[i].used)
        {
            t = &ctx->store->tn[i];
            break;
        }
    }
    if (!t)
    {
        // No Telnet capacity: drop the connection (transport owns the teardown).
        ConnPool.slot = ctx->ns->slot;
        ConnPool.close(ConnPool.internal);
        return;
    }
    mem.set(t, 0, sizeof(*t));
    t->used = PROTO_TRUE;
    t->slot = ctx->ns->slot;
    t->st = TN_NORMAL;
    ctx->conn = t;

    // Server-side echo + character-at-a-time (suppress go-ahead).
    static const uint8_t neg[] = {T_IAC, T_WILL, OPT_ECHO, T_IAC, T_WILL, OPT_SGA};
    command_send(ctx->ns->slot, neg, sizeof(neg));
    static const char greet[] = "PC Telnet ready\r\n> ";
    command_send(ctx->ns->slot, greet, sizeof(greet) - 1);
}

static void close_conn(struct TelnetInternal *restrict ctx)
{
    find_conn(ctx);
    if (ctx->conn)
    {
        ctx->conn->used = PROTO_FALSE;
    }
}

// Terminate the line, echo the newline, hand it to the application, and prompt again.
static void line_dispatch(uint8_t slot, Nvt *t)
{
    t->line[t->len] = '\0';
    command_send(slot, "\r\n", 2);
    if (s_telnet.cmd_cb != NULL)
    {
        s_telnet.cmd_cb(t->line, (uint8_t)(t - s_store.tn));
    }
    t->len = 0;
    command_send(slot, "> ", 2);
}

// Process one decoded data byte (not part of an IAC sequence).
static void nvt_data(uint8_t slot, Nvt *t, uint8_t b)
{
    // RFC 854: a CR is followed by LF for a new line or by NUL for a carriage return alone. Both end
    // the line, and the byte that completes the pair is consumed here rather than falling through to
    // the control-byte arm below, which would drop the NUL and leave the line undispatched.
    if (t->saw_cr)
    {
        t->saw_cr = PROTO_FALSE;
        if (b == '\n' || b == 0)
        {
            line_dispatch(slot, t);
            return;
        }
    }
    if (b == '\r')
    {
        t->saw_cr = PROTO_TRUE;
        return;
    }
    if (b == '\n') // a bare LF ends the line too
    {
        line_dispatch(slot, t);
        return;
    }
    if (b == 0x08 || b == 0x7F) // backspace / delete
    {
        if (t->len > 0)
        {
            t->len--;
            command_send(slot, "\b \b", 3);
        }
        return;
    }
    if (b < 0x20) // ignore other control characters
    {
        return;
    }
    if (t->len < sizeof(t->line) - 1)
    {
        t->line[t->len++] = (char)b;
        data_send(slot, &b, 1); // echo (doubles a literal IAC per RFC 854)
    }
}

// The worker fills this slot's scratch once, then the IAC state machine walks it.
static void rx(struct TelnetInternal *restrict ctx)
{
    find_conn(ctx);
    if (!ctx->conn)
    {
        return;
    }
    const uint8_t slot = ctx->ns->slot;
    Nvt *t = ctx->conn;

    ConnPool.slot = slot;
    ConnPool.io.buf = ctx->store->rx;
    ConnPool.io.cap = sizeof(ctx->store->rx);
    ConnPool.read(ConnPool.internal);

    for (size_t i = 0; i < ConnPool.n; i++)
    {
        const uint8_t b = ctx->store->rx[i];
        switch (t->st)
        // (TN_NORMAL/TN_IAC/TN_OPT/TN_SB/TN_SB_IAC) has a case below; the compiler's defensive "no case
        // matched" branch can't be reached from any host input
        {
        case TN_NORMAL:
            if (b == T_IAC)
            {
                t->st = TN_IAC;
            }
            else
            {
                nvt_data(slot, t, b);
            }
            break;
        case TN_IAC:
            if (b == T_SB)
            {
                t->st = TN_SB;
            }
            else if (b == T_WILL || b == T_WONT || b == T_DO || b == T_DONT)
            {
                t->cmd = b;
                t->st = TN_OPT;
            }
            else if (b == T_IAC)
            {
                nvt_data(slot, t, 0xFF); // escaped literal 0xFF
                t->st = TN_NORMAL;
            }
            else
            {
                t->st = TN_NORMAL; // other 2-byte command (GA, NOP, ...) - consume
            }
            break;
        case TN_OPT: {
            // Refuse what we don't actively support; stay quiet on options we
            // already offered (ECHO/SGA) to avoid negotiation loops.
            uint8_t reply = 0;
            if (t->cmd == T_DO && b != OPT_ECHO && b != OPT_SGA)
            {
                reply = T_WONT;
            }
            else if (t->cmd == T_WILL)
            {
                reply = T_DONT;
            }
            if (reply)
            {
                uint8_t resp[3] = {T_IAC, reply, b};
                command_send(slot, resp, 3);
            }
            t->st = TN_NORMAL;
            break;
        }
        case TN_SB:
            if (b == T_IAC)
            {
                t->st = TN_SB_IAC; // only IAC SE ends a subnegotiation (RFC 855); a bare 240 is data
            }
            break;
        case TN_SB_IAC:
            if (b == T_SE)
            {
                t->st = TN_NORMAL; // IAC SE closes the subnegotiation; its contents are ignored
            }
            else
            {
                t->st = TN_SB; // IAC IAC (doubled) or a stray IAC - stay in the subnegotiation
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Application API
// ---------------------------------------------------------------------------

static void on_command(struct TelnetInternal *restrict ctx)
{
    ctx->cmd_cb = ctx->ns->cb;
}

// RFC 854: application output travels the NVT data stream, so a literal IAC is doubled on the way.
static void broadcast(struct TelnetInternal *restrict ctx, const char *s, size_t n)
{
    for (int i = 0; i < MAX_TELNET_CONNS; i++)
    {
        if (ctx->store->tn[i].used)
        {
            data_send(ctx->store->tn[i].slot, s, n);
        }
    }
}

static void print(struct TelnetInternal *restrict ctx)
{
    if (ctx->ns->out.text)
    {
        broadcast(ctx, ctx->ns->out.text, strnlen(ctx->ns->out.text, TELNET_BUF_SIZE)); // line-oriented console
    }
}

static void println(struct TelnetInternal *restrict ctx)
{
    if (ctx->ns->out.text)
    {
        broadcast(ctx, ctx->ns->out.text, strnlen(ctx->ns->out.text, TELNET_BUF_SIZE));
    }
    broadcast(ctx, "\r\n", 2);
}

static void frame_out(struct TelnetInternal *restrict ctx)
{
    char buf[TELNET_BUF_SIZE];
    size_t n = frame.build(buf, sizeof(buf), ctx->ns->out.spec, ctx->ns->out.val, ctx->ns->out.nv);
    if (n > 0)
    {
        broadcast(ctx, buf, n);
    }
}

static void client_count(struct TelnetInternal *restrict ctx)
{
    uint8_t c = 0;
    for (int i = 0; i < MAX_TELNET_CONNS; i++)
    {
        if (ctx->store->tn[i].used)
        {
            c++;
        }
    }
    ctx->ns->u8 = c;
}

// The session layer's seam dictates these shapes, so they stay as they are and carry the slot onto
// the handle before the call that does the work.
static void evt_accept(uint8_t slot)
{
    Telnet.slot = slot;
    accept_conn(&s_telnet);
}
static void evt_rx(uint8_t slot)
{
    Telnet.slot = slot;
    rx(&s_telnet);
}
static void evt_close(uint8_t slot)
{
    Telnet.slot = slot;
    close_conn(&s_telnet);
}

// The Telnet ProtoHandler (Layer 5 dispatch seam) - installed by the builtins list through this
// accessor, so this module carries no dependency on the session layer.
static const ProtoHandler s_telnet_handler = {evt_accept, evt_rx, evt_close, NULL};

static void proto_handler(struct TelnetInternal *restrict ctx)
{
    ctx->ns->handler = &s_telnet_handler;
}

// Designated, so a member's position in the struct does not decide what it binds to.
TelnetNs Telnet = {.on_command = on_command,
                   .print = print,
                   .println = println,
                   .frame = frame_out,
                   .client_count = client_count,
                   .accept = accept_conn,
                   .rx = rx,
                   .close = close_conn,
                   .proto_handler = proto_handler,
                   .internal = &s_telnet};

#endif // PROTOCORE_ENABLE_TELNET
