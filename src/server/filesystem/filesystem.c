// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file filesystem.c
 * @brief The filesystem accessor: root ownership, path resolution, and dispatch to the mount.
 *
 * Two path buffers: rename holds a source and a destination at once, every other operation
 * resolves, dispatches, and is done with its buffer before it returns.
 */

#include "server/filesystem/filesystem.h"
#include "shared_primitives/runops.h" // the bounded word-at-a-time length scan
#include <string.h>                   // strncmp (root-name match), memcpy

// One bound root. The prefix is a copy, not the caller's pointer, and always ends '/', so the join
// concatenates `root` and `dir` without adding a separator.
typedef struct
{
    char name[PC_FS_ROOT_NAME_MAX];    ///< what a service asked for, e.g. "mnt/scp".
    char path[PC_FILESYSTEM_PATH_MAX]; ///< the prefix it resolves to; always ends '/'.
} FsRoot;

typedef struct
{
    FsRoot root[PC_FS_MAX_ROOTS];
    uint8_t count;
    char path[2][PC_FILESYSTEM_PATH_MAX];

    // The tree walks' path (pc_fs_remove / pc_fs_copy), plus the destination a copy writes to. One
    // buffer each, not one per level: the path is the stack. Descending appends "/child", ascending
    // truncates at the last separator, and readdir writes the entry's own name at the append point.
    char walk[PC_FILESYSTEM_PATH_MAX];
    char dwalk[PC_FILESYSTEM_PATH_MAX];

    // Where each level resumes. Remove re-opens and takes the first survivor every pass, so its
    // cursor cannot be invalidated by its own removals; copy does not consume what it reads, so it
    // re-opens and skips to where it left off.
    uint16_t idx[PC_FS_MAX_DEPTH + 2];

    // One storage block. The copy transfers whole blocks, so the store never read-modify-writes a
    // partial one (see PC_FS_BLOCK).
    uint8_t block[PC_FS_BLOCK];

    uint32_t status; ///< sticky reasons operations have failed (see the status block in filesystem.h).
} FilesystemCtx;
static FilesystemCtx s_fs;

// The mounted store, or NULL. A filesystem with no store behind it is a legitimate configuration,
// so the absence is recorded in the status mask rather than returned as a bare false.
static const pc_mnt_backend *store(void)
{
    const pc_mnt_backend *b = pc_mnt_active();
    if (b == NULL)
    {
        s_fs.status |= PC_FS_STORAGE_EXHAUSTED;
    }
    return b;
}

// Descend @p path (length @p len) into @p child in place: append the separator and the name.
// @return the new length, or 0 if it would not fit - which stops the walk rather than truncating a
// name into a different file's.
static size_t walk_push(char *path, size_t len, const char *child)
{
    size_t n = proto_scan_nul(child, PC_FILESYSTEM_PATH_MAX);
    // The root already IS the separator, so it is the one path that does not get another.
    proto_bool need_sep = !(len == 1 && path[0] == '/');
    if (len + (need_sep ? 1 : 0) + n + 1 > PC_FILESYSTEM_PATH_MAX)
    {
        return 0;
    }
    if (need_sep)
    {
        path[len++] = '/';
    }
    memcpy(path + len, child, n);
    len += n;
    path[len] = '\0';
    return len;
}

// Ascend one level in place: cut at the separator this level was appended at. @return the new length.
static size_t walk_pop(char *path, size_t len)
{
    while (len > 1 && path[len - 1] != '/')
    {
        len--;
    }
    if (len > 1) // drop the separator itself, unless what is left is the root
    {
        len--;
    }
    path[len] = '\0';
    return len;
}

// Resolve a request against @p root into buffer @p slot. Returns NULL on a bad root, traversal,
// or overflow, which is what makes every operation below a single null test instead of an
// error-code ladder.
static const char *resolve_into(int slot, int root, const char *dir, const char *name)
{
    if (root < 0 || root >= (int)s_fs.count || dir == NULL || name == NULL)
    {
        s_fs.status |= PC_FS_BAD_ROOT;
        return NULL;
    }
    // pc_fs_resolve separates the two ways a path can be rejected; the mask keeps them separate for
    // the caller instead of flattening both into the same NULL.
    int rc = pc_fs_resolve(s_fs.root[root].path, dir, name, s_fs.path[slot], PC_FILESYSTEM_PATH_MAX);
    if (rc != 0)
    {
        s_fs.status |= (rc == -1) ? PC_FS_TRAVERSAL : PC_FS_TOO_LONG;
        return NULL;
    }
    return s_fs.path[slot];
}

