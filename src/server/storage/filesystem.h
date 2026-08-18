// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file filesystem.h
 * @brief The filesystem accessor: it owns the mount root, it owns the resolved path, and it is the
 *        only way a request reaches storage.
 *
 * A wire protocol (SFTP, SCP, WebDAV) knows a *request* path - the bytes a client sent. It does not
 * know where the mount lives, and it must not be able to escape it. So it never builds a path and
 * never holds one: it hands the request path to an operation here, and this file joins it onto the
 * root, rejects `..`, and dispatches to the mounted backend (mnt.h, alongside this file).
 *
 * That is why the operations take a request path rather than returning one. An accessor that
 * returned a resolved string would put a path buffer, its capacity, and its overflow check into
 * every caller - which is exactly the duplication that had nine identical `char disk[]` arrays and
 * two copies of the `..` guard spread across the SFTP and SCP servers. There is one buffer here,
 * and callers do not size it, see it, or carry it.
 *
 * protocore_fs_path() is the single exception, for the one caller that genuinely needs the text: SFTP
 * REALPATH answers with a path string. It hands back a pointer into this file's storage, or
 * nullptr when it cannot - there is no out/capacity pair to get wrong.
 *
 * The `..` guard is a rejection, not realpath/symlink resolution: the on-flash filesystems
 * (the small embedded ones) have no symlinks, so a `..`-free joined path cannot leave the root.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_FILESYSTEM_H
#define PROTOCORE_FILESYSTEM_H

#include "protocore_config.h"

#include "mmgr/protoframe.h" // the one frame engine
#include "server/storage/mnt.h"

PROTOCORE_BEGIN_DECLS

// root, dir, name. A whole path is these three pieces, so it is ONE build: a caller that assembled
// dir+name itself and handed the result over would frame the same bytes twice, into two buffers,
// for the same result. A mount root ends with '/', and a dir that carries a name ends with '/', so
// both separators are already in the strings and the spec needs no literal between the fields.
static const protocore_field FILESYSTEM_JOIN[] = {PROTOCORE_STR, PROTOCORE_STR, PROTOCORE_STR, PROTOCORE_END};

// The mount root, copied in so its trailing '/' is owned rather than assumed (see protocore_fs_begin).
static const protocore_field FILESYSTEM_ROOT[] = {PROTOCORE_STR, PROTOCORE_END};

/**
 * @brief Roots that can be bound at once (see protocore_fs_begin).
 *
 * One per service that wants its own storage - "mnt/scp", "mnt/sftp", a local region held for temp
 * files - and more can be bound by raising this. Two services naming the same root share it and
 * cost one entry.
 */
#ifndef PROTOCORE_FS_MAX_ROOTS
#define PROTOCORE_FS_MAX_ROOTS 4
#endif

/** @brief Longest root name (e.g. "mnt/sftp"), including the terminator. */
#ifndef PROTOCORE_FS_ROOT_NAME_MAX
#define PROTOCORE_FS_ROOT_NAME_MAX 24
#endif

/**
 * @brief How deep protocore_fs_remove() / protocore_fs_copy() descend before refusing a tree.
 *
 * The bound is what turns a tree walk into a fixed cost: one level of path storage per level, all of
 * it in this file's context, and no call recursion - the walks are loops over that array, so the
 * depth a request can force is the array's extent rather than however many stack frames it takes to
 * exhaust RAM.
 */
#ifndef PROTOCORE_FS_MAX_DEPTH
#define PROTOCORE_FS_MAX_DEPTH 8
#endif

/**
 * @brief The storage block this file aligns its transfers to.
 *
 * Block alignment is ours, not the caller's and not the store's. Flash and SD both erase and program
 * in blocks: a transfer that is not a whole block makes the store read the block, merge, and write
 * it back, so an unaligned copy costs a read-modify-write per block and burns an erase cycle to move
 * bytes that were already there. 512 is the SD sector and the common flash program page, so one
 * block is one operation on both.
 *
 * This is why protocore_fs_copy() moves a block at a time rather than "some convenient buffer size", and
 * why the number lives here instead of at each call site - a caller cannot know what the mounted
 * store erases in, and should not have to.
 */
#ifndef PROTOCORE_FS_BLOCK
#define PROTOCORE_FS_BLOCK 512
#endif

