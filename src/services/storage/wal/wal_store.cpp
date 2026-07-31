// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_wal_store.cpp
 * @brief A/B superblock + checkpoint + mount/recover over a block-device seam (see pc_wal_store.h).
 */

#include "services/storage/wal/wal_store.h"

#if PC_ENABLE_WAL

#include <string.h>

#include "shared_primitives/endian.h"

namespace
{
// SUPER_USED bytes are CRC-covered; the u32 CRC follows immediately.
const size_t SUPER_USED = 28;

// Exact-length device I/O: any short move is a failure.
bool dev_read(const WalDev &d, uint64_t off, uint8_t *buf, size_t len)
{
    return d.read && d.read(d.ctx, off, buf, len) == len;
}
bool dev_write(const WalDev &d, uint64_t off, const uint8_t *buf, size_t len)
{
    return d.write && d.write(d.ctx, off, buf, len) == len;
}

// Encode a superblock into a WAL_SUPER_SIZE scratch and write it to copy `ab` (0 or 1).
bool write_super(WalStore *s, int ab, uint64_t gen, uint64_t head, uint64_t seq)
{
    uint8_t sb[WAL_SUPER_SIZE];
    memset(sb, 0, sizeof(sb));
    pc_wr32le(sb + 0, WAL_SUPER_MAGIC);
    pc_wr64le(sb + 4, gen);
    pc_wr64le(sb + 12, head);
    pc_wr64le(sb + 20, seq);
    pc_wr32le(sb + 28, pc_wal_crc32(sb, SUPER_USED));
    return dev_write(s->dev, (uint64_t)ab * WAL_SUPER_SIZE, sb, sizeof(sb));
}

// Read + validate a superblock copy. On a good CRC+magic fills gen/head/seq and returns true.
bool read_super(const WalStore *s, int ab, uint64_t &gen, uint64_t &head, uint64_t &seq)
{
    uint8_t sb[WAL_SUPER_SIZE];
    if (!dev_read(s->dev, (uint64_t)ab * WAL_SUPER_SIZE, sb, sizeof(sb)))
    {
        return false;
    }
    if (pc_rd32le(sb + 0) != WAL_SUPER_MAGIC)
    {
        return false;
    }
    if (pc_rd32le(sb + 28) != pc_wal_crc32(sb, SUPER_USED))
    {
        return false;
    }
    gen = pc_rd64le(sb + 4);
    head = pc_rd64le(sb + 12);
    seq = pc_rd64le(sb + 20);
    // A head past the data region is corruption a matching CRC cannot happen for, but guard anyway.
    if (head > s->data_cap)
    {
        return false;
    }
    return true;
}

// Replay records appended after the committed head: each is CRC self-validating, so recover them one by
// one and stop at the first torn / short / non-record. Advances s->head and s->next_seq.
void pc_wal_replay_tail(WalStore *s)
{
    uint8_t hdr[WAL_RECORD_HEADER];
    uint8_t chunk[256];
    for (;;)
    {
        uint64_t off = s->head;
        if (off + WAL_RECORD_HEADER > s->data_cap)
        {
            break;
        }
        if (!dev_read(s->dev, s->data_off + off, hdr, WAL_RECORD_HEADER))
        {
            break;
        }
        if (pc_rd32le(hdr + 0) != WAL_MAGIC)
        {
            break;
        }
        uint64_t seq = pc_rd64le(hdr + 4);
        uint32_t plen = pc_rd32le(hdr + 12);
        uint32_t crc_stored = pc_rd32le(hdr + 16);
        if (off + (uint64_t)WAL_RECORD_HEADER + plen > s->data_cap)
        {
            break; // truncated tail
        }
        // CRC over the 16 header bytes then the payload, streamed in small chunks.
        uint32_t crc = pc_wal_crc32_update(pc_wal_crc32_init(), hdr, 16);
        uint64_t pos = s->data_off + off + WAL_RECORD_HEADER;
        uint32_t left = plen;
        bool read_ok = true;
        while (left)
        {
            size_t n = left < sizeof(chunk) ? left : sizeof(chunk);
            if (!dev_read(s->dev, pos, chunk, n))
            {
                read_ok = false;
                break;
            }
            crc = pc_wal_crc32_update(crc, chunk, n);
            pos += n;
            left -= (uint32_t)n;
        }
        if (!read_ok || pc_wal_crc32_final(crc) != crc_stored)
        {
            break; // torn / corrupt record - this is the durable end
        }
        s->head = off + (uint64_t)WAL_RECORD_HEADER + plen;
        if (seq + 1 > s->next_seq)
        {
            s->next_seq = seq + 1;
        }
    }
}
} // namespace

