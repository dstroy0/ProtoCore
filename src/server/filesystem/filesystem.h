// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
 * pc_fs_path() is the single exception, for the one caller that genuinely needs the text: SFTP
 * REALPATH answers with a path string. It hands back a pointer into this file's storage, or
 * nullptr when it cannot - there is no out/capacity pair to get wrong.
 *
 * The `..` guard is a rejection, not realpath/symlink resolution: the on-flash filesystems
 * (FAT / LittleFS) have no symlinks, so a `..`-free joined path cannot leave the root.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_FILESYSTEM_H
#define PROTOCORE_FILESYSTEM_H

#include "protocore_config.h"
#include "server/filesystem/mnt.h"
#include "shared_primitives/frame.h" // the one frame engine
#include <stddef.h>
#include <stdint.h>

// root, dir, name. A whole path is these three pieces, so it is ONE build: a caller that assembled
// dir+name itself and handed the result over would frame the same bytes twice, into two buffers,
// for the same result. A mount root ends with '/', and a dir that carries a name ends with '/', so
// both separators are already in the strings and the spec needs no literal between the fields.
static const pc_field FILESYSTEM_JOIN[] = {PC_STR, PC_STR, PC_STR, PC_END};

// The mount root, copied in so its trailing '/' is owned rather than assumed (see pc_fs_begin).
static const pc_field FILESYSTEM_ROOT[] = {PC_STR, PC_END};

/**
 * @brief Roots that can be bound at once (see pc_fs_begin).
 *
 * One per service that wants its own storage - "mnt/scp", "mnt/sftp", a local region held for temp
 * files - and more can be bound by raising this. Two services naming the same root share it and
 * cost one entry.
 */
#ifndef PC_FS_MAX_ROOTS
#define PC_FS_MAX_ROOTS 4
#endif

/** @brief Longest root name (e.g. "mnt/sftp"), including the terminator. */
#ifndef PC_FS_ROOT_NAME_MAX
#define PC_FS_ROOT_NAME_MAX 24
#endif

/**
 * @brief How deep pc_fs_remove() / pc_fs_copy() descend before refusing a tree.
 *
 * The bound is what turns a tree walk into a fixed cost: one level of path storage per level, all of
 * it in this file's context, and no call recursion - the walks are loops over that array, so the
 * depth a request can force is the array's extent rather than however many stack frames it takes to
 * exhaust RAM.
 */
#ifndef PC_FS_MAX_DEPTH
#define PC_FS_MAX_DEPTH 8
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
 * This is why pc_fs_copy() moves a block at a time rather than "some convenient buffer size", and
 * why the number lives here instead of at each call site - a caller cannot know what the mounted
 * store erases in, and should not have to.
 */
#ifndef PC_FS_BLOCK
#define PC_FS_BLOCK 512
#endif

/** @brief Join a mount @p root, a request @p dir, and a leaf @p name into @p out.
 *
 * @param name the leaf, or "" when @p dir is the whole path. When @p name is given, @p dir must end
 *             with '/' - that is what a directory destination means, and it is known at the call
 *             site rather than tested here.
 * @return bytes written, or 0 on overflow - the engine already knows the length, so it is handed
 *         back rather than left for the caller to rediscover with a scan. */
