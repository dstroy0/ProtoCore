// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_wal_fs.h
 * @brief Bind the WAL store's ::WalDev block-device seam to a real fs::FS file (PC_ENABLE_WAL, ESP32 only).
 *
 * The store in pc_wal_store.h does all I/O through three function pointers so its logic stays pure and
 * host-testable; this header is the thin, device-only adapter that points those pointers at a preallocated
 * fs::File on any Arduino filesystem - an SD card or LittleFS. Random access uses `File::seek`, and the
 * durability barrier is `File::flush`.
 *
 * Usage:
 * @code
 *   pc_wal_fs_prealloc(SD, "/wal.bin", 256 * 1024);       // once: fixed-size, zero-filled backing file
 *   fs::File f = SD.open("/wal.bin", "r+");             // random read+write, no truncation
 *   WalDev dev = pc_wal_fs_dev(&f, 256 * 1024);
 *   WalStore s;
 *   pc_wal_store_mount(&s, &dev) || pc_wal_store_format(&s, &dev);   // recover, or initialize a fresh file
 * @endcode
 *
 * The backing file is preallocated to a fixed size so every store offset lands inside it and `seek`+`write`
 * overwrites in place (no sparse-file / past-EOF behavior differences between FAT and LittleFS). The fs::File
 * must outlive any ::WalDev bound to it.
 */

#ifndef PROTOCORE_WAL_FS_H
#define PROTOCORE_WAL_FS_H

#include "protocore_config.h"

#if PC_ENABLE_WAL && defined(ARDUINO)

#include "services/storage/wal/wal_store.h"
#include <FS.h>
#include <string.h>

namespace pc_wal_fs_detail
{
inline size_t fs_read(void *ctx, uint64_t off, uint8_t *buf, size_t len)
{
    fs::File *f = (fs::File *)ctx;
    if (!f->seek((uint32_t)off))
    {
        return 0;
    }
    return f->read(buf, len);
}
inline size_t fs_write(void *ctx, uint64_t off, const uint8_t *buf, size_t len)
{
    fs::File *f = (fs::File *)ctx;
    if (!f->seek((uint32_t)off))
    {
        return 0;
    }
    return f->write(buf, len);
}
inline bool fs_sync(void *ctx)
{
    ((fs::File *)ctx)->flush();
    return true;
}
} // namespace pc_wal_fs_detail

/**
 * @brief Ensure @p path on @p fsys exists and is at least @p size bytes (created zero-filled if missing/short).
 * @return true on success. Call once before opening the file for the store.
 */
inline bool pc_wal_fs_prealloc(fs::FS &fsys, const char *path, uint64_t size)
{
    if (fsys.exists(path))
    {
        fs::File ex = fsys.open(path, "r");
        uint64_t have = ex ? (uint64_t)ex.size() : 0;
        if (ex)
        {
            ex.close();
        }
        if (have >= size)
        {
            return true;
        }
    }
    fs::File f = fsys.open(path, "w"); // create / truncate
    if (!f)
    {
        return false;
    }
    uint8_t z[256];
    memset(z, 0, sizeof(z));
    uint64_t left = size;
    bool ok = true;
    while (left)
    {
        size_t n = left < sizeof(z) ? (size_t)left : sizeof(z);
        if (f.write(z, n) != n)
        {
            ok = false;
            break;
        }
        left -= n;
    }
    f.flush();
    f.close();
    return ok;
}

/**
 * @brief Build a ::WalDev that reads/writes @p f (an open "r+" file) as a @p size-byte block device.
 * @note @p f must stay open for as long as the returned ::WalDev (and any ::WalStore mounted on it) is used.
 */
inline WalDev pc_wal_fs_dev(fs::File *f, uint64_t size)
{
    WalDev d;
    d.read = pc_wal_fs_detail::fs_read;
    d.write = pc_wal_fs_detail::fs_write;
    d.sync = pc_wal_fs_detail::fs_sync;
    d.ctx = f;
    d.size = size;
    return d;
}

#endif // PC_ENABLE_WAL && ARDUINO
#endif // PROTOCORE_WAL_FS_H
