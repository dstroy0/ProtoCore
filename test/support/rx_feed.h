// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

// Feed bytes into a connection's rx ring, the way the wire would.

#ifndef PROTOCORE_TEST_RX_FEED_H
#define PROTOCORE_TEST_RX_FEED_H

#include <stddef.h>
#include <stdint.h>

// TcpConn, conn_pool and RX_BUF_SIZE, named here rather than left to the includer: a suite that
// pulled them in through some other header broke the moment they moved to common.h.
#include "network_drivers/transport/tcp/common/common.h"
#include "rx_feed.h"

// Lay connection @p c's ring fresh and empty, with nothing owed to the window.
static inline void rx_reset(TcpConn *c)
{
    (void)EMBED_CALL(anularis.init, AnularisCfg, .ring = &c->rx, .buf = c->rx_buffer, .capacity = RX_BUF_SIZE,
                     .segment_count = 1u);
    c->rx_unacked = 0;
}

// Lay connection @p c's ring when no pool init has reached it. A laid ring holds its buffer's
// address, so a ring of all zero bytes is one nothing has laid.
static inline void rx_ensure(TcpConn *c)
{
    for (size_t i = 0; i < sizeof(c->rx.opaque); i++)
    {
        if (c->rx.opaque[i] != 0u)
        {
            return;
        }
    }
    rx_reset(c);
}

// Append the byte @p b to connection @p c's ring, the way the wire would.
static inline void rx_put(TcpConn *c, uint8_t b)
{
    rx_ensure(c);
    (void)EMBED_CALL(anularis.put, AnularisCfg, .ring = &c->rx, .src = &b, .bytes = 1u);
}

// The bytes waiting in connection @p c's ring.
static inline size_t rx_used(TcpConn *c)
{
    rx_ensure(c);
    return EMBED_CALL(anularis.available, AnularisCfg, .ring = &c->rx);
}

// The bytes connection @p c's ring still has room for.
static inline size_t rx_vacant(TcpConn *c)
{
    rx_ensure(c);
    return EMBED_CALL(anularis.vacant, AnularisCfg, .ring = &c->rx);
}

// Advance connection @p c's empty ring by @p n bytes, so the next bytes land @p n past where they would have.
static inline void rx_advance(TcpConn *c, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        rx_put(c, 0u);
    }
    EMBED_CALL(anularis.consume, AnularisCfg, .ring = &c->rx, .bytes = n);
}

// Take the next byte from connection @p c's ring, or 0 when it is empty.
static inline uint8_t rx_take(TcpConn *c)
{
    uint8_t b = 0u;
    rx_ensure(c);
    (void)EMBED_CALL(anularis.read_byte, AnularisCfg, .ring = &c->rx, .dst = &b);
    return b;
}

// Append @p len bytes to slot @p slot's rx ring.
static void push_bytes(uint8_t slot, const uint8_t *b, size_t len)
{
    TcpConn *c = &conn_pool[slot];
    for (size_t i = 0; i < len; i++)
    {
        rx_put(c, b[i]);
    }
}

// Append the NUL-terminated @p s to slot @p slot's rx ring.
static void push_str(uint8_t slot, const char *s)
{
    size_t n = 0;
    while (s[n])
    {
        n++;
    }
    push_bytes(slot, (const uint8_t *)s, n);
}

#endif
