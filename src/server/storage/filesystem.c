// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file filesystem.c
 * @brief The filesystem accessor: root ownership, path resolution, and dispatch to the mount.
 *
 * Two path buffers: rename holds a source and a destination at once, every other operation
 * resolves, dispatches, and is done with its buffer before it returns.
 */

#include "server/storage/filesystem.h"
#include "mmgr/protomem.h"
#include "mmgr/protostr.h" // str: the bounded-run walks
                           // strncmp (root-name match), memcpy

// One bound root. The prefix is a copy, not the caller's pointer, and always ends '/', so the join
// concatenates `root` and `dir` without adding a separator.
typedef struct
{
    char name[PROTOCORE_FS_ROOT_NAME_MAX];    ///< what a service asked for, e.g. "mnt/scp".
    char path[PROTOCORE_FILESYSTEM_PATH_MAX]; ///< the prefix it resolves to; always ends '/'.
} FsRoot;

typedef struct
{
    FsRoot root[PROTOCORE_FS_MAX_ROOTS];
    uint8_t count;
    char path[2][PROTOCORE_FILESYSTEM_PATH_MAX];

    // The tree walks' path (protocore_fs_remove / protocore_fs_copy), plus the destination a copy writes to. One
    // buffer each, not one per level: the path is the stack. Descending appends "/child", ascending
    // truncates at the last separator, and readdir writes the entry's own name at the append point.
    char walk[PROTOCORE_FILESYSTEM_PATH_MAX];
    char dwalk[PROTOCORE_FILESYSTEM_PATH_MAX];

    // Where each level resumes. Remove re-opens and takes the first survivor every pass, so its
    // cursor cannot be invalidated by its own removals; copy does not consume what it reads, so it
    // re-opens and skips to where it left off.
    uint16_t idx[PROTOCORE_FS_MAX_DEPTH + 2];

    // One storage block. The copy transfers whole blocks, so the store never read-modify-writes a
    // partial one (see PROTOCORE_FS_BLOCK).
    uint8_t block[PROTOCORE_FS_BLOCK];

    uint32_t status; ///< sticky reasons operations have failed (see the status block in filesystem.h).
} FilesystemCtx;
static FilesystemCtx s_fs;

// The mounted store, or NULL. A filesystem with no store behind it is a legitimate configuration,
// so the absence is recorded in the status mask rather than returned as a bare false.
static const protocore_mnt_backend *store(void)
{
    const protocore_mnt_backend *b = protocore_mnt_active();
    if (b == NULL)
    {
        s_fs.status |= PROTOCORE_FS_STORAGE_EXHAUSTED;
    }
    return b;
}

// Descend @p path (length @p len) into @p child in place: append the separator and the name.
// @return the new length, or 0 if it would not fit - which stops the walk rather than truncating a
// name into a different file's.
static size_t walk_push(char *path, size_t len, const char *child)
{
    size_t n = str.len(child, PROTOCORE_FILESYSTEM_PATH_MAX);
    // The root already IS the separator, so it is the one path that does not get another.
    proto_bool need_sep = !(len == 1 && path[0] == '/');
    if (len + (need_sep ? 1 : 0) + n + 1 > PROTOCORE_FILESYSTEM_PATH_MAX)
    {
        return 0;
    }
    if (need_sep)
    {
        path[len++] = '/';
    }
    mem.cpy(path + len, child, n);
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
        s_fs.status |= PROTOCORE_FS_BAD_ROOT;
        return NULL;
    }
    // protocore_fs_resolve separates the two ways a path can be rejected; the mask keeps them separate for
    // the caller instead of flattening both into the same NULL.
    int rc = protocore_fs_resolve(s_fs.root[root].path, dir, name, s_fs.path[slot], PROTOCORE_FILESYSTEM_PATH_MAX);
    if (rc != 0)
    {
        s_fs.status |= (rc == -1) ? PROTOCORE_FS_TRAVERSAL : PROTOCORE_FS_TOO_LONG;
        return NULL;
    }
    return s_fs.path[slot];
}

static void fs_status(struct FilesystemInternal *restrict ctx)
{

    ctx->ns->bits = s_fs.status;
}

static void fs_clear(struct FilesystemInternal *restrict ctx)
{
    (void)ctx;

    s_fs.status = PROTOCORE_FS_OK;
}

static void fs_present(struct FilesystemInternal *restrict ctx)
{

    // Asked of the mount directly, not of the mask: the mask says what has failed, this says what is
    // true now. A hotswap can attach a store between the two.
    ctx->ns->ok = protocore_mnt_active() != NULL;
}

