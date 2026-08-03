// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A pc_mnt_backend over real littlefs, on a RAM block device.
//
// This is the filesystem the device runs, so a caller that walks a collection - WebDAV's PROPFIND,
// a recursive copy, anything that opens a directory - is answered by the same code on the host as
// on hardware. A hand-rolled tree only ever agrees with itself: it invents its own ordering, never
// runs out of space, and cannot disagree with the device, which is exactly the disagreement worth
// finding. Real littlefs gives real directory semantics, real ENOSPC, and real error codes.
//
// The block device is ours rather than vendored: littlefs asks for four callbacks (read / prog /
// erase / sync) and a geometry, and over RAM each one is a memcpy or a memset. The package ships
// the portable core only, which is the whole of what a filesystem needs from us.
//
// Sizes are the smallest littlefs is happy with rather than a device's real geometry: a test wants
// a tree, not a flash part, and a small volume keeps the BSS honest.

#ifndef PROTOCORE_LFS_MOCK_H
#define PROTOCORE_LFS_MOCK_H

#include "server/filesystem/mnt.h"
#include <lfs.h>
#include <string.h>

#define LFSM_READ_SIZE 16
#define LFSM_PROG_SIZE 16
#define LFSM_BLOCK_SIZE 512
#define LFSM_BLOCK_COUNT 64 ///< 32 KB volume - enough for a tree, small enough to stay in BSS
#define LFSM_CACHE_SIZE 64
#define LFSM_LOOKAHEAD 16
#define LFSM_HANDLES 8

typedef struct
{
    proto_bool open;
    proto_bool is_dir;
    lfs_file_t file;
    lfs_dir_t dir;
} LfsmHandle;

typedef struct
{
    uint8_t storage[LFSM_BLOCK_SIZE * LFSM_BLOCK_COUNT];
    lfs_t lfs;
    struct lfs_config cfg;
    proto_bool mounted;

    uint8_t rbuf[LFSM_CACHE_SIZE];
    uint8_t pbuf[LFSM_CACHE_SIZE];
    uint8_t lbuf[LFSM_LOOKAHEAD];

    LfsmHandle h[LFSM_HANDLES];
} LfsmCtx;

// One instance for the whole program, like every other host-driver global: the test writes the
// tree from its translation unit and the code under test reads it from another.
__attribute__((weak)) LfsmCtx g_lfsm;

// --- the block device: RAM, so each call is the obvious move ------------------

static inline int lfsm_bd_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buf, lfs_size_t size)
{
    (void)c;
    memcpy(buf, g_lfsm.storage + (size_t)block * LFSM_BLOCK_SIZE + off, size);
    return 0;
}

static inline int lfsm_bd_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buf,
                               lfs_size_t size)
{
    (void)c;
    memcpy(g_lfsm.storage + (size_t)block * LFSM_BLOCK_SIZE + off, buf, size);
    return 0;
}

static inline int lfsm_bd_erase(const struct lfs_config *c, lfs_block_t block)
{
    (void)c;
    memset(g_lfsm.storage + (size_t)block * LFSM_BLOCK_SIZE, 0xFF, LFSM_BLOCK_SIZE);
    return 0;
}

static inline int lfsm_bd_sync(const struct lfs_config *c)
{
    (void)c; // RAM is already where the bytes live
    return 0;
}

/** @brief Format and mount an empty volume. Call in setUp(); it is the whole of the reset. */
static inline void lfsm_format(void)
{
    if (g_lfsm.mounted)
    {
        lfs_unmount(&g_lfsm.lfs);
        g_lfsm.mounted = PROTO_FALSE;
    }
    for (int i = 0; i < LFSM_HANDLES; i++)
    {
        g_lfsm.h[i].open = PROTO_FALSE;
    }

    memset(&g_lfsm.cfg, 0, sizeof(g_lfsm.cfg));
    g_lfsm.cfg.read = lfsm_bd_read;
    g_lfsm.cfg.prog = lfsm_bd_prog;
    g_lfsm.cfg.erase = lfsm_bd_erase;
    g_lfsm.cfg.sync = lfsm_bd_sync;
    g_lfsm.cfg.read_size = LFSM_READ_SIZE;
    g_lfsm.cfg.prog_size = LFSM_PROG_SIZE;
    g_lfsm.cfg.block_size = LFSM_BLOCK_SIZE;
    g_lfsm.cfg.block_count = LFSM_BLOCK_COUNT;
    g_lfsm.cfg.block_cycles = 500;
    g_lfsm.cfg.cache_size = LFSM_CACHE_SIZE;
    g_lfsm.cfg.lookahead_size = LFSM_LOOKAHEAD;
    g_lfsm.cfg.read_buffer = g_lfsm.rbuf;
    g_lfsm.cfg.prog_buffer = g_lfsm.pbuf;
    g_lfsm.cfg.lookahead_buffer = g_lfsm.lbuf;

    memset(g_lfsm.storage, 0xFF, sizeof(g_lfsm.storage));
    if (lfs_format(&g_lfsm.lfs, &g_lfsm.cfg) == 0 && lfs_mount(&g_lfsm.lfs, &g_lfsm.cfg) == 0)
    {
        g_lfsm.mounted = PROTO_TRUE;
    }
}

