// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file telnet.c
 * @brief Minimal RFC 854 Telnet server implementation.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_TELNET

#include "mmgr/plaintext/plaintext.h"   // the persistent end this module's state is taken from
#include "mmgr/protoframe/protoframe.h" // frame.build: a console line is a spec, not a format string
#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h" // str: the bounded-run walks
#include "network_drivers/presentation/telnet/telnet.h"

#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool: the slot a call acts on
#include "server/core/proto_handler.h"
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
    TelnetCommandCb cmd_cb;  ///< what a completed line is handed to
    Nvt *conn;               ///< the row bound to the slot a call names, or NULL
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define TELNET_OFF_CTX 0u
static_assert(TELNET_OFF_CTX + sizeof(struct TelnetStorage) <= PROTOCORE_TELNET_BORROW,
              "PROTOCORE_TELNET_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define TELNET_CTX(w) ((struct TelnetStorage *)(void *)((w) + TELNET_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_TELNET_BORROW persistent bytes
} TelnetOwnCtx;
static TelnetOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_telnet_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_TELNET_BORROW).buf;
    }
    return s_own.span;
}

// Point TELNET_CTX(work)->conn at the row bound to ns->slot, or NULL.
static void find_conn(uint8_t *restrict work)
{
    TELNET_CTX(work)->conn = NULL;
    for (int i = 0; i < MAX_TELNET_CONNS; i++)
    {
        if (TELNET_CTX(work)->tn[i].used && TELNET_CTX(work)->tn[i].slot == TelnetV.slot)
        {
            TELNET_CTX(work)->conn = &TELNET_CTX(work)->tn[i];
            return;
        }
    }
}

static void command_send(uint8_t slot, const void *data, size_t n)
{
    ConnPoolV.slot = slot;
    ConnPool.active(protocore_conn_pool_span());
    if (!ConnPoolV.ok ||
        // fixed nonzero literal length. (Marker must sit on this line: gcov attributes
        // the whole multi-line condition's branches to the "if" line, not the operand's
        // own line - a marker on the next line silently fails to exclude anything.)
        n == 0)
    {
        return;
    }
    ConnPoolV.io.data = data;
    ConnPoolV.io.len = (proto_u16)n;
    ConnPool.send(protocore_conn_pool_span());
    ConnPool.flush(protocore_conn_pool_span());
}

// Send Telnet *data* (echo + application output): a literal IAC byte (0xFF) MUST be
// doubled so the client does not read it as a command introducer (RFC 854). Sends
// runs of non-IAC bytes directly and emits "\xff\xff" for each IAC. Protocol commands
// (IAC WILL/DO/...) use command_send directly - they send IAC intentionally.
static void data_send(uint8_t slot, const void *data, size_t n)
{
    ConnPoolV.slot = slot;
    ConnPool.active(protocore_conn_pool_span());
    if (!ConnPoolV.ok || n == 0)
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
                ConnPoolV.io.data = b + start;
                ConnPoolV.io.len = (proto_u16)(i - start);
                ConnPool.send(protocore_conn_pool_span());
            }
            ConnPoolV.io.data = "\xff\xff"; // doubled IAC
            ConnPoolV.io.len = 2;
            ConnPool.send(protocore_conn_pool_span());
            start = i + 1;
        }
    }
    if (n > start)
    {
        ConnPoolV.io.data = b + start;
        ConnPoolV.io.len = (proto_u16)(n - start);
        ConnPool.send(protocore_conn_pool_span());
    }
    ConnPool.flush(protocore_conn_pool_span());
}

// ---------------------------------------------------------------------------
// Connection lifecycle (called from the session layer)
// ---------------------------------------------------------------------------

void protocore_telnet_accept(uint8_t *restrict work)
{
    Nvt *t = NULL;
    for (int i = 0; i < MAX_TELNET_CONNS; i++)
    {
        if (!TELNET_CTX(work)->tn[i].used)
        {
            t = &TELNET_CTX(work)->tn[i];
            break;
        }
    }
    if (!t)
    {
        // No Telnet capacity: drop the connection (transport owns the teardown).
        ConnPoolV.slot = TelnetV.slot;
        ConnPool.close(protocore_conn_pool_span());
        return;
    }
    mem.set(t, 0, sizeof(*t));
    t->used = PROTO_TRUE;
    t->slot = TelnetV.slot;
    t->st = TN_NORMAL;
    TELNET_CTX(work)->conn = t;

    // Server-side echo + character-at-a-time (suppress go-ahead).
    static const uint8_t neg[] = {T_IAC, T_WILL, OPT_ECHO, T_IAC, T_WILL, OPT_SGA};
    command_send(TelnetV.slot, neg, sizeof(neg));
    static const char greet[] = "PC Telnet ready\r\n> ";
    command_send(TelnetV.slot, greet, sizeof(greet) - 1);
}

