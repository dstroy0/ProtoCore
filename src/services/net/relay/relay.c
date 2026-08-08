// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file relay.c
 * @brief TCP relay / DNAT byte pump implementation (see relay.h).
 */

#include "relay.h"

#if PC_ENABLE_RELAY

// Read one non-blocking chunk from src and forward it to dst. Sets *src_eof on a src seam error;
// returns -1 on a dst send error, else 0. A zero-length read leaves the buffers untouched.
static int pump_refill(pc_relay_end *src, pc_relay_end *dst, uint8_t *buf, uint16_t *len, uint16_t *off,
                       proto_bool *src_eof, uint32_t *counter)
{
    int r = src->recv(src->ctx, buf, PC_RELAY_BUF);
    if (r < 0)
    {
        *src_eof = PROTO_TRUE;
        return 0;
    }
    if (r > 0)
    {
        *len = (uint16_t)r;
        int s = dst->send(dst->ctx, buf, *len);
        if (s < 0)
        {
            return -1;
        }
        *off = (uint16_t)s;
        *counter += (uint32_t)s;
    }
    return 0;
}

// Pump one direction (src -> dst) one non-blocking pass: flush pending bytes, then read more.
// @param dst_shut_sent  the "shutdown already called" flag for @p dst (the peer that stops receiving
//                       once this direction finishes). Returns -1 on a seam error, else 0.
static int pump(pc_relay_end *src, pc_relay_end *dst, uint8_t *buf, uint16_t *len, uint16_t *off, proto_bool *src_eof,
                proto_bool *dir_done, proto_bool *dst_shut_sent, uint32_t *counter)
{
    if (*dir_done)
    {
        return 0;
    }

    // 1. flush whatever is already buffered for dst
    if (*off < *len)
    {
        int s = dst->send(dst->ctx, buf + *off, (size_t)(*len - *off));
        if (s < 0)
        {
            return -1;
        }
        *off = (uint16_t)(*off + s);
        *counter += (uint32_t)s;
    }

    // 2. buffer drained: read more from src (unless it already hit EOF), then try to send it right
    //    away so one step moves data end to end
    if (*off >= *len)
    {
        *off = 0;
        *len = 0;
        if (!*src_eof && pump_refill(src, dst, buf, len, off, src_eof, counter) < 0)
        {
            return -1;
        }
    }

    // 3. this direction is finished once src is at EOF and nothing is left to flush
    if (*src_eof && *off >= *len)
    {
        *dir_done = PROTO_TRUE;
        // The `!*dst_shut_sent` == false side is unreachable: *dst_shut_sent is written only inside
        // this block (right below), which can execute at most once per direction over a pc_relay's
        // lifetime - *dir_done just went true on the line above, and the guard at the top of this
        // function (`if (*dir_done) return 0;`) skips this whole block on every later call for that
        // direction. pc_relay_init() zero-fills the struct, so *dst_shut_sent starts false, and
        // nothing outside this block (and outside pc_relay_init's memset) ever writes it. So on
        // this - the only - entry, *dst_shut_sent cannot already be true.
        if (dst->shutdown && !*dst_shut_sent)
        {
            dst->shutdown(dst->ctx); // propagate the half-close to the peer that stops receiving
            *dst_shut_sent = PROTO_TRUE;
        }
    }
    return 0;
}

void pc_relay_init(pc_relay *r, const pc_relay_end *client, const pc_relay_end *origin)
{
    if (!r || !client || !origin)
    {
        return;
    }
    memset(r, 0, sizeof(*r));
    r->a = *client;
    r->b = *origin;
}

pc_relay_status pc_relay_step(pc_relay *r)
{
    if (!r)
    {
        return PC_RELAY_ERROR;
    }
    // a -> b: dst is b, so b's shutdown fires when this direction finishes
    if (pump(&r->a, &r->b, r->buf_a2b, &r->a2b_len, &r->a2b_off, &r->a_eof, &r->a2b_done, &r->b_shut_sent,
             &r->bytes_a2b) < 0)
    {
        return PC_RELAY_ERROR;
    }
    // b -> a: dst is a
    if (pump(&r->b, &r->a, r->buf_b2a, &r->b2a_len, &r->b2a_off, &r->b_eof, &r->b2a_done, &r->a_shut_sent,
             &r->bytes_b2a) < 0)
    {
        return PC_RELAY_ERROR;
    }

    return (r->a2b_done && r->b2a_done) ? PC_RELAY_DONE : PC_RELAY_RUNNING;
}

void pc_relay_note_eof(pc_relay *r, proto_bool origin)
{
    if (!r)
    {
        return;
    }
    // The next pc_relay_step drains that source's buffered bytes, then finishes its direction.
    if (origin)
    {
        r->b_eof = PROTO_TRUE;
    }
    else
    {
        r->a_eof = PROTO_TRUE;
    }
}

#endif // PC_ENABLE_RELAY