/** @brief Join a mount @p root, a request @p dir, and a leaf @p name into @p out.
 *
 * @param name the leaf, or "" when @p dir is the whole path. When @p name is given, @p dir must end
 *             with '/' - that is what a directory destination means, and it is known at the call
 *             site rather than tested here.
 * @return bytes written, or 0 on overflow - the engine already knows the length, so it is handed
 *         back rather than left for the caller to rediscover with a scan. */
PROTOCORE_INLINE size_t protocore_fs_join(const char *root, const char *dir, const char *name, char *out, size_t cap)
{
    if (dir[0] == '/')
    {
        dir++; // the root carries the separator; a second one would be "//"
    }
    return frame.build(out, cap, FILESYSTEM_JOIN,
                       (const protocore_fval[]){PROTOCORE_VSTR(root), PROTOCORE_VSTR(dir), PROTOCORE_VSTR(name)}, 3);
}

/**
 * @brief Resolve a mount @p root + a request @p dir + a leaf @p name to an on-disk path in @p out:
 *        reject any `..` traversal, join onto the root, and drop a trailing '/'.
 * @return 0 on success, -1 on a traversal attempt (`..` present), -2 if the joined path would
 *         overflow @p out.
 */
/** @brief True if @p s contains a `..` traversal.
 *
 * ".." is two bytes at one offset, not a pattern to search for: compare the pair and advance. */
