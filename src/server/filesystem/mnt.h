// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mnt.h
 * @brief The mount: which store is behind the filesystem, and the vtable it answers through
 *        (PC_ENABLE_MNT).
 *
 * This file answers one question - *what is mounted* - and nothing else. The operations a caller
 * performs live on the filesystem accessor (server/filesystem.h), which resolves a request path
 * against its root and then dispatches here. Splitting it that way means a backend author
 * implements storage and never touches path policy, and the `..` guard cannot be bypassed by
 * reaching a backend directly.
 *
 * Two backends ship:
 *
 *  - **RAM** (built-in, host-testable, zero-heap): a fixed pool of PC_MNT_RAM_FILES files of up to
 *    PC_MNT_RAM_FILE_SIZE bytes each, all in BSS - deterministic, bounded, and identical on host
 *    and target. It is what lets the SFTP/SCP/WebDAV servers run under a native test.
 *
 *  - **Arduino FS** (board layer): wraps a real `fs::FS` (LittleFS / SD / SPIFFS) for persistent
 *    storage. It lives in board_drivers/ because it speaks a vendor framework, which the core does
 *    not.
 *
 * Handles are small ints the backend assigns, and a directory cursor is one of them - so ::close
 * releases either kind and there is no second lifetime to get wrong. Every entry point fails
 * closed (-1 / false) when nothing is mounted.
 *
 * Single-accessor like the other services: use it from one context (a worker / loop), not
 * concurrently.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MNT_H
#define PROTOCORE_MNT_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

/**
 * @brief Open modes.
 *
 * PC_MNT_RDWR is the only one that admits a seek before a write. Under PC_MNT_APPEND a write lands
 * at end-of-file whatever the position says, which is what O_APPEND means on a real filesystem, so a
 * caller that overwrites in place has to ask for RDWR and a backend that cannot offer it answers -1.
 */
typedef enum PROTO_ENUM_PACKED
{
    PC_MNT_READ = 0,   ///< Read existing file (fails if absent).
    PC_MNT_WRITE = 1,  ///< Create/truncate for writing.
    PC_MNT_APPEND = 2, ///< Create/open for appending at end.
    PC_MNT_RDWR = 3,   ///< Open existing for random read+write, no truncation (fails if absent).
} pc_mnt_mode;

/**
 * @brief What a stat and a directory entry both answer: the facts about one file, and no name.
 *
 * The name is deliberately absent. A name buffer welded into this struct would set one length for
 * every consumer, and the consumers do not agree: the SFTP codec sizes its entry scratch from what
 * one NAME response must hold, WebDAV from an href, the RAM backend from its own file table. Each
 * translation unit derives the length it needs and passes a buffer, so this type stays the four
 * facts and carries no policy.
 *
 * Stat and readdir share it because they answer the same question about the same directory record
 * - asking a backend for a size, then whether it is a directory, then its mtime is three lookups
 * of one record, which on FAT is three seeks to learn what the first read already had.
 */
typedef struct
{
    proto_bool is_dir;
    uint64_t size;  ///< 0 for a directory
    uint32_t mtime; ///< unix epoch seconds, 0 if the backend keeps no timestamp
} pc_mnt_stat;

/**
 * @brief A storage backend. Each open call returns a small handle (>= 0) or -1.
 *
 * Implement this to add a store; the built-in RAM disk is returned by pc_mnt_ram().
 *
 * Every call here is one node. mnt is blind - it does not know what a path means, so it cannot know
 * what a subtree is, and nothing here takes one. A whole-tree operation is composed from these by
 * the accessor (server/filesystem/filesystem.h), which is the seam that does know.
 */
