// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file telnet.c
 * @brief Minimal RFC 854 Telnet server implementation.
 */

#include "network_drivers/presentation/telnet/telnet.h"

#if PC_ENABLE_TELNET

#include "network_drivers/session/proto_handler.h"
#include "network_drivers/transport/tcp.h"
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
    TN_SB
} TelnetState;

typedef struct
{
    uint8_t slot;
    proto_bool used;
    TelnetState st; // IAC parser state
    uint8_t cmd;    // pending WILL/WONT/DO/DONT
    uint16_t len;   // bytes in line[]
    char line[TELNET_BUF_SIZE];
} TelnetConn;

// All telnet presentation state, owned by one instance (internal linkage): the per-slot
// connection table and the command callback. One named owner, unreachable cross-TU.
typedef struct
{
    TelnetConn tn[MAX_TELNET_CONNS];
    TelnetCommandCb cmd_cb;
} TelnetCtx;
static TelnetCtx s_telnet;

static TelnetConn *find_conn(uint8_t slot)
{
    for (int i = 0; i < MAX_TELNET_CONNS; i++)
    {
        if (s_telnet.tn[i].used && s_telnet.tn[i].slot == slot)
        {
            return &s_telnet.tn[i];
        }
    }
    return NULL;
}

static void raw_send(uint8_t slot, const void *data, size_t n)
{
    if (!pc_conn_active(slot) ||
        // fixed nonzero literal length. (Marker must sit on this line: gcov attributes
        // the whole multi-line condition's branches to the "if" line, not the operand's
        // own line - a marker on the next line silently fails to exclude anything.)
        n == 0)
    {
        return;
    }
    Tcp.conn->send(slot, data, (proto_u16)n);
    Tcp.conn->flush(slot);
}

// Send Telnet *data* (echo + application output): a literal IAC byte (0xFF) MUST be
// doubled so the client does not read it as a command introducer (RFC 854). Sends
// runs of non-IAC bytes directly and emits "\xff\xff" for each IAC. Protocol commands
// (IAC WILL/DO/...) use raw_send directly - they send IAC intentionally.
static void send_escaped(uint8_t slot, const void *data, size_t n)
{
    if (!pc_conn_active(slot) || n == 0)
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
                Tcp.conn->send(slot, b + start, (proto_u16)(i - start));
            }
            Tcp.conn->send(slot, "\xff\xff", 2); // doubled IAC
            start = i + 1;
        }
    }
    if (n > start)
    {
        Tcp.conn->send(slot, b + start, (proto_u16)(n - start));
    }
    Tcp.conn->flush(slot);
}

// ---------------------------------------------------------------------------
// Connection lifecycle (called from the session layer)
// ---------------------------------------------------------------------------

void pc_telnet_accept(uint8_t slot)
{
    TelnetConn *t = NULL;
    for (int i = 0; i < MAX_TELNET_CONNS; i++)
    {
        if (!s_telnet.tn[i].used)
        {
            t = &s_telnet.tn[i];
            break;
        }
    }
    if (!t)
    {
        // No Telnet capacity: drop the connection (transport owns the teardown).
        Tcp.conn->close(slot);
        return;
    }
    memset(t, 0, sizeof(*t));
    t->used = PROTO_TRUE;
    t->slot = slot;
    t->st = TN_NORMAL;

    // Server-side echo + character-at-a-time (suppress go-ahead).
    static const uint8_t neg[] = {T_IAC, T_WILL, OPT_ECHO, T_IAC, T_WILL, OPT_SGA};
    raw_send(slot, neg, sizeof(neg));
    raw_send(slot, "PC Telnet ready\r\n> ", 22);
}

void pc_telnet_close(uint8_t slot)
{
    TelnetConn *t = find_conn(slot);
    if (t)
    {
        t->used = PROTO_FALSE;
    }
}

// Process one decoded data byte (not part of an IAC sequence).
static void handle_data(uint8_t slot, TelnetConn *t, uint8_t b)
{
    if (b == '\r')
    {
        return; // wait for the LF of CRLF
    }
    if (b == '\n')
    {
        t->line[t->len] = '\0';
        raw_send(slot, "\r\n", 2);
        if (s_telnet.cmd_cb)
        {
            s_telnet.cmd_cb(t->line, (uint8_t)(t - s_telnet.tn));
        }
        t->len = 0;
        raw_send(slot, "> ", 2);
        return;
    }
    if (b == 0x08 || b == 0x7F) // backspace / delete
    {
        if (t->len > 0)
        {
            t->len--;
            raw_send(slot, "\b \b", 3);
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
        send_escaped(slot, &b, 1); // echo (doubles a literal IAC per RFC 854)
    }
}

void pc_telnet_rx(uint8_t slot)
{
    TelnetConn *t = find_conn(slot);
    if (!t)
    {
        return;
    }

    uint8_t b;
    while (pc_conn_read_byte(slot, &b))
    {
        switch (t->st)
        // (TN_NORMAL/TN_IAC/TN_OPT/TN_SB) has a case below; the compiler's defensive "no case matched"
        // branch can't be reached from any host input
        {
        case TN_NORMAL:
            if (b == T_IAC)
            {
                t->st = TN_IAC;
            }
            else
            {
                handle_data(slot, t, b);
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
                handle_data(slot, t, 0xFF); // escaped literal 0xFF
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
                raw_send(slot, resp, 3);
            }
            t->st = TN_NORMAL;
            break;
        }
        case TN_SB:
            if (b == T_SE)
            {
                t->st = TN_NORMAL; // end of subnegotiation (contents ignored)
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Application API
// ---------------------------------------------------------------------------

void pc_telnet_on_command(TelnetCommandCb cb)
{
    s_telnet.cmd_cb = cb;
}

static void broadcast(const char *s, size_t n)
{
    for (int i = 0; i < MAX_TELNET_CONNS; i++)
    {
        if (s_telnet.tn[i].used)
        {
            send_escaped(s_telnet.tn[i].slot, s, n); // app output: escape IAC (RFC 854)
        }
    }
}

void pc_telnet_print(const char *s)
{
    if (s)
    {
        broadcast(s, strnlen(s, TELNET_BUF_SIZE)); // line-oriented console, same cap as pc_telnet_printf
    }
}

void pc_telnet_println(const char *s)
{
    if (s)
    {
        broadcast(s, strnlen(s, TELNET_BUF_SIZE));
    }
    broadcast("\r\n", 2);
}

void pc_telnet_frame(const pc_field *spec, ...)
{
    char buf[TELNET_BUF_SIZE];
    va_list ap;
    va_start(ap, spec);
    size_t n = pc_frame_vbuild(buf, sizeof(buf), spec, ap);
    va_end(ap);
    if (n > 0)
    {
        broadcast(buf, n);
    }
}

uint8_t pc_telnet_client_count()
{
    uint8_t c = 0;
    for (int i = 0; i < MAX_TELNET_CONNS; i++)
    {
        if (s_telnet.tn[i].used)
        {
            c++;
        }
    }
    return c;
}

// The Telnet ProtoHandler (Layer 5 dispatch seam) - installed by proto_register_builtins() via this
// accessor, so this module carries no dependency on the session layer.
static const ProtoHandler s_telnet_handler = {pc_telnet_accept, pc_telnet_rx, pc_telnet_close, NULL};
const ProtoHandler *pc_telnet_proto_handler(void)
{
    return &s_telnet_handler;
}

#endif // PC_ENABLE_TELNET