static void fs_begin(struct FilesystemInternal *restrict ctx)
{
    const char *name = ctx->ns->mount;

    const char *want = (name == NULL || name[0] == '\0') ? "/" : name;

    // Binding a name already bound hands back the same root. Two services naming the same storage
    // is an arrangement the application is entitled to make, and it must not cost a second root or
    // give them two views of one thing.
    for (uint8_t i = 0; i < s_fs.count; i++)
    {
        if (strncmp(s_fs.root[i].name, want, PROTOCORE_FS_ROOT_NAME_MAX) == 0)
        {
            ctx->ns->i32 = (int)i;
            return;
        }
    }
    if (s_fs.count >= PROTOCORE_FS_MAX_ROOTS)
    {
        ctx->ns->i32 = -1;
        return; // refused, not silently aliased onto someone else's root
    }

    FsRoot *r = &s_fs.root[s_fs.count];

    // One byte of the capacity is held back for the separator below, so appending cannot overrun.
    size_t n = frame.build(r->path, PROTOCORE_FILESYSTEM_PATH_MAX - 1, FILESYSTEM_ROOT, (const protocore_fval[]){PROTOCORE_VSTR(want)}, 1);
    if (n == 0) // a root that does not fit - refused, not truncated into another directory
    {
        ctx->ns->i32 = -1;
        return;
    }
    if (r->path[n - 1] != '/') // the engine returned the length, so the last byte is an index
    {
        r->path[n] = '/';      // the separator the join relies on, added once here rather than
        r->path[n + 1] = '\0'; // tested on every resolve
    }
    if (frame.build(r->name, PROTOCORE_FS_ROOT_NAME_MAX, FILESYSTEM_ROOT, (const protocore_fval[]){PROTOCORE_VSTR(want)}, 1) == 0)
    {
        ctx->ns->i32 = -1;
        return; // a name too long to record is a name that could not be matched again
    }

    int id = (int)s_fs.count;
    s_fs.count++;
    ctx->ns->i32 = id;
}

static void fs_resolve(struct FilesystemInternal *restrict ctx)
{
    const int root = ctx->ns->path.root;
    const char *dir = ctx->ns->path.dir;
    const char *name = ctx->ns->path.name;

    ctx->ns->text = resolve_into(0, root, dir, name);
}

static void fs_open(struct FilesystemInternal *restrict ctx)
{
    const int root = ctx->ns->path.root;
    const char *dir = ctx->ns->path.dir;
    const char *name = ctx->ns->path.name;
    const protocore_mnt_mode mode = ctx->ns->io.mode;

    const protocore_mnt_backend *b = store();
    const char *p = resolve_into(0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        ctx->ns->i32 = -1;
        return;
    }
    ctx->ns->i32 = b->open(p, (int)(mode));
    return; // cross the int backend ABI
}

static void fs_read(struct FilesystemInternal *restrict ctx)
{
    const int handle = ctx->ns->io.handle;
    void *buf = ctx->ns->io.buf;
    const size_t n = ctx->ns->io.n;

    const protocore_mnt_backend *b = store();
    ctx->ns->i32 = (b == NULL) ? -1 : b->read(handle, buf, n);
}

static void fs_write(struct FilesystemInternal *restrict ctx)
{
    const int handle = ctx->ns->io.handle;
    const void *buf = ctx->ns->io.wbuf;
    const size_t n = ctx->ns->io.n;

    const protocore_mnt_backend *b = store();
    ctx->ns->i32 = (b == NULL) ? -1 : b->write(handle, buf, n);
}

static void fs_close(struct FilesystemInternal *restrict ctx)
{
    const int handle = ctx->ns->io.handle;

    const protocore_mnt_backend *b = store();
    if (b != NULL)
    {
        b->close(handle);
    }
}

static void fs_seek(struct FilesystemInternal *restrict ctx)
{
    const int handle = ctx->ns->io.handle;
    const uint64_t off = ctx->ns->io.off;

    const protocore_mnt_backend *b = store();
    ctx->ns->ok = (b == NULL) ? PROTO_FALSE : b->seek(handle, off);
}

static void fs_size(struct FilesystemInternal *restrict ctx)
{
    const int root = ctx->ns->path.root;
    const char *dir = ctx->ns->path.dir;
    const char *name = ctx->ns->path.name;

    const protocore_mnt_backend *b = store();
    const char *p = resolve_into(0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        ctx->ns->len = -1;
        return;
    }
    ctx->ns->len = b->size(p);
}