typedef struct pc_mnt_backend
{
    int (*open)(const char *path, int mode);                ///< -> handle (>=0) or -1.
    int (*read)(int handle, void *buf, size_t n);           ///< bytes read, or -1.
    int (*write)(int handle, const void *buf, size_t n);    ///< bytes written, or -1.
    void (*close)(int handle);                              ///< release a file OR directory handle.
    proto_bool (*seek)(int handle, uint64_t off);           ///< absolute seek; false if unsupported.
    long (*size)(const char *path);                         ///< file size, or -1 if absent.
    proto_bool (*exists)(const char *path);                 ///< true if the path exists.
    proto_bool (*remove)(const char *path);                 ///< delete a file; true on success.
    proto_bool (*rename)(const char *from, const char *to); ///< rename; true on success.
    proto_bool (*mkdir)(const char *path);                  ///< create a directory; true on success.
    proto_bool (*rmdir)(const char *path);                  ///< remove an empty directory; true on success.
    proto_bool (*stat)(const char *path, pc_mnt_stat *out); ///< fill @p out for @p path; false if absent.
    int (*opendir)(const char *path);                       ///< -> directory handle (>=0) or -1.
    /** @brief Next entry: facts into @p out, name into @p name (NUL-terminated). False at end. */
    proto_bool (*readdir)(int handle, pc_mnt_stat *out, char *name, size_t name_cap);
    /**
     * @brief Push everything written to @p handle past the backend's own buffering, and report
     *        whether it got there. May be NULL when the backend cannot promise it.
     *
     * This is the durability barrier, and it is the one call here whose absence is not the same as
     * failure: a log that orders its writes around a barrier is only power-loss-safe if the barrier
     * is real, so a backend that cannot make the promise says so with NULL and the caller refuses to
     * mount rather than running as though it had one. Returning true without doing anything would
     * make an unsafe store indistinguishable from a safe one.
     */
    proto_bool (*sync)(int handle);
} pc_mnt_backend;

// Everything above and the two calls below are the HAL: the shape of a store, and which one is
// mounted. All declarations plus one pointer, so a feature that reads through the seam pays nothing
// and never has to enable a service just to name a type. The seam fails closed when nothing is
// mounted, which is the honest answer rather than an error.
/** @brief Mount the active backend (call once at setup; NULL unmounts). */
/** @brief The id a route carries when it serves no mount point. */
#define PC_MNT_NONE 0xFFu

/**
 * @brief Record a mount point - a backend and the subtree it serves - and return the id naming it,
 *        or ::PC_MNT_NONE when full.
 *
 * Both registrars that offer a mount describe one with this pair, so it lives with mounting rather
 * than being copied into each of their route entries. A null @p backend is legal and means whatever
 * is currently mounted.
 */
uint8_t pc_mnt_point_add(const pc_mnt_backend *backend, const char *root);

/// @brief The backend @p id names, or nullptr - which also means "use whatever is mounted".
const pc_mnt_backend *pc_mnt_point_backend(uint8_t id);

/// @brief The subtree @p id names, as a request-path piece. Empty, never null.
const char *pc_mnt_point_root(uint8_t id);

void pc_mnt_mount(const pc_mnt_backend *backend);

/**
 * @brief The mounted backend, or NULL if nothing is mounted.
 *
 * A hotswap: storage can come and go under a running server, so this answers the live question
 * rather than reporting whether a pointer was ever set. Every seam entry point fails closed, so a
 * cold mount degrades instead of faulting.
 *
 * Naming a store is not this file's job. mnt offers storage to connect; the mapping from a name to
 * a root belongs to the seam that resolves paths, which is the only thing that knows what a root
 * means.
 */
const pc_mnt_backend *pc_mnt_active(void);

// The RAM backend is the only part with a footprint (PC_MNT_RAM_FILES x PC_MNT_RAM_FILE_SIZE of
// BSS), so it is the only part the flag gates.
#if PC_ENABLE_MNT

/** @brief The built-in deterministic RAM backend (fixed BSS pool, no heap). */
const pc_mnt_backend *pc_mnt_ram(void);

/** @brief Clear the RAM backend (all files, directories, and open handles). */
void pc_mnt_ram_format(void);

#endif // PC_ENABLE_MNT

PROTO_END_DECLS

#endif // PROTOCORE_MNT_H