bool pc_wal_store_format(WalStore *s, const WalDev *dev)
{
    if (!dev || dev->size <= WAL_DATA_OFFSET)
    {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->dev = *dev;
    s->data_off = WAL_DATA_OFFSET;
    s->data_cap = dev->size - WAL_DATA_OFFSET;
    s->head = 0;
    s->committed = 0;
    s->next_seq = 0;
    s->gen = 1;
    s->ab = 0;
    // Invalidate copy B, then commit copy A as the live generation-1 superblock.
    uint8_t zero[WAL_SUPER_SIZE];
    memset(zero, 0, sizeof(zero));
    if (!dev_write(s->dev, (uint64_t)1 * WAL_SUPER_SIZE, zero, sizeof(zero)))
    {
        return false;
    }
    if (!write_super(s, 0, 1, 0, 0))
    {
        return false;
    }
    return s->dev.sync ? s->dev.sync(s->dev.ctx) : true;
}

bool pc_wal_store_mount(WalStore *s, const WalDev *dev)
{
    if (!dev || dev->size <= WAL_DATA_OFFSET)
    {
        return false;
    }
    memset(s, 0, sizeof(*s));
    s->dev = *dev;
    s->data_off = WAL_DATA_OFFSET;
    s->data_cap = dev->size - WAL_DATA_OFFSET;

    uint64_t genA = 0;
    uint64_t headA = 0;
    uint64_t seqA = 0;
    uint64_t genB = 0;
    uint64_t headB = 0;
    uint64_t seqB = 0;
    bool okA = read_super(s, 0, genA, headA, seqA);
    bool okB = read_super(s, 1, genB, headB, seqB);
    if (!okA && !okB)
    {
        return false; // unformatted or both torn
    }

    // Newest valid superblock wins (higher generation).
    if (okA && (!okB || genA >= genB))
    {
        s->ab = 0;
        s->gen = genA;
        s->committed = headA;
        s->next_seq = seqA;
    }
    else
    {
        s->ab = 1;
        s->gen = genB;
        s->committed = headB;
        s->next_seq = seqB;
    }
    s->head = s->committed;

    // Recover any records appended after the committed head (each CRC self-validating).
    pc_wal_replay_tail(s);
    return true;
}

bool pc_wal_store_append(WalStore *s, const uint8_t *payload, uint32_t len)
{
    uint64_t need = (uint64_t)WAL_RECORD_HEADER + len;
    if (s->head + need > s->data_cap)
    {
        return false; // log full
    }
    // Assemble the 20-byte header (magic+seq+len+crc); CRC covers header + payload without buffering both.
    uint8_t hdr[WAL_RECORD_HEADER];
    pc_wr32le(hdr + 0, WAL_MAGIC);
    pc_wr64le(hdr + 4, s->next_seq);
    pc_wr32le(hdr + 12, len);
    uint32_t crc = pc_wal_crc32_update(pc_wal_crc32_init(), hdr, 16);
    crc = pc_wal_crc32_update(crc, payload, len);
    pc_wr32le(hdr + 16, pc_wal_crc32_final(crc));

    uint64_t at = s->data_off + s->head;
    if (!dev_write(s->dev, at, hdr, WAL_RECORD_HEADER))
    {
        return false;
    }
    if (len && !dev_write(s->dev, at + WAL_RECORD_HEADER, payload, len))
    {
        return false;
    }
    s->head += need;
    s->next_seq++;
    return true;
}

bool pc_wal_store_checkpoint(WalStore *s)
{
    // Data first: the appended records must be durable before the pointer that commits them advances.
    if (s->dev.sync && !s->dev.sync(s->dev.ctx))
    {
        return false;
    }
    int target = 1 - s->ab;
    uint64_t gen = s->gen + 1;
    if (!write_super(s, target, gen, s->head, s->next_seq))
    {
        return false;
    }
    if (s->dev.sync && !s->dev.sync(s->dev.ctx))
    {
        return false;
    }
    // The superblock flip is the commit point.
    s->gen = gen;
    s->ab = target;
    s->committed = s->head;
    return true;
}

size_t pc_wal_store_scan(WalStore *s, WalStoreRecordCb cb, void *ctx, uint8_t *scratch, size_t scratch_len)
{
    if (scratch_len < WAL_RECORD_HEADER)
    {
        return 0;
    }
    size_t count = 0;
    uint64_t off = 0;
    while (off + WAL_RECORD_HEADER <= s->head)
    {
        if (!dev_read(s->dev, s->data_off + off, scratch, WAL_RECORD_HEADER))
        {
            break;
        }
        if (pc_rd32le(scratch) != WAL_MAGIC)
        {
            break;
        }
        uint32_t plen = pc_rd32le(scratch + 12);
        size_t total = (size_t)WAL_RECORD_HEADER + plen;
        if (off + total > s->head || total > scratch_len)
        {
            break; // truncated within the log, or a record too large for the caller's scratch
        }
        if (!dev_read(s->dev, s->data_off + off, scratch, total))
        {
            break;
        }
        uint32_t crc = pc_wal_crc32_update(pc_wal_crc32_init(), scratch, 16);
        crc = pc_wal_crc32_update(crc, scratch + WAL_RECORD_HEADER, plen);
        if (pc_wal_crc32_final(crc) != pc_rd32le(scratch + 16))
        {
            break;
        }
        if (cb)
        {
            cb(pc_rd64le(scratch + 4), off, scratch + WAL_RECORD_HEADER, plen, ctx);
        }
        count++;
        off += total;
    }
    return count;
}

bool pc_wal_store_pread(WalStore *s, uint64_t off, uint8_t *buf, size_t len)
{
    if (off + len > s->data_cap)
    {
        return false;
    }
    return dev_read(s->dev, s->data_off + off, buf, len);
}

#endif // PC_ENABLE_WAL
