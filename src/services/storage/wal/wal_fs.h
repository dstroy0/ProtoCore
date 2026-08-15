// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wal_fs.h
 * @brief Bind the WAL store's ::WalDev block-device seam to a file on a mounted store (PROTOCORE_ENABLE_WAL).
 *
 * The store in wal_store.h does all I/O through three function pointers so its logic stays pure and
 * host-testable; this header is the thin adapter that points those pointers at a preallocated file on any
 * ::protocore_mnt_backend. Random access is the backend's `seek`, and the durability barrier is its `sync`.
 *
 * Usage:
 * @code
 *   const protocore_mnt_backend *store = protocore_mnt_active();
 *   protocore_wal_fs_prealloc(store, "/wal.bin", 256 * 1024);   // once: fixed-size, zero-filled backing file
 *   protocore_wal_fs_ctx c;
 *   WalDev dev;
 *   if (protocore_wal_fs_open(&c, &dev, store, "/wal.bin", 256 * 1024))
 *   {
 *       WalStore s;
 *       protocore_wal_store_mount(&s, &dev) || protocore_wal_store_format(&s, &dev);  // recover, or initialize
 *   }
 * @endcode
 *
 * The backing file is preallocated to a fixed size so every store offset lands inside it and seek+write
 * overwrites in place, rather than depending on past-EOF behavior that differs between FAT and littlefs.
 * The ::protocore_wal_fs_ctx must outlive any ::WalDev bound to it.
 *
 * A backend with no `sync` is refused at open. The WAL's checkpoint is an ordering guarantee built on that
 * barrier, so a store that cannot promise it cannot carry a power-loss-safe log, and saying so at mount is
 * the only honest answer: a barrier that returns true having done nothing makes an unsafe store look safe.
 */

#ifndef PROTOCORE_WAL_FS_H
#define PROTOCORE_WAL_FS_H

#include "mmgr/protomem.h"
#include "protocore_config.h"
#include "server/storage/mnt.h" // protocore_mnt_backend - the store the log lives on
#include "services/storage/wal/wal_store.h"

#if PROTOCORE_ENABLE_WAL

/** @brief What the adapter needs to reach one open file: the store and the handle it returned. */
typedef struct
{
    const protocore_mnt_backend *fs;
    int handle;
} protocore_wal_fs_ctx;

PROTOCORE_INLINE size_t protocore_wal_fs_read(void *ctx, uint64_t off, uint8_t *buf, size_t len)
{
    protocore_wal_fs_ctx *c = (protocore_wal_fs_ctx *)ctx;
    if (!c->fs->seek(c->handle, off))
    {
        return 0;
    }
    int n = c->fs->read(c->handle, buf, len);
    return n < 0 ? 0 : (size_t)n;
}

PROTOCORE_INLINE size_t protocore_wal_fs_write(void *ctx, uint64_t off, const uint8_t *buf, size_t len)
{
    protocore_wal_fs_ctx *c = (protocore_wal_fs_ctx *)ctx;
    if (!c->fs->seek(c->handle, off))
    {
        return 0;
    }
    int n = c->fs->write(c->handle, buf, len);
    return n < 0 ? 0 : (size_t)n;
}

PROTOCORE_INLINE proto_bool protocore_wal_fs_sync(void *ctx)
{
    protocore_wal_fs_ctx *c = (protocore_wal_fs_ctx *)ctx;
    return c->fs->sync(c->handle);
}

/**
 * @brief Ensure @p path on @p fs exists and is at least @p size bytes (created zero-filled if missing/short).
 * @return true on success. Call once before opening the file for the store.
 */
PROTOCORE_INLINE proto_bool protocore_wal_fs_prealloc(const protocore_mnt_backend *fs, const char *path, uint64_t size)
{
    if (!fs || !path)
    {
        return PROTO_FALSE;
    }
    if (fs->exists(path) && fs->size(path) >= 0 && (uint64_t)fs->size(path) >= size)
    {
        return PROTO_TRUE;
    }
    int h = fs->open(path, PROTOCORE_MNT_WRITE); // create / truncate
    if (h < 0)
    {
        return PROTO_FALSE;
    }
    uint8_t z[256];
    mem.set(z, 0, sizeof z);
    uint64_t left = size;
    proto_bool ok = PROTO_TRUE;
    while (left)
    {
        size_t n = left < sizeof z ? (size_t)left : sizeof z;
        if (fs->write(h, z, n) != (int)n)
        {
            ok = PROTO_FALSE;
            break;
        }
        left -= n;
    }
    if (ok && fs->sync)
    {
        ok = fs->sync(h);
    }
    fs->close(h);
    return ok;
}

/**
 * @brief Open @p path on @p fs and build a ::WalDev over it as a @p size-byte block device.
 *
 * @param c    filled with the store and handle; must outlive @p dev and any ::WalStore mounted on it.
 * @param dev  filled with the three function pointers and @p size.
 * @return false if @p fs has no durability barrier, or the file will not open. On false, nothing is
 *         left open and @p dev is not usable.
 */
PROTOCORE_INLINE proto_bool protocore_wal_fs_open(protocore_wal_fs_ctx *c, WalDev *dev, const protocore_mnt_backend *fs,
                                                  const char *path, uint64_t size)
{
    if (!c || !dev || !fs || !path || !fs->sync)
    {
        return PROTO_FALSE;
    }
    int h = fs->open(path, PROTOCORE_MNT_RDWR); // random read+write over the preallocated extent, no truncation
    if (h < 0)
    {
        return PROTO_FALSE;
    }
    c->fs = fs;
    c->handle = h;
    dev->read = protocore_wal_fs_read;
    dev->write = protocore_wal_fs_write;
    dev->sync = protocore_wal_fs_sync;
    dev->ctx = c;
    dev->size = size;
    return PROTO_TRUE;
}

/** @brief Close the file a ::protocore_wal_fs_ctx holds. The ::WalDev built over it is dead after this. */
PROTOCORE_INLINE void protocore_wal_fs_close(protocore_wal_fs_ctx *c)
{
    if (c && c->fs)
    {
        c->fs->close(c->handle);
        c->fs = NULL;
        c->handle = -1;
    }
}

#endif // PROTOCORE_ENABLE_WAL
#endif // PROTOCORE_WAL_FS_H
