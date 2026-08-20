// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file filesystem.c
 * @brief The filesystem accessor: root ownership, path resolution, and dispatch to the mount.
 *
 * Two path buffers: rename holds a source and a destination at once, every other operation
 * resolves, dispatches, and is done with its buffer before it returns.
 */

#include "server/storage/filesystem/filesystem.h"
#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h" // str: the bounded-run walks
                                    // strncmp (root-name match), memcpy
#include "server/storage/mnt/mnt.h" // Mnt.active: the filesystem every call works through

static uint8_t mnt_work[16]; // the borrow an entry takes; Mnt never reads it

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
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define FILESYSTEM_OFF_CTX 0u
static_assert(FILESYSTEM_OFF_CTX + sizeof(FilesystemCtx) <= PROTOCORE_FILESYSTEM_BORROW,
              "PROTOCORE_FILESYSTEM_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(
    FILESYSTEM_OFF_CTX % _Alignof(FilesystemCtx) == 0,
    "FILESYSTEM_OFF_CTX is not a multiple of alignof(FilesystemCtx) - FILESYSTEM_CTX() would return a misaligned "
    "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define FILESYSTEM_CTX(w) ((FilesystemCtx *)(void *)((w) + FILESYSTEM_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_FILESYSTEM_BORROW persistent bytes
} FilesystemOwnCtx;
static FilesystemOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_filesystem_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_FILESYSTEM_BORROW).buf;
    }
    return s_own.span;
}