inline size_t pc_fs_join(const char *root, const char *dir, const char *name, char *out, size_t cap)
{
    if (dir[0] == '/')
    {
        dir++; // the root carries the separator; a second one would be "//"
    }
    return pc_frame_build(out, cap, FILESYSTEM_JOIN, root, dir, name);
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
inline bool pc_fs_has_dotdot(const char *s)
{
    for (const char *p = s; p[0] != '\0' && p[1] != '\0'; p++)
    {
        if (p[0] == '.' && p[1] == '.')
        {
            return true;
        }
    }
    return false;
}

inline int pc_fs_resolve(const char *root, const char *dir, const char *name, char *out, size_t cap)
{
    // Both request-supplied pieces are checked; the root is ours.
    if (pc_fs_has_dotdot(dir) || pc_fs_has_dotdot(name))
    {
        return -1; // path traversal - refuse before touching the filesystem
    }
    size_t fpl = pc_fs_join(root, dir, name, out, cap);
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
// A null store is a legitimate, intentional configuration, not an error. pc_fs_begin() binds a root
// and pc_fs_path() resolves against it with nothing mounted, so an application can hold a filesystem
// and do local-only path work before (or without) ever attaching a store. What it must not do is
// look identical to a fault.
//
// So the reason is a sticky mask, the same way pc_cspan carries a sticky ok: bits accumulate as
// operations fail and a caller tests once, at whatever granularity suits it, instead of branching on
// every call. Mask it for the bit you care about.
//
//     pc_fs_clear_status();
//     pc_fs_write_file(root, "/a", "", buf, n);
//     pc_fs_write_file(root, "/b", "", buf, n);
//     if (pc_fs_status() & PC_FS_STORAGE_EXHAUSTED) { ... }   // no store, or it would not take more

#define PC_FS_OK 0u
#define PC_FS_STORAGE_EXHAUSTED (1u << 0) ///< nothing mounted, or the store could not take the write.
#define PC_FS_BAD_ROOT (1u << 1)          ///< the root handle was never bound (see pc_fs_begin).
#define PC_FS_TRAVERSAL (1u << 2)         ///< the request path contained `..` and was refused.
#define PC_FS_TOO_LONG (1u << 3)          ///< the resolved path did not fit.

/** @brief The accumulated reasons operations have failed since the last clear. */
uint32_t pc_fs_status(void);

/** @brief Clear the mask. Call before a sequence whose outcome you intend to test as a whole. */
void pc_fs_clear_status(void);

/** @brief True while no store is mounted - the local-only configuration, stated rather than inferred
 *         from a call that returned false. */
bool pc_fs_storage_present(void);

/**
 * @brief Bind a root and get the handle a service works through (e.g. "mnt/scp", "mnt/sftp").
 *
 * A service calls this once in its own begin() and keeps what comes back. That is what lets two of
 * them be live at the same time over different storage - SCP landing on a card while SFTP serves a
 * RAM pool - or over the same storage, which is the application's arrangement and not something
 * either service can tell.
 *
 * The name maps to a root here, because this seam is the only thing that knows what a root means. A
 * root knows its own extent, so nothing downstream carries a capacity beside a pointer.
 *
 * A NULL or empty @p name binds "/". Re-binding a name already bound returns the same handle rather
 * than a second root over the same bytes.
 *
 * @return a root handle (>= 0), or -1 if the root table is full.
 */
int pc_fs_begin(const char *name);

/**
 * @brief The resolved on-disk path for request @p dir + leaf @p name.
 *
 * @return a pointer to this file's path storage, valid until the next pc_fs_* call, or nullptr if
 *         the request attempts traversal or does not fit. The buffer is not the caller's: copy it
 *         if it must outlive the next call.
 */
const char *pc_fs_path(int root, const char *dir, const char *name);

// --- operations ------------------------------------------------------------------------------
// A path-taking call carries the @p root it resolves against plus the REQUEST path as its two
// pieces - a @p dir and a leaf @p name, "" when the dir is the whole path - and they are framed
// onto that root here, once. A caller that joined them itself would build the same bytes twice.
//
// A handle-taking call carries no root and no path: the handle came from a root, so naming it
// again would be asking the caller to keep two things in agreement that cannot disagree.

/** @brief Open request path @p dir + @p name under @p root. @return a handle (>= 0), or -1. */
int pc_fs_open(int root, const char *dir, const char *name, pc_mnt_mode mode);
/** @brief Read up to @p n bytes from @p handle into @p buf. @return bytes read, or -1. */
int pc_fs_read(int handle, void *buf, size_t n);
/** @brief Write @p n bytes from @p buf to @p handle. @return bytes written, or -1. */
int pc_fs_write(int handle, const void *buf, size_t n);
/** @brief Close an open file or directory @p handle. */
void pc_fs_close(int handle);
/** @brief Seek @p handle to absolute offset @p off. @return true on success. */
bool pc_fs_seek(int handle, uint64_t off);
/** @brief Size of the file at @p dir + @p name. @return the size in bytes, or -1 if absent. */
long pc_fs_size(int root, const char *dir, const char *name);
/** @brief @return true if @p dir + @p name exists. */
bool pc_fs_exists(int root, const char *dir, const char *name);
/** @brief Fill @p out with the facts about @p dir + @p name. @return false if absent. */
bool pc_fs_stat(int root, const char *dir, const char *name, pc_mnt_stat *out);
/**
 * @brief Delete @p dir + @p name: a file, or a directory and everything under it.
 *
 * The subtree is this file's operation, not the mount's and not a protocol server's. mnt is blind -
 * it does not know what a path means, so it cannot know what a subtree is - and a protocol server
 * that assembled one out of opendir/readdir/remove would need a level stack, a depth bound, a path
 * buffer per level, and a rule for not invalidating the cursor it is standing on. WebDAV, SFTP and
 * SCP would each need their own. There is one here.
 *
 * @return true when the whole tree is gone; false if any part of it could not be removed.
 */
bool pc_fs_remove(int root, const char *dir, const char *name);
/** @brief Rename @p from_dir + @p from_name to @p to_dir + @p to_name. @return true on success. */
bool pc_fs_rename(int root, const char *from_dir, const char *from_name, const char *to_dir, const char *to_name);
/**
 * @brief Copy @p from_dir + @p from_name onto @p to_dir + @p to_name: a file, or a whole tree.
 *
 * The destination is replaced if it exists. @return true when the whole tree arrived.
 */
bool pc_fs_copy(int root, const char *from_dir, const char *from_name, const char *to_dir, const char *to_name);
/** @brief Create a directory at @p dir + @p name. @return true on success. */
bool pc_fs_mkdir(int root, const char *dir, const char *name);
/** @brief Remove the empty directory at @p dir + @p name. @return true on success. */
bool pc_fs_rmdir(int root, const char *dir, const char *name);
/** @brief Open @p dir + @p name as a directory. @return a handle (>= 0), or -1. */
int pc_fs_opendir(int root, const char *dir, const char *name);
/**
 * @brief Next entry of directory @p handle: facts into @p out, the entry's own name into @p name.
 * @return false at the end of the directory. @p name_cap is the caller's, derived from what that
 *         caller's own frame must hold - this file imposes no name length.
 */
bool pc_fs_readdir(int handle, pc_mnt_stat *out, char *name, size_t name_cap);

/** @brief Read the whole file at @p dir + @p name into @p buf.
 *  @return bytes read (0..cap), or -1 if absent / would exceed @p cap. */
long pc_fs_read_file(int root, const char *dir, const char *name, void *buf, size_t cap);
/** @brief Create/truncate @p dir + @p name and write @p n bytes. @return true on success. */
bool pc_fs_write_file(int root, const char *dir, const char *name, const void *buf, size_t n);

#endif // PROTOCORE_FILESYSTEM_H
