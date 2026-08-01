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
#include <string.h> // strncmp (root-name match)

namespace
{
// Root plus the resolved-path storage, owned by one instance (internal linkage). The root is here
// rather than in each protocol server because it is a property of what is mounted: two servers over
// the same storage cannot disagree about where it begins.
// One bound root. The prefix is a copy, not the caller's pointer: the join emits `root || dir` with
// no separator of its own because a root ending in '/' is a known property, and owning the bytes is
// what makes it known rather than assumed. A caller-supplied "/gcode" resolved `/part.nc` to
// `/gcodepart.nc` when that was left to the caller.
struct FsRoot
{
    char name[PC_FS_ROOT_NAME_MAX];    ///< what a service asked for, e.g. "mnt/scp".
    char path[PC_FILESYSTEM_PATH_MAX]; ///< the prefix it resolves to; always ends '/'.
};

struct FilesystemCtx
{
    FsRoot root[PC_FS_MAX_ROOTS];
    uint8_t count;
    char path[2][PC_FILESYSTEM_PATH_MAX];
};
static FilesystemCtx s_fs;

// Resolve a request against @p root into buffer @p slot. Returns nullptr on a bad root, traversal,
// or overflow, which is what makes every operation below a single null test instead of an
// error-code ladder.
const char *resolve_into(int slot, int root, const char *dir, const char *name)
{
    if (root < 0 || root >= (int)s_fs.count || dir == nullptr || name == nullptr)
    {
        return nullptr;
    }
    if (pc_fs_resolve(s_fs.root[root].path, dir, name, s_fs.path[slot], PC_FILESYSTEM_PATH_MAX) != 0)
    {
        return nullptr;
    }
    return s_fs.path[slot];
}
} // namespace

int pc_fs_begin(const char *name)
{
    const char *want = (name == nullptr || name[0] == '\0') ? "/" : name;

    // Binding a name already bound hands back the same root. Two services naming the same storage
    // is an arrangement the application is entitled to make, and it must not cost a second root or
    // give them two views of one thing.
    for (uint8_t i = 0; i < s_fs.count; i++)
    {
        if (strncmp(s_fs.root[i].name, want, PC_FS_ROOT_NAME_MAX) == 0)
        {
            return (int)i;
        }
    }
    if (s_fs.count >= PC_FS_MAX_ROOTS)
    {
        return -1; // refused, not silently aliased onto someone else's root
    }

    FsRoot *r = &s_fs.root[s_fs.count];

    // One byte of the capacity is held back for the separator below, so appending cannot overrun.
    size_t n = pc_frame_build(r->path, PC_FILESYSTEM_PATH_MAX - 1, FILESYSTEM_ROOT, want);
    if (n == 0) // a root that does not fit - refused, not truncated into another directory
    {
        return -1;
    }
    if (r->path[n - 1] != '/') // the engine returned the length, so the last byte is an index
    {
        r->path[n] = '/';      // the separator the join relies on, added once here rather than
        r->path[n + 1] = '\0'; // tested on every resolve
    }
    if (pc_frame_build(r->name, PC_FS_ROOT_NAME_MAX, FILESYSTEM_ROOT, want) == 0)
    {
        return -1; // a name too long to record is a name that could not be matched again
    }

    int id = (int)s_fs.count;
    s_fs.count++;
    return id;
}

const char *pc_fs_path(int root, const char *dir, const char *name)
{
    return resolve_into(0, root, dir, name);
}

int pc_fs_open(int root, const char *dir, const char *name, pc_mnt_mode mode)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *p = resolve_into(0, root, dir, name);
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

long pc_fs_size(int root, const char *dir, const char *name)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *p = resolve_into(0, root, dir, name);
    if (b == nullptr || p == nullptr)
    {
        return -1;
    }
    return b->size(p);
}

bool pc_fs_exists(int root, const char *dir, const char *name)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *p = resolve_into(0, root, dir, name);
    if (b == nullptr || p == nullptr)
    {
        return false;
    }
    return b->exists(p);
}

bool pc_fs_stat(int root, const char *dir, const char *name, pc_mnt_stat *out)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *p = resolve_into(0, root, dir, name);
    if (b == nullptr || p == nullptr)
    {
        return false;
    }
    return b->stat(p, out);
}

bool pc_fs_remove(int root, const char *dir, const char *name)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *p = resolve_into(0, root, dir, name);
    if (b == nullptr || p == nullptr)
    {
        return false;
    }
    return b->remove(p);
}

bool pc_fs_rename(int root, const char *from_dir, const char *from_name, const char *to_dir, const char *to_name)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *fp = resolve_into(0, root, from_dir, from_name);
    const char *tp = resolve_into(1, root, to_dir, to_name); // the one op needing both paths live at once
    if (b == nullptr || fp == nullptr || tp == nullptr)
    {
        return false;
    }
    return b->rename(fp, tp);
}

bool pc_fs_mkdir(int root, const char *dir, const char *name)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *p = resolve_into(0, root, dir, name);
    if (b == nullptr || p == nullptr)
    {
        return false;
    }
    return b->mkdir(p);
}

bool pc_fs_rmdir(int root, const char *dir, const char *name)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *p = resolve_into(0, root, dir, name);
    if (b == nullptr || p == nullptr)
    {
        return false;
    }
    return b->rmdir(p);
}

int pc_fs_opendir(int root, const char *dir, const char *name)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *p = resolve_into(0, root, dir, name);
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

long pc_fs_read_file(int root, const char *dir, const char *name, void *buf, size_t cap)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *p = resolve_into(0, root, dir, name); // resolved once; the loop below works on the handle
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

bool pc_fs_write_file(int root, const char *dir, const char *name, const void *buf, size_t n)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *p = resolve_into(0, root, dir, name);
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