// The mounted store, or NULL. A filesystem with no store behind it is a legitimate configuration,
// so the absence is recorded in the status mask rather than returned as a bare false.
static const protocore_mnt_backend *store(uint8_t *restrict work)
{
    Mnt.active(mnt_work);
    const protocore_mnt_backend *b = MntV.backend;
    if (b == NULL)
    {
        FILESYSTEM_CTX(work)->status |= PROTOCORE_FS_STORAGE_EXHAUSTED;
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
static const char *resolve_into(uint8_t *restrict work, int slot, int root, const char *dir, const char *name)
{
    if (root < 0 || root >= (int)FILESYSTEM_CTX(work)->count || dir == NULL || name == NULL)
    {
        FILESYSTEM_CTX(work)->status |= PROTOCORE_FS_BAD_ROOT;
        return NULL;
    }
    // protocore_fs_resolve separates the two ways a path can be rejected; the mask keeps them separate for
    // the caller instead of flattening both into the same NULL.
    int rc = protocore_fs_resolve(FILESYSTEM_CTX(work)->root[root].path, dir, name, FILESYSTEM_CTX(work)->path[slot],
                                  PROTOCORE_FILESYSTEM_PATH_MAX);
    if (rc != 0)
    {
        FILESYSTEM_CTX(work)->status |= (rc == -1) ? PROTOCORE_FS_TRAVERSAL : PROTOCORE_FS_TOO_LONG;
        return NULL;
    }
    return FILESYSTEM_CTX(work)->path[slot];
}

static void fs_status(uint8_t *restrict work)
{
    Fs.bits = FILESYSTEM_CTX(work)->status;
}

static void fs_clear(uint8_t *restrict work)
{
    FILESYSTEM_CTX(work)->status = PROTOCORE_FS_OK;
}

static void fs_present(uint8_t *restrict work)
{
    (void)work;

    // Asked of the mount directly, not of the mask: the mask says what has failed, this says what is
    // true now. A hotswap can attach a store between the two.
    Mnt.active(mnt_work);
    Fs.ok = MntV.backend != NULL;
}

static void fs_begin(uint8_t *restrict work)
{
    const char *name = Fs.mount;

    const char *want = (name == NULL || name[0] == '\0') ? "/" : name;

    // Binding a name already bound hands back the same root. Two services naming the same storage
    // is an arrangement the application is entitled to make, and it must not cost a second root or
    // give them two views of one thing.
    for (uint8_t i = 0; i < FILESYSTEM_CTX(work)->count; i++)
    {
        if (str.eq(FILESYSTEM_CTX(work)->root[i].name, want, PROTOCORE_FS_ROOT_NAME_MAX, PROTO_FALSE))
        {
            Fs.i32 = (int)i;
            return;
        }
    }
    if (FILESYSTEM_CTX(work)->count >= PROTOCORE_FS_MAX_ROOTS)
    {
        Fs.i32 = -1;
        return; // refused, not silently aliased onto someone else's root
    }

    FsRoot *r = &FILESYSTEM_CTX(work)->root[FILESYSTEM_CTX(work)->count];

    // One byte of the capacity is held back for the separator below, so appending cannot overrun.
    size_t n = frame.build(r->path, PROTOCORE_FILESYSTEM_PATH_MAX - 1, FILESYSTEM_ROOT,
                           (const protocore_fval[]){PROTOCORE_VSTR(want)}, 1);
    if (n == 0) // a root that does not fit - refused, not truncated into another directory
    {
        Fs.i32 = -1;
        return;
    }
    if (r->path[n - 1] != '/') // the engine returned the length, so the last byte is an index
    {
        r->path[n] = '/';      // the separator the join relies on, added once here rather than
        r->path[n + 1] = '\0'; // tested on every resolve
    }
    if (frame.build(r->name, PROTOCORE_FS_ROOT_NAME_MAX, FILESYSTEM_ROOT,
                    (const protocore_fval[]){PROTOCORE_VSTR(want)}, 1) == 0)
    {
        Fs.i32 = -1;
        return; // a name too long to record is a name that could not be matched again
    }

    int id = (int)FILESYSTEM_CTX(work)->count;
    FILESYSTEM_CTX(work)->count++;
    Fs.i32 = id;
}

static void fs_resolve(uint8_t *restrict work)
{
    const int root = Fs.path.root;
    const char *dir = Fs.path.dir;
    const char *name = Fs.path.name;

    Fs.text = resolve_into(work, 0, root, dir, name);
}

static void fs_open(uint8_t *restrict work)
{
    const int root = Fs.path.root;
    const char *dir = Fs.path.dir;
    const char *name = Fs.path.name;
    const protocore_mnt_mode mode = Fs.io.mode;

    const protocore_mnt_backend *b = store(work);
    const char *p = resolve_into(work, 0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        Fs.i32 = -1;
        return;
    }
    Fs.i32 = b->open(p, (int)(mode));
    return; // cross the int backend ABI
}

static void fs_read(uint8_t *restrict work)
{
    const int handle = Fs.io.handle;
    void *buf = Fs.io.buf;
    const size_t n = Fs.io.n;

    const protocore_mnt_backend *b = store(work);
    Fs.i32 = (b == NULL) ? -1 : b->read(handle, buf, n);
}

static void fs_write(uint8_t *restrict work)
{
    const int handle = Fs.io.handle;
    const void *buf = Fs.io.wbuf;
    const size_t n = Fs.io.n;

    const protocore_mnt_backend *b = store(work);
    Fs.i32 = (b == NULL) ? -1 : b->write(handle, buf, n);
}

static void fs_close(uint8_t *restrict work)
{
    const int handle = Fs.io.handle;

    const protocore_mnt_backend *b = store(work);
    if (b != NULL)
    {
        b->close(handle);
    }
}

static void fs_seek(uint8_t *restrict work)
{
    const int handle = Fs.io.handle;
    const uint64_t off = Fs.io.off;

    const protocore_mnt_backend *b = store(work);
    Fs.ok = (b == NULL) ? PROTO_FALSE : b->seek(handle, off);
}

static void fs_size(uint8_t *restrict work)
{
    const int root = Fs.path.root;
    const char *dir = Fs.path.dir;
    const char *name = Fs.path.name;

    const protocore_mnt_backend *b = store(work);
    const char *p = resolve_into(work, 0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        Fs.len = -1;
        return;
    }
    Fs.len = b->size(p);
}

static void fs_exists(uint8_t *restrict work)
{
    const int root = Fs.path.root;
    const char *dir = Fs.path.dir;
    const char *name = Fs.path.name;

    const protocore_mnt_backend *b = store(work);
    const char *p = resolve_into(work, 0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        Fs.ok = PROTO_FALSE;
        return;
    }
    Fs.ok = b->exists(p);
}

static void fs_stat(uint8_t *restrict work)
{
    const int root = Fs.path.root;
    const char *dir = Fs.path.dir;
    const char *name = Fs.path.name;
    protocore_mnt_stat *out = Fs.io.stat;

    const protocore_mnt_backend *b = store(work);
    const char *p = resolve_into(work, 0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        Fs.ok = PROTO_FALSE;
        return;
    }
    Fs.ok = b->stat(p, out);
}

static void fs_remove(uint8_t *restrict work)
{
    const int root = Fs.path.root;
    const char *dir = Fs.path.dir;
    const char *name = Fs.path.name;

    const protocore_mnt_backend *b = store(work);
    const char *p = resolve_into(work, 0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        Fs.ok = PROTO_FALSE;
        return;
    }

    // A root is what this file is rooted AT, never a resource inside it, so it is not the walk's to
    // delete. This is the layer that can say so: mnt is blind to what a path means and a caller
    // only knows the path it asked for, while the root table lives here. Without it the walk below
    // empties the whole mount - WebDAV's COPY clears an existing destination before writing, and a
    // Destination resolving here (a mount with an empty fs_root, target "/") turned that into
    // "remove everything", after which the copy failed and answered 409 over an emptied volume.
    const char *rp = FILESYSTEM_CTX(work)->root[root].path;
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
        FILESYSTEM_CTX(work)->status |= PROTOCORE_FS_BAD_ROOT;
        Fs.ok = PROTO_FALSE;
        return;
    }

    // One stat decides which of the two this is. A plain file is the one call it always was; a
    // directory takes the walk below, which is what makes "delete this" mean the same thing to every
    // caller instead of each protocol carrying its own tree logic.
    protocore_mnt_stat st;
    if (!b->stat(p, &st))
    {
        Fs.ok = PROTO_FALSE;
        return;
    }
    if (!st.is_dir)
    {
        Fs.ok = b->remove(p);
        return;
    }

    // The walk. FILESYSTEM_CTX(work)->walk is the current level's path and the stack both: descending appends the
    // child, finishing a level truncates back. Each pass re-opens the current level and takes its
    // first surviving entry, so the cursor is never carried across a removal that would invalidate
    // it - whatever the previous pass removed is already gone when the next open happens.
    size_t len = frame.build(FILESYSTEM_CTX(work)->walk, PROTOCORE_FILESYSTEM_PATH_MAX, FILESYSTEM_ROOT,
                             (const protocore_fval[]){PROTOCORE_VSTR(p)}, 1);
    if (len == 0)
    {
        Fs.ok = PROTO_FALSE;
        return;
    }

    int lvl = 0;
    for (;;)
    {
        int d = b->opendir(FILESYSTEM_CTX(work)->walk);
        if (d < 0)
        {
            Fs.ok = PROTO_FALSE;
            return;
        }
        // The entry's name is read straight onto the end of the path, one byte past the separator,
        // so a matched child is already a full path and an empty directory costs no assembly.
        proto_bool sep = !(len == 1 && FILESYSTEM_CTX(work)->walk[0] == '/');
        size_t at = len + (sep ? 1 : 0);
        protocore_mnt_stat cst;
        proto_bool got = (at + 1 < PROTOCORE_FILESYSTEM_PATH_MAX) &&
                         b->readdir(d, &cst, FILESYSTEM_CTX(work)->walk + at, PROTOCORE_FILESYSTEM_PATH_MAX - at);
        b->close(d);

        if (!got) // drained: remove this level and step back out
        {
            FILESYSTEM_CTX(work)->walk[len] = '\0'; // undo the probe, whatever readdir left there
            if (!b->rmdir(FILESYSTEM_CTX(work)->walk))
            {
                Fs.ok = PROTO_FALSE;
                return;
            }
            if (lvl == 0)
            {
                Fs.ok = PROTO_TRUE;
                return;
            }
            len = walk_pop(FILESYSTEM_CTX(work)->walk, len);
            lvl--;
            continue;
        }
        if (sep)
        {
            FILESYSTEM_CTX(work)->walk[len] = '/'; // commit the separator the name was written past
        }

        if (!cst.is_dir)
        {
            proto_bool ok = b->remove(FILESYSTEM_CTX(work)->walk);
            FILESYSTEM_CTX(work)->walk[len] = '\0'; // back to this level, whatever happened
            if (!ok)
            {
                Fs.ok = PROTO_FALSE;
                return;
            }
            continue; // same level, re-opened next pass
        }
        if (lvl + 1 > PROTOCORE_FS_MAX_DEPTH)
        {
            Fs.ok = PROTO_FALSE;
            return; // refuse a pathologically deep tree rather than walk forever
        }
        len = str.len(FILESYSTEM_CTX(work)->walk, PROTOCORE_FILESYSTEM_PATH_MAX);
        lvl++;
    }
}

static void fs_rename(uint8_t *restrict work)
{
    const int root = Fs.path.root;
    const char *from_dir = Fs.path.dir;
    const char *from_name = Fs.path.name;
    const char *to_dir = Fs.dest.dir;
    const char *to_name = Fs.dest.name;

    const protocore_mnt_backend *b = store(work);
    const char *fp = resolve_into(work, 0, root, from_dir, from_name);
    const char *tp = resolve_into(work, 1, root, to_dir, to_name); // the one op needing both paths live at once
    if (b == NULL || fp == NULL || tp == NULL)
    {
        Fs.ok = PROTO_FALSE;
        return;
    }
    Fs.ok = b->rename(fp, tp);
}

// Copy one file's bytes through the chunk buffer. The only place in this file that holds two handles
// at once, and the only reader of FILESYSTEM_CTX(work)->block.
static proto_bool copy_one(uint8_t *restrict work, const protocore_mnt_backend *b, const char *src, const char *dst)
{
    int in = b->open(src, (int)(PROTOCORE_MNT_READ));
    if (in < 0)
    {
        return PROTO_FALSE;
    }
    int out = b->open(dst, (int)(PROTOCORE_MNT_WRITE));
    if (out < 0)
    {
        b->close(in);
        return PROTO_FALSE;
    }
    proto_bool ok = PROTO_TRUE;
    for (;;)
    {
        int n = b->read(in, FILESYSTEM_CTX(work)->block, sizeof(FILESYSTEM_CTX(work)->block));
        if (n <= 0)
        {
            break;
        }
        if (b->write(out, FILESYSTEM_CTX(work)->block, (size_t)(n)) != n)
        {
            ok = PROTO_FALSE; // out of space / write fault: reported, not left as a short copy
            break;
        }
    }
    b->close(in);
    b->close(out);
    return ok;
}

static void fs_copy(uint8_t *restrict work)
{
    const int root = Fs.path.root;
    const char *from_dir = Fs.path.dir;
    const char *from_name = Fs.path.name;
    const char *to_dir = Fs.dest.dir;
    const char *to_name = Fs.dest.name;

    const protocore_mnt_backend *b = store(work);
    const char *sp = resolve_into(work, 0, root, from_dir, from_name);
    const char *dp = resolve_into(work, 1, root, to_dir, to_name); // both paths live at once, as in rename
    if (b == NULL || sp == NULL || dp == NULL)
    {
        Fs.ok = PROTO_FALSE;
        return;
    }

    protocore_mnt_stat st;
    if (!b->stat(sp, &st))
    {
        Fs.ok = PROTO_FALSE;
        return;
    }
    if (!st.is_dir)
    {
        Fs.ok = copy_one(work, b, sp, dp);
        return;
    }

    size_t slen = frame.build(FILESYSTEM_CTX(work)->walk, PROTOCORE_FILESYSTEM_PATH_MAX, FILESYSTEM_ROOT,
                              (const protocore_fval[]){PROTOCORE_VSTR(sp)}, 1);
    size_t dlen = frame.build(FILESYSTEM_CTX(work)->dwalk, PROTOCORE_FILESYSTEM_PATH_MAX, FILESYSTEM_ROOT,
                              (const protocore_fval[]){PROTOCORE_VSTR(dp)}, 1);
    if (slen == 0 || dlen == 0)
    {
        Fs.ok = PROTO_FALSE;
        return;
    }
    if (!b->mkdir(FILESYSTEM_CTX(work)->dwalk))
    {
        Fs.ok = PROTO_FALSE;
        return;
    }
    FILESYSTEM_CTX(work)->idx[0] = 0;

    // The same walk as protocore_fs_remove, on two paths pushed and popped in lockstep, with one
    // difference: a copy does not consume what it reads, so re-opening and taking the first entry
    // would repeat that entry forever. Each level records the child it left off at and skips forward
    // to it, which stays correct even when a store invalidates an open cursor across the writes the
    // copy makes into the destination.
    int lvl = 0;
    for (;;)
    {
        int d = b->opendir(FILESYSTEM_CTX(work)->walk);
        if (d < 0)
        {
            Fs.ok = PROTO_FALSE;
            return;
        }
        // The name is read onto the end of the source path; the destination takes the same name
        // through walk_push once the entry is known to exist.
        proto_bool sep = !(slen == 1 && FILESYSTEM_CTX(work)->walk[0] == '/');
        size_t at = slen + (sep ? 1 : 0);
        protocore_mnt_stat cst;
        proto_bool got = PROTO_FALSE;
        if (at + 1 < PROTOCORE_FILESYSTEM_PATH_MAX)
        {
            for (uint16_t i = 0; i <= FILESYSTEM_CTX(work)->idx[lvl]; i++)
            {
                got = b->readdir(d, &cst, FILESYSTEM_CTX(work)->walk + at, PROTOCORE_FILESYSTEM_PATH_MAX - at);
                if (!got)
                {
                    break;
                }
            }
        }
        b->close(d);

        if (!got) // exhausted: step back out and advance the parent past this level
        {
            FILESYSTEM_CTX(work)->walk[slen] = '\0';
            if (lvl == 0)
            {
                Fs.ok = PROTO_TRUE;
                return;
            }
            slen = walk_pop(FILESYSTEM_CTX(work)->walk, slen);
            dlen = walk_pop(FILESYSTEM_CTX(work)->dwalk, dlen);
            lvl--;
            FILESYSTEM_CTX(work)->idx[lvl]++;
            continue;
        }

        size_t ndlen = walk_push(FILESYSTEM_CTX(work)->dwalk, dlen, FILESYSTEM_CTX(work)->walk + at);
        if (sep)
        {
            FILESYSTEM_CTX(work)->walk[slen] = '/'; // commit the separator the name was written past
        }
        if (ndlen == 0)
        {
            Fs.ok = PROTO_FALSE;
            return;
        }

        if (!cst.is_dir)
        {
            proto_bool ok = copy_one(work, b, FILESYSTEM_CTX(work)->walk, FILESYSTEM_CTX(work)->dwalk);
            FILESYSTEM_CTX(work)->walk[slen] = '\0';
            FILESYSTEM_CTX(work)->dwalk[dlen] = '\0';
            if (!ok)
            {
                Fs.ok = PROTO_FALSE;
                return;
            }
            FILESYSTEM_CTX(work)->idx[lvl]++;
            continue;
        }
        if (lvl + 1 > PROTOCORE_FS_MAX_DEPTH)
        {
            Fs.ok = PROTO_FALSE;
            return;
        }
        if (!b->mkdir(FILESYSTEM_CTX(work)->dwalk))
        {
            Fs.ok = PROTO_FALSE;
            return;
        }
        slen = str.len(FILESYSTEM_CTX(work)->walk, PROTOCORE_FILESYSTEM_PATH_MAX);
        dlen = ndlen;
        lvl++;
        FILESYSTEM_CTX(work)->idx[lvl] = 0;
    }
}

static void fs_mkdir(uint8_t *restrict work)
{
    const int root = Fs.path.root;
    const char *dir = Fs.path.dir;
    const char *name = Fs.path.name;

    const protocore_mnt_backend *b = store(work);
    const char *p = resolve_into(work, 0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        Fs.ok = PROTO_FALSE;
        return;
    }
    Fs.ok = b->mkdir(p);
}

static void fs_rmdir(uint8_t *restrict work)
{
    const int root = Fs.path.root;
    const char *dir = Fs.path.dir;
    const char *name = Fs.path.name;

    const protocore_mnt_backend *b = store(work);
    const char *p = resolve_into(work, 0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        Fs.ok = PROTO_FALSE;
        return;
    }
    Fs.ok = b->rmdir(p);
}

static void fs_opendir(uint8_t *restrict work)
{
    const int root = Fs.path.root;
    const char *dir = Fs.path.dir;
    const char *name = Fs.path.name;

    const protocore_mnt_backend *b = store(work);
    const char *p = resolve_into(work, 0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        Fs.i32 = -1;
        return;
    }
    Fs.i32 = b->opendir(p);
}

static void fs_readdir(uint8_t *restrict work)
{
    const int handle = Fs.io.handle;
    protocore_mnt_stat *out = Fs.io.stat;
    char *name = Fs.io.name_out;
    const size_t name_cap = Fs.io.name_cap;

    const protocore_mnt_backend *b = store(work);
    Fs.ok = (b == NULL) ? PROTO_FALSE : b->readdir(handle, out, name, name_cap);
}

static void fs_read_file(uint8_t *restrict work)
{
    const int root = Fs.path.root;
    const char *dir = Fs.path.dir;
    const char *name = Fs.path.name;
    void *buf = Fs.io.buf;
    const size_t cap = Fs.io.n;

    const protocore_mnt_backend *b = store(work);
    const char *p = resolve_into(work, 0, root, dir, name); // resolved once; the loop below works on the handle
    if (b == NULL || p == NULL)
    {
        Fs.len = -1;
        return;
    }
    long sz = b->size(p);
    if (sz < 0 || (size_t)(sz) > cap)
    {
        Fs.len = -1;
        return;
    }
    int h = b->open(p, (int)(PROTOCORE_MNT_READ));
    if (h < 0)
    {
        Fs.len = -1;
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
    Fs.len = (long)(total);
}

static void fs_write_file(uint8_t *restrict work)
{
    const int root = Fs.path.root;
    const char *dir = Fs.path.dir;
    const char *name = Fs.path.name;
    const void *buf = Fs.io.wbuf;
    const size_t n = Fs.io.n;

    const protocore_mnt_backend *b = store(work);
    const char *p = resolve_into(work, 0, root, dir, name);
    if (b == NULL || p == NULL)
    {
        Fs.ok = PROTO_FALSE;
        return;
    }
    int h = b->open(p, (int)(PROTOCORE_MNT_WRITE));
    if (h < 0)
    {
        Fs.ok = PROTO_FALSE;
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
        FILESYSTEM_CTX(work)->status |= PROTOCORE_FS_STORAGE_EXHAUSTED;
        Fs.ok = PROTO_FALSE;
        return;
    }
    Fs.ok = PROTO_TRUE;
}

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
                   .write_file = fs_write_file};