PROTOCORE_INLINE proto_bool protocore_fs_has_dotdot(const char *s)
{
    for (const char *p = s; p[0] != '\0' && p[1] != '\0'; p++)
    {
        if (p[0] == '.' && p[1] == '.')
        {
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

PROTOCORE_INLINE int protocore_fs_resolve(const char *root, const char *dir, const char *name, char *out, size_t cap)
{
    // Both request-supplied pieces are checked; the root is ours.
    if (protocore_fs_has_dotdot(dir) || protocore_fs_has_dotdot(name))
    {
        return -1; // path traversal - refuse before touching the filesystem
    }
    size_t fpl = protocore_fs_join(root, dir, name, out, cap);
    if (fpl == 0)
    {
        return -2;
    }
    if (fpl > 1 && out[fpl - 1] == '/')
    {
        out[fpl - 1] = '\0';
    }
    return 0;
}

// --- status ---------------------------------------------------------------------------------
//
// Every operation here fails closed: 0, false, or -1. That is the right answer, but on its own it
// throws away WHY, and the two reasons a caller cares about are not the same thing - "the path you
// asked for is wrong" and "there is no storage behind this filesystem" want different handling.
//
// A null store is a legitimate, intentional configuration, not an error. protocore_fs_begin() binds a root
// and protocore_fs_path() resolves against it with nothing mounted, so an application can hold a filesystem
// and do local-only path work before (or without) ever attaching a store. What it must not do is
// look identical to a fault.
//
// So the reason is a sticky mask, the same way protocore_cspan carries a sticky ok: bits accumulate as
// operations fail and a caller tests once, at whatever granularity suits it, instead of branching on
// every call. Mask it for the bit you care about.
//
//     protocore_fs_clear_status();
//     protocore_fs_write_file(root, "/a", "", buf, n);
//     protocore_fs_write_file(root, "/b", "", buf, n);
//     if (protocore_fs_status() & PROTOCORE_FS_STORAGE_EXHAUSTED) { ... }   // no store, or it would not take more

#define PROTOCORE_FS_OK 0u
#define PROTOCORE_FS_STORAGE_EXHAUSTED (1u << 0) ///< nothing mounted, or the store could not take the write.
#define PROTOCORE_FS_BAD_ROOT (1u << 1)          ///< the root handle was never bound (see protocore_fs_begin).
#define PROTOCORE_FS_TRAVERSAL (1u << 2)         ///< the request path contained `..` and was refused.
#define PROTOCORE_FS_TOO_LONG (1u << 3)          ///< the resolved path did not fit.

/** @brief The file one call names: which mount, which directory, which entry. */
typedef struct
{
    int root;         ///< the mount point the path is resolved against
    const char *dir;  ///< the directory within it
    const char *name; ///< the entry within that
} FsPathArgs;

/** @brief The second path a rename or a copy needs. */
typedef struct
{
    const char *dir;  ///< the destination directory
    const char *name; ///< the destination entry
} FsDestArgs;

/** @brief The open file a call acts on, and the bytes it moves. */
typedef struct
{
    int handle;               ///< the open file or directory a call acts on
    protocore_mnt_mode mode;  ///< how an open asks for it
    void *buf;                ///< where a read lands, or what a write sends
    const void *wbuf;         ///< the bytes a write sends, when they are const
    size_t n;                 ///< how many
    uint64_t off;             ///< where a seek moves to
    char *name_out;           ///< where a directory read writes the entry name
    size_t name_cap;          ///< how much room that has
    protocore_mnt_stat *stat; ///< where a stat or a directory read lands its entry
} FsIoArgs;

/**
 * @brief The path-safe filesystem surface over a mount.
 *
 * A caller sets the members a call takes, invokes it through ::Fs, and reads the outcome off the
 * same handle. Every path is joined and bounds-checked here before it reaches a backend.
 *
 * @var FilesystemNs::path      the file one call names
 * @var FilesystemNs::dest      the second path a rename or a copy needs
 * @var FilesystemNs::io        the open file a call acts on, and the bytes it moves
 * @var FilesystemNs::mount     the mount a begin opens, by name
 * @var FilesystemNs::ok        a call's true/false outcome
 * @var FilesystemNs::i32       a handle, a byte count, or < 0 on failure
 * @var FilesystemNs::len       a file length a size or a whole-file read reports
 * @var FilesystemNs::bits      the sticky fault bits
 * @var FilesystemNs::text      the joined path a resolve reports, or NULL when it would escape
 * @var FilesystemNs::status    the sticky fault bits since the last clear
 * @var FilesystemNs::clear     drop them
 * @var FilesystemNs::present   storage is mounted and answering
 * @var FilesystemNs::begin     open the named mount
 * @var FilesystemNs::resolve   join and bounds-check a path without touching storage
 * @var FilesystemNs::open      open a file
 * @var FilesystemNs::read      read from an open file
 * @var FilesystemNs::write     write to an open file
 * @var FilesystemNs::close     close an open file or directory
 * @var FilesystemNs::seek      move an open file's cursor
 * @var FilesystemNs::size      a file's length
 * @var FilesystemNs::exists    a file is there
 * @var FilesystemNs::stat      a file's metadata
 * @var FilesystemNs::remove    delete a file
 * @var FilesystemNs::rename    move one
 * @var FilesystemNs::copy      duplicate one
 * @var FilesystemNs::mkdir     create a directory
 * @var FilesystemNs::rmdir     remove one
 * @var FilesystemNs::opendir   open a directory for listing
 * @var FilesystemNs::readdir   take the next entry from it
 * @var FilesystemNs::read_file  read a whole file into one buffer
 * @var FilesystemNs::write_file write one buffer as a whole file
 *
 * The joined path is `..`-free by construction, and the small embedded filesystems have no
 * symlinks, so a resolved path cannot leave its root.
 */
typedef struct
{
    FsPathArgs path;
    FsDestArgs dest;
    FsIoArgs io;
    const char *mount;

    proto_bool ok;
    int i32;
    long len;
    uint32_t bits;
    const char *text;

    void (*const status)(uint8_t *restrict work);
    void (*const clear)(uint8_t *restrict work);
    void (*const present)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const resolve)(uint8_t *restrict work);
    void (*const open)(uint8_t *restrict work);
    void (*const read)(uint8_t *restrict work);
    void (*const write)(uint8_t *restrict work);
    void (*const close)(uint8_t *restrict work);
    void (*const seek)(uint8_t *restrict work);
    void (*const size)(uint8_t *restrict work);
    void (*const exists)(uint8_t *restrict work);
    void (*const stat)(uint8_t *restrict work);
    void (*const remove)(uint8_t *restrict work);
    void (*const rename)(uint8_t *restrict work);
    void (*const copy)(uint8_t *restrict work);
    void (*const mkdir)(uint8_t *restrict work);
    void (*const rmdir)(uint8_t *restrict work);
    void (*const opendir)(uint8_t *restrict work);
    void (*const readdir)(uint8_t *restrict work);
    void (*const read_file)(uint8_t *restrict work);
    void (*const write_file)(uint8_t *restrict work);
} FilesystemNs;

/** @brief The one symbol this module exports. */
extern FilesystemNs Fs;

/**
 * @brief The PROTOCORE_FILESYSTEM_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_filesystem_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_FILESYSTEM_H
