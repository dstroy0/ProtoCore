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

    // The tree walks' level stack (pc_fs_remove / pc_fs_copy). This IS the stack: both walks are
    // loops over an index into these arrays, not call recursion, so a deep tree costs one array
    // entry per level instead of a live call frame per level - the one allocation the fixed-footprint
    // accounting cannot see. The depth a request can force is the array's extent.
    //
    // A level is a source path and a destination path: remove uses the first, copy uses both. One
    // entry beyond PC_FS_MAX_DEPTH, so the deepest legal level still has a slot above it to name a
    // candidate child in before the walk decides whether that child becomes a level.
    //
    // idx is where each level resumes. Remove does not need it - it re-opens and takes the first
    // survivor every pass, which is what keeps a cursor from being invalidated by its own removals -
    // but copy does not consume what it reads, so it re-opens and skips to where it left off.
    char walk[PC_FS_MAX_DEPTH + 2][PC_FILESYSTEM_PATH_MAX];
    char dwalk[PC_FS_MAX_DEPTH + 2][PC_FILESYSTEM_PATH_MAX];
    uint16_t idx[PC_FS_MAX_DEPTH + 2];

    // One child name, not one per level: an entry is read, used to build the next level's path, and
    // dead. Nothing needs a previous level's child name once that level's path exists.
    char child[PC_FILESYSTEM_PATH_MAX];

    // One storage block. The copy transfers whole blocks so the store never has to read-modify-write
    // a partial one (see PC_FS_BLOCK). Alignment is this file's problem: nothing above it knows what
    // the mounted store erases in.
    uint8_t block[PC_FS_BLOCK];
};
static FilesystemCtx s_fs;

// Join a level's directory path onto a child name, separator included.
//
// The separator is a field of the spec, not a string the caller picks: pc_fs_join deliberately butts
// its pieces together, because a mount root and a directory destination both already end with '/'.
// A descent has neither, so joining a level onto a child through that spec would resolve "/a/sub" +
// "child" to "/a/subchild".
bool walk_join(const char *dir, const char *child, char *out, size_t cap)
{
    // The root is the one path that IS the separator. Every other resolved path has had its trailing
    // '/' dropped (see pc_fs_resolve), so the descent spec always has exactly one to add.
    if (dir[0] == '/' && dir[1] == '\0')
    {
        return pc_frame_build(out, cap, FILESYSTEM_JOIN, dir, child, "") != 0;
    }
    return pc_frame_build(out, cap, FILESYSTEM_DESCEND, dir, child) != 0;
}

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

    // One stat decides which of the two this is. A plain file is the one call it always was; a
    // directory takes the walk below, which is what makes "delete this" mean the same thing to every
    // caller instead of each protocol carrying its own tree logic.
    pc_mnt_stat st;
    if (!b->stat(p, &st))
    {
        return false;
    }
    if (!st.is_dir)
    {
        return b->remove(p);
    }

    // The walk. Level 0 is the resolved path; descending is lvl++, finishing a level is lvl--, and
    // the depth a request can force is the array's extent. Each pass re-opens the current level and
    // takes its first surviving entry, so the cursor is never carried across a removal that would
    // invalidate it - whatever the previous pass removed is already gone when the next open happens.
    if (pc_frame_build(s_fs.walk[0], PC_FILESYSTEM_PATH_MAX, FILESYSTEM_ROOT, p) == 0)
    {
        return false;
    }

    int lvl = 0;
    for (;;)
    {
        int d = b->opendir(s_fs.walk[lvl]);
        if (d < 0)
        {
            return false;
        }
        pc_mnt_stat cst;
        bool got = b->readdir(d, &cst, s_fs.child, sizeof(s_fs.child));
        b->close(d);

        if (!got) // drained: remove this level and step back out
        {
            if (!b->rmdir(s_fs.walk[lvl]))
            {
                return false;
            }
            if (lvl == 0)
            {
                return true;
            }
            lvl--;
            continue;
        }

        // The child's path, built one level up. That slot always exists: the arrays carry one entry
        // beyond PC_FS_MAX_DEPTH so the deepest legal level can still name a child before the test
        // below decides whether that child becomes a level.
        if (!walk_join(s_fs.walk[lvl], s_fs.child, s_fs.walk[lvl + 1], PC_FILESYSTEM_PATH_MAX))
        {
            return false;
        }
        if (!cst.is_dir)
        {
            if (!b->remove(s_fs.walk[lvl + 1]))
            {
                return false;
            }
            continue; // same level, re-opened next pass
        }
        if (lvl + 1 > PC_FS_MAX_DEPTH)
        {
            return false; // refuse a pathologically deep tree rather than run off the level stack
        }
        lvl++;
    }
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