static void fs_exists(struct FilesystemInternal *restrict ctx)
{
    const int root = ctx->ns->path.root;
    const char *dir = ctx->ns->path.dir;
    const char *name = ctx->ns->path.name;

    const protocore_mnt_backend *b = store();
    const char *p = resolve_into(0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    ctx->ns->ok = b->exists(p);
}

static void fs_stat(struct FilesystemInternal *restrict ctx)
{
    const int root = ctx->ns->path.root;
    const char *dir = ctx->ns->path.dir;
    const char *name = ctx->ns->path.name;
    protocore_mnt_stat *out = ctx->ns->io.stat;

    const protocore_mnt_backend *b = store();
    const char *p = resolve_into(0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    ctx->ns->ok = b->stat(p, out);
}

static void fs_remove(struct FilesystemInternal *restrict ctx)
{
    const int root = ctx->ns->path.root;
    const char *dir = ctx->ns->path.dir;
    const char *name = ctx->ns->path.name;

    const protocore_mnt_backend *b = store();
    const char *p = resolve_into(0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }

    // A root is what this file is rooted AT, never a resource inside it, so it is not the walk's to
    // delete. This is the layer that can say so: mnt is blind to what a path means and a caller
    // only knows the path it asked for, while the root table lives here. Without it the walk below
    // empties the whole mount - WebDAV's COPY clears an existing destination before writing, and a
    // Destination resolving here (a mount with an empty fs_root, target "/") turned that into
    // "remove everything", after which the copy failed and answered 409 over an emptied volume.
    const char *rp = s_fs.root[root].path;
    size_t rlen = str.len(rp, PROTOCORE_FILESYSTEM_PATH_MAX);
    size_t plen = str.len(p, PROTOCORE_FILESYSTEM_PATH_MAX);
    if (rlen > 0 && rp[rlen - 1] == '/') // the root always carries the separator; a resolve may not
    {
        rlen--;
    }
    if (plen > 0 && p[plen - 1] == '/')
    {
        plen--;
    }
    if (plen == rlen && mem.cmp(p, rp, plen) == 0)
    {
        s_fs.status |= PROTOCORE_FS_BAD_ROOT;
        ctx->ns->ok = PROTO_FALSE;
        return;
    }

    // One stat decides which of the two this is. A plain file is the one call it always was; a
    // directory takes the walk below, which is what makes "delete this" mean the same thing to every
    // caller instead of each protocol carrying its own tree logic.
    protocore_mnt_stat st;
    if (!b->stat(p, &st))
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    if (!st.is_dir)
    {
        ctx->ns->ok = b->remove(p);
        return;
    }

    // The walk. s_fs.walk is the current level's path and the stack both: descending appends the
    // child, finishing a level truncates back. Each pass re-opens the current level and takes its
    // first surviving entry, so the cursor is never carried across a removal that would invalidate
    // it - whatever the previous pass removed is already gone when the next open happens.
    size_t len = frame.build(s_fs.walk, PROTOCORE_FILESYSTEM_PATH_MAX, FILESYSTEM_ROOT, (const protocore_fval[]){PROTOCORE_VSTR(p)}, 1);
    if (len == 0)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }

    int lvl = 0;
    for (;;)
    {
        int d = b->opendir(s_fs.walk);
        if (d < 0)
        {
            ctx->ns->ok = PROTO_FALSE;
            return;
        }
        // The entry's name is read straight onto the end of the path, one byte past the separator,
        // so a matched child is already a full path and an empty directory costs no assembly.
        proto_bool sep = !(len == 1 && s_fs.walk[0] == '/');
        size_t at = len + (sep ? 1 : 0);
        protocore_mnt_stat cst;
        proto_bool got =
            (at + 1 < PROTOCORE_FILESYSTEM_PATH_MAX) && b->readdir(d, &cst, s_fs.walk + at, PROTOCORE_FILESYSTEM_PATH_MAX - at);
        b->close(d);

        if (!got) // drained: remove this level and step back out
        {
            s_fs.walk[len] = '\0'; // undo the probe, whatever readdir left there
            if (!b->rmdir(s_fs.walk))
            {
                ctx->ns->ok = PROTO_FALSE;
                return;
            }
            if (lvl == 0)
            {
                ctx->ns->ok = PROTO_TRUE;
                return;
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
                ctx->ns->ok = PROTO_FALSE;
                return;
            }
            continue; // same level, re-opened next pass
        }
        if (lvl + 1 > PROTOCORE_FS_MAX_DEPTH)
        {
            ctx->ns->ok = PROTO_FALSE;
            return; // refuse a pathologically deep tree rather than walk forever
        }
        len = str.len(s_fs.walk, PROTOCORE_FILESYSTEM_PATH_MAX);
        lvl++;
    }
}

static void fs_rename(struct FilesystemInternal *restrict ctx)
{
    const int root = ctx->ns->path.root;
    const char *from_dir = ctx->ns->path.dir;
    const char *from_name = ctx->ns->path.name;
    const char *to_dir = ctx->ns->dest.dir;
    const char *to_name = ctx->ns->dest.name;

    const protocore_mnt_backend *b = store();
    const char *fp = resolve_into(0, root, from_dir, from_name);
    const char *tp = resolve_into(1, root, to_dir, to_name); // the one op needing both paths live at once
    if (b == NULL || fp == NULL || tp == NULL)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    ctx->ns->ok = b->rename(fp, tp);
}

// Copy one file's bytes through the chunk buffer. The only place in this file that holds two handles
// at once, and the only reader of s_fs.block.
static proto_bool copy_one(const protocore_mnt_backend *b, const char *src, const char *dst)
{
    int in = b->open(src, (int)(PROTOCORE_MNT_READ));
    if (in < 0)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    int out = b->open(dst, (int)(PROTOCORE_MNT_WRITE));
    if (out < 0)
    {
        b->close(in);
        ctx->ns->ok = PROTO_FALSE;
        return;
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
    ctx->ns->ok = ok;
}

static void fs_copy(struct FilesystemInternal *restrict ctx)
{
    const int root = ctx->ns->path.root;
    const char *from_dir = ctx->ns->path.dir;
    const char *from_name = ctx->ns->path.name;
    const char *to_dir = ctx->ns->dest.dir;
    const char *to_name = ctx->ns->dest.name;

    const protocore_mnt_backend *b = store();
    const char *sp = resolve_into(0, root, from_dir, from_name);
    const char *dp = resolve_into(1, root, to_dir, to_name); // both paths live at once, as in rename
    if (b == NULL || sp == NULL || dp == NULL)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }

    protocore_mnt_stat st;
    if (!b->stat(sp, &st))
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    if (!st.is_dir)
    {
        ctx->ns->ok = copy_one(b, sp, dp);
        return;
    }

    size_t slen = frame.build(s_fs.walk, PROTOCORE_FILESYSTEM_PATH_MAX, FILESYSTEM_ROOT, (const protocore_fval[]){PROTOCORE_VSTR(sp)}, 1);
    size_t dlen = frame.build(s_fs.dwalk, PROTOCORE_FILESYSTEM_PATH_MAX, FILESYSTEM_ROOT, (const protocore_fval[]){PROTOCORE_VSTR(dp)}, 1);
    if (slen == 0 || dlen == 0)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    if (!b->mkdir(s_fs.dwalk))
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    s_fs.idx[0] = 0;

    // The same walk as protocore_fs_remove, on two paths pushed and popped in lockstep, with one
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
            ctx->ns->ok = PROTO_FALSE;
            return;
        }
        // The name is read onto the end of the source path; the destination takes the same name
        // through walk_push once the entry is known to exist.
        proto_bool sep = !(slen == 1 && s_fs.walk[0] == '/');
        size_t at = slen + (sep ? 1 : 0);
        protocore_mnt_stat cst;
        proto_bool got = PROTO_FALSE;
        if (at + 1 < PROTOCORE_FILESYSTEM_PATH_MAX)
        {
            for (uint16_t i = 0; i <= s_fs.idx[lvl]; i++)
            {
                got = b->readdir(d, &cst, s_fs.walk + at, PROTOCORE_FILESYSTEM_PATH_MAX - at);
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
                ctx->ns->ok = PROTO_TRUE;
                return;
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
            ctx->ns->ok = PROTO_FALSE;
            return;
        }

        if (!cst.is_dir)
        {
            proto_bool ok = copy_one(b, s_fs.walk, s_fs.dwalk);
            s_fs.walk[slen] = '\0';
            s_fs.dwalk[dlen] = '\0';
            if (!ok)
            {
                ctx->ns->ok = PROTO_FALSE;
                return;
            }
            s_fs.idx[lvl]++;
            continue;
        }
        if (lvl + 1 > PROTOCORE_FS_MAX_DEPTH)
        {
            ctx->ns->ok = PROTO_FALSE;
            return;
        }
        if (!b->mkdir(s_fs.dwalk))
        {
            ctx->ns->ok = PROTO_FALSE;
            return;
        }
        slen = str.len(s_fs.walk, PROTOCORE_FILESYSTEM_PATH_MAX);
        dlen = ndlen;
        lvl++;
        s_fs.idx[lvl] = 0;
    }
}

