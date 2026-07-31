// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file filesystem.cpp
 * @brief The filesystem accessor: root ownership, path resolution, and dispatch to the mount.
 *
 * Two path buffers, because exactly one operation needs two paths at once - rename, which must
 * hold its source and its destination together. Every other operation resolves, dispatches, and is
 * done with the buffer before it returns, so the count is derived from the widest operation rather
 * than chosen.
 */

#include "server/filesystem/filesystem.h"

#if PC_ENABLE_MNT

namespace
{
// Root plus the resolved-path storage, owned by one instance (internal linkage). The root is here
// rather than in each protocol server because it is a property of what is mounted: two servers over
// the same storage cannot disagree about where it begins.
struct FilesystemCtx
{
    // A copy, not the caller's pointer: the join emits `root || dir` with no separator of its own
    // because a root ending in '/' is a known property, and owning the bytes is what makes it known
    // rather than assumed. A caller-supplied "/gcode" resolved `/part.nc` to `/gcodepart.nc`.
    // "/" until pc_fs_begin() says otherwise, so a resolve before setup joins onto a valid root
    // rather than an empty string. Stated on the member, not at the instance, so the declaration
    // does not have to name every other field to say this one thing.
    char root[PC_FILESYSTEM_PATH_MAX] = "/";
    char path[2][PC_FILESYSTEM_PATH_MAX];
};
static FilesystemCtx s_fs;

// Resolve a request into buffer @p slot. Returns nullptr on traversal or overflow, which is what
// makes every operation below a single null test instead of an error-code ladder.
const char *resolve_into(int slot, const char *dir, const char *name)
{
    if (dir == nullptr || name == nullptr)
    {
        return nullptr;
    }
    if (pc_fs_resolve(s_fs.root, dir, name, s_fs.path[slot], PC_FILESYSTEM_PATH_MAX) != 0)
    {
        return nullptr;
    }
    return s_fs.path[slot];
}
} // namespace

void pc_fs_begin(const char *root)
{
    // One byte of the capacity is held back for the separator below, so appending it cannot overrun.
    size_t n = (root == nullptr) ? 0 : pc_frame_build(s_fs.root, PC_FILESYSTEM_PATH_MAX - 1, FILESYSTEM_ROOT, root);
    if (n == 0) // empty, or a root that does not fit - refused, not truncated into another directory
    {
        s_fs.root[0] = '/';
        s_fs.root[1] = '\0';
        return;
    }
    if (s_fs.root[n - 1] != '/') // the engine returned the length, so the last byte is an index
    {
        s_fs.root[n] = '/';      // the separator the join relies on, added once here rather than
        s_fs.root[n + 1] = '\0'; // tested on every resolve
    }
}

const char *pc_fs_path(const char *dir, const char *name)
{
    return resolve_into(0, dir, name);
}

int pc_fs_open(const char *dir, const char *name, pc_mnt_mode mode)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *p = resolve_into(0, dir, name);
    if (b == nullptr || p == nullptr)
    {
        return -1;
    }
    return b->open(p, static_cast<int>(mode)); // cross the int backend ABI
}

int pc_fs_read(int handle, void *buf, size_t n)
{
    const pc_mnt_backend *b = pc_mnt_active();
    return (b == nullptr) ? -1 : b->read(handle, buf, n);
}

int pc_fs_write(int handle, const void *buf, size_t n)
{
    const pc_mnt_backend *b = pc_mnt_active();
    return (b == nullptr) ? -1 : b->write(handle, buf, n);
}

void pc_fs_close(int handle)
{
    const pc_mnt_backend *b = pc_mnt_active();
    if (b != nullptr)
    {
        b->close(handle);
    }
}

bool pc_fs_seek(int handle, uint64_t off)
{
    const pc_mnt_backend *b = pc_mnt_active();
    return (b == nullptr) ? false : b->seek(handle, off);
}

long pc_fs_size(const char *dir, const char *name)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *p = resolve_into(0, dir, name);
    if (b == nullptr || p == nullptr)
    {
        return -1;
    }
    return b->size(p);
}

bool pc_fs_exists(const char *dir, const char *name)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *p = resolve_into(0, dir, name);
    if (b == nullptr || p == nullptr)
    {
        return false;
    }
    return b->exists(p);
}

bool pc_fs_stat(const char *dir, const char *name, pc_mnt_stat *out)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *p = resolve_into(0, dir, name);
    if (b == nullptr || p == nullptr)
    {
        return false;
    }
    return b->stat(p, out);
}

bool pc_fs_remove(const char *dir, const char *name)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *p = resolve_into(0, dir, name);
    if (b == nullptr || p == nullptr)
    {
        return false;
    }
    return b->remove(p);
}

bool pc_fs_rename(const char *from_dir, const char *from_name, const char *to_dir, const char *to_name)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *fp = resolve_into(0, from_dir, from_name);
    const char *tp = resolve_into(1, to_dir, to_name); // the one op needing both paths live at once
    if (b == nullptr || fp == nullptr || tp == nullptr)
    {
        return false;
    }
    return b->rename(fp, tp);
}

bool pc_fs_mkdir(const char *dir, const char *name)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *p = resolve_into(0, dir, name);
    if (b == nullptr || p == nullptr)
    {
        return false;
    }
    return b->mkdir(p);
}

bool pc_fs_rmdir(const char *dir, const char *name)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *p = resolve_into(0, dir, name);
    if (b == nullptr || p == nullptr)
    {
        return false;
    }
    return b->rmdir(p);
}

int pc_fs_opendir(const char *dir, const char *name)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *p = resolve_into(0, dir, name);
    if (b == nullptr || p == nullptr)
    {
        return -1;
    }
    return b->opendir(p);
}

bool pc_fs_readdir(int handle, pc_mnt_stat *out, char *name, size_t name_cap)
{
    const pc_mnt_backend *b = pc_mnt_active();
    return (b == nullptr) ? false : b->readdir(handle, out, name, name_cap);
}

long pc_fs_read_file(const char *dir, const char *name, void *buf, size_t cap)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *p = resolve_into(0, dir, name); // resolved once; the loop below works on the handle
    if (b == nullptr || p == nullptr)
    {
        return -1;
    }
    long sz = b->size(p);
    if (sz < 0 || static_cast<size_t>(sz) > cap)
    {
        return -1;
    }
    int h = b->open(p, static_cast<int>(pc_mnt_mode::PC_MNT_READ));
    if (h < 0)
    {
        return -1;
    }
    size_t total = 0;
    uint8_t *out = static_cast<uint8_t *>(buf);
    while (total < static_cast<size_t>(sz))
    {
        int r = b->read(h, out + total, static_cast<size_t>(sz) - total);
        if (r <= 0)
        {
            break;
        }
        total += static_cast<size_t>(r);
    }
    b->close(h);
    return static_cast<long>(total);
}

bool pc_fs_write_file(const char *dir, const char *name, const void *buf, size_t n)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *p = resolve_into(0, dir, name);
    if (b == nullptr || p == nullptr)
    {
        return false;
    }
    int h = b->open(p, static_cast<int>(pc_mnt_mode::PC_MNT_WRITE));
    if (h < 0)
    {
        return false;
    }
    size_t total = 0;
    const uint8_t *in = static_cast<const uint8_t *>(buf);
    while (total < n)
    {
        int w = b->write(h, in + total, n - total);
        if (w <= 0)
        {
            break;
        }
        total += static_cast<size_t>(w);
    }
    b->close(h);
    return total == n;
}

#endif // PC_ENABLE_MNT
