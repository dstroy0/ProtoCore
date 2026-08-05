// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file bus_host.c
 * @brief Host-side bus capture - implementation. See bus_host.h.
 *
 * Two pools and a transfer log, all fixed: the writes a driver made, the bytes a test queued for
 * it to read, and one record per transfer naming what it addressed. A read past the queued bytes
 * hands back zeros, which is what an absent device clocks out.
 */

#include "services/peripherals/bus_host.h"
#include "shared_primitives/rawmemcpy.h" // proto_raw_read: the span moves

#if !PROTOCORE_HOT

// All capture state, owned by one instance (internal linkage): the bytes written, the bytes
// queued to be read, the transfer log, and how many transfers are still set to fail.
typedef struct
{
    uint8_t wr[PC_BUS_HOST_POOL];
    size_t wr_len;
    uint8_t rd[PC_BUS_HOST_POOL];
    size_t rd_len;
    size_t rd_pos;
    pc_bus_host_txn log[PC_BUS_HOST_MAX_TXN];
    size_t log_len;
    size_t fail;
} BusHostCtx;
static BusHostCtx s_bus = {.wr = {0}, .wr_len = 0, .rd = {0}, .rd_len = 0, .rd_pos = 0, .log_len = 0, .fail = 0};

void pc_bus_host_reset(void)
{
    s_bus.wr_len = 0;
    s_bus.rd_len = 0;
    s_bus.rd_pos = 0;
    s_bus.log_len = 0;
    s_bus.fail = 0;
}

void pc_bus_host_load(const uint8_t *bytes, size_t len)
{
    if (bytes == NULL)
    {
        return;
    }
    size_t room = sizeof(s_bus.rd) - s_bus.rd_len;
    size_t n = len < room ? len : room;
    proto_raw_read(&s_bus.rd[s_bus.rd_len], bytes, n);
    s_bus.rd_len += n;
}

const uint8_t *pc_bus_host_tx(size_t *len)
{
    if (len != NULL)
    {
        *len = s_bus.wr_len;
    }
    return s_bus.wr;
}

size_t pc_bus_host_count(void)
{
    return s_bus.log_len;
}

const pc_bus_host_txn *pc_bus_host_txn_at(size_t i)
{
    return i < s_bus.log_len ? &s_bus.log[i] : NULL;
}

const uint8_t *pc_bus_host_txn_bytes(size_t i, size_t *len)
{
    if (i >= s_bus.log_len)
    {
        return NULL;
    }
    if (len != NULL)
    {
        *len = s_bus.log[i].wlen;
    }
    return &s_bus.wr[s_bus.log[i].woff];
}

size_t pc_bus_host_avail(void)
{
    return s_bus.rd_len - s_bus.rd_pos;
}

void pc_bus_host_fail_next(size_t n)
{
    s_bus.fail = n;
}

// Record one transfer. Returns 0 when the test asked for a failure or the pools are full, which
// is what a driver sees from a device that does not answer.
static int record(uint8_t kind, uint16_t target, const uint8_t *w, size_t wlen, size_t rlen)
{
    if (s_bus.fail > 0)
    {
        s_bus.fail--;
        return 0;
    }
    if (s_bus.log_len >= PC_BUS_HOST_MAX_TXN || s_bus.wr_len + wlen > sizeof(s_bus.wr))
    {
        return 0;
    }
    pc_bus_host_txn *t = &s_bus.log[s_bus.log_len];
    t->kind = kind;
    t->target = target;
    t->woff = (uint16_t)s_bus.wr_len;
    t->wlen = (uint16_t)wlen;
    t->rlen = (uint16_t)rlen;
    if (w != NULL && wlen > 0)
    {
        proto_raw_read(&s_bus.wr[s_bus.wr_len], w, wlen);
        s_bus.wr_len += wlen;
    }
    s_bus.log_len++;
    return 1;
}

// Hand out the next queued bytes, zero-filling past the end.
static void deliver(uint8_t *buf, size_t len)
{
    if (buf == NULL)
    {
        return;
    }
    for (size_t i = 0; i < len; i++)
    {
        buf[i] = s_bus.rd_pos < s_bus.rd_len ? s_bus.rd[s_bus.rd_pos] : 0u;
        if (s_bus.rd_pos < s_bus.rd_len)
        {
            s_bus.rd_pos++;
        }
    }
}

int pc_bus_host_write(uint8_t kind, uint16_t target, const uint8_t *buf, size_t len)
{
    return record(kind, target, buf, len, 0);
}

int pc_bus_host_read(uint8_t kind, uint16_t target, uint8_t *buf, size_t len)
{
    if (!record(kind, target, NULL, 0, len))
    {
        return 0;
    }
    deliver(buf, len);
    return 1;
}

int pc_bus_host_write_read(uint8_t kind, uint16_t target, const uint8_t *w, size_t wlen, uint8_t *r, size_t rlen)
{
    if (!record(kind, target, w, wlen, rlen))
    {
        return 0;
    }
    deliver(r, rlen);
    return 1;
}

#endif // !PROTOCORE_HOT