static void fs_mkdir(struct FilesystemInternal *restrict ctx)
{
    const int root = ctx->ns->path.root;
    const char *dir = ctx->ns->path.dir;
    const char *name = ctx->ns->path.name;

    const protocore_mnt_backend *b = store();
    const char *p = resolve_into(0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    ctx->ns->ok = b->mkdir(p);
}

static void fs_rmdir(struct FilesystemInternal *restrict ctx)
{
    const int root = ctx->ns->path.root;
    const char *dir = ctx->ns->path.dir;
    const char *name = ctx->ns->path.name;

    const protocore_mnt_backend *b = store();
    const char *p = resolve_into(0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    ctx->ns->ok = b->rmdir(p);
}

static void fs_opendir(struct FilesystemInternal *restrict ctx)
{
    const int root = ctx->ns->path.root;
    const char *dir = ctx->ns->path.dir;
    const char *name = ctx->ns->path.name;

    const protocore_mnt_backend *b = store();
    const char *p = resolve_into(0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        ctx->ns->i32 = -1;
        return;
    }
    ctx->ns->i32 = b->opendir(p);
}

static void fs_readdir(struct FilesystemInternal *restrict ctx)
{
    const int handle = ctx->ns->io.handle;
    protocore_mnt_stat *out = ctx->ns->io.stat;
    char *name = ctx->ns->io.name_out;
    const size_t name_cap = ctx->ns->io.name_cap;

    const protocore_mnt_backend *b = store();
    ctx->ns->ok = (b == NULL) ? PROTO_FALSE : b->readdir(handle, out, name, name_cap);
}

static void fs_read_file(struct FilesystemInternal *restrict ctx)
{
    const int root = ctx->ns->path.root;
    const char *dir = ctx->ns->path.dir;
    const char *name = ctx->ns->path.name;
    void *buf = ctx->ns->io.buf;
    const size_t cap = ctx->ns->io.n;

    const protocore_mnt_backend *b = store();
    const char *p = resolve_into(0, root, dir, name); // resolved once; the loop below works on the handle
    if (b == NULL || p == NULL)
    {
        ctx->ns->len = -1;
        return;
    }
    long sz = b->size(p);
    if (sz < 0 || (size_t)(sz) > cap)
    {
        ctx->ns->len = -1;
        return;
    }
    int h = b->open(p, (int)(PROTOCORE_MNT_READ));
    if (h < 0)
    {
        ctx->ns->len = -1;
        return;
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
    ctx->ns->len = (long)(total);
}

static void fs_write_file(struct FilesystemInternal *restrict ctx)
{
    const int root = ctx->ns->path.root;
    const char *dir = ctx->ns->path.dir;
    const char *name = ctx->ns->path.name;
    const void *buf = ctx->ns->io.wbuf;
    const size_t n = ctx->ns->io.n;

    const protocore_mnt_backend *b = store();
    const char *p = resolve_into(0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    int h = b->open(p, (int)(PROTOCORE_MNT_WRITE));
    if (h < 0)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
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
        s_fs.status |= PROTOCORE_FS_STORAGE_EXHAUSTED;
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    ctx->ns->ok = PROTO_TRUE;
}

/**
 * @brief The mount state and the calls that reach it - what FilesystemNs points at.
 *
 * @var FilesystemInternal::ns  the handle a caller sets a call's members on
 */
struct FilesystemInternal
{
    FilesystemNs *ns;
};

static struct FilesystemInternal s_fs_ctx = {.ns = &Fs};

// Designated, so a member's position in the struct does not decide what it binds to.
FilesystemNs Fs = {.status = fs_status,
                   .clear = fs_clear,
                   .present = fs_present,
                   .begin = fs_begin,
                   .resolve = fs_resolve,
                   .open = fs_open,
                   .read = fs_read,
                   .write = fs_write,
                   .close = fs_close,
                   .seek = fs_seek,
                   .size = fs_size,
                   .exists = fs_exists,
                   .stat = fs_stat,
                   .remove = fs_remove,
                   .rename = fs_rename,
                   .copy = fs_copy,
                   .mkdir = fs_mkdir,
                   .rmdir = fs_rmdir,
                   .opendir = fs_opendir,
                   .readdir = fs_readdir,
                   .read_file = fs_read_file,
                   .write_file = fs_write_file,
                   .internal = &s_fs_ctx};