void protocore_telnet_close(uint8_t *restrict work)
{
    find_conn(work);
    if (TELNET_CTX(work)->conn)
    {
        TELNET_CTX(work)->conn->used = PROTO_FALSE;
    }
}

// Terminate the line, echo the newline, hand it to the application, and prompt again.
static void line_dispatch(uint8_t slot, Nvt *t)
{
    t->line[t->len] = '\0';
    command_send(slot, "\r\n", 2);
    if (TELNET_CTX(protocore_telnet_span())->cmd_cb != NULL)
    {
        TELNET_CTX(protocore_telnet_span())->cmd_cb(t->line, (uint8_t)(t - TELNET_CTX(protocore_telnet_span())->tn));
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
void protocore_telnet_rx(uint8_t *restrict work)
{
    find_conn(work);
    if (!TELNET_CTX(work)->conn)
    {
        return;
    }
    const uint8_t slot = TelnetV.slot;
    Nvt *t = TELNET_CTX(work)->conn;

    ConnPoolV.slot = slot;
    ConnPoolV.io.buf = TELNET_CTX(work)->rx;
    ConnPoolV.io.cap = sizeof(TELNET_CTX(work)->rx);
    ConnPool.read(protocore_conn_pool_span());

    for (size_t i = 0; i < ConnPoolV.n; i++)
    {
        const uint8_t b = TELNET_CTX(work)->rx[i];
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

void protocore_telnet_on_command(uint8_t *restrict work)
{
    TELNET_CTX(work)->cmd_cb = TelnetV.cb;
}

// RFC 854: application output travels the NVT data stream, so a literal IAC is doubled on the way.
static void broadcast(uint8_t *restrict work, const char *s, size_t n)
{
    for (int i = 0; i < MAX_TELNET_CONNS; i++)
    {
        if (TELNET_CTX(work)->tn[i].used)
        {
            data_send(TELNET_CTX(work)->tn[i].slot, s, n);
        }
    }
}

void protocore_telnet_print(uint8_t *restrict work)
{
    if (TelnetV.out.text)
    {
        broadcast(work, TelnetV.out.text, str.len(TelnetV.out.text, TELNET_BUF_SIZE)); // line-oriented console
    }
}

void protocore_telnet_println(uint8_t *restrict work)
{
    if (TelnetV.out.text)
    {
        broadcast(work, TelnetV.out.text, str.len(TelnetV.out.text, TELNET_BUF_SIZE));
    }
    broadcast(work, "\r\n", 2);
}

void protocore_telnet_frame(uint8_t *restrict work)
{
    char buf[TELNET_BUF_SIZE];
    size_t n = frame.build(buf, sizeof(buf), TelnetV.out.spec, TelnetV.out.val, TelnetV.out.nv);
    if (n > 0)
    {
        broadcast(work, buf, n);
    }
}

void protocore_telnet_client_count(uint8_t *restrict work)
{
    uint8_t c = 0;
    for (int i = 0; i < MAX_TELNET_CONNS; i++)
    {
        if (TELNET_CTX(work)->tn[i].used)
        {
            c++;
        }
    }
    TelnetV.u8 = c;
}

// The session layer's seam dictates these shapes, so they stay as they are and carry the slot onto
// the handle before the call that does the work.
static void evt_accept(uint8_t slot)
{
    TelnetV.slot = slot;
    protocore_telnet_accept(protocore_telnet_span());
}
static void evt_rx(uint8_t slot)
{
    TelnetV.slot = slot;
    protocore_telnet_rx(protocore_telnet_span());
}
static void evt_close(uint8_t slot)
{
    TelnetV.slot = slot;
    protocore_telnet_close(protocore_telnet_span());
}

// The Telnet ProtoHandler (Layer 5 dispatch seam) - installed by the builtins list through this
// accessor, so this module carries no dependency on the session layer.
// Designated, so a member's position in the struct does not decide what it binds to. on_abort and
// on_poll are unset: a null on_abort falls back to on_close, and this protocol is not polled.
static const ProtoHandler s_telnet_handler = {.on_accept = evt_accept, .on_data = evt_rx, .on_close = evt_close};

void protocore_telnet_proto_handler(uint8_t *restrict work)
{
    (void)work;
    TelnetV.handler = &s_telnet_handler;
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
TelnetVars TelnetV;

#endif // PROTOCORE_ENABLE_TELNET