uint32_t pc_fs_status(void)
{
    return s_fs.status;
}

void pc_fs_clear_status(void)
{
    s_fs.status = PC_FS_OK;
}

proto_bool pc_fs_storage_present(void)
{
    // Asked of the mount directly, not of the mask: the mask says what has failed, this says what is
    // true now. A hotswap can attach a store between the two.
    return pc_mnt_active() != NULL;
}

int pc_fs_begin(const char *name)
{
    const char *want = (name == NULL || name[0] == '\0') ? "/" : name;

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
    const pc_mnt_backend *b = store();
    const char *p = resolve_into(0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        return -1;
    }
    return b->open(p, (int)(mode)); // cross the int backend ABI
}

int pc_fs_read(int handle, void *buf, size_t n)
{
    const pc_mnt_backend *b = store();
    return (b == NULL) ? -1 : b->read(handle, buf, n);
}

int pc_fs_write(int handle, const void *buf, size_t n)
{
    const pc_mnt_backend *b = store();
    return (b == NULL) ? -1 : b->write(handle, buf, n);
}

void pc_fs_close(int handle)
{
    const pc_mnt_backend *b = store();
    if (b != NULL)
    {
        b->close(handle);
    }
}

proto_bool pc_fs_seek(int handle, uint64_t off)
{
    const pc_mnt_backend *b = store();
    return (b == NULL) ? PROTO_FALSE : b->seek(handle, off);
}

long pc_fs_size(int root, const char *dir, const char *name)
{
    const pc_mnt_backend *b = store();
    const char *p = resolve_into(0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        return -1;
    }
    return b->size(p);
}

proto_bool pc_fs_exists(int root, const char *dir, const char *name)
{
    const pc_mnt_backend *b = store();
    const char *p = resolve_into(0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        return PROTO_FALSE;
    }
    return b->exists(p);
}

proto_bool pc_fs_stat(int root, const char *dir, const char *name, pc_mnt_stat *out)
{
    const pc_mnt_backend *b = store();
    const char *p = resolve_into(0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        return PROTO_FALSE;
    }
    return b->stat(p, out);
}

proto_bool pc_fs_remove(int root, const char *dir, const char *name)
{
    const pc_mnt_backend *b = store();
    const char *p = resolve_into(0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        return PROTO_FALSE;
    }

    // One stat decides which of the two this is. A plain file is the one call it always was; a
    // directory takes the walk below, which is what makes "delete this" mean the same thing to every
    // caller instead of each protocol carrying its own tree logic.
    pc_mnt_stat st;
    if (!b->stat(p, &st))
    {
        return PROTO_FALSE;
    }
    if (!st.is_dir)
    {
        return b->remove(p);
    }

    // The walk. s_fs.walk is the current level's path and the stack both: descending appends the
    // child, finishing a level truncates back. Each pass re-opens the current level and takes its
    // first surviving entry, so the cursor is never carried across a removal that would invalidate
    // it - whatever the previous pass removed is already gone when the next open happens.
    size_t len = pc_frame_build(s_fs.walk, PC_FILESYSTEM_PATH_MAX, FILESYSTEM_ROOT, p);
    if (len == 0)
    {
        return PROTO_FALSE;
    }

    int lvl = 0;
    for (;;)
    {
        int d = b->opendir(s_fs.walk);
        if (d < 0)
        {
            return PROTO_FALSE;
        }
        // The entry's name is read straight onto the end of the path, one byte past the separator,
        // so a matched child is already a full path and an empty directory costs no assembly.
        proto_bool sep = !(len == 1 && s_fs.walk[0] == '/');
        size_t at = len + (sep ? 1 : 0);
        pc_mnt_stat cst;
        proto_bool got =
            (at + 1 < PC_FILESYSTEM_PATH_MAX) && b->readdir(d, &cst, s_fs.walk + at, PC_FILESYSTEM_PATH_MAX - at);
        b->close(d);

        if (!got) // drained: remove this level and step back out
        {
            s_fs.walk[len] = '\0'; // undo the probe, whatever readdir left there
            if (!b->rmdir(s_fs.walk))
            {
                return PROTO_FALSE;
            }
            if (lvl == 0)
            {
                return PROTO_TRUE;
            }
            len = walk_pop(s_fs.walk, len);
            lvl--;
            continue;
        }
        if (sep)
        {
            s_fs.walk[len] = '/'; // commit the separator the name was written past
        }

        if (!cst.is_dir)
        {
            proto_bool ok = b->remove(s_fs.walk);
            s_fs.walk[len] = '\0'; // back to this level, whatever happened
            if (!ok)
            {
                return PROTO_FALSE;
            }
            continue; // same level, re-opened next pass
        }
        if (lvl + 1 > PC_FS_MAX_DEPTH)
        {
            return PROTO_FALSE; // refuse a pathologically deep tree rather than walk forever
        }
        len = proto_scan_nul(s_fs.walk, PC_FILESYSTEM_PATH_MAX);
        lvl++;
    }
}