/**
 * @brief Create every directory above @p path, the way `mkdir -p` would.
 *
 * A real filesystem refuses to create a file under a directory that is not there, which the
 * flat mocks never did - they stored a path string and called it a file. A test that says
 * "there is a file at /dav/sub/x.txt" means the collection too, so the fixture makes it.
 */
static inline void lfsm_mkdir_parents(const char *path)
{
    char buf[LFS_NAME_MAX * 2];
    size_t n = strlen(path);
    if (n >= sizeof(buf))
    {
        return;
    }
    memcpy(buf, path, n + 1);
    for (size_t i = 1; i < n; i++)
    {
        if (buf[i] != '/')
        {
            continue;
        }
        buf[i] = '\0';
        lfs_mkdir(&g_lfsm.lfs, buf); // already-there is not an error worth reporting here
        buf[i] = '/';
    }
}

/** @brief Write a whole file, creating the collections above it first. */
static inline proto_bool lfsm_write_file(const char *path, const void *data, size_t len)
{
    lfsm_mkdir_parents(path);
    lfs_file_t f;
    if (lfs_file_open(&g_lfsm.lfs, &f, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) < 0)
    {
        return PROTO_FALSE;
    }
    lfs_ssize_t n = lfs_file_write(&g_lfsm.lfs, &f, data, (lfs_size_t)len);
    lfs_file_close(&g_lfsm.lfs, &f);
    return (n == (lfs_ssize_t)len) ? PROTO_TRUE : PROTO_FALSE;
}

static inline proto_bool lfsm_write_text(const char *path, const char *text)
{
    return lfsm_write_file(path, text, strlen(text));
}

static inline proto_bool lfsm_mkdir(const char *path)
{
    lfsm_mkdir_parents(path);
    int rc = lfs_mkdir(&g_lfsm.lfs, path);
    return (rc == 0 || rc == LFS_ERR_EXIST) ? PROTO_TRUE : PROTO_FALSE;
}

// --- the backend --------------------------------------------------------------

static inline int lfsm_slot(void)
{
    for (int i = 0; i < LFSM_HANDLES; i++)
    {
        if (!g_lfsm.h[i].open)
        {
            return i;
        }
    }
    return -1;
}

static inline int lfsm_open(const char *path, int mode)
{
    int i = lfsm_slot();
    if (i < 0 || !g_lfsm.mounted)
    {
        return -1;
    }
    int flags = LFS_O_RDONLY;
    if (mode == (int)PC_MNT_WRITE)
    {
        flags = LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC;
    }
    else if (mode == (int)PC_MNT_APPEND)
    {
        flags = LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND;
    }
    if (lfs_file_open(&g_lfsm.lfs, &g_lfsm.h[i].file, path, flags) < 0)
    {
        return -1;
    }
    g_lfsm.h[i].open = PROTO_TRUE;
    g_lfsm.h[i].is_dir = PROTO_FALSE;
    return i;
}

static inline int lfsm_read(int handle, void *buf, size_t n)
{
    if (handle < 0 || handle >= LFSM_HANDLES || !g_lfsm.h[handle].open || g_lfsm.h[handle].is_dir)
    {
        return -1;
    }
    return (int)lfs_file_read(&g_lfsm.lfs, &g_lfsm.h[handle].file, buf, (lfs_size_t)n);
}

static inline int lfsm_write(int handle, const void *buf, size_t n)
{
    if (handle < 0 || handle >= LFSM_HANDLES || !g_lfsm.h[handle].open || g_lfsm.h[handle].is_dir)
    {
        return -1;
    }
    return (int)lfs_file_write(&g_lfsm.lfs, &g_lfsm.h[handle].file, buf, (lfs_size_t)n);
}

static inline void lfsm_close(int handle)
{
    if (handle < 0 || handle >= LFSM_HANDLES || !g_lfsm.h[handle].open)
    {
        return;
    }
    if (g_lfsm.h[handle].is_dir)
    {
        lfs_dir_close(&g_lfsm.lfs, &g_lfsm.h[handle].dir);
    }
    else
    {
        lfs_file_close(&g_lfsm.lfs, &g_lfsm.h[handle].file);
    }
    g_lfsm.h[handle].open = PROTO_FALSE;
}