// Copy one file's bytes through the chunk buffer. The only place in this file that holds two handles
// at once, and the only reader of s_fs.block.
static bool copy_one(const pc_mnt_backend *b, const char *src, const char *dst)
{
    int in = b->open(src, static_cast<int>(pc_mnt_mode::PC_MNT_READ));
    if (in < 0)
    {
        return false;
    }
    int out = b->open(dst, static_cast<int>(pc_mnt_mode::PC_MNT_WRITE));
    if (out < 0)
    {
        b->close(in);
        return false;
    }
    bool ok = true;
    for (;;)
    {
        int n = b->read(in, s_fs.block, sizeof(s_fs.block));
        if (n <= 0)
        {
            break;
        }
        if (b->write(out, s_fs.block, static_cast<size_t>(n)) != n)
        {
            ok = false; // out of space / write fault: reported, not left as a short copy
            break;
        }
    }
    b->close(in);
    b->close(out);
    return ok;
}

bool pc_fs_copy(int root, const char *from_dir, const char *from_name, const char *to_dir, const char *to_name)
{
    const pc_mnt_backend *b = pc_mnt_active();
    const char *sp = resolve_into(0, root, from_dir, from_name);
    const char *dp = resolve_into(1, root, to_dir, to_name); // both paths live at once, as in rename
    if (b == nullptr || sp == nullptr || dp == nullptr)
    {
        return false;
    }

    pc_mnt_stat st;
    if (!b->stat(sp, &st))
    {
        return false;
    }
    if (!st.is_dir)
    {
        return copy_one(b, sp, dp);
    }

    if (pc_frame_build(s_fs.walk[0], PC_FILESYSTEM_PATH_MAX, FILESYSTEM_ROOT, sp) == 0 ||
        pc_frame_build(s_fs.dwalk[0], PC_FILESYSTEM_PATH_MAX, FILESYSTEM_ROOT, dp) == 0)
    {
        return false;
    }
    if (!b->mkdir(s_fs.dwalk[0]))
    {
        return false;
    }
    s_fs.idx[0] = 0;

    // The same loop as pc_fs_remove with one difference: a copy does not consume what it reads, so
    // re-opening and taking the first entry would repeat that entry forever. Each level records the
    // child it left off at and skips forward to it, which stays correct even when a store invalidates
    // an open cursor across the writes the copy makes into the destination.
    int lvl = 0;
    for (;;)
    {
        int d = b->opendir(s_fs.walk[lvl]);
        if (d < 0)
        {
            return false;
        }
        pc_mnt_stat cst;
        bool got = false;
        for (uint16_t i = 0; i <= s_fs.idx[lvl]; i++)
        {
            got = b->readdir(d, &cst, s_fs.child, sizeof(s_fs.child));
            if (!got)
            {
                break;
            }
        }
        b->close(d);

        if (!got) // exhausted: step back out and advance the parent past this level
        {
            if (lvl == 0)
            {
                return true;
            }
            lvl--;
            s_fs.idx[lvl]++;
            continue;
        }

        if (!walk_join(s_fs.walk[lvl], s_fs.child, s_fs.walk[lvl + 1], PC_FILESYSTEM_PATH_MAX) ||
            !walk_join(s_fs.dwalk[lvl], s_fs.child, s_fs.dwalk[lvl + 1], PC_FILESYSTEM_PATH_MAX))
        {
            return false;
        }
        if (!cst.is_dir)
        {
            if (!copy_one(b, s_fs.walk[lvl + 1], s_fs.dwalk[lvl + 1]))
            {
                return false;
            }
            s_fs.idx[lvl]++;
            continue;
        }
        if (lvl + 1 > PC_FS_MAX_DEPTH)
        {
            return false;
        }
        if (!b->mkdir(s_fs.dwalk[lvl + 1]))
        {
            return false;
        }
        lvl++;
        s_fs.idx[lvl] = 0;
    }
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