proto_bool pc_fs_rename(int root, const char *from_dir, const char *from_name, const char *to_dir, const char *to_name)
{
    const pc_mnt_backend *b = store();
    const char *fp = resolve_into(0, root, from_dir, from_name);
    const char *tp = resolve_into(1, root, to_dir, to_name); // the one op needing both paths live at once
    if (b == NULL || fp == NULL || tp == NULL)
    {
        return PROTO_FALSE;
    }
    return b->rename(fp, tp);
}

// Copy one file's bytes through the chunk buffer. The only place in this file that holds two handles
// at once, and the only reader of s_fs.block.
static proto_bool copy_one(const pc_mnt_backend *b, const char *src, const char *dst)
{
    int in = b->open(src, (int)(PC_MNT_READ));
    if (in < 0)
    {
        return PROTO_FALSE;
    }
    int out = b->open(dst, (int)(PC_MNT_WRITE));
    if (out < 0)
    {
        b->close(in);
        return PROTO_FALSE;
    }
    proto_bool ok = PROTO_TRUE;
    for (;;)
    {
        int n = b->read(in, s_fs.block, sizeof(s_fs.block));
        if (n <= 0)
        {
            break;
        }
        if (b->write(out, s_fs.block, (size_t)(n)) != n)
        {
            ok = PROTO_FALSE; // out of space / write fault: reported, not left as a short copy
            break;
        }
    }
    b->close(in);
    b->close(out);
    return ok;
}

proto_bool pc_fs_copy(int root, const char *from_dir, const char *from_name, const char *to_dir, const char *to_name)
{
    const pc_mnt_backend *b = store();
    const char *sp = resolve_into(0, root, from_dir, from_name);
    const char *dp = resolve_into(1, root, to_dir, to_name); // both paths live at once, as in rename
    if (b == NULL || sp == NULL || dp == NULL)
    {
        return PROTO_FALSE;
    }

    pc_mnt_stat st;
    if (!b->stat(sp, &st))
    {
        return PROTO_FALSE;
    }
    if (!st.is_dir)
    {
        return copy_one(b, sp, dp);
    }

    size_t slen = pc_frame_build(s_fs.walk, PC_FILESYSTEM_PATH_MAX, FILESYSTEM_ROOT, sp);
    size_t dlen = pc_frame_build(s_fs.dwalk, PC_FILESYSTEM_PATH_MAX, FILESYSTEM_ROOT, dp);
    if (slen == 0 || dlen == 0)
    {
        return PROTO_FALSE;
    }
    if (!b->mkdir(s_fs.dwalk))
    {
        return PROTO_FALSE;
    }
    s_fs.idx[0] = 0;

    // The same walk as pc_fs_remove, on two paths pushed and popped in lockstep, with one
    // difference: a copy does not consume what it reads, so re-opening and taking the first entry
    // would repeat that entry forever. Each level records the child it left off at and skips forward
    // to it, which stays correct even when a store invalidates an open cursor across the writes the
    // copy makes into the destination.
    int lvl = 0;
    for (;;)
    {
        int d = b->opendir(s_fs.walk);
        if (d < 0)
        {
            return PROTO_FALSE;
        }
        // The name is read onto the end of the source path; the destination takes the same name
        // through walk_push once the entry is known to exist.
        proto_bool sep = !(slen == 1 && s_fs.walk[0] == '/');
        size_t at = slen + (sep ? 1 : 0);
        pc_mnt_stat cst;
        proto_bool got = PROTO_FALSE;
        if (at + 1 < PC_FILESYSTEM_PATH_MAX)
        {
            for (uint16_t i = 0; i <= s_fs.idx[lvl]; i++)
            {
                got = b->readdir(d, &cst, s_fs.walk + at, PC_FILESYSTEM_PATH_MAX - at);
                if (!got)
                {
                    break;
                }
            }
        }
        b->close(d);

        if (!got) // exhausted: step back out and advance the parent past this level
        {
            s_fs.walk[slen] = '\0';
            if (lvl == 0)
            {
                return PROTO_TRUE;
            }
            slen = walk_pop(s_fs.walk, slen);
            dlen = walk_pop(s_fs.dwalk, dlen);
            lvl--;
            s_fs.idx[lvl]++;
            continue;
        }

        size_t ndlen = walk_push(s_fs.dwalk, dlen, s_fs.walk + at);
        if (sep)
        {
            s_fs.walk[slen] = '/'; // commit the separator the name was written past
        }
        if (ndlen == 0)
        {
            return PROTO_FALSE;
        }

        if (!cst.is_dir)
        {
            proto_bool ok = copy_one(b, s_fs.walk, s_fs.dwalk);
            s_fs.walk[slen] = '\0';
            s_fs.dwalk[dlen] = '\0';
            if (!ok)
            {
                return PROTO_FALSE;
            }
            s_fs.idx[lvl]++;
            continue;
        }
        if (lvl + 1 > PC_FS_MAX_DEPTH)
        {
            return PROTO_FALSE;
        }
        if (!b->mkdir(s_fs.dwalk))
        {
            return PROTO_FALSE;
        }
        slen = proto_scan_nul(s_fs.walk, PC_FILESYSTEM_PATH_MAX);
        dlen = ndlen;
        lvl++;
        s_fs.idx[lvl] = 0;
    }
}