static inline proto_bool lfsm_seek(int handle, uint64_t off)
{
    if (handle < 0 || handle >= LFSM_HANDLES || !g_lfsm.h[handle].open || g_lfsm.h[handle].is_dir)
    {
        return PROTO_FALSE;
    }
    return lfs_file_seek(&g_lfsm.lfs, &g_lfsm.h[handle].file, (lfs_soff_t)off, LFS_SEEK_SET) >= 0 ? PROTO_TRUE
                                                                                                  : PROTO_FALSE;
}

static inline proto_bool lfsm_stat(const char *path, pc_mnt_stat *out)
{
    struct lfs_info info;
    if (!g_lfsm.mounted || lfs_stat(&g_lfsm.lfs, path, &info) < 0)
    {
        return PROTO_FALSE;
    }
    out->is_dir = (info.type == LFS_TYPE_DIR) ? PROTO_TRUE : PROTO_FALSE;
    out->size = (info.type == LFS_TYPE_DIR) ? 0u : info.size;
    out->mtime = 0; // littlefs keeps no clock of its own; a timestamp is an attribute the app sets
    return PROTO_TRUE;
}

static inline long lfsm_size(const char *path)
{
    pc_mnt_stat st;
    return lfsm_stat(path, &st) ? (long)st.size : -1;
}

static inline proto_bool lfsm_exists(const char *path)
{
    pc_mnt_stat st;
    return lfsm_stat(path, &st);
}

static inline proto_bool lfsm_remove(const char *path)
{
    return lfs_remove(&g_lfsm.lfs, path) == 0 ? PROTO_TRUE : PROTO_FALSE;
}

static inline proto_bool lfsm_rename(const char *from, const char *to)
{
    return lfs_rename(&g_lfsm.lfs, from, to) == 0 ? PROTO_TRUE : PROTO_FALSE;
}

static inline proto_bool lfsm_mkdir_op(const char *path)
{
    return lfs_mkdir(&g_lfsm.lfs, path) == 0 ? PROTO_TRUE : PROTO_FALSE;
}

static inline proto_bool lfsm_rmdir(const char *path)
{
    return lfs_remove(&g_lfsm.lfs, path) == 0 ? PROTO_TRUE : PROTO_FALSE; // littlefs removes either kind
}

static inline int lfsm_opendir(const char *path)
{
    int i = lfsm_slot();
    if (i < 0 || !g_lfsm.mounted || lfs_dir_open(&g_lfsm.lfs, &g_lfsm.h[i].dir, path) < 0)
    {
        return -1;
    }
    g_lfsm.h[i].open = PROTO_TRUE;
    g_lfsm.h[i].is_dir = PROTO_TRUE;
    return i;
}

static inline proto_bool lfsm_readdir(int handle, pc_mnt_stat *out, char *name, size_t name_cap)
{
    if (handle < 0 || handle >= LFSM_HANDLES || !g_lfsm.h[handle].open || !g_lfsm.h[handle].is_dir)
    {
        return PROTO_FALSE;
    }
    struct lfs_info info;
    for (;;)
    {
        int rc = lfs_dir_read(&g_lfsm.lfs, &g_lfsm.h[handle].dir, &info);
        if (rc <= 0)
        {
            return PROTO_FALSE;
        }
        if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0)
        {
            continue; // the seam lists children, not the walk's own anchors
        }
        size_t n = strlen(info.name);
        if (n >= name_cap)
        {
            continue; // the caller's buffer cannot hold it - skip rather than truncate a name
        }
        memcpy(name, info.name, n + 1);
        out->is_dir = (info.type == LFS_TYPE_DIR) ? PROTO_TRUE : PROTO_FALSE;
        out->size = (info.type == LFS_TYPE_DIR) ? 0u : info.size;
        out->mtime = 0;
        return PROTO_TRUE;
    }
}

__attribute__((weak)) const pc_mnt_backend g_lfsm_backend = {
    lfsm_open,   lfsm_read,   lfsm_write,    lfsm_close, lfsm_seek, lfsm_size,    lfsm_exists,
    lfsm_remove, lfsm_rename, lfsm_mkdir_op, lfsm_rmdir, lfsm_stat, lfsm_opendir, lfsm_readdir};

/** @brief The backend to hand pc_mnt_mount(). */
static inline const pc_mnt_backend *lfsm(void)
{
    return &g_lfsm_backend;
}

#endif // PROTOCORE_LFS_MOCK_H
