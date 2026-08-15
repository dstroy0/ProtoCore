// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

// Feed bytes into a connection's rx ring, the way the wire would.

#ifndef PROTOCORE_TEST_RX_FEED_H
#define PROTOCORE_TEST_RX_FEED_H

#include <stddef.h>
#include <stdint.h>

// TcpConn, conn_pool and RX_BUF_SIZE, named here rather than left to the includer: a suite that
// pulled them in through some other header broke the moment they moved to common.h.
#include "network_drivers/transport/tcp/common.h"

// Append @p len bytes to slot @p slot's rx ring.
static void push_bytes(uint8_t slot, const uint8_t *b, size_t len)
{
    TcpConn *c = &conn_pool[slot];
    for (size_t i = 0; i < len; i++)
    {
        c->rx_buffer[c->rx_head] = b[i];
        c->rx_head = (c->rx_head + 1) % RX_BUF_SIZE;
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