proto_bool pc_fs_mkdir(int root, const char *dir, const char *name)
{
    const pc_mnt_backend *b = store();
    const char *p = resolve_into(0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        return PROTO_FALSE;
    }
    return b->mkdir(p);
}

proto_bool pc_fs_rmdir(int root, const char *dir, const char *name)
{
    const pc_mnt_backend *b = store();
    const char *p = resolve_into(0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        return PROTO_FALSE;
    }
    return b->rmdir(p);
}

int pc_fs_opendir(int root, const char *dir, const char *name)
{
    const pc_mnt_backend *b = store();
    const char *p = resolve_into(0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        return -1;
    }
    return b->opendir(p);
}

proto_bool pc_fs_readdir(int handle, pc_mnt_stat *out, char *name, size_t name_cap)
{
    const pc_mnt_backend *b = store();
    return (b == NULL) ? PROTO_FALSE : b->readdir(handle, out, name, name_cap);
}

long pc_fs_read_file(int root, const char *dir, const char *name, void *buf, size_t cap)
{
    const pc_mnt_backend *b = store();
    const char *p = resolve_into(0, root, dir, name); // resolved once; the loop below works on the handle
    if (b == NULL || p == NULL)
    {
        return -1;
    }
    long sz = b->size(p);
    if (sz < 0 || (size_t)(sz) > cap)
    {
        return -1;
    }
    int h = b->open(p, (int)(PC_MNT_READ));
    if (h < 0)
    {
        return -1;
    }
    size_t total = 0;
    uint8_t *out = (uint8_t *)(buf);
    while (total < (size_t)(sz))
    {
        int r = b->read(h, out + total, (size_t)(sz)-total);
        if (r <= 0)
        {
            break;
        }
        total += (size_t)(r);
    }
    b->close(h);
    return (long)(total);
}

proto_bool pc_fs_write_file(int root, const char *dir, const char *name, const void *buf, size_t n)
{
    const pc_mnt_backend *b = store();
    const char *p = resolve_into(0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        return PROTO_FALSE;
    }
    int h = b->open(p, (int)(PC_MNT_WRITE));
    if (h < 0)
    {
        return PROTO_FALSE;
    }
    size_t total = 0;
    const uint8_t *in = (const uint8_t *)(buf);
    while (total < n)
    {
        int w = b->write(h, in + total, n - total);
        if (w <= 0)
        {
            break;
        }
        total += (size_t)(w);
    }
    b->close(h);
    if (total != n)
    {
        // The store took some of it and stopped. That is the other half of "exhausted": the same bit
        // a caller tests for "there is nowhere to put this", whether the cause is no store at all or
        // a store with no room left.
        s_fs.status |= PC_FS_STORAGE_EXHAUSTED;
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}
